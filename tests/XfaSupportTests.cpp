#include "pdf/forms/XfaSupport.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

using aipdf::pdf::XfaField;
using aipdf::pdf::XfaReport;
using aipdf::pdf::XfaStatus;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 把对象列表拼成一份带正确 xref 的 PDF。
//
// 为什么要自己造：XFA 的形态（/XFA 放在目录上还是 /AcroForm 里、
// 里面是单个流还是数组、模板里有没有数据集）每一处都能改变结论，
// 只有自己拼才能把每一种形态单独钉住。
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

// 没有 /XFA 的普通 PDF。
QByteArray plainPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [] /Count 0 >>")));
    return buildPdf(objects, 1);
}

// /XFA 写在文档目录上的 PDF（规范位置是 /AcroForm，但确实有生成器这么写）。
QByteArray xfaOnCatalog(const QByteArray& xml) {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /XFA [4 0 R] >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [] /Count 0 >>")));
    objects.append(qMakePair(4, streamObject(QByteArray(), xml)));
    return buildPdf(objects, 1);
}

// /XFA 写在 /AcroForm 里的 PDF（规范位置）。
QByteArray xfaInAcroForm(const QByteArray& xml) {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /AcroForm 3 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [] /Count 0 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Fields [] /XFA [4 0 R] >>")));
    objects.append(qMakePair(4, streamObject(QByteArray(), xml)));
    return buildPdf(objects, 1);
}

// 一份静态 XFA：模板里有三个字段（其中一个没有 name），数据集里带值。
//
// 注意 XFA 的数据集里，值的元素名就是字段名本身（<xfa:TextField1>），
// 不是 <xfa:field name="TextField1">——提取器必须认得这一点。
QByteArray staticXfaXml() {
    return QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xdp/\" "
        "xmlns:xfa=\"http://www.xfa.org/schema/xfa-template/3.3/\">\n"
        " <xdp:template>\n"
        "  <xfa:subform name=\"form1\">\n"
        "   <xfa:field name=\"TextField1\" required=\"true\">\n"
        "    <xfa:ui><xfa:textEdit>"
        "<xfa:caption><xfa:value><xfa:text>姓名</xfa:text></xfa:value></xfa:caption>"
        "</xfa:textEdit></xfa:ui>\n"
        "   </xfa:field>\n"
        "   <xfa:field name=\"CheckBox1\">\n"
        "    <xfa:ui><xfa:checkBox/></xfa:ui>\n"
        "   </xfa:field>\n"
        "   <xfa:field>\n"
        "    <xfa:ui><xfa:textEdit/></xfa:ui>\n"
        "   </xfa:field>\n"
        "  </xfa:subform>\n"
        " </xdp:template>\n"
        " <xdp:datasets>\n"
        "  <xfa:datasets xmlns:xfa=\"http://www.xfa.org/schema/xfa-data/1.0/\">\n"
        "   <xfa:data>\n"
        "    <xfa:form1>\n"
        "     <xfa:TextField1><xfa:value><xfa:text>张三</xfa:text></xfa:value></xfa:TextField1>\n"
        "    </xfa:form1>\n"
        "   </xfa:data>\n"
        "  </xfa:datasets>\n"
        " </xdp:datasets>\n"
        "</xdp:xdp>\n");
}

// 带动态逻辑的 XFA：模板里有脚本和校验。
QByteArray dynamicXfaXml() {
    return QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xdp/\" "
        "xmlns:xfa=\"http://www.xfa.org/schema/xfa-template/3.3/\">\n"
        " <xdp:template>\n"
        "  <xfa:subform name=\"form1\">\n"
        "   <xfa:field name=\"TextField1\">\n"
        "    <xfa:ui><xfa:textEdit/></xfa:ui>\n"
        "    <xfa:script><xfa:exObject name=\"calc\"/></xfa:script>\n"
        "    <xfa:validation><xfa:validate type=\"required\"/></xfa:validation>\n"
        "   </xfa:field>\n"
        "  </xfa:subform>\n"
        " </xdp:template>\n"
        "</xdp:xdp>\n");
}

// 动态逻辑写在数据集里的 XFA（计算与绑定）。
QByteArray datasetsLogicXfaXml() {
    return QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xdp/\" "
        "xmlns:xfa=\"http://www.xfa.org/schema/xfa-template/3.3/\">\n"
        " <xdp:template>\n"
        "  <xfa:subform name=\"form1\">\n"
        "   <xfa:field name=\"TextField1\">\n"
        "    <xfa:ui><xfa:textEdit/></xfa:ui>\n"
        "   </xfa:field>\n"
        "  </xfa:subform>\n"
        " </xdp:template>\n"
        " <xdp:datasets>\n"
        "  <xfa:datasets xmlns:xfa=\"http://www.xfa.org/schema/xfa-data/1.0/\">\n"
        "   <xfa:data>\n"
        "    <xfa:form1>\n"
        "     <xfa:TextField1><xfa:value><xfa:text>v</xfa:text></xfa:value></xfa:TextField1>\n"
        "    </xfa:form1>\n"
        "   </xfa:data>\n"
        "   <xfa:calculate><xfa:script/></xfa:calculate>\n"
        "   <xfa:bind match=\"Data.Value\"/>\n"
        "  </xfa:datasets>\n"
        " </xdp:datasets>\n"
        "</xdp:xdp>\n");
}

// XML 损坏的 XFA。
QByteArray brokenXfaXml() {
    return QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xdp/\">\n"
        " <xdp:template><xfa:field name=\"TextField1\">\n"
        "</xdp:xdp>\n");
}

const XfaField* findField(const XfaReport& report, const QString& name) {
    for (const XfaField& field : report.fields) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

}  // namespace

class XfaSupportTests : public QObject {
    Q_OBJECT

private slots:
    void emptyBytesFail();
    void nonPdfBytesFail();
    void plainPdfIsNotXfa();
    void staticXfaFieldsAreExtracted();
    void fieldWithoutNameIsSkippedAndCounted();
    void labelFallsBackToFieldName();
    void requiredFlagIsRead();
    void valueComesFromDatasets();
    void scriptMarksDynamicLogic();
    void validationMarksDynamicLogic();
    void calculateAndBindMarkDynamicLogic();
    void datasetsLogicIsMarkedAsDynamic();
    void brokenXmlFailsWithChineseReason();
    void xfaInsideAcroFormIsFound();
    void csvStartsWithUtf8Bom();
    void csvHasChineseHeaderAndCrlf();
    void csvEscapesCommaAndQuote();
    void csvOfEmptyReportIsHeaderOnly();
    void csvRowCountMatchesFieldCount();
    void summaryDescribesState();
};

void XfaSupportTests::emptyBytesFail() {
    QString error;
    const XfaReport report = aipdf::pdf::inspectXfa(QByteArray(), &error);
    QCOMPARE(report.status, XfaStatus::Error);
    QVERIFY(!report.error.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(report.error.contains(QStringLiteral("空")));
    QVERIFY(!report.isXfa);
}

void XfaSupportTests::nonPdfBytesFail() {
    QString error;
    const XfaReport report = aipdf::pdf::inspectXfa(QByteArray("not a pdf"), &error);
    QCOMPARE(report.status, XfaStatus::Error);
    QVERIFY(!report.error.isEmpty());
    QVERIFY(!error.isEmpty());
}

void XfaSupportTests::plainPdfIsNotXfa() {
    REQUIRE_QPDF();
    QString error;
    const XfaReport report = aipdf::pdf::inspectXfa(plainPdf(), &error);
    // 不是 XFA 表单不是错误，只是一个事实：状态是 NotXfa，错误说明为空。
    QCOMPARE(report.status, XfaStatus::NotXfa);
    QVERIFY(!report.isXfa);
    QVERIFY(report.error.isEmpty());
    QVERIFY(error.isEmpty());
    QVERIFY(report.fields.isEmpty());
    QVERIFY(report.summary().contains(QStringLiteral("不是 XFA")));
}

void XfaSupportTests::staticXfaFieldsAreExtracted() {
    REQUIRE_QPDF();
    const XfaReport report = aipdf::pdf::inspectXfa(xfaOnCatalog(staticXfaXml()));
    QCOMPARE(report.status, XfaStatus::Static);
    QVERIFY(report.isXfa);
    QVERIFY(!report.hasDynamicLogic);
    QVERIFY(report.dynamicMarkers.isEmpty());
    // 三个模板字段里有一个没有 name，只剩两个能导出。
    QCOMPARE(report.fields.size(), 2);

    const XfaField* text = findField(report, QStringLiteral("TextField1"));
    QVERIFY(text != nullptr);
    QCOMPARE(text->label, QStringLiteral("姓名"));
    QCOMPARE(text->type, QStringLiteral("textField"));
    QCOMPARE(text->value, QStringLiteral("张三"));
    QVERIFY(text->required);

    const XfaField* check = findField(report, QStringLiteral("CheckBox1"));
    QVERIFY(check != nullptr);
    QCOMPARE(check->type, QStringLiteral("checkBox"));
    QVERIFY(!check->required);
}

void XfaSupportTests::fieldWithoutNameIsSkippedAndCounted() {
    REQUIRE_QPDF();
    const XfaReport report = aipdf::pdf::inspectXfa(xfaOnCatalog(staticXfaXml()));
    // 没有 name 的字段没法导出成有意义的行：跳过并报数。
    QCOMPARE(report.skippedWithoutName, 1);
    QCOMPARE(report.fields.size(), 2);
    QVERIFY(findField(report, QString()) == nullptr);
}

void XfaSupportTests::labelFallsBackToFieldName() {
    REQUIRE_QPDF();
    const XfaReport report = aipdf::pdf::inspectXfa(xfaOnCatalog(staticXfaXml()));
    const XfaField* check = findField(report, QStringLiteral("CheckBox1"));
    QVERIFY(check != nullptr);
    // 没有 caption 的字段，标签退回字段名，别留空。
    QCOMPARE(check->label, QStringLiteral("CheckBox1"));
}

void XfaSupportTests::requiredFlagIsRead() {
    REQUIRE_QPDF();
    const XfaReport report = aipdf::pdf::inspectXfa(xfaOnCatalog(staticXfaXml()));
    const XfaField* text = findField(report, QStringLiteral("TextField1"));
    const XfaField* check = findField(report, QStringLiteral("CheckBox1"));
    QVERIFY(text != nullptr);
    QVERIFY(check != nullptr);
    QVERIFY(text->required);
    QVERIFY(!check->required);
}

void XfaSupportTests::valueComesFromDatasets() {
    REQUIRE_QPDF();
    const XfaReport report = aipdf::pdf::inspectXfa(xfaOnCatalog(staticXfaXml()));
    const XfaField* text = findField(report, QStringLiteral("TextField1"));
    QVERIFY(text != nullptr);
    // 模板里没有值，值在数据集里（XFA 的数据集用字段名做元素名）。
    QCOMPARE(text->value, QStringLiteral("张三"));
}

void XfaSupportTests::scriptMarksDynamicLogic() {
    REQUIRE_QPDF();
    const XfaReport report = aipdf::pdf::inspectXfa(xfaOnCatalog(dynamicXfaXml()));
    QCOMPARE(report.status, XfaStatus::Dynamic);
    QVERIFY(report.hasDynamicLogic);
    QVERIFY(report.dynamicMarkers.contains(QStringLiteral("script")));
    // 字段照样抽得出来，只是不能装作脚本已经执行过。
    QCOMPARE(report.fields.size(), 1);
    QVERIFY(report.summary().contains(QStringLiteral("动态")));
}

void XfaSupportTests::validationMarksDynamicLogic() {
    REQUIRE_QPDF();
    const XfaReport report = aipdf::pdf::inspectXfa(xfaOnCatalog(dynamicXfaXml()));
    QVERIFY(report.hasDynamicLogic);
    QVERIFY(report.dynamicMarkers.contains(QStringLiteral("validation")));
}

void XfaSupportTests::calculateAndBindMarkDynamicLogic() {
    REQUIRE_QPDF();
    const XfaReport report = aipdf::pdf::inspectXfa(xfaOnCatalog(datasetsLogicXfaXml()));
    QCOMPARE(report.status, XfaStatus::Dynamic);
    QVERIFY(report.dynamicMarkers.contains(QStringLiteral("calculate")));
    QVERIFY(report.dynamicMarkers.contains(QStringLiteral("bind")));
}

void XfaSupportTests::datasetsLogicIsMarkedAsDynamic() {
    REQUIRE_QPDF();
    const XfaReport report = aipdf::pdf::inspectXfa(xfaOnCatalog(datasetsLogicXfaXml()));
    QVERIFY(report.hasDynamicLogic);
    // 数据集里的业务逻辑要单独点出来：用户得知道「值是算出来的」。
    QVERIFY(report.dynamicMarkers.contains(QStringLiteral("datasets")));
}

void XfaSupportTests::brokenXmlFailsWithChineseReason() {
    REQUIRE_QPDF();
    QString error;
    const XfaReport report = aipdf::pdf::inspectXfa(xfaOnCatalog(brokenXfaXml()), &error);
    QCOMPARE(report.status, XfaStatus::Error);
    QVERIFY(report.isXfa);
    QVERIFY(!report.error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("XML")), "失败原因要写清是 XML 解析失败");
    QVERIFY(report.summary().contains(QStringLiteral("失败")));
}

void XfaSupportTests::xfaInsideAcroFormIsFound() {
    REQUIRE_QPDF();
    const XfaReport report = aipdf::pdf::inspectXfa(xfaInAcroForm(staticXfaXml()));
    QCOMPARE(report.status, XfaStatus::Static);
    QVERIFY(report.isXfa);
    QCOMPARE(report.fields.size(), 2);
}

void XfaSupportTests::csvStartsWithUtf8Bom() {
    XfaReport report;
    XfaField field;
    field.name = QStringLiteral("TextField1");
    field.label = QStringLiteral("姓名");
    field.type = QStringLiteral("textField");
    field.value = QStringLiteral("张三");
    field.required = true;
    report.fields.append(field);

    const QByteArray csv = aipdf::pdf::exportXfaFieldsToCsv(report);
    // BOM：Windows 上的 Excel 靠它认 UTF-8，不然中文表头是乱码。
    QVERIFY(csv.startsWith(QByteArray("\xEF\xBB\xBF", 3)));
}

void XfaSupportTests::csvHasChineseHeaderAndCrlf() {
    XfaReport report;
    XfaField field;
    field.name = QStringLiteral("TextField1");
    field.label = QStringLiteral("姓名");
    field.type = QStringLiteral("textField");
    field.value = QStringLiteral("张三");
    field.required = true;
    report.fields.append(field);

    const QByteArray csv = aipdf::pdf::exportXfaFieldsToCsv(report);
    const QString text = QString::fromUtf8(csv.mid(3));
    const QStringList lines = text.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0), QStringLiteral("名称,标签,类型,当前值,是否必填"));
    QVERIFY(lines.at(1).contains(QStringLiteral("姓名")));
    QVERIFY(lines.at(1).contains(QStringLiteral("是")));
    // 行尾是 CRLF，不是 LF。
    QVERIFY(text.contains(QStringLiteral("\r\n")));
}

void XfaSupportTests::csvEscapesCommaAndQuote() {
    XfaReport report;
    XfaField field;
    field.name = QStringLiteral("TextField1");
    field.label = QStringLiteral("逗号, 和引号\"");
    field.type = QStringLiteral("textField");
    field.value = QStringLiteral("a\"b");
    report.fields.append(field);

    const QByteArray csv = aipdf::pdf::exportXfaFieldsToCsv(report);
    const QString text = QString::fromUtf8(csv.mid(3));
    const QStringList lines = text.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
    // 含逗号与引号的单元格要用双引号包起来，里面的引号写成两个。
    QVERIFY(lines.at(1).contains(QStringLiteral("\"逗号, 和引号\"\"\"")));
    QVERIFY(lines.at(1).contains(QStringLiteral("\"a\"\"b\"")));
}

void XfaSupportTests::csvOfEmptyReportIsHeaderOnly() {
    XfaReport report;
    const QByteArray csv = aipdf::pdf::exportXfaFieldsToCsv(report);
    const QString text = QString::fromUtf8(csv.mid(3));
    const QStringList lines = text.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.at(0), QStringLiteral("名称,标签,类型,当前值,是否必填"));
}

void XfaSupportTests::csvRowCountMatchesFieldCount() {
    REQUIRE_QPDF();
    const XfaReport report = aipdf::pdf::inspectXfa(xfaOnCatalog(staticXfaXml()));
    const QByteArray csv = aipdf::pdf::exportXfaFieldsToCsv(report);
    const QString text = QString::fromUtf8(csv.mid(3));
    const QStringList lines = text.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), report.fields.size() + 1);
}

void XfaSupportTests::summaryDescribesState() {
    REQUIRE_QPDF();
    const XfaReport notXfa = aipdf::pdf::inspectXfa(plainPdf());
    QVERIFY(notXfa.summary().contains(QStringLiteral("不是 XFA")));

    const XfaReport dynamic = aipdf::pdf::inspectXfa(xfaOnCatalog(dynamicXfaXml()));
    QVERIFY(dynamic.summary().contains(QStringLiteral("动态")));
    // 摘要里必须说明「不执行脚本」，不能让用户以为值是准的。
    QVERIFY(dynamic.summary().contains(QStringLiteral("不执行脚本")));
}

QTEST_GUILESS_MAIN(XfaSupportTests)
#include "XfaSupportTests.moc"
