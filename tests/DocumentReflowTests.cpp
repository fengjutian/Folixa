#include "pdf/ops/DocumentReflow.h"

#include "pdf/compliance/StructTreeReader.h"

#include <QTest>

#include <fpdf_text.h>
#include <fpdfview.h>

using aipdf::pdf::DocumentReflowResult;
using aipdf::pdf::readStructTree;
using aipdf::pdf::ReflowOptions;
using aipdf::pdf::reflowTaggedDocument;
using aipdf::pdf::StructTreeReport;

namespace {

// PDFium 的库初始化。
void initPdfium() {
    FPDF_LIBRARY_CONFIG config = {};
    config.version = 2;
    FPDF_InitLibraryWithConfig(&config);
}

// 用 PDFium 打开一段字节，返回页数；打不开返回 -1。
int pageCountOf(const QByteArray& bytes) {
    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(bytes.constData(), static_cast<int>(bytes.size()), nullptr);
    if (document == nullptr) return -1;
    const int count = FPDF_GetPageCount(document);
    FPDF_CloseDocument(document);
    return count;
}

// 用 PDFium 取某一页的文字（全部页拼起来）。
//
// 为什么用 PDFium 取文字：这是与本模块完全无关的一条路——
// 「重排后的文字真的印在页面上」这件事，得由渲染器说了算。
QString textOf(const QByteArray& bytes) {
    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(bytes.constData(), static_cast<int>(bytes.size()), nullptr);
    if (document == nullptr) return QString();
    QString result;
    const int pages = FPDF_GetPageCount(document);
    for (int index = 0; index < pages; ++index) {
        FPDF_PAGE page = FPDF_LoadPage(document, index);
        if (page == nullptr) continue;
        FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
        if (textPage != nullptr) {
            const int count = FPDFText_CountChars(textPage);
            QByteArray raw;
            raw.resize(count * 2);
            if (count > 0) {
                FPDFText_GetText(textPage, 0, count, reinterpret_cast<unsigned short*>(raw.data()));
            }
            result += QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()), count);
            FPDFText_ClosePage(textPage);
        }
        FPDF_ClosePage(page);
    }
    FPDF_CloseDocument(document);
    return result;
}

// 用 PDFium 取某一页的文字。
QString textOfPage(const QByteArray& bytes, int pageIndex) {
    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(bytes.constData(), static_cast<int>(bytes.size()), nullptr);
    if (document == nullptr) return QString();
    QString result;
    FPDF_PAGE page = FPDF_LoadPage(document, pageIndex);
    if (page != nullptr) {
        FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
        if (textPage != nullptr) {
            const int count = FPDFText_CountChars(textPage);
            QByteArray raw;
            raw.resize(count * 2);
            if (count > 0) {
                FPDFText_GetText(textPage, 0, count, reinterpret_cast<unsigned short*>(raw.data()));
            }
            result = QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()), count);
            FPDFText_ClosePage(textPage);
        }
        FPDF_ClosePage(page);
    }
    FPDF_CloseDocument(document);
    return result;
}

// 造一份 PDF。
//
// xref 表必须覆盖到最大的对象号：表短了的话，后面几个对象就成了「找不到的引用」。
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
    return QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
        .arg(content.size())
        .arg(QString::fromLatin1(content))
        .toLatin1();
}

// 没有标签树的普通 PDF：用来验「没有结构标签就拒绝重排」。
QByteArray plainPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject("BT /F1 12 Tf 40 250 Td (Plain text) Tj ET")));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    return buildPdf(objects);
}

// 带标签的两页 PDF。
//
// 结构刻意做成常见的样子：Document 下面挂 H1、P（里面再套 Span）、Figure、
// Table（里面挂 TD）、Head。
//   * H1、P、TD 是正文 → 重新排版；
//   * Figure 是图片 → 位置与图片一起搬过去；
//   * Table 的表格结构按行列重建，单元格文字写进重建出来的表格；
//   * Head 是页眉 → 不混进正文流，按「每页重复」的语义写到每一页上。
QByteArray taggedPdf() {
    const QByteArray pageOne =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 250 Td (Chapter one) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 1 >> BDC\n"
        "BT /F1 12 Tf 40 220 Td (Body paragraph text) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 2 >> BDC\n"
        "% 这里是图片，标签树里没有对应的文字\n"
        "EMC";
    const QByteArray pageTwo =
        "/Tag << /MCID 3 >> BDC\n"
        "BT /F1 12 Tf 40 250 Td (Cell text) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 4 >> BDC\n"
        "BT /F1 12 Tf 40 220 Td (Running head) Tj ET\n"
        "EMC";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /Lang (zh-CN) "
                                            "/StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(pageOne)));
    objects.append(qMakePair(5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 6 0 R /StructParents 1 >>")));
    objects.append(qMakePair(6, streamObject(pageTwo)));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [9 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R "
                                             "/K [10 0 R 11 0 R 12 0 R 13 0 R 16 0 R] >>")));
    // 标题：自己的 MCID。
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /H1 /P 9 0 R "
                                             "/Pg 3 0 R /K 0 >>")));
    // 段落：文字在 Span 里，段落自己没文字。
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /P /P 9 0 R "
                                             "/Pg 3 0 R /K [14 0 R] >>")));
    // 图片：没有文字。
    objects.append(qMakePair(12, QByteArray("<< /Type /StructElem /S /Figure /P 9 0 R "
                                             "/Pg 3 0 R /Alt (Figure alt) /K 2 >>")));
    // 表格：结构保留不住，单元格文字在 TD 里。
    objects.append(qMakePair(13, QByteArray("<< /Type /StructElem /S /Table /P 9 0 R "
                                             "/Pg 5 0 R /K [15 0 R] >>")));
    objects.append(qMakePair(14, QByteArray("<< /Type /StructElem /S /Span /P 11 0 R /K 1 >>")));
    objects.append(qMakePair(15, QByteArray("<< /Type /StructElem /S /TD /P 13 0 R "
                                             "/Pg 5 0 R /K 3 >>")));
    // 页眉：按「每页重复」的语义写出去，不混进正文流。
    objects.append(qMakePair(16, QByteArray("<< /Type /StructElem /S /Head /P 9 0 R "
                                             "/Pg 5 0 R /K 4 >>")));
    return buildPdf(objects);
}

// 一份「有东西搬不过去」的标签文档：正文是一段正常文字。
//
// 搬不过去靠**排版参数**触发（测试里把首行缩进设得比可用宽度还大），
// 不是靠造一份怪内容：layoutParagraph 在这种参数下会明确失败，
// 这一段就搬不过去，会进「丢弃清单」，门禁也会因此拦住写回。
QByteArray droppablePdf() {
    const QByteArray pageContent =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 250 Td (A paragraph that does not fit into the new page) Tj ET\n"
        "EMC";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 6 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 5 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(pageContent)));
    objects.append(qMakePair(5, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(6, QByteArray("<< /Type /StructTreeRoot /K [8 0 R] >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructElem /S /Document /P 6 0 R /K [9 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /P /P 8 0 R /Pg 3 0 R /K 0 >>")));
    return buildPdf(objects);
}

// 只有图片的标签文档：正文一个字都没有。
QByteArray imageOnlyPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject("/Tag << /MCID 0 >> BDC\n% 图片\nEMC")));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [9 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R /K [10 0 R] >>")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /Figure /P 9 0 R "
                                             "/Pg 3 0 R /Alt (only a picture) /K 0 >>")));
    return buildPdf(objects);
}

// 文字里带「标准 14 号字体表示不了」的字符的标签文档。
//
// 怎么把非 Latin-1 的文字放进标签树：靠字体的 /Encoding /Differences。
// 字形码 0x41 映射到字形名 "minus"（U+2212 数学减号），pdf::readStructTree
// 就会把内容流里的 (A) 还原成「−」。
//
// （/ToUnicode 那条路目前在本仓库的 readStructTree 里解析不出东西——
// CMap 段落的切分差了一个字符。那是别人的模块，不在本次改动范围内。）
//
// 这一份专门用来验「标准 14 号字体表示不了的字符会被写成 '?'」这件事被如实报告。
QByteArray unrepresentableTextPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 6 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 5 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject("/Tag << /MCID 0 >> BDC\n"
                                              "BT /F1 12 Tf 40 250 Td (A) Tj ET\nEMC")));
    objects.append(qMakePair(5, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                            "/Encoding << /Type /Encoding /Differences [65 /minus] >> >>")));
    objects.append(qMakePair(6, QByteArray("<< /Type /StructTreeRoot /K [8 0 R] >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructElem /S /Document /P 6 0 R /K [9 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /P /P 8 0 R /Pg 3 0 R /K 0 >>")));
    return buildPdf(objects);
}

// 一份常规的重排参数。
ReflowOptions defaultOptions() {
    ReflowOptions options;
    options.pageWidth = 300.0;
    options.pageHeight = 400.0;
    options.marginLeft = 20.0;
    options.marginRight = 20.0;
    options.marginTop = 20.0;
    options.marginBottom = 20.0;
    options.paragraph.fontSize = 11.0;
    options.paragraph.lineSpacing = 1.2;
    return options;
}

}  // namespace

class DocumentReflowTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- 负例 ----

    void emptyInputFails() {
        const DocumentReflowResult result = reflowTaggedDocument(QByteArray(), defaultOptions());
        QVERIFY(!result.ok());
        QVERIFY2(result.report.error.contains(QStringLiteral("没有可重排的 PDF 内容")),
                 qPrintable(result.report.error));
    }

    void notAPdfFails() {
        const DocumentReflowResult result =
            reflowTaggedDocument(QByteArray("not a pdf"), defaultOptions());
        QVERIFY(!result.ok());
        QVERIFY2(!result.report.error.isEmpty(), "必须写明失败原因");
        QVERIFY(result.bytes.isEmpty());
    }

    void aDocumentWithoutTagsIsRefused() {
        const DocumentReflowResult result = reflowTaggedDocument(plainPdf(), defaultOptions());
        QVERIFY(!result.ok());
        QVERIFY2(result.report.error.contains(QStringLiteral("没有结构标签")),
                 qPrintable(result.report.error));
        // 拒绝的理由要讲清楚「为什么不能凑合」。
        QVERIFY2(result.report.error.contains(QStringLiteral("拒绝重排")),
                 qPrintable(result.report.error));
        QVERIFY(result.bytes.isEmpty());
    }

    void aNonPositivePageWidthFails() {
        ReflowOptions options = defaultOptions();
        options.pageWidth = 0.0;
        DocumentReflowResult result = reflowTaggedDocument(taggedPdf(), options);
        QVERIFY(!result.ok());
        QVERIFY2(result.report.error.contains(QStringLiteral("新页宽必须大于 0")),
                 qPrintable(result.report.error));

        options.pageWidth = -100.0;
        result = reflowTaggedDocument(taggedPdf(), options);
        QVERIFY(!result.ok());
        QVERIFY2(result.report.error.contains(QStringLiteral("新页宽必须大于 0")),
                 qPrintable(result.report.error));
    }

    void marginsThatEatTheWidthFail() {
        ReflowOptions options = defaultOptions();
        options.pageWidth = 100.0;
        options.marginLeft = 60.0;
        options.marginRight = 60.0;
        const DocumentReflowResult result = reflowTaggedDocument(taggedPdf(), options);
        QVERIFY(!result.ok());
        QVERIFY2(result.report.error.contains(QStringLiteral("可用")),
                 qPrintable(result.report.error));
    }

    void aNonPositiveFontSizeFails() {
        ReflowOptions options = defaultOptions();
        options.paragraph.fontSize = 0.0;
        const DocumentReflowResult result = reflowTaggedDocument(taggedPdf(), options);
        QVERIFY(!result.ok());
        QVERIFY2(result.report.error.contains(QStringLiteral("字号")), qPrintable(result.report.error));
    }

    void aDocumentWithoutBodyTextIsRefused() {
        const DocumentReflowResult result = reflowTaggedDocument(imageOnlyPdf(), defaultOptions());
        QVERIFY(!result.ok());
        QVERIFY2(result.report.error.contains(QStringLiteral("没有可重排的正文")),
                 qPrintable(result.report.error));
    }

    // ---- 重排本身 ----

    void theReflowedPdfOpensAndHasPages() {
        const DocumentReflowResult result = reflowTaggedDocument(taggedPdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        QVERIFY(!result.bytes.isEmpty());
        // PDFium 能打开，而且页数大于 0。
        const int pages = pageCountOf(result.bytes);
        QVERIFY2(pages > 0, "重排后的 PDF 必须有页");
    }

    void theReflowedPdfKeepsTheBodyText() {
        const DocumentReflowResult result = reflowTaggedDocument(taggedPdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        const QString text = textOf(result.bytes);
        QVERIFY2(text.contains(QStringLiteral("Chapter one")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("Body paragraph text")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("Cell text")), qPrintable(text));
    }

    void theRunningHeadIsNotMoved() {
        // 这里的「不搬」指的是**不混进正文流**：页眉不是正文块，
        // 它按「每页重复」的语义单独写到每一页上。
        //
        // （旧语义是「页眉整句话都不出现在新文档里」——
        //  新语义下页眉是要搬过去的，所以那条断言已经不成立。）
        const DocumentReflowResult result = reflowTaggedDocument(taggedPdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        // 页眉算页眉，不算正文文字块。
        QCOMPARE(result.report.sourceCounts.headers, 1);
        QCOMPARE(result.report.sourceCounts.textBlocks, 3);
        QVERIFY(result.report.outputCounts.headers >= 1);
        QCOMPARE(result.report.outputCounts.textBlocks, 3);
    }

    void theRunningHeadIsRepeatedOnEveryPage() {
        const DocumentReflowResult result = reflowTaggedDocument(taggedPdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        QVERIFY(result.report.outputPageCount > 0);
        // 每一页上都有那句页眉。
        for (int pageIndex = 0; pageIndex < result.report.outputPageCount; ++pageIndex) {
            const QString text = textOfPage(result.bytes, pageIndex);
            QVERIFY2(text.contains(QStringLiteral("Running head")),
                     qPrintable(QStringLiteral("第 %1 页没有页眉：%2").arg(pageIndex).arg(text)));
        }
    }

    void theReportCountsTheParagraphs() {
        const DocumentReflowResult result = reflowTaggedDocument(taggedPdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        // 标题、段落、表格单元格这三块是正文。
        QCOMPARE(result.report.blockCount, 3);
        QCOMPARE(result.report.reflowedBlockCount, 3);
        QCOMPARE(result.report.sourcePageCount, 2);
        QVERIFY(result.report.outputPageCount > 0);
    }

    void theDroppedListMatchesTheDroppedCount() {
        // 全都搬过去了：丢弃清单必须是空的。
        const DocumentReflowResult result = reflowTaggedDocument(taggedPdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        QVERIFY(result.report.dropped.isEmpty());
        QCOMPARE(result.report.droppedCount, result.report.dropped.size());
        QCOMPARE(result.report.droppedCount, 0);
    }

    void theDroppedListNamesWhatWasNotMoved() {
        // 有一块确实搬不过去时，清单要写清楚是哪一块、为什么。
        ReflowOptions options = defaultOptions();
        options.paragraph.firstLineIndent = options.pageWidth;
        const DocumentReflowResult result = reflowTaggedDocument(droppablePdf(), options);
        QVERIFY2(!result.ok(), "搬不过去时门禁必须拦住写回");
        QVERIFY2(result.bytes.isEmpty(), "门禁拦住时不能产出字节");
        QVERIFY(!result.report.dropped.isEmpty());
        QCOMPARE(result.report.droppedCount, result.report.dropped.size());

        QStringList tags;
        QStringList reasons;
        for (const aipdf::pdf::ReflowDropped& item : result.report.dropped) {
            QVERIFY2(!item.reason.isEmpty(), "每一条丢弃都要写明原因");
            tags.append(item.tag);
            reasons.append(item.reason);
        }
        // 搬不过去的是那一段正文（P）。
        QVERIFY2(tags.contains(QStringLiteral("P")), qPrintable(tags.join(QLatin1Char(','))));
        QVERIFY2(reasons.join(QLatin1Char('\n')).contains(QStringLiteral("排不进去")),
                 qPrintable(reasons.join(QLatin1Char('\n'))));
        // 门禁的错误也要说清楚少了什么。
        QVERIFY2(result.report.error.contains(QStringLiteral("1 个文字块")),
                 qPrintable(result.report.error));
    }

    void aNarrowerPageNeedsMorePages() {
        ReflowOptions wide = defaultOptions();
        wide.pageWidth = 600.0;
        const DocumentReflowResult wideResult = reflowTaggedDocument(taggedPdf(), wide);
        QVERIFY2(wideResult.ok(), qPrintable(wideResult.report.error));

        ReflowOptions narrow = defaultOptions();
        narrow.pageWidth = 120.0;
        narrow.pageHeight = 200.0;
        const DocumentReflowResult narrowResult = reflowTaggedDocument(taggedPdf(), narrow);
        QVERIFY2(narrowResult.ok(), qPrintable(narrowResult.report.error));

        QVERIFY2(narrowResult.report.outputPageCount >= wideResult.report.outputPageCount,
                 "页宽变窄，页数不该变少");
    }

    void theOutputHasItsOwnStructureTree() {
        const DocumentReflowResult result = reflowTaggedDocument(taggedPdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        // 重排后的文档仍然是带标签的：可访问性不能因为重排而丢掉。
        const StructTreeReport tree = readStructTree(result.bytes);
        QVERIFY2(tree.ok(), qPrintable(tree.error));
        QVERIFY2(tree.hasStructTree, "重排后的文档应当重新打上标签");
        QVERIFY(!tree.elements.isEmpty());
    }

    void theOutputCanBeReflowedAgain() {
        const DocumentReflowResult first = reflowTaggedDocument(taggedPdf(), defaultOptions());
        QVERIFY2(first.ok(), qPrintable(first.report.error));
        // 重排出来的文档还是带标签的，所以还能再重排一次。
        const DocumentReflowResult second = reflowTaggedDocument(first.bytes, defaultOptions());
        QVERIFY2(second.ok(), qPrintable(second.report.error));
        QVERIFY(second.bytes.size() > 0);
        QVERIFY(pageCountOf(second.bytes) > 0);
    }

    void unrepresentableCharactersAreReportedHonestly() {
        const DocumentReflowResult result =
            reflowTaggedDocument(unrepresentableTextPdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        // 中文在标准 14 号字体里表示不了，排版时会写成 '?'；
        // 这不是「丢掉」，但也不能装作没发生。
        QCOMPARE(result.report.degradedBlockCount, 1);
        QVERIFY2(result.report.notes.join(QLatin1Char('\n')).contains(QStringLiteral("'")),
                 qPrintable(result.report.notes.join(QLatin1Char('\n'))));
    }

    void theNotesSayTheLayoutIsNotPreserved() {
        const DocumentReflowResult result = reflowTaggedDocument(taggedPdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        const QString notes = result.report.notes.join(QLatin1Char('\n'));
        QVERIFY2(notes.contains(QStringLiteral("版面")), qPrintable(notes));
    }
};

void DocumentReflowTests::initTestCase() {
    initPdfium();
}

// 这里用 QTEST_MAIN 而不是 QTEST_GUILESS_MAIN。
//
// 原因：pdf::layoutParagraph 用 QFontMetricsF 量文字宽度，而 Qt 的字体库必须在
// QGuiApplication 之后才能访问——QTEST_GUILESS_MAIN 建的是 QCoreApplication，
// 会在 QFontDatabase 处直接 QFATAL（整个测试进程 abort）。
// 仓库里 tests/ParagraphFormatTests.cpp 有同样的做法与同样的说明。
QTEST_MAIN(DocumentReflowTests)
#include "DocumentReflowTests.moc"
