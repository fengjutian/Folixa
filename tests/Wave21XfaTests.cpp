// 2026-11-19 wave21：XFA 字段值改写 + 脚本子集执行。
//
// 夹具是自己拼的 PDF：XFA 的形态（/XFA 放目录还是 /AcroForm、数据集在哪、
// 脚本挂在 calculate 还是 field 下）每一处都能改变结论，
// 只有自己拼才能把每一种形态单独钉住。
#include "pdf/forms/XfaScriptRunner.h"
#include "pdf/forms/XfaSupport.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

using aipdf::pdf::evaluateXfaScript;
using aipdf::pdf::XfaField;
using aipdf::pdf::XfaReport;
using aipdf::pdf::XfaScriptField;
using aipdf::pdf::XfaStatus;
using aipdf::pdf::XfaWriteReport;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 把对象列表拼成一份带正确 xref 的 PDF。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects, int rootId) {
    QByteArray out("%PDF-1.7\n");
    QHash<int, int> offsets;
    int maxId = 0;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        maxId = qMax(maxId, object.first);
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = maxId + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        if (offsets.contains(id)) {
            out += QStringLiteral("%1 00000 n \n")
                       .arg(offsets.value(id), 10, 10, QLatin1Char('0'))
                       .toLatin1();
        } else {
            out += QByteArray("0000000000 65535 f \n");
        }
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root %2 0 R >>\nstartxref\n%3\n%%EOF\n")
               .arg(count)
               .arg(rootId)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// 一个流对象：prefix 是字典里 /Length 之前的内容（不含开头的 << 与收尾的 >>）。
QByteArray streamObject(const QByteArray& prefix, const QByteArray& data) {
    QByteArray out = "<<";
    out += prefix;
    if (!out.endsWith(' ')) out += ' ';
    out += "/Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data + "\nendstream";
    return out;
}

// /XFA 写在文档目录上的 PDF（规范位置是 /AcroForm，但确实有生成器这么写）。
QByteArray xfaOnCatalog(const QByteArray& xml) {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /XFA [4 0 R] >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [] /Count 0 >>")));
    objects.append(qMakePair(4, streamObject(QByteArray(), xml)));
    return buildPdf(objects, 1);
}

// 没有 /XFA 的普通 PDF。
QByteArray plainPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [] /Count 0 >>")));
    return buildPdf(objects, 1);
}

// 一段脚本代码的 XML 转义（脚本里有 < 和 & 时必须转义）。
QByteArray escapeXml(const QString& text) {
    QString out = text;
    out.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    out.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    out.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return out.toUtf8();
}

// 一个计算脚本块：<calculate><script><exObject name="...">代码</exObject></script></calculate>。
QByteArray calculateBlock(const QString& name, const QString& code) {
    QByteArray out = "<xfa:calculate><xfa:script><xfa:exObject name=\"";
    out += name.toUtf8();
    out += "\">";
    out += escapeXml(code);
    out += "</xfa:exObject></xfa:script></xfa:calculate>";
    return out;
}

// 模板：六个字段，覆盖文本、数字、复选框、只读、以及脚本要写的字段。
QByteArray templateXml() {
    return QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xdp/\" "
        "xmlns:xfa=\"http://www.xfa.org/schema/xfa-template/3.3/\">\n"
        " <xdp:template>\n"
        "  <xfa:subform name=\"form1\">\n"
        "   <xfa:field name=\"TextField1\">\n"
        "    <xfa:ui><xfa:textEdit/></xfa:ui>\n"
        "    <xfa:caption><xfa:value><xfa:text>姓名</xfa:text></xfa:value></xfa:caption>\n"
        "   </xfa:field>\n"
        "   <xfa:field name=\"Amount\">\n"
        "    <xfa:ui><xfa:numericField/></xfa:ui>\n"
        "   </xfa:field>\n"
        "   <xfa:field name=\"CheckBox1\">\n"
        "    <xfa:ui><xfa:checkBox/></xfa:ui>\n"
        "   </xfa:field>\n"
        "   <xfa:field name=\"LockedField\" access=\"readOnly\">\n"
        "    <xfa:ui><xfa:textEdit/></xfa:ui>\n"
        "   </xfa:field>\n"
        "   <xfa:field name=\"Base\">\n"
        "    <xfa:ui><xfa:numericField/></xfa:ui>\n"
        "   </xfa:field>\n"
        "   <xfa:field name=\"Total\">\n"
        "    <xfa:ui><xfa:numericField/></xfa:ui>\n"
        "   </xfa:field>\n"
        "  </xfa:subform>\n"
        " </xdp:template>\n");
}

// 数据集：六个字段的当前值。
QByteArray datasetsXml() {
    return QByteArray(
        " <xdp:datasets>\n"
        "  <xfa:datasets xmlns:xfa=\"http://www.xfa.org/schema/xfa-data/1.0/\">\n"
        "   <xfa:data>\n"
        "    <xfa:form1>\n"
        "     <xfa:TextField1><xfa:value><xfa:text>张三</xfa:text></xfa:value></xfa:TextField1>\n"
        "     <xfa:Amount><xfa:value><xfa:text>10</xfa:text></xfa:value></xfa:Amount>\n"
        "     <xfa:CheckBox1><xfa:value><xfa:text>0</xfa:text></xfa:value></xfa:CheckBox1>\n"
        "     <xfa:LockedField><xfa:value><xfa:text>锁定</xfa:text></xfa:value></xfa:LockedField>\n"
        "     <xfa:Base><xfa:value><xfa:text>5</xfa:text></xfa:value></xfa:Base>\n"
        "     <xfa:Total><xfa:value><xfa:text>0</xfa:text></xfa:value></xfa:Total>\n"
        "    </xfa:form1>\n"
        "   </xfa:data>\n"
        "  </xfa:datasets>\n"
        " </xdp:datasets>\n");
}

// 拼一份完整的 XFA 包：模板 + 数据集 + 附加块（脚本、事件）。
QByteArray xfaPackage(const QByteArray& extra) {
    QByteArray out = templateXml();
    out += datasetsXml();
    out += extra;
    out += "</xdp:xdp>\n";
    return out;
}

// 带一个计算脚本的 XFA：Total = Amount * 2。
QByteArray xfaWithCalculation() {
    return xfaPackage(calculateBlock(QStringLiteral("calculateSum"),
                                     QStringLiteral("xfa.form.form1.Total.value = "
                                                    "xfa.form.form1.Amount.value * 2;")));
}

// 两个互相依赖的计算脚本：Total 依赖 Amount，Amount 依赖 Base。
//
// 顺序故意写反（先 Total 后 Amount）：真按文件顺序执行会算出 10（用的旧 Amount），
// 只有按依赖重排才得到 12。
QByteArray xfaWithDependentCalculations() {
    QByteArray extra = calculateBlock(QStringLiteral("calculateTotal"),
                                       QStringLiteral("xfa.form.form1.Total.value = "
                                                      "xfa.form.form1.Amount.value * 2;"));
    extra += calculateBlock(QStringLiteral("calculateAmount"),
                            QStringLiteral("xfa.form.form1.Amount.value = "
                                           "xfa.form.form1.Base.value + 1;"));
    return xfaPackage(extra);
}

// 用了子集外语法（for 循环）的计算脚本。
QByteArray xfaWithUnsupportedScript() {
    return xfaPackage(calculateBlock(QStringLiteral("brokenLoop"),
                                     QStringLiteral("for (var i = 0; i < 3; i++) { "
                                                    "xfa.form.form1.Total.value = i; }")));
}

// 带事件脚本的 XFA：事件脚本本程序不执行，但要点名说明。
QByteArray xfaWithEventScript() {
    QByteArray extra = "<xfa:event><xfa:script><xfa:exObject name=\"initialize\">";
    extra += escapeXml(QStringLiteral("xfa.form.form1.Total.value = 999;"));
    extra += "</xfa:exObject></xfa:script></xfa:event>";
    return xfaPackage(extra);
}

// 只有模板、没有数据集的 XFA：值无处可写。
QByteArray xfaWithoutDatasets() {
    QByteArray out = templateXml();
    out += "</xdp:xdp>\n";
    return out;
}

const XfaField* findField(const XfaReport& report, const QString& name) {
    for (const XfaField& field : report.fields) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

QString updateError(const XfaWriteReport& report, const QString& field) {
    for (const aipdf::pdf::XfaFieldUpdate& update : report.updates) {
        if (update.field == field) return update.error;
    }
    return QString();
}

}  // namespace

class Wave21XfaTests : public QObject {
    Q_OBJECT

private slots:
    void writeValueIsReadableAfterRoundTrip();
    void untouchedFieldsKeepTheirValues();
    void writingKeepsFieldStructure();
    void unknownFieldIsReportedWithChineseReason();
    void readOnlyFieldIsReportedWithChineseReason();
    void invalidNumberIsReportedWithChineseReason();
    void invalidCheckBoxIsReportedWithChineseReason();
    void batchWriteAppliesEveryField();
    void scriptCalculationWritesResultIntoField();
    void scriptResultIsReadableAfterRoundTrip();
    void scriptsAreReorderedByDependency();
    void unsupportedScriptIsReportedWithChineseReason();
    void failedScriptLeavesPdfUntouched();
    void eventScriptIsNotExecutedButNamed();
    void nonXfaPdfIsRejected();
    void emptyInputIsRejected();
    void xfaWithoutDatasetsIsRejected();
    void runnerComputesFieldValue();
    void runnerReportsUnsupportedSyntax();
    void runnerNamesUnknownFieldAsWarning();
};

void Wave21XfaTests::writeValueIsReadableAfterRoundTrip() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::writeXfaFieldValue(xfaOnCatalog(xfaWithCalculation()),
                                       QStringLiteral("TextField1"), QStringLiteral("李四"), &written);
    QVERIFY2(report.ok, qPrintable(report.error));
    QVERIFY(!written.isEmpty());

    // 写完必须能再读出新值：往返一致。
    const XfaReport after = aipdf::pdf::inspectXfa(written);
    QCOMPARE(after.status, XfaStatus::Dynamic);
    const XfaField* text = findField(after, QStringLiteral("TextField1"));
    QVERIFY(text != nullptr);
    QCOMPARE(text->value, QStringLiteral("李四"));
}

void Wave21XfaTests::untouchedFieldsKeepTheirValues() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::writeXfaFieldValue(xfaOnCatalog(xfaWithCalculation()),
                                       QStringLiteral("TextField1"), QStringLiteral("李四"), &written);
    QVERIFY2(report.ok, qPrintable(report.error));

    const XfaReport after = aipdf::pdf::inspectXfa(written);
    const XfaField* amount = findField(after, QStringLiteral("Amount"));
    const XfaField* locked = findField(after, QStringLiteral("LockedField"));
    const XfaField* total = findField(after, QStringLiteral("Total"));
    QVERIFY(amount != nullptr);
    QVERIFY(locked != nullptr);
    QVERIFY(total != nullptr);
    // 没动的字段值必须原样保留。
    QCOMPARE(amount->value, QStringLiteral("10"));
    QCOMPARE(locked->value, QStringLiteral("锁定"));
    QCOMPARE(total->value, QStringLiteral("0"));
}

void Wave21XfaTests::writingKeepsFieldStructure() {
    REQUIRE_QPDF();
    const QByteArray source = xfaOnCatalog(xfaWithCalculation());
    const XfaReport before = aipdf::pdf::inspectXfa(source);
    QCOMPARE(before.fields.size(), 6);

    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::writeXfaFieldValue(source, QStringLiteral("TextField1"),
                                       QStringLiteral("李四"), &written);
    QVERIFY2(report.ok, qPrintable(report.error));

    const XfaReport after = aipdf::pdf::inspectXfa(written);
    // 改值不该动结构：字段数量、名字、类型、标签都还在。
    QCOMPARE(after.fields.size(), before.fields.size());
    for (const XfaField& field : before.fields) {
        const XfaField* other = findField(after, field.name);
        QVERIFY2(other != nullptr, qPrintable(field.name));
        QCOMPARE(other->type, field.type);
        QCOMPARE(other->label, field.label);
    }
    QCOMPARE(findField(after, QStringLiteral("TextField1"))->label, QStringLiteral("姓名"));
}

void Wave21XfaTests::unknownFieldIsReportedWithChineseReason() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::writeXfaFieldValue(xfaOnCatalog(xfaWithCalculation()),
                                       QStringLiteral("NoSuchField"), QStringLiteral("x"), &written);
    QVERIFY(!report.ok);
    QVERIFY(!report.updates.isEmpty());
    const QString reason = updateError(report, QStringLiteral("NoSuchField"));
    QVERIFY(!reason.isEmpty());
    QVERIFY2(reason.contains(QStringLiteral("不存在")), qPrintable(reason));
    QVERIFY2(reason.contains(QStringLiteral("NoSuchField")), qPrintable(reason));
    // 没改成就不该产出 PDF。
    QVERIFY(written.isEmpty());
}

void Wave21XfaTests::readOnlyFieldIsReportedWithChineseReason() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::writeXfaFieldValue(xfaOnCatalog(xfaWithCalculation()),
                                       QStringLiteral("LockedField"), QStringLiteral("改一下"), &written);
    QVERIFY(!report.ok);
    const QString reason = updateError(report, QStringLiteral("LockedField"));
    QVERIFY(!reason.isEmpty());
    QVERIFY2(reason.contains(QStringLiteral("只读")), qPrintable(reason));
    QVERIFY(written.isEmpty());
}

void Wave21XfaTests::invalidNumberIsReportedWithChineseReason() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::writeXfaFieldValue(xfaOnCatalog(xfaWithCalculation()),
                                       QStringLiteral("Amount"), QStringLiteral("abc"), &written);
    QVERIFY(!report.ok);
    const QString reason = updateError(report, QStringLiteral("Amount"));
    QVERIFY(!reason.isEmpty());
    QVERIFY2(reason.contains(QStringLiteral("数字")), qPrintable(reason));
    QVERIFY2(reason.contains(QStringLiteral("abc")), qPrintable(reason));
    QVERIFY(written.isEmpty());
}

void Wave21XfaTests::invalidCheckBoxIsReportedWithChineseReason() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::writeXfaFieldValue(xfaOnCatalog(xfaWithCalculation()),
                                       QStringLiteral("CheckBox1"), QStringLiteral("yes"), &written);
    QVERIFY(!report.ok);
    const QString reason = updateError(report, QStringLiteral("CheckBox1"));
    QVERIFY(!reason.isEmpty());
    QVERIFY2(reason.contains(QStringLiteral("复选框")), qPrintable(reason));
    QVERIFY(written.isEmpty());
}

void Wave21XfaTests::batchWriteAppliesEveryField() {
    REQUIRE_QPDF();
    QVector<QPair<QString, QString>> values;
    values.append(qMakePair(QStringLiteral("TextField1"), QStringLiteral("李四")));
    values.append(qMakePair(QStringLiteral("Amount"), QStringLiteral("42")));
    values.append(qMakePair(QStringLiteral("NoSuchField"), QStringLiteral("x")));

    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::writeXfaFieldValues(xfaOnCatalog(xfaWithCalculation()), values, &written);
    // 有一项失败，整体不算成功，但成功的两项已经写进去了。
    QVERIFY(!report.ok);
    QVERIFY(!written.isEmpty());
    QCOMPARE(report.updates.size(), 3);
    QCOMPARE(report.updates.at(0).error, QString());
    QCOMPARE(report.updates.at(1).error, QString());
    QVERIFY(!report.updates.at(2).error.isEmpty());

    const XfaReport after = aipdf::pdf::inspectXfa(written);
    QCOMPARE(findField(after, QStringLiteral("TextField1"))->value, QStringLiteral("李四"));
    QCOMPARE(findField(after, QStringLiteral("Amount"))->value, QStringLiteral("42"));
}

void Wave21XfaTests::scriptCalculationWritesResultIntoField() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::runXfaCalculateScripts(xfaOnCatalog(xfaWithCalculation()), &written);
    QVERIFY2(report.ok, qPrintable(report.error));
    QVERIFY(!written.isEmpty());
    // Amount 是 10，脚本算的是 Amount * 2 → 20。
    QCOMPARE(report.updates.size(), 1);
    QCOMPARE(report.updates.at(0).field, QStringLiteral("Total"));
    QCOMPARE(report.updates.at(0).value, QStringLiteral("20"));
}

void Wave21XfaTests::scriptResultIsReadableAfterRoundTrip() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::runXfaCalculateScripts(xfaOnCatalog(xfaWithCalculation()), &written);
    QVERIFY2(report.ok, qPrintable(report.error));

    const XfaReport after = aipdf::pdf::inspectXfa(written);
    const XfaField* total = findField(after, QStringLiteral("Total"));
    QVERIFY(total != nullptr);
    QCOMPARE(total->value, QStringLiteral("20"));
    // 脚本只改 Total，其它值不动。
    QCOMPARE(findField(after, QStringLiteral("Amount"))->value, QStringLiteral("10"));
}

void Wave21XfaTests::scriptsAreReorderedByDependency() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::runXfaCalculateScripts(xfaOnCatalog(xfaWithDependentCalculations()), &written);
    QVERIFY2(report.ok, qPrintable(report.error));

    const XfaReport after = aipdf::pdf::inspectXfa(written);
    // Base=5 → Amount=6 → Total=12。
    // 按文件顺序执行会拿旧的 Amount 算出 10，所以这条用例钉的是依赖重排。
    const XfaField* amount = findField(after, QStringLiteral("Amount"));
    const XfaField* total = findField(after, QStringLiteral("Total"));
    QVERIFY2(amount != nullptr, "脚本算出来的 Amount 应当写进数据集");
    QVERIFY2(total != nullptr, "脚本算出来的 Total 应当写进数据集");
    QCOMPARE(amount->value, QStringLiteral("6"));
    QCOMPARE(total->value, QStringLiteral("12"));
}

void Wave21XfaTests::unsupportedScriptIsReportedWithChineseReason() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::runXfaCalculateScripts(xfaOnCatalog(xfaWithUnsupportedScript()), &written);
    QVERIFY(!report.ok);
    QVERIFY(!report.error.isEmpty());
    QVERIFY2(report.error.contains(QStringLiteral("不支持的语法")), qPrintable(report.error));
    QVERIFY2(report.error.contains(QStringLiteral("脚本")), qPrintable(report.error));
    // 脚本失败就不该产出半成品 PDF。
    QVERIFY(written.isEmpty());
}

void Wave21XfaTests::failedScriptLeavesPdfUntouched() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::runXfaCalculateScripts(xfaOnCatalog(xfaWithUnsupportedScript()), &written);
    QVERIFY(!report.ok);
    QVERIFY(written.isEmpty());
}

void Wave21XfaTests::eventScriptIsNotExecutedButNamed() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::runXfaCalculateScripts(xfaOnCatalog(xfaWithEventScript()), &written);
    // 没有可执行的计算脚本：不算失败，但要点名说明没执行。
    QVERIFY2(report.ok, qPrintable(report.error));
    QVERIFY(!report.warnings.isEmpty());
    bool named = false;
    for (const QString& warning : report.warnings) {
        if (warning.contains(QStringLiteral("initialize")) && warning.contains(QStringLiteral("没有执行"))) {
            named = true;
        }
    }
    QVERIFY2(named, "事件脚本必须在提示里点名，不能静默忽略");
    QVERIFY(written.isEmpty());
}

void Wave21XfaTests::nonXfaPdfIsRejected() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::writeXfaFieldValue(plainPdf(), QStringLiteral("TextField1"),
                                       QStringLiteral("x"), &written);
    QVERIFY(!report.ok);
    QVERIFY(!report.error.isEmpty());
    QVERIFY2(report.error.contains(QStringLiteral("不是 XFA")), qPrintable(report.error));
    QVERIFY(written.isEmpty());
}

void Wave21XfaTests::emptyInputIsRejected() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::writeXfaFieldValue(QByteArray(), QStringLiteral("TextField1"),
                                       QStringLiteral("x"), &written);
    QVERIFY(!report.ok);
    QVERIFY(!report.error.isEmpty());
    QVERIFY(written.isEmpty());
}

void Wave21XfaTests::xfaWithoutDatasetsIsRejected() {
    REQUIRE_QPDF();
    QByteArray written;
    const XfaWriteReport report =
        aipdf::pdf::writeXfaFieldValue(xfaOnCatalog(xfaWithoutDatasets()),
                                       QStringLiteral("TextField1"), QStringLiteral("x"), &written);
    QVERIFY(!report.ok);
    QVERIFY(!report.error.isEmpty());
    QVERIFY2(report.error.contains(QStringLiteral("数据集")), qPrintable(report.error));
    QVERIFY(written.isEmpty());
}

void Wave21XfaTests::runnerComputesFieldValue() {
    QVector<XfaScriptField> fields;
    XfaScriptField amount;
    amount.name = QStringLiteral("Amount");
    amount.value = QStringLiteral("10");
    amount.numeric = true;
    fields.append(amount);

    const aipdf::pdf::XfaScriptRun run = evaluateXfaScript(
        QStringLiteral("xfa.form.form1.Total.value = xfa.form.form1.Amount.value * 2;"), fields,
        QString());
    QVERIFY2(run.ok, qPrintable(run.error));
    QCOMPARE(run.assignments.size(), 1);
    QCOMPARE(run.assignments.at(0).first, QStringLiteral("Total"));
    QCOMPARE(run.assignments.at(0).second, QStringLiteral("20"));
    QVERIFY(run.writes.contains(QStringLiteral("Total")));
    QVERIFY(run.reads.contains(QStringLiteral("Amount")));
}

void Wave21XfaTests::runnerReportsUnsupportedSyntax() {
    const aipdf::pdf::XfaScriptRun run = evaluateXfaScript(
        QStringLiteral("for (var i = 0; i < 3; i++) { xfa.form.form1.Total.value = i; }"),
        QVector<XfaScriptField>(), QString());
    QVERIFY(!run.ok);
    QVERIFY(!run.error.isEmpty());
    QVERIFY2(run.error.contains(QStringLiteral("不支持的语法")), qPrintable(run.error));
}

void Wave21XfaTests::runnerNamesUnknownFieldAsWarning() {
    QVector<XfaScriptField> fields;
    XfaScriptField amount;
    amount.name = QStringLiteral("Amount");
    amount.value = QStringLiteral("10");
    amount.numeric = true;
    fields.append(amount);

    const aipdf::pdf::XfaScriptRun run = evaluateXfaScript(
        QStringLiteral("xfa.form.form1.Total.value = xfa.form.form1.Missing.value;"), fields,
        QString());
    // 引用不存在的字段不算失败：脚本照样跑完，但要在提示里点名。
    QVERIFY2(run.ok, qPrintable(run.error));
    QVERIFY(!run.warnings.isEmpty());
    bool named = false;
    for (const QString& warning : run.warnings) {
        if (warning.contains(QStringLiteral("Missing"))) named = true;
    }
    QVERIFY2(named, "未知字段必须在提示里点名");
}

QTEST_GUILESS_MAIN(Wave21XfaTests)
#include "Wave21XfaTests.moc"
