// 表单域自动打标签（结构标签树 + 替代文本）。
//
// 这里验的是「写出来的东西对不对」：
//   * 每个表单域在结构树里都要有一个 /Form 元素，/T 是字段名、/Alt 是替代文本；
//   * 替代文本按「提示文字 → 字段名」的顺序取；两者都没有时如实记一条问题，
//     不替用户编一句替代文本；
//   * widget 注解要挂上 /StructParent —— 没有它，读屏软件找不到对应的结构元素。
//
// 负例同样是重点：
//   * 文档里没有表单域 → 必须失败并说清楚中文原因，不生成一棵空标签树；
//   * 结构树里的 /Parent 成环 → 不能爆栈、不能死循环；
//   * 重跑一次 → 结构树里的 /Form 元素数量不能翻倍；
//   * 垃圾字节 → 必须失败并说清楚中文原因。

#include "pdf/FormAutoTag.h"

#include "pdf/FormFieldWriter.h"
#include "pdf/StructTreeReader.h"

#include <QRectF>
#include <QTest>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::autoTagFormFields;
using aipdf::pdf::FormAutoTagResult;
using aipdf::pdf::FormFieldInsert;
using aipdf::pdf::FormFieldKind;
using aipdf::pdf::FormFieldWriteResult;
using aipdf::pdf::appendFormField;
using aipdf::pdf::readStructTree;
using aipdf::pdf::StructElement;
using aipdf::pdf::StructTreeReport;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 造一份 PDF（经典 xref 表）。
//
// xref 表必须覆盖到最大的对象号：表短了的话，后面几个对象就成了「找不到的引用」，
// qpdf 会去重建 xref，读回来的东西就不确定了。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects) {
    int maxId = 0;
    for (const auto& object : objects) maxId = qMax(maxId, object.first);
    QByteArray out("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = maxId + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        if (!offsets.contains(id)) {
            out += "0000000000 65535 f \n";
            continue;
        }
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id, 0), 10, 10, QLatin1Char('0'))
                   .toLatin1();
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// 一份「有域但域不完整」的 PDF：
//   * orphanField：有字段却没有 widget 注解——没有可挂标签的对象；
//   * nowhereField：widget 存在，却不在任何页上——挂不上 /StructParent。
//
// 这两种都要如实记进 issues，不能当成打好了。
QByteArray incompleteFieldsPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [4 0 R 5 0 R] >> >>"});
    objects.append({2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"});
    objects.append({3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>"});
    objects.append({4, "<< /FT /Tx /T (orphanField) >>"});
    objects.append({5, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (nowhereField) "
                       "/Rect [72 700 272 720] /F 4 >>"});
    return buildPdf(objects);
}

// 一页普通的 PDF：没有任何表单域。
QByteArray plainPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, "<< /Type /Catalog /Pages 2 0 R >>"});
    objects.append({2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"});
    objects.append({3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>"});
    return buildPdf(objects);
}

// 一页有内容的 PDF：用来在上面建域。
QByteArray minimalPdf() {
    const QByteArray content = "BT /F1 12 Tf 72 700 Td (Hi) Tj ET\n";
    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, "<< /Type /Catalog /Pages 2 0 R >>"});
    objects.append({2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"});
    objects.append({3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R "
                       "/Resources << /Font << /F1 5 0 R >> >> >>"});
    objects.append({4, "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content +
                           "endstream"});
    objects.append({5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"});
    return buildPdf(objects);
}

// 三个 widget，一次覆盖替代文本的三种情形：
//   * withTip：有提示文字（/TU）——替代文本用提示文字；
//   * noTip：只有字段名（/T）——替代文本用字段名；
//   * 第三个既没有 /TU 也没有 /T —— 没有替代文本，如实记一条问题。
//
// /TU 写成 UTF-16BE 十六进制串：PDF 字面量串按 PDFDocEncoding 解，
// 直接塞 UTF-8 字节会读成乱码。
QByteArray threeAltCasesPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [4 0 R 5 0 R 6 0 R] >> >>"});
    objects.append({2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"});
    objects.append({3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                       "/Annots [4 0 R 5 0 R 6 0 R] /Resources << >> >>"});
    objects.append({4, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (withTip) "
                       "/TU <FEFF8BF7586B519959D3540D> /Rect [72 700 272 720] /F 4 >>"});
    objects.append({5, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (noTip) "
                       "/Rect [72 660 272 680] /F 4 >>"});
    objects.append({6, "<< /Type /Annot /Subtype /Widget /FT /Tx "
                       "/Rect [72 620 272 640] /F 4 >>"});
    return buildPdf(objects);
}

// 一份「结构树里有环」的 PDF：两个 /Form 元素互相当对方的 /Parent，
// 而且只有其中一个挂在树里——另一个只能靠 /Parent 反查找到。
//
// 这是坏文件：遍历它的时候必须停手，不能把自己绕死。
QByteArray cyclicParentPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [4 0 R] >> "
                       "/StructTreeRoot 5 0 R >>"});
    objects.append({2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"});
    objects.append({3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [4 0 R] "
                       "/Resources << >> >>"});
    objects.append({4, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (cycleField) "
                       "/Rect [72 700 272 720] /F 4 >>"});
    objects.append({5, "<< /Type /StructTreeRoot /K [6 0 R] /ParentTree << /Nums [0 []] >> >>"});
    objects.append({6, "<< /Type /StructElem /S /Sect /Pg 3 0 R /K [7 0 R] >>"});
    objects.append({7, "<< /Type /StructElem /S /Form /Parent 8 0 R /K [4 0 R] >>"});
    objects.append({8, "<< /Type /StructElem /S /Form /Parent 7 0 R /K [] >>"});
    return buildPdf(objects);
}

// 在一份 PDF 上建一个文本域。
QByteArray addField(const QByteArray& pdf, const QString& name, const QString& toolTip, double y) {
    FormFieldInsert field;
    field.pageIndex = 0;
    field.name = name;
    field.toolTip = toolTip;
    field.rect = QRectF(72.0, y, 200.0, 20.0);
    field.kind = FormFieldKind::Text;
    const FormFieldWriteResult written = appendFormField(pdf, field);
    if (!written.ok) return {};
    return written.bytes;
}

// 读出结构树里的 /Form 元素。
QVector<StructElement> formElements(const StructTreeReport& report) {
    QVector<StructElement> result;
    for (const StructElement& element : report.elements) {
        if (element.tag == QLatin1String("Form")) result.append(element);
    }
    return result;
}

#ifdef AIPDF_WITH_QPDF
// 读出所有 widget 注解的 /StructParent（没有就是 -1）。
QVector<int> widgetStructParents(const QByteArray& bytes, QString* error) {
    QVector<int> result;
    try {
        QPDF pdf;
        pdf.processMemoryFile("form-auto-tag-test.pdf", bytes.constData(),
                              static_cast<size_t>(bytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        for (const QPDFObjectHandle& page : pages) {
            const QPDFObjectHandle annots = page.getKey("/Annots");
            if (!annots.isArray()) continue;
            for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
                if (!annot.isDictionary()) continue;
                const QPDFObjectHandle subtype = annot.getKey("/Subtype");
                if (!subtype.isName() || subtype.getName() != "/Widget") continue;
                const QPDFObjectHandle structParent = annot.getKey("/StructParent");
                result.append(structParent.isInteger() ? static_cast<int>(structParent.getIntValue()) : -1);
            }
        }
    } catch (const std::exception& exception) {
        if (error) *error = QString::fromUtf8(exception.what());
    }
    return result;
}
#endif

}  // namespace

class FormAutoTagTests : public QObject {
    Q_OBJECT

private slots:
    void writesOneFormElementPerField();
    void altFollowsTooltipThenName();
    void widgetAnnotationsCarryStructParent();
    void reportsTaggedCountAndPageCount();
    void reportsFieldsWithoutWidgetOrPage();
    void failsWhenDocumentHasNoFormFields();
    void survivesCyclicParentChain();
    void rerunDoesNotDuplicateFormElements();
    void failsOnInvalidInput();
};

// 每个表单域都要在结构树里落一个 /Form 元素，/T 是字段名。
void FormAutoTagTests::writesOneFormElementPerField() {
    REQUIRE_QPDF();
    QByteArray pdf = minimalPdf();
    pdf = addField(pdf, QStringLiteral("customerName"), QStringLiteral("客户姓名"), 700.0);
    QVERIFY(!pdf.isEmpty());
    pdf = addField(pdf, QStringLiteral("phone"), QString(), 660.0);
    QVERIFY(!pdf.isEmpty());
    pdf = addField(pdf, QStringLiteral("address"), QString(), 620.0);
    QVERIFY(!pdf.isEmpty());

    const FormAutoTagResult result = autoTagFormFields(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.taggedFieldCount, 3);

    const StructTreeReport report = readStructTree(result.bytes);
    QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
    QVERIFY(report.hasStructTree);

    const QVector<StructElement> forms = formElements(report);
    QCOMPARE(forms.size(), 3);

    QStringList titles;
    for (const StructElement& element : forms) titles.append(element.title);
    QVERIFY(titles.contains(QStringLiteral("customerName")));
    QVERIFY(titles.contains(QStringLiteral("phone")));
    QVERIFY(titles.contains(QStringLiteral("address")));
}

// 替代文本：有提示文字用提示文字，只有字段名用字段名，两者都没有就如实记问题。
void FormAutoTagTests::altFollowsTooltipThenName() {
    REQUIRE_QPDF();
    const FormAutoTagResult result = autoTagFormFields(threeAltCasesPdf());
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.taggedFieldCount, 3);

    const StructTreeReport report = readStructTree(result.bytes);
    QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
    const QVector<StructElement> forms = formElements(report);
    QCOMPARE(forms.size(), 3);

    QString altWithTip;
    QString altNoTip;
    QString altMissing;
    for (const StructElement& element : forms) {
        if (element.title == QLatin1String("withTip")) altWithTip = element.alt;
        else if (element.title == QLatin1String("noTip")) altNoTip = element.alt;
        else altMissing = element.alt;
    }
    // 提示文字优先。
    QCOMPARE(altWithTip, QString::fromUtf8("请填写姓名"));
    // 没有提示文字就用字段名。
    QCOMPARE(altNoTip, QStringLiteral("noTip"));
    // 两者都没有：不编一句替代文本出来。
    QVERIFY(altMissing.isEmpty());

    // 而且这件事要进 issues，不能悄悄吞掉。
    QCOMPARE(result.issues.size(), 1);
    QVERIFY(!result.issues.first().fieldName.isEmpty());
    QVERIFY(result.issues.first().reason.contains(QString::fromUtf8("替代文本")));
}

// widget 注解要挂 /StructParent：没有它，读屏软件找不到对应的结构元素。
void FormAutoTagTests::widgetAnnotationsCarryStructParent() {
    REQUIRE_QPDF();
    const FormAutoTagResult result = autoTagFormFields(threeAltCasesPdf());
    QVERIFY2(result.ok, qPrintable(result.error));

#ifdef AIPDF_WITH_QPDF
    QString error;
    const QVector<int> parents = widgetStructParents(result.bytes, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(parents.size(), 3);
    for (int parent : parents) {
        QVERIFY(parent >= 0);  // 每个 widget 都要有下标
    }
    // 三个下标互不相同（不能都指向同一个结构元素）。
    QSet<int> unique(parents.begin(), parents.end());
    QCOMPARE(unique.size(), 3);
#endif
}

// 结果里要带上「打了几个域」与「几页」这两个数。
void FormAutoTagTests::reportsTaggedCountAndPageCount() {
    REQUIRE_QPDF();
    QByteArray pdf = minimalPdf();
    pdf = addField(pdf, QStringLiteral("onlyField"), QString(), 700.0);
    QVERIFY(!pdf.isEmpty());

    const FormAutoTagResult result = autoTagFormFields(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.taggedFieldCount, 1);
    QCOMPARE(result.pageCount, 1);
    QVERIFY(!result.bytes.isEmpty());
    QVERIFY(result.error.isEmpty());
}

// 域不完整时要如实记问题：没有 widget 的域、找不到页面的 widget，都要说出来。
void FormAutoTagTests::reportsFieldsWithoutWidgetOrPage() {
    REQUIRE_QPDF();
    const FormAutoTagResult result = autoTagFormFields(incompleteFieldsPdf());
    QVERIFY2(result.ok, qPrintable(result.error));
    // 两个域都要在结构树里有一个元素（域数与元素数对得上）。
    QCOMPARE(result.taggedFieldCount, 2);
    QCOMPARE(formElements(readStructTree(result.bytes)).size(), 2);

    // 两条问题：一条说没有 widget，一条说找不到页面。
    QCOMPARE(result.issues.size(), 2);
    QStringList reasons;
    for (const aipdf::pdf::FormTagIssue& issue : result.issues) {
        QVERIFY(!issue.fieldName.isEmpty());
        reasons.append(issue.reason);
    }
    QVERIFY(reasons.join(QLatin1Char('\n')).contains(QString::fromUtf8("widget 注解")));
    QVERIFY(reasons.join(QLatin1Char('\n')).contains(QString::fromUtf8("所在页面")));
}

// 负例：文档里一个表单域都没有 → 必须失败并说清楚中文原因。
void FormAutoTagTests::failsWhenDocumentHasNoFormFields() {
    REQUIRE_QPDF();
    const FormAutoTagResult result = autoTagFormFields(plainPdf());
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    // 原因得是中文，而且说清楚是「没有表单域」。
    QVERIFY(result.error.contains(QString::fromUtf8("表单域")));
    // 不能顺手生成一棵空标签树。
    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(result.taggedFieldCount, 0);
}

// 负例：结构树里的 /Parent 成环 → 不能爆栈、不能死循环。
void FormAutoTagTests::survivesCyclicParentChain() {
    REQUIRE_QPDF();
    const FormAutoTagResult result = autoTagFormFields(cyclicParentPdf());
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.taggedFieldCount, 1);

    // 环里的那两个 /Form 元素要被摘掉，只留下新写的这一个。
    const StructTreeReport report = readStructTree(result.bytes);
    QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
    QCOMPARE(formElements(report).size(), 1);
}

// 负例：重跑一次不能叠加——结构树里的 /Form 元素数量要与域数一致。
void FormAutoTagTests::rerunDoesNotDuplicateFormElements() {
    REQUIRE_QPDF();
    QByteArray pdf = minimalPdf();
    pdf = addField(pdf, QStringLiteral("customerName"), QStringLiteral("客户姓名"), 700.0);
    QVERIFY(!pdf.isEmpty());
    pdf = addField(pdf, QStringLiteral("phone"), QString(), 660.0);
    QVERIFY(!pdf.isEmpty());

    const FormAutoTagResult first = autoTagFormFields(pdf);
    QVERIFY2(first.ok, qPrintable(first.error));
    QCOMPARE(formElements(readStructTree(first.bytes)).size(), 2);

    // 同一份文件再跑一遍。
    const FormAutoTagResult second = autoTagFormFields(first.bytes);
    QVERIFY2(second.ok, qPrintable(second.error));
    QCOMPARE(second.taggedFieldCount, 2);

    const StructTreeReport report = readStructTree(second.bytes);
    QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
    // 关键：数量没有翻倍。
    QCOMPARE(formElements(report).size(), 2);
}

// 负例：垃圾字节 → 失败并给中文原因，不许崩。
void FormAutoTagTests::failsOnInvalidInput() {
    REQUIRE_QPDF();
    const FormAutoTagResult garbage = autoTagFormFields(QByteArray("this is not a pdf at all"));
    QVERIFY(!garbage.ok);
    QVERIFY(!garbage.error.isEmpty());
    QVERIFY(garbage.error.contains(QString::fromUtf8("失败")));
    QVERIFY(garbage.bytes.isEmpty());

    const FormAutoTagResult empty = autoTagFormFields(QByteArray());
    QVERIFY(!empty.ok);
    QVERIFY(!empty.error.isEmpty());
}

QTEST_GUILESS_MAIN(FormAutoTagTests)

#include "FormAutoTagTests.moc"
