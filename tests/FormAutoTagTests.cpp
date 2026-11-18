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
//
// 合并语义（文档本来就有标签树时）：
//   * 树里原有的其它结构元素（/H1、/P、/Span）与阅读顺序必须**原样保留**；
//   * 树里旧的 /Form 元素必须被**替换**掉，不能变成两份；
//   * 结果里要说清是「新建」还是「合并」，以及保留了多少既有元素。

#include "pdf/forms/FormAutoTag.h"

#include "pdf/forms/FormFieldWriter.h"
#include "pdf/compliance/StructTreeReader.h"

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

// 一份**已经有标签树**的 PDF：
//   * 标签树里有章节标题（/H1）、段落（/P）、行内元素（/Span）——用户自己打的标签；
//   * 页面上有一个表单域（AcroForm 字段 + widget 注解）。
//
// 打标签不能把上面这些既有元素弄丢：那是用户已经排好的阅读顺序。
QByteArray taggedPdfWithExistingTree() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [6 0 R] >> "
                       "/StructTreeRoot 4 0 R /MarkInfo << /Marked true >> >>"});
    objects.append({2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"});
    objects.append({3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [6 0 R] "
                       "/StructParents 0 /Resources << >> >>"});
    objects.append({4, "<< /Type /StructTreeRoot /K [5 0 R] /ParentTree << /Nums [0 []] >> >>"});
    objects.append({5, "<< /Type /StructElem /S /Document /P 4 0 R /K [9 0 R 10 0 R] >>"});
    objects.append({6, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (customerName) "
                       "/TU <FEFF5BA2623759D3540D> /Rect [72 700 272 720] /F 4 >>"});
    objects.append({9, "<< /Type /StructElem /S /H1 /T (Chapter One) /P 5 0 R /Pg 3 0 R "
                       "/K [11 0 R] >>"});
    objects.append({10, "<< /Type /StructElem /S /P /P 5 0 R /Pg 3 0 R /K [12 0 R] >>"});
    objects.append({11, "<< /Type /StructElem /S /Span /P 9 0 R /Pg 3 0 R >>"});
    objects.append({12, "<< /Type /StructElem /S /Span /P 10 0 R /Pg 3 0 R >>"});
    return buildPdf(objects);
}

// 一份「标签树里已经有旧的 /Form 元素」的 PDF：
// 上一轮（或别的工具）给这个域打过标签——结构树里留着一个 /Form 元素，
// widget 上还挂着 /StructParent，ParentTree 里也指着那个旧元素。
//
// 重新打标签必须是**替换**（树里只剩一个 /Form 元素），不能变成两份。
QByteArray taggedPdfWithStaleFormElement() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [6 0 R] >> "
                       "/StructTreeRoot 4 0 R /MarkInfo << /Marked true >> >>"});
    objects.append({2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"});
    objects.append({3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [6 0 R] "
                       "/StructParents 0 /Resources << >> >>"});
    objects.append({4, "<< /Type /StructTreeRoot /K [5 0 R] /ParentTree << /Nums [0 [13 0 R]] >> >>"});
    objects.append({5, "<< /Type /StructElem /S /Document /P 4 0 R /K [9 0 R 13 0 R] >>"});
    objects.append({6, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (customerName) "
                       "/TU <FEFF5BA2623759D3540D> /Rect [72 700 272 720] /F 4 /StructParent 0 >>"});
    objects.append({9, "<< /Type /StructElem /S /H1 /T (Chapter One) /P 5 0 R /Pg 3 0 R >>"});
    objects.append({13, "<< /Type /StructElem /S /Form /T (customerName) /Alt (stale alt) "
                        "/P 5 0 R /Pg 3 0 R /K [6 0 R] >>"});
    return buildPdf(objects);
}

// 一份「文档元素埋在章节下面」的 PDF：
// 标签树是 根 → /Sect → [/Document, /H1]，文档元素不在根的下一层。
//
// 合并时必须认出这个**已有的**文档元素：另建一个就会让同一份文件出现两棵文档分支。
QByteArray taggedPdfWithNestedDocumentElement() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [6 0 R] >> "
                       "/StructTreeRoot 4 0 R /MarkInfo << /Marked true >> >>"});
    objects.append({2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"});
    objects.append({3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [6 0 R] "
                       "/StructParents 0 /Resources << >> >>"});
    objects.append({4, "<< /Type /StructTreeRoot /K [9 0 R] /ParentTree << /Nums [0 []] >> >>"});
    objects.append({6, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (customerName) "
                       "/TU <FEFF5BA2623759D3540D> /Rect [72 700 272 720] /F 4 >>"});
    objects.append({9, "<< /Type /StructElem /S /Sect /P 4 0 R /Pg 3 0 R /K [10 0 R 11 0 R] >>"});
    objects.append({10, "<< /Type /StructElem /S /Document /P 9 0 R /K [12 0 R] >>"});
    objects.append({11, "<< /Type /StructElem /S /H1 /T (Chapter One) /P 9 0 R /Pg 3 0 R >>"});
    objects.append({12, "<< /Type /StructElem /S /P /P 10 0 R /Pg 3 0 R >>"});
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

// 找出某个标签的元素下标；找不到返回 -1。
//
// 为什么需要：J7 之后表单域挂在**页的 /Part** 下面，不再平铺在文档元素下，
// 所以断言父节点时要先找到那个 /Part 在哪。
int indexOfTag(const StructTreeReport& report, const QString& tag) {
    for (int i = 0; i < report.elements.size(); ++i) {
        if (report.elements.at(i).tag == tag) return i;
    }
    return -1;
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

// 读回 ParentTree 里某个键对应的数组，报告每个条目的结构标签（/S）。
//
// 为什么需要它：结构元素挂对了还不够——widget 的 /StructParent 指向的那个下标
// 必须在 ParentTree 里真的落到这个结构元素上，否则读屏软件顺着下标找不到人。
QVector<QString> parentTreeTags(const QByteArray& bytes, int key, QString* error) {
    QVector<QString> tags;
    try {
        QPDF pdf;
        pdf.processMemoryFile("form-auto-tag-parenttree.pdf", bytes.constData(),
                              static_cast<size_t>(bytes.size()));
        const QPDFObjectHandle root = pdf.getRoot().getKey("/StructTreeRoot");
        const QPDFObjectHandle nums = root.getKey("/ParentTree").getKey("/Nums");
        if (!nums.isArray()) {
            if (error) *error = QStringLiteral("ParentTree 里没有 /Nums");
            return tags;
        }
        const std::vector<QPDFObjectHandle> items = nums.getArrayAsVector();
        for (size_t i = 0; i + 1 < items.size(); i += 2) {
            if (!items.at(i).isInteger()) continue;
            if (static_cast<int>(items.at(i).getIntValue()) != key) continue;
            const QPDFObjectHandle value = items.at(i + 1);
            if (!value.isArray()) continue;
            for (const QPDFObjectHandle& entry : value.getArrayAsVector()) {
                if (!entry.isDictionary()) {
                    tags.append(QStringLiteral("(null)"));
                    continue;
                }
                const QPDFObjectHandle tag = entry.getKey("/S");
                tags.append(tag.isName() ? QString::fromLatin1(tag.getName().c_str() + 1)
                                         : QStringLiteral("(none)"));
            }
        }
    } catch (const std::exception& exception) {
        if (error) *error = QString::fromUtf8(exception.what());
    }
    return tags;
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
    void createsTagTreeWhenDocumentHasNone();
    void mergesIntoExistingTagTreeKeepingOtherElements();
    void replacesStaleFormElementInExistingTagTree();
    void readBackReadsPreservedAndNewElements();
    void mergesIntoNestedDocumentElement();
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

// 文档本来没有标签树 → 新建一棵（不是合并）。
void FormAutoTagTests::createsTagTreeWhenDocumentHasNone() {
    REQUIRE_QPDF();
    QByteArray pdf = minimalPdf();
    pdf = addField(pdf, QStringLiteral("customerName"), QStringLiteral("客户姓名"), 700.0);
    QVERIFY(!pdf.isEmpty());

    const FormAutoTagResult result = autoTagFormFields(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    // 新建：没有可保留的既有元素。
    QVERIFY(!result.mergedIntoExistingTree);
    QCOMPARE(result.preservedElementCount, 0);
    QVERIFY(result.preservedTags.isEmpty());
    QCOMPARE(result.taggedFieldCount, 1);

    const StructTreeReport report = readStructTree(result.bytes);
    QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
    QVERIFY(report.hasStructTree);
    QCOMPARE(formElements(report).size(), 1);
}

// 文档已经有标签树 → 合并：既有的 /H1、/P、/Span 一个都不能丢，阅读顺序不变，
// 新的 /Form 挂进**已有的**文档元素（不是另起一棵树）。
void FormAutoTagTests::mergesIntoExistingTagTreeKeepingOtherElements() {
    REQUIRE_QPDF();
    const FormAutoTagResult result = autoTagFormFields(taggedPdfWithExistingTree());
    QVERIFY2(result.ok, qPrintable(result.error));

    // 这次是合并，不是新建。
    QVERIFY(result.mergedIntoExistingTree);
    // 保留下来的既有元素：文档元素 + /H1 + /P + 两个 /Span。
    QCOMPARE(result.preservedElementCount, 5);

    QHash<QString, int> byTag;
    for (const aipdf::pdf::FormTagPreservedTag& item : result.preservedTags) {
        byTag.insert(item.tag, item.count);
    }
    QCOMPARE(byTag.value(QStringLiteral("Document")), 1);
    QCOMPARE(byTag.value(QStringLiteral("H1")), 1);
    QCOMPARE(byTag.value(QStringLiteral("P")), 1);
    QCOMPARE(byTag.value(QStringLiteral("Span")), 2);

    const StructTreeReport report = readStructTree(result.bytes);
    QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
    QVERIFY(report.hasStructTree);

    // 既有元素的顺序不变：文档元素 → /H1 → /Span → /P → /Span。
    QStringList preservedOrder;
    for (int i = 0; i < 5 && i < report.elements.size(); ++i) {
        preservedOrder.append(report.elements.at(i).tag);
    }
    QCOMPARE(preservedOrder,
             QStringList({QStringLiteral("Document"), QStringLiteral("H1"), QStringLiteral("Span"),
                          QStringLiteral("P"), QStringLiteral("Span")}));

    // 新的 /Form 元素挂在**所在页的 /Part** 下面（J7 之后不再平铺在文档元素下：
    // 域平铺在文档下时，读屏念出来的域顺序与页面上看到的顺序对不上）。
    const QVector<StructElement> forms = formElements(report);
    QCOMPARE(forms.size(), 1);
    const int partIndex = indexOfTag(report, QStringLiteral("Part"));
    QVERIFY2(partIndex >= 0, "生成的标签树里应当有 /Part");
    QCOMPARE(forms.first().parentIndex, partIndex);
}

// 树里旧的 /Form 元素 → 被替换掉，不是叠加成两份。
void FormAutoTagTests::replacesStaleFormElementInExistingTagTree() {
    REQUIRE_QPDF();
    const FormAutoTagResult result = autoTagFormFields(taggedPdfWithStaleFormElement());
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.mergedIntoExistingTree);
    // 旧的 /Form 不算「保留下来的」：只有文档元素与 /H1。
    QCOMPARE(result.preservedElementCount, 2);

    const StructTreeReport report = readStructTree(result.bytes);
    QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
    // 不是叠加：结构树里只有一个 /Form 元素。
    const QVector<StructElement> forms = formElements(report);
    QCOMPARE(forms.size(), 1);
    // 而且是新的那个：替代文本是新写的，旧的替代文本不该还在。
    QCOMPARE(forms.first().alt, QString::fromUtf8("客户姓名"));
    for (const StructElement& element : report.elements) {
        QVERIFY(element.alt != QStringLiteral("stale alt"));
    }
    // 既有元素还在。
    QStringList tags;
    for (const StructElement& element : report.elements) tags.append(element.tag);
    QVERIFY(tags.contains(QStringLiteral("H1")));
}

// 读回校验：既有的元素与新的 /Form 元素都能从结构树里读出来，
// 而且 widget 的 /StructParent 顺着 ParentTree 真能落到那个 /Form 元素上。
void FormAutoTagTests::readBackReadsPreservedAndNewElements() {
    REQUIRE_QPDF();
    const FormAutoTagResult result = autoTagFormFields(taggedPdfWithExistingTree());
    QVERIFY2(result.ok, qPrintable(result.error));

    const StructTreeReport report = readStructTree(result.bytes);
    QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
    QVERIFY(report.hasStructTree);

    int headings = 0;
    int paragraphs = 0;
    int spans = 0;
    for (const StructElement& element : report.elements) {
        if (element.tag == QLatin1String("H1")) ++headings;
        else if (element.tag == QLatin1String("P")) ++paragraphs;
        else if (element.tag == QLatin1String("Span")) ++spans;
    }
    QCOMPARE(headings, 1);
    QCOMPARE(paragraphs, 1);
    QCOMPARE(spans, 2);

    // 新的 /Form 元素：标题是字段名，替代文本是提示文字。
    const QVector<StructElement> forms = formElements(report);
    QCOMPARE(forms.size(), 1);
    QCOMPARE(forms.first().title, QStringLiteral("customerName"));
    QCOMPARE(forms.first().alt, QString::fromUtf8("客户姓名"));

#ifdef AIPDF_WITH_QPDF
    // widget 的 /StructParent 指向的下标，在 ParentTree 里就是这个 /Form 元素。
    QString error;
    const QVector<int> parents = widgetStructParents(result.bytes, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(parents.size(), 1);
    QVERIFY(parents.first() >= 0);
    const QVector<QString> treeTags = parentTreeTags(result.bytes, parents.first(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(treeTags.size(), 1);
    QCOMPARE(treeTags.first(), QStringLiteral("Form"));
#endif
}

// 文档元素埋在章节下面 → 合并时认出这个已有的文档元素，不另起一棵树。
void FormAutoTagTests::mergesIntoNestedDocumentElement() {
    REQUIRE_QPDF();
    const FormAutoTagResult result = autoTagFormFields(taggedPdfWithNestedDocumentElement());
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.mergedIntoExistingTree);
    // 既有元素：/Sect、/Document、/P、/H1。
    QCOMPARE(result.preservedElementCount, 4);

    const StructTreeReport report = readStructTree(result.bytes);
    QVERIFY2(report.error.isEmpty(), qPrintable(report.error));

    int documents = 0;
    int documentIndex = -1;
    QStringList tags;
    for (int i = 0; i < report.elements.size(); ++i) {
        const StructElement& element = report.elements.at(i);
        tags.append(element.tag);
        if (element.tag == QLatin1String("Document")) {
            ++documents;
            documentIndex = i;
        }
    }
    // 只有一个文档元素：没有另起一棵文档分支。
    QCOMPARE(documents, 1);
    // 既有的元素都还在。
    QVERIFY(tags.contains(QStringLiteral("Sect")));
    QVERIFY(tags.contains(QStringLiteral("H1")));
    QVERIFY(tags.contains(QStringLiteral("P")));

    // 新的 /Form 挂在这个文档的**页 /Part** 下面。
    const QVector<StructElement> forms = formElements(report);
    QCOMPARE(forms.size(), 1);
    const int partIndex = indexOfTag(report, QStringLiteral("Part"));
    QVERIFY2(partIndex >= 0, "生成的标签树里应当有 /Part");
    QCOMPARE(forms.first().parentIndex, partIndex);
    // 这个 /Part 仍然挂在那个既有的文档元素下面 —— 没另起一棵文档分支。
    QCOMPARE(report.elements.at(partIndex).parentIndex, documentIndex);
}

QTEST_GUILESS_MAIN(FormAutoTagTests)

#include "FormAutoTagTests.moc"
