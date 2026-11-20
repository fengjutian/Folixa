#include "pdf/compliance/SemanticAltText.h"

#include <QTest>

#if AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <memory>
#include <vector>
#endif

using aipdf::pdf::readSemanticTreeIndex;
using aipdf::pdf::SemanticTreeIndex;
using aipdf::pdf::SemanticElementInfo;
using aipdf::pdf::SemanticAltTextOptions;
using aipdf::pdf::SemanticAltTextResult;
using aipdf::pdf::setSemanticAltText;

namespace {

// 造一份 PDF。
//
// xref 表必须覆盖到最大的对象号：表短了的话，后面几个对象就成了「找不到的引用」，
// qpdf 会去重建 xref，行为就不确定了。
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

QByteArray streamObject(const QByteArray& content) {
    // endstream 前面不加换行：/Length 已经写死了内容长度，多一个换行
    // qpdf 会报「expected endstream」。
    return QStringLiteral("<< /Length %1 >>\nstream\n%2endstream")
        .arg(content.size())
        .arg(QString::fromLatin1(content))
        .toLatin1();
}

// 一份普通 PDF：没有标签树。
QByteArray plainPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << >> /Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject("BT (Plain) Tj ET")));
    return buildPdf(objects);
}

// 一份有标签树的 PDF：一张图（走 /OBJR）、一个表单域（widget 直接挂在 /K 里）、
// 两个带 /Alt 的文字元素。
//
// 形状刻意与本仓库 TagTreeWriter 的输出一致：
//   * 图像元素：/K 是数组，里面是直接写的 /OBJR 字典，/Obj 指向图像 XObject；
//   * 表单域元素：/K 里直接是 widget 注解（不是 /OBJR）。
QByteArray taggedPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /Lang (zh-CN) "
                                            "/StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /XObject << /Im1 5 0 R >> "
                                            "/Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R /Annots [6 0 R] /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject("BT /F1 12 Tf 40 250 Td (Page text) Tj ET")));
    // 图像 XObject（流）。
    objects.append(qMakePair(5, QByteArray("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 "
                                            "/ColorSpace /DeviceGray /BitsPerComponent 8 /Length 4 >>\n"
                                            "stream\nAAAA" "endstream")));
    // 表单域的 widget 注解。
    objects.append(qMakePair(6, QByteArray("<< /Type /Annot /Subtype /Widget /Rect [10 10 120 30] "
                                            "/FT /Tx /T (Name) /TU (Your name) /P 3 0 R >>")));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [9 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R "
                                            "/K [10 0 R 11 0 R 12 0 R 13 0 R] >>")));
    // 图：/K 里是 /OBJR，/Obj 指向对象 5。
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /Figure /P 9 0 R /Pg 3 0 R "
                                             "/Alt (Old image caption) "
                                             "/K [<< /Type /OBJR /Obj 5 0 R /Pg 3 0 R >>] >>")));
    // 表单域：/K 里直接是 widget（对象 6）。
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /Form /P 9 0 R /Pg 3 0 R "
                                             "/Alt (Old field hint) /K [6 0 R] >>")));
    // 两个带 /Alt 的文字元素：改图的时候它们不能跟着变。
    objects.append(qMakePair(12, QByteArray("<< /Type /StructElem /S /H1 /P 9 0 R /Pg 3 0 R "
                                             "/T (Chapter) /Alt (Heading caption) /K 0 >>")));
    objects.append(qMakePair(13, QByteArray("<< /Type /StructElem /S /P /P 9 0 R /Pg 3 0 R "
                                             "/Alt (Paragraph caption) /K 1 >>")));
    return buildPdf(objects);
}

// 有标签树，但树里没有任何元素引用对象 5。
QByteArray taggedPdfWithoutImageReference() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /XObject << /Im1 5 0 R >> >> "
                                            "/Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject("BT (Page text) Tj ET")));
    objects.append(qMakePair(5, QByteArray("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 "
                                            "/ColorSpace /DeviceGray /BitsPerComponent 8 /Length 4 >>\n"
                                            "stream\nAAAA" "endstream")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [9 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R "
                                            "/K [10 0 R] >>")));
    // 文字元素：跟图像对象毫无关系。
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /P /P 9 0 R /Pg 3 0 R "
                                             "/Alt (Paragraph caption) /K 0 >>")));
    return buildPdf(objects);
}

// 同一个图像 XObject 被两页各引用一次（页眉页脚那种 logo）。
QByteArray sharedImagePdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /XObject << /Im1 6 0 R >> >> "
                                            "/Contents 5 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /XObject << /Im1 6 0 R >> >> "
                                            "/Contents 7 0 R /StructParents 1 >>")));
    objects.append(qMakePair(5, streamObject("BT (Page one) Tj ET")));
    objects.append(qMakePair(6, QByteArray("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 "
                                            "/ColorSpace /DeviceGray /BitsPerComponent 8 /Length 4 >>\n"
                                            "stream\nAAAA" "endstream")));
    objects.append(qMakePair(7, streamObject("BT (Page two) Tj ET")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [9 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R "
                                            "/K [10 0 R 11 0 R] >>")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /Figure /P 9 0 R /Pg 3 0 R "
                                             "/Alt (Logo on page one) "
                                             "/K [<< /Type /OBJR /Obj 6 0 R /Pg 3 0 R >>] >>")));
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /Figure /P 9 0 R /Pg 4 0 R "
                                             "/Alt (Logo on page two) "
                                             "/K [<< /Type /OBJR /Obj 6 0 R /Pg 4 0 R >>] >>")));
    return buildPdf(objects);
}

#if AIPDF_WITH_QPDF
// 一页的内容流（解压后的字节）：用来确认「页面内容没被动」。
QByteArray pageContentOf(const QByteArray& pdfBytes, int pageIndex) {
    QPDF pdf;
    pdf.processMemoryFile("probe.pdf", pdfBytes.constData(),
                          static_cast<size_t>(pdfBytes.size()));
    const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return {};
    QPDFObjectHandle page = pages.at(static_cast<size_t>(pageIndex));
    QByteArray content;
    for (const QPDFObjectHandle& item : page.getPageContents()) {
        if (!item.isStream()) continue;
        QPDFObjectHandle stream = item;
        const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
        if (!data) continue;
        content += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                               static_cast<qsizetype>(data->getSize()));
    }
    return content;
}

// 一页的批注字典（unparse 出来比较）：用来确认「批注没被动」。
QByteArray pageAnnotationsOf(const QByteArray& pdfBytes, int pageIndex) {
    QPDF pdf;
    pdf.processMemoryFile("probe.pdf", pdfBytes.constData(),
                          static_cast<size_t>(pdfBytes.size()));
    const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return {};
    const QPDFObjectHandle annotations = pages.at(static_cast<size_t>(pageIndex)).getKey("/Annots");
    if (!annotations.isArray()) return {};
    QByteArray out;
    for (const QPDFObjectHandle& item : annotations.getArrayAsVector()) {
        out += item.unparse();
        out += '\n';
    }
    return out;
}
#endif  // AIPDF_WITH_QPDF

int indexOfTag(const SemanticTreeIndex& index, const QString& tag) {
    for (int i = 0; i < index.elements.size(); ++i) {
        if (index.elements.at(i).tag == tag) return i;
    }
    return -1;
}

}  // namespace

class SemanticAltTextTests : public QObject {
    Q_OBJECT

private slots:
    // ---- 读侧 ----

    void readsTheSemanticTreeIndex() {
        const SemanticTreeIndex index = readSemanticTreeIndex(taggedPdf());
        QVERIFY2(index.ok(), qPrintable(index.error));
        QVERIFY(index.hasStructTree);
        // Document / Figure / Form / H1 / P。
        QCOMPARE(index.elements.size(), 5);
        QCOMPARE(index.elements.at(0).tag, QStringLiteral("Document"));
        QCOMPARE(index.elements.at(1).tag, QStringLiteral("Figure"));
        QCOMPARE(index.elements.at(1).alt, QStringLiteral("Old image caption"));
        QCOMPARE(index.elements.at(2).tag, QStringLiteral("Form"));
        QCOMPARE(index.elements.at(2).alt, QStringLiteral("Old field hint"));
    }

    void mapsTheImageAndTheWidgetToTheirElements() {
        const SemanticTreeIndex index = readSemanticTreeIndex(taggedPdf());
        QVERIFY2(index.ok(), qPrintable(index.error));
        const int figure = indexOfTag(index, QStringLiteral("Figure"));
        const int form = indexOfTag(index, QStringLiteral("Form"));
        QVERIFY(figure >= 0);
        QVERIFY(form >= 0);
        // 图：/OBJR 的 /Obj 是对象 5。表单域：widget 直接挂在 /K 里，是对象 6。
        QVERIFY(index.elements.at(figure).referencedObjectIds.contains(5));
        QVERIFY(index.elements.at(form).referencedObjectIds.contains(6));
        // 按对象号反查也要能查回来。
        QCOMPARE(index.indexOfObject(5), figure);
        QCOMPARE(index.indexOfObject(6), form);
        QCOMPARE(index.indexOfObject(999), -1);
    }

    void reportsAnEmptyTreeWhenThereIsNoStructTreeRoot() {
        const SemanticTreeIndex index = readSemanticTreeIndex(plainPdf());
        QVERIFY(index.ok());
        QVERIFY(!index.hasStructTree);
        QVERIFY(index.elements.isEmpty());
    }

    // ---- 写侧：改得到 ----

    void changesTheAltOfTheElementThatReferencesTheImage() {
        const SemanticAltTextResult result =
            setSemanticAltText(taggedPdf(), 5, QStringLiteral("新的图片说明"));
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY(!result.bytes.isEmpty());
        QCOMPARE(result.elementObjectId, 10);
        QCOMPARE(result.previousAlt, QStringLiteral("Old image caption"));

        const SemanticTreeIndex after = readSemanticTreeIndex(result.bytes);
        QVERIFY2(after.ok(), qPrintable(after.error));
        const int figure = indexOfTag(after, QStringLiteral("Figure"));
        QVERIFY(figure >= 0);
        QCOMPARE(after.elements.at(figure).alt, QStringLiteral("新的图片说明"));
    }

    void changesTheAltOfTheElementThatReferencesTheWidget() {
        const SemanticAltTextResult result =
            setSemanticAltText(taggedPdf(), 6, QStringLiteral("新的表单域说明"));
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.elementObjectId, 11);

        const SemanticTreeIndex after = readSemanticTreeIndex(result.bytes);
        QVERIFY2(after.ok(), qPrintable(after.error));
        const int form = indexOfTag(after, QStringLiteral("Form"));
        QVERIFY(form >= 0);
        QCOMPARE(after.elements.at(form).alt, QStringLiteral("新的表单域说明"));
    }

    void leavesTheOtherElementsAlone() {
        const SemanticTreeIndex before = readSemanticTreeIndex(taggedPdf());
        QVERIFY2(before.ok(), qPrintable(before.error));
        const SemanticAltTextResult result =
            setSemanticAltText(taggedPdf(), 5, QStringLiteral("新的图片说明"));
        QVERIFY2(result.ok(), qPrintable(result.error));

        const SemanticTreeIndex after = readSemanticTreeIndex(result.bytes);
        QVERIFY2(after.ok(), qPrintable(after.error));
        QCOMPARE(after.elements.size(), before.elements.size());
        for (int i = 0; i < before.elements.size(); ++i) {
            if (before.elements.at(i).objectId == 10) {
                QCOMPARE(after.elements.at(i).alt, QStringLiteral("新的图片说明"));
                continue;
            }
            QCOMPARE(after.elements.at(i).alt, before.elements.at(i).alt);
            // 对象号不能直接比（qpdf 写回时重新编号），比引用的个数与类型。
            QCOMPARE(after.elements.at(i).referencedObjectIds.size(),
                     before.elements.at(i).referencedObjectIds.size());
            QCOMPARE(after.elements.at(i).referencedObjectTypes,
                     before.elements.at(i).referencedObjectTypes);
        }
    }

    void leavesThePageContentAndTheAnnotationsAlone() {
        const QByteArray original = taggedPdf();
        const SemanticAltTextResult result =
            setSemanticAltText(original, 5, QStringLiteral("新的图片说明"));
        QVERIFY2(result.ok(), qPrintable(result.error));
#if AIPDF_WITH_QPDF
        QCOMPARE(pageContentOf(result.bytes, 0), pageContentOf(original, 0));
        QCOMPARE(pageAnnotationsOf(result.bytes, 0), pageAnnotationsOf(original, 0));
#else
        QVERIFY(true);
#endif
    }

    // ---- 写侧：改不到 ----

    void reportsAnErrorWhenNoElementReferencesTheObject() {
        const SemanticAltTextResult result =
            setSemanticAltText(taggedPdf(), 999, QStringLiteral("说明"));
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains(QStringLiteral("没有引用该对象")), qPrintable(result.error));
        // 失败时绝不产出改了一半的文件。
        QVERIFY(result.bytes.isEmpty());
    }

    void reportsAnErrorWhenTheDocumentHasNoSemanticTree() {
        const SemanticAltTextResult result =
            setSemanticAltText(plainPdf(), 5, QStringLiteral("说明"));
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains(QStringLiteral("没有语义标签树")), qPrintable(result.error));
        QVERIFY(result.bytes.isEmpty());
    }

    void reportsAnErrorWhenTheTreeHasNoElementForTheImage() {
        const SemanticAltTextResult result =
            setSemanticAltText(taggedPdfWithoutImageReference(), 5, QStringLiteral("说明"));
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains(QStringLiteral("没有引用该对象")), qPrintable(result.error));
        QVERIFY(result.bytes.isEmpty());
    }

    // ---- 空替代文本：行为是明确的（默认拒绝，显式允许才清空） ----

    void rejectsAnEmptyAlternativeTextByDefault() {
        const SemanticAltTextResult result = setSemanticAltText(taggedPdf(), 5, QString());
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains(QStringLiteral("不能为空")), qPrintable(result.error));
        QVERIFY(result.bytes.isEmpty());
    }

    void clearsTheAlternativeTextWhenClearingIsAllowed() {
        SemanticAltTextOptions options;
        options.allowClear = true;
        const SemanticAltTextResult result =
            setSemanticAltText(taggedPdf(), 5, QString(), options);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY(result.cleared);
        QCOMPARE(result.previousAlt, QStringLiteral("Old image caption"));

        const SemanticTreeIndex after = readSemanticTreeIndex(result.bytes);
        QVERIFY2(after.ok(), qPrintable(after.error));
        const int figure = indexOfTag(after, QStringLiteral("Figure"));
        QVERIFY(figure >= 0);
        QVERIFY(after.elements.at(figure).alt.isEmpty());
        // 其它元素不受影响。
        const int form = indexOfTag(after, QStringLiteral("Form"));
        QVERIFY(form >= 0);
        QCOMPARE(after.elements.at(form).alt, QStringLiteral("Old field hint"));
    }

    // ---- 同一个对象被多个元素引用：靠页码区分 ----

    void reportsAnErrorWhenSeveralElementsReferenceTheSameObject() {
        const SemanticAltTextResult result =
            setSemanticAltText(sharedImagePdf(), 6, QStringLiteral("新的 logo 说明"));
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains(QStringLiteral("无法确定改哪一个")), qPrintable(result.error));
        QVERIFY(result.bytes.isEmpty());
    }

    void usesThePageToPickTheRightElement() {
        SemanticAltTextOptions options;
        options.pageIndex = 1;
        const SemanticAltTextResult result =
            setSemanticAltText(sharedImagePdf(), 6, QStringLiteral("新的 logo 说明"), options);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.elementObjectId, 11);

        const SemanticTreeIndex after = readSemanticTreeIndex(result.bytes);
        QVERIFY2(after.ok(), qPrintable(after.error));
        const int first = indexOfTag(after, QStringLiteral("Figure"));
        QVERIFY(first >= 0);
        QCOMPARE(after.elements.at(first).alt, QStringLiteral("Logo on page one"));
        QCOMPARE(after.elements.at(first + 1).alt, QStringLiteral("新的 logo 说明"));
    }
};

QTEST_GUILESS_MAIN(SemanticAltTextTests)
#include "SemanticAltTextTests.moc"
