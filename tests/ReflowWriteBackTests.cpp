#include "pdf/kernel/PageObjectModel.h"

#include "pdf/compliance/StructTreeReader.h"
#include "pdf/ops/DocumentReflow.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <fpdf_text.h>
#include <fpdfview.h>

using aipdf::pdf::DocumentReflowResult;
using aipdf::pdf::DocumentObjectModel;
using aipdf::pdf::PageObjectCounts;
using aipdf::pdf::PageObjectKind;
using aipdf::pdf::PageTextBlockIndex;
using aipdf::pdf::PageTextRole;
using aipdf::pdf::readPageObjectModel;
using aipdf::pdf::readStructTree;
using aipdf::pdf::ReflowOptions;
using aipdf::pdf::reflowTaggedDocument;
using aipdf::pdf::StructTreeReport;

namespace {

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

// 一张 40×20 的 RGB 位图（2400 字节）。
QByteArray imageData(int width, int height) {
    QByteArray data;
    data.resize(width * height * 3);
    for (int index = 0; index < data.size(); ++index) {
        data[index] = static_cast<char>(index % 251);
    }
    return data;
}

QByteArray imageObject(int id, int width, int height) {
    const QString head = QStringLiteral("<< /Type /XObject /Subtype /Image /Width %1 /Height %2 "
                                        "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Length %3 >>\n"
                                        "stream\n")
                             .arg(width)
                             .arg(height)
                             .arg(width * height * 3);
    return head.toLatin1() + imageData(width, height) + QByteArray("\nendstream");
}

// 一页的内容：页眉 + 正文 + 2×2 表格 + 一张图片。
//
// 版面（400×300）：
//   y=285  页眉「Running head」（MCID 0）
//   y=260  正文「Body paragraph text」（MCID 1）
//   y=220  表格第一行：A1（MCID 2）、B1（MCID 3）
//   y=200  表格第二行：A2（MCID 4）、B2（MCID 5）
//   图片：40,60 起，40×20 点（MCID 6）
QByteArray samplePageContent() {
    return "/Tag << /MCID 0 >> BDC\n"
           "BT /F1 12 Tf 40 285 Td (Running head) Tj ET\n"
           "EMC\n"
           "/Tag << /MCID 1 >> BDC\n"
           "BT /F1 12 Tf 40 260 Td (Body paragraph text) Tj ET\n"
           "EMC\n"
           "/Tag << /MCID 2 >> BDC\n"
           "BT /F1 10 Tf 40 220 Td (A1) Tj ET\n"
           "EMC\n"
           "/Tag << /MCID 3 >> BDC\n"
           "BT /F1 10 Tf 200 220 Td (B1) Tj ET\n"
           "EMC\n"
           "/Tag << /MCID 4 >> BDC\n"
           "BT /F1 10 Tf 40 200 Td (A2) Tj ET\n"
           "EMC\n"
           "/Tag << /MCID 5 >> BDC\n"
           "BT /F1 10 Tf 200 200 Td (B2) Tj ET\n"
           "EMC\n"
           "/Tag << /MCID 6 >> BDC\n"
           "q 40 0 0 20 40 60 cm /Im1 Do Q\n"
           "EMC";
}

// 与 samplePdf 相同，但正文是一段**排不进去**的段落。
//
// 排不进去的触发方式在测试里：把首行缩进设得比可用宽度还大
// （layoutParagraph 的失败条件之一：「首行缩进不小于可用的排版宽度」）。
// 段落排不进去 → 那一块没被搬过去 → 门禁发现「少了 1 个文字块」→ 拦住写回。
//
// 为什么不用「一个超长的单词」当触发条件：layoutParagraph 会按字符把
// 过长的单词拆开，所以那种情况其实**排得进去**，不会触发门禁。
QByteArray unlayoutablePdf() {
    const QByteArray pageContent =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 285 Td (Running head) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 1 >> BDC\n"
        "BT /F1 12 Tf 40 260 Td (A paragraph that does not fit into the new page) Tj ET\n"
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
    objects.append(qMakePair(8, QByteArray("<< /Type /StructElem /S /Document /P 6 0 R /K [9 0 R 10 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /Head /P 8 0 R /Pg 3 0 R /K 0 >>")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /P /P 8 0 R /Pg 3 0 R /K 1 >>")));
    return buildPdf(objects);
}

// 表格大到一页放不下：40 行 × 2 列。
//
// 门禁用例：新页高 400、上下边距各 20 → 可用高度 360；
// 40 行 × 每行约 20 点 = 800 点，放不下 → 表格搬不过去 → 门禁拦住写回。
QByteArray hugeTablePdf() {
    QString content;
    content += "/Tag << /MCID 0 >> BDC\n"
               "BT /F1 12 Tf 40 285 Td (Running head) Tj ET\n"
               "EMC\n";
    int mcid = 1;
    for (int row = 0; row < 40; ++row) {
        const double y = 260.0 - row * 6.0;
        content += QStringLiteral("/Tag << /MCID %1 >> BDC\n"
                                  "BT /F1 10 Tf 40 %2 Td (R%3C1) Tj ET\n"
                                  "EMC\n")
                       .arg(mcid++)
                       .arg(y)
                       .arg(row);
        content += QStringLiteral("/Tag << /MCID %1 >> BDC\n"
                                  "BT /F1 10 Tf 200 %2 Td (R%3C2) Tj ET\n"
                                  "EMC\n")
                       .arg(mcid++)
                       .arg(y)
                       .arg(row);
    }

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 6 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 5 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(content.toLatin1())));
    objects.append(qMakePair(5, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(6, QByteArray("<< /Type /StructTreeRoot /K [8 0 R] >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructElem /S /Document /P 6 0 R /K [9 0 R 10 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /Head /P 8 0 R /Pg 3 0 R /K 0 >>")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /Table /P 8 0 R /Pg 3 0 R /K 11 0 R >>")));
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /TBody /P 10 0 R >>")));
    return buildPdf(objects);
}

// 样本 PDF：页眉 + 正文 + 2×2 表格 + 图片。
QByteArray samplePdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> "
                                            "/XObject << /Im1 9 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(samplePageContent())));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [10 0 R] >>")));
    objects.append(qMakePair(9, imageObject(9, 40, 20)));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R "
                                             "/K [11 0 R 12 0 R 13 0 R] >>")));
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /Head /P 10 0 R "
                                             "/Pg 3 0 R /K 0 >>")));
    objects.append(qMakePair(12, QByteArray("<< /Type /StructElem /S /P /P 10 0 R "
                                             "/Pg 3 0 R /K 1 >>")));
    objects.append(qMakePair(13, QByteArray("<< /Type /StructElem /S /Table /P 10 0 R "
                                             "/Pg 3 0 R /K [14 0 R 15 0 R 16 0 R 17 0 R] >>")));
    objects.append(qMakePair(14, QByteArray("<< /Type /StructElem /S /TD /P 13 0 R /Pg 3 0 R /K 2 >>")));
    objects.append(qMakePair(15, QByteArray("<< /Type /StructElem /S /TD /P 13 0 R /Pg 3 0 R /K 3 >>")));
    objects.append(qMakePair(16, QByteArray("<< /Type /StructElem /S /TD /P 13 0 R /Pg 3 0 R /K 4 >>")));
    objects.append(qMakePair(17, QByteArray("<< /Type /StructElem /S /TD /P 13 0 R /Pg 3 0 R /K 5 >>")));
    return buildPdf(objects);
}

// 两页的样本：每页都有页眉与一段正文。
//
// 正文给得足够长：窄页宽（160 点）下两页正文连起来要排成好几页，
// 这样「页眉在每一页上重复」才看得出来（否则正文只占一页，重复无从谈起）。
QByteArray twoPagePdf() {
    const QString body =
        QStringLiteral("Lorem ipsum dolor sit amet consectetur adipiscing elit ").repeated(24);
    const QByteArray pageOne =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 285 Td (Running head) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 1 >> BDC\n"
        "BT /F1 12 Tf 40 260 Td (" + body.toLatin1() + ") Tj ET\n"
        "EMC";
    const QByteArray pageTwo =
        "/Tag << /MCID 2 >> BDC\n"
        "BT /F1 12 Tf 40 285 Td (Running head) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 3 >> BDC\n"
        "BT /F1 12 Tf 40 260 Td (" + body.toLatin1() + ") Tj ET\n"
        "EMC";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 8 0 R >>")));
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
                                             "/K [10 0 R 11 0 R 12 0 R 13 0 R] >>")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /Head /P 9 0 R /Pg 3 0 R /K 0 >>")));
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /P /P 9 0 R /Pg 3 0 R /K 1 >>")));
    objects.append(qMakePair(12, QByteArray("<< /Type /StructElem /S /Head /P 9 0 R /Pg 5 0 R /K 2 >>")));
    objects.append(qMakePair(13, QByteArray("<< /Type /StructElem /S /P /P 9 0 R /Pg 5 0 R /K 3 >>")));
    return buildPdf(objects);
}

// 与领域层同口径的「文字块归属索引」。
//
// 页眉、正文、表格单元格各自成块。
PageTextBlockIndex sampleIndex() {
    PageTextBlockIndex index;
    index.mcidToBlock.insert(PageTextBlockIndex::mcidKey(0, 0), 0);
    index.blockRoles.append(PageTextRole::Header);
    index.blockTexts.append(QStringLiteral("Running head"));
    index.blockTags.append(QStringLiteral("Head"));
    index.mcidToBlock.insert(PageTextBlockIndex::mcidKey(0, 1), 1);
    index.blockRoles.append(PageTextRole::Body);
    index.blockTexts.append(QStringLiteral("Body paragraph text"));
    index.blockTags.append(QStringLiteral("P"));
    for (int mcid = 2; mcid <= 5; ++mcid) {
        const int block = mcid;
        index.mcidToBlock.insert(PageTextBlockIndex::mcidKey(0, mcid), block);
        index.blockRoles.append(PageTextRole::Body);
        index.blockTexts.append(QString());
        index.blockTags.append(QStringLiteral("TD"));
    }
    return index;
}

// 常规的重排参数。
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

// 用 PDFium 取某一页的文字。
QString pageTextOf(const QByteArray& bytes, int pageIndex) {
    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(bytes.constData(), static_cast<int>(bytes.size()), nullptr);
    if (document == nullptr) return QString();
    QString text;
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
            text = QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()), count);
            FPDFText_ClosePage(textPage);
        }
        FPDF_ClosePage(page);
    }
    FPDF_CloseDocument(document);
    return text;
}

int pageCountOf(const QByteArray& bytes) {
    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(bytes.constData(), static_cast<int>(bytes.size()), nullptr);
    if (document == nullptr) return -1;
    const int count = FPDF_GetPageCount(document);
    FPDF_CloseDocument(document);
    return count;
}

}  // namespace

class ReflowWriteBackTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- 写回后的对象还在 ----

    void theReflowedDocumentKeepsTheTextTheImageAndTheTable() {
        const DocumentReflowResult result = reflowTaggedDocument(samplePdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        QVERIFY(!result.bytes.isEmpty());

        // 落盘 → 重新打开 → 重新统计。
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("reflowed.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(result.bytes);
        file.close();

        QFile reopened(path);
        QVERIFY(reopened.open(QIODevice::ReadOnly));
        const QByteArray onDisk = reopened.readAll();
        reopened.close();
        QVERIFY(!onDisk.isEmpty());

        const DocumentObjectModel output = readPageObjectModel(onDisk);
        QVERIFY2(output.ok(), qPrintable(output.error));

        // 原文档的统计（独立读一遍，用与领域层同口径的归属索引）。
        const DocumentObjectModel source = readPageObjectModel(samplePdf(), sampleIndex());
        QVERIFY2(source.ok(), qPrintable(source.error));
        QCOMPARE(source.counts.textBlocks, 1);
        QCOMPARE(source.counts.images, 1);
        QCOMPARE(source.counts.tables, 1);
        QCOMPARE(source.counts.headers, 1);

        // 整篇总数比：文字 / 图片 / 表格必须一样多。
        QCOMPARE(output.counts.textBlocks, source.counts.textBlocks);
        QCOMPARE(output.counts.images, source.counts.images);
        QCOMPARE(output.counts.tables, source.counts.tables);
        QCOMPARE(output.counts.headers, source.counts.headers);

        // 报告里的计数也要和重新统计的结果一致。
        QCOMPARE(result.report.outputCounts.textBlocks, output.counts.textBlocks);
        QCOMPARE(result.report.outputCounts.images, output.counts.images);
        QCOMPARE(result.report.outputCounts.tables, output.counts.tables);
        QCOMPARE(result.report.sourceCounts.textBlocks, source.counts.textBlocks);
    }

    void theReflowedDocumentStillHasItsText() {
        const DocumentReflowResult result = reflowTaggedDocument(samplePdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        const QString text = pageTextOf(result.bytes, 0);
        QVERIFY2(text.contains(QStringLiteral("Body paragraph text")), qPrintable(text));
        // 表格的单元格文字也在。
        QVERIFY2(text.contains(QStringLiteral("A1")) && text.contains(QStringLiteral("B2")),
                 qPrintable(text));
    }

    void theReflowedDocumentStillHasItsImage() {
        const DocumentReflowResult result = reflowTaggedDocument(samplePdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        const DocumentObjectModel output = readPageObjectModel(result.bytes);
        QVERIFY2(output.ok(), qPrintable(output.error));
        QCOMPARE(output.counts.images, 1);
    }

    void theReflowedDocumentStillHasItsTable() {
        const DocumentReflowResult result = reflowTaggedDocument(samplePdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        const DocumentObjectModel output = readPageObjectModel(result.bytes);
        QVERIFY2(output.ok(), qPrintable(output.error));
        QCOMPARE(output.counts.tables, 1);
        // 表格是按行列重建的：还是 2 行 2 列。
        for (const auto& object : output.objects) {
            if (object.kind != PageObjectKind::Table) continue;
            QCOMPARE(object.rowCount, 2);
            QCOMPARE(object.columnCount, 2);
            QCOMPARE(object.cells.size(), 4);
            // 单元格文字必须还在**原来那一格**：行 0 是最上面一行
            // （原文档里 A1/B1 在 y=220，A2/B2 在 y=200）。
            // 行序要是翻了，格子还是 2×2、单元格还是 4 个，
            // 但 A1 跑到下面去了——只看行列数是发现不了的。
            QStringList rowMajor;
            for (const auto& cell : object.cells) {
                rowMajor.append(QStringLiteral("%1,%2=%3").arg(cell.row).arg(cell.column).arg(cell.text));
            }
            rowMajor.sort();
            const QStringList expected = {
                QStringLiteral("0,0=A1"), QStringLiteral("0,1=B1"),
                QStringLiteral("1,0=A2"), QStringLiteral("1,1=B2")};
            QCOMPARE(rowMajor, expected);
        }
    }

    void theReflowedDocumentIsStillTagged() {
        const DocumentReflowResult result = reflowTaggedDocument(samplePdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        const StructTreeReport tree = readStructTree(result.bytes);
        QVERIFY2(tree.ok(), qPrintable(tree.error));
        QVERIFY2(tree.hasStructTree, "重排后的文档应当重新打上标签");
    }

    // ---- 页眉页脚 ----

    void theRunningHeadIsRepeatedOnEveryPage() {
        ReflowOptions options = defaultOptions();
        options.pageWidth = 160.0;
        options.pageHeight = 220.0;
        const DocumentReflowResult result = reflowTaggedDocument(twoPagePdf(), options);
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        QVERIFY2(result.report.outputPageCount >= 2,
                 "窄页宽应当排出两页以上");
        // 每一页都要有页眉。
        for (int pageIndex = 0; pageIndex < result.report.outputPageCount; ++pageIndex) {
            const QString text = pageTextOf(result.bytes, pageIndex);
            QVERIFY2(text.contains(QStringLiteral("Running head")),
                     qPrintable(QStringLiteral("第 %1 页没有页眉：%2").arg(pageIndex).arg(text)));
        }
    }

    void theRunningHeadIsStillCountedAsAHeader() {
        const DocumentReflowResult result = reflowTaggedDocument(samplePdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        // 页眉不是正文：正文文字块只有一段。
        QCOMPARE(result.report.sourceCounts.textBlocks, 1);
        QCOMPARE(result.report.sourceCounts.headers, 1);
        QCOMPARE(result.report.outputCounts.headers, 1);
    }

    // ---- 门禁 ----

    void theGateBlocksWhenAParagraphCannotBeLaidOut() {
        // 首行缩进比可用宽度还大 → 这一段在新页宽下排不进去 → 那一块没搬过去。
        ReflowOptions options = defaultOptions();
        options.paragraph.firstLineIndent = options.pageWidth;
        const DocumentReflowResult result = reflowTaggedDocument(unlayoutablePdf(), options);
        QVERIFY(!result.ok());
        QVERIFY2(result.bytes.isEmpty(), "门禁拦住时不能产出字节");
        QVERIFY2(result.report.error.contains(QStringLiteral("已阻止写回")),
                 qPrintable(result.report.error));
        QVERIFY2(result.report.error.contains(QStringLiteral("文字块")),
                 qPrintable(result.report.error));
    }

    void theGateBlocksWhenATableCannotFit() {
        // 页高小到一行的放不下：表格没法拆，必须被门禁拦住。
        ReflowOptions options = defaultOptions();
        options.pageHeight = 20.0;
        options.marginTop = 8.0;
        options.marginBottom = 8.0;
        const DocumentReflowResult result = reflowTaggedDocument(hugeTablePdf(), options);
        QVERIFY(!result.ok());
        QVERIFY2(result.bytes.isEmpty(), "门禁拦住时不能产出字节");
        QVERIFY2(result.report.error.contains(QStringLiteral("已阻止写回")),
                 qPrintable(result.report.error));
        QVERIFY2(result.report.error.contains(QStringLiteral("表格")),
                 qPrintable(result.report.error));
    }

    void theGateExplainsWhatIsMissing() {
        ReflowOptions options = defaultOptions();
        options.paragraph.firstLineIndent = options.pageWidth;
        const DocumentReflowResult result = reflowTaggedDocument(unlayoutablePdf(), options);
        QVERIFY(!result.ok());
        // 错误里要说清楚少了什么：几个文字块。
        QVERIFY2(result.report.error.contains(QStringLiteral("1 个文字块")),
                 qPrintable(result.report.error));
    }

    // ---- 锚点计划 ----

    void thePlanRecordsWhereEveryObjectWent() {
        const DocumentReflowResult result = reflowTaggedDocument(samplePdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        const auto& plan = result.report.plan;
        QVERIFY2(plan.anchorCount() > 0, "计划里必须有锚点");
        QCOMPARE(plan.sourcePageCount, 1);
        QCOMPARE(plan.targetPageCount, result.report.outputPageCount);
        for (const auto& anchor : plan.anchors) {
            QVERIFY2(!anchor.objectId.isEmpty(), "锚点必须有稳定身份");
            QVERIFY2(anchor.targetPage >= 0, "锚点必须写明搬到了第几页");
            QVERIFY2(!anchor.targetBounds.isEmpty(), "锚点必须写明搬到了哪里");
        }
        // 计划里的身份能在模型里找到。
        const DocumentObjectModel source = readPageObjectModel(samplePdf(), sampleIndex());
        for (const auto& anchor : plan.anchors) {
            bool found = false;
            for (const auto& object : source.objects) {
                if (object.id == anchor.objectId) found = true;
            }
            QVERIFY2(found, qPrintable(anchor.objectId));
        }
    }

    void thePlanHasAnAnchorForTheImageAndTheTable() {
        const DocumentReflowResult result = reflowTaggedDocument(samplePdf(), defaultOptions());
        QVERIFY2(result.ok(), qPrintable(result.report.error));
        const auto images = result.report.plan.anchorsOfKind(PageObjectKind::Image);
        const auto tables = result.report.plan.anchorsOfKind(PageObjectKind::Table);
        QCOMPARE(images.size(), 1);
        QCOMPARE(tables.size(), 1);
        QVERIFY2(images.first().targetPage >= 0, "图片必须搬到某一页");
        QVERIFY2(!images.first().targetBounds.isEmpty(), "图片必须搬到某个位置");
    }

    // ---- 负例 ----

    void aDocumentWithoutTagsIsRefused() {
        // 没有标签树的文档：直接拒绝，不做启发式重排。
        QVector<QPair<int, QByteArray>> objects;
        objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
        objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
        objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                                "/Resources << /Font << /F1 5 0 R >> >> "
                                                "/Contents 4 0 R >>")));
        objects.append(qMakePair(4, streamObject("BT /F1 12 Tf 40 260 Td (Plain text) Tj ET")));
        objects.append(qMakePair(5, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
        const DocumentReflowResult result = reflowTaggedDocument(buildPdf(objects), defaultOptions());
        QVERIFY(!result.ok());
        QVERIFY(result.bytes.isEmpty());
        QVERIFY2(result.report.error.contains(QStringLiteral("没有结构标签")),
                 qPrintable(result.report.error));
    }

    void theOutputCanBeReflowedAgain() {
        const DocumentReflowResult first = reflowTaggedDocument(samplePdf(), defaultOptions());
        QVERIFY2(first.ok(), qPrintable(first.report.error));
        const DocumentReflowResult second = reflowTaggedDocument(first.bytes, defaultOptions());
        QVERIFY2(second.ok(), qPrintable(second.report.error));
        QVERIFY(pageCountOf(second.bytes) > 0);
    }
};

void ReflowWriteBackTests::initTestCase() {}

QTEST_MAIN(ReflowWriteBackTests)
#include "ReflowWriteBackTests.moc"
