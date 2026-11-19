// 2026-11-19 wave21：导出 HTML 的深度（带图、带矢量图形、真实字号）。
//
// 为什么要单独一组用例：导出 HTML 以前只出文字，字号还是估算的。
// 现在多了「矢量路径 → 内嵌 SVG」「内嵌位图 → data URI」「真实字号」三件事，
// 这三件事各自都能坏掉，而且坏掉时 HTML 看起来还是「有内容」的——
// 光看文件大小发现不了，只能一条条断言。
#include "pdf/convert/ExportFormats.h"

#include "pdf/content/PageGraphics.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>
#include <QXmlStreamReader>

using aipdf::pdf::exportToHtml;
using aipdf::pdf::extractCharFontSizes;
using aipdf::pdf::extractPageGraphics;
using aipdf::pdf::HtmlExportOptions;
using aipdf::pdf::HtmlPage;
using aipdf::pdf::PageGraphics;
using aipdf::pdf::TextPage;

namespace {

// 手写一份最小 PDF 的骨架：对象、xref、trailer。
//
// 为什么自己造：现成的夹具都是纯文字的，验不了「图形与图片有没有进 HTML」。
QByteArray buildPdf(const QVector<QByteArray>& bodies) {
    QByteArray out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    QVector<int> offsets;
    for (int i = 0; i < bodies.size(); ++i) {
        offsets.append(out.size());
        out += QByteArray::number(i + 1) + " 0 obj\n" + bodies.at(i) + "\nendobj\n";
    }
    const int xref = out.size();
    out += "xref\n0 " + QByteArray::number(bodies.size() + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (int offset : offsets) {
        out += QByteArray::number(offset, 10).rightJustified(10, '0') + " 00000 n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(bodies.size() + 1) + " /Root 1 0 R >>\n";
    out += "startxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return out;
}

// 2×2 的 RGB 位图（不压缩，直接放原始采样：解码路径要能处理这种最朴素的图）。
QByteArray twoByTwoPixels() {
    QByteArray pixels;
    pixels.append(char(255));
    pixels.append(char(0));
    pixels.append(char(0));
    pixels.append(char(0));
    pixels.append(char(255));
    pixels.append(char(0));
    pixels.append(char(0));
    pixels.append(char(0));
    pixels.append(char(255));
    pixels.append(char(255));
    pixels.append(char(255));
    pixels.append(char(0));
    return pixels;
}

// 一页里同时有文字、两条矢量路径和一张内嵌位图的 PDF。
//
// 字号故意用 Courier 20pt 的小写文字：Courier 的 x 高度只有 0.426 em，
// 「按字形高度反推」的估算会得到 16 点左右，与真实的 20 点差得开——
// 用 Helvetica 的话估算值几乎等于真值，这条用例就白写了。
QByteArray richFixturePdf() {
    const QByteArray content =
        "BT /F1 20 Tf 20 60 Td (hello graph) Tj ET\n"
        "1 0 0 RG 2 w 20 20 m 180 20 l S\n"
        "0 0 1 rg 20 40 60 30 re f\n"
        "q 120 0 0 30 140 20 cm /Im1 Do Q\n";
    const QByteArray pixels = twoByTwoPixels();

    QVector<QByteArray> bodies;
    bodies.append("<< /Type /Catalog /Pages 2 0 R >>");
    bodies.append("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    bodies.append("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                  "/Resources << /Font << /F1 5 0 R >> /XObject << /Im1 6 0 R >> >> "
                  "/Contents 4 0 R >>");
    bodies.append("<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content +
                  "\nendstream");
    bodies.append("<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>");
    bodies.append("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB "
                  "/BitsPerComponent 8 /Length " +
                  QByteArray::number(pixels.size()) + " >>\nstream\n" + pixels + "\nendstream");
    return buildPdf(bodies);
}

// 只有文字的页面：用来验「没有图形不是错误」。
QByteArray textOnlyFixturePdf() {
    const QByteArray content = "BT /F1 12 Tf 20 60 Td (plain text) Tj ET\n";
    QVector<QByteArray> bodies;
    bodies.append("<< /Type /Catalog /Pages 2 0 R >>");
    bodies.append("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    bodies.append("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                  "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>");
    bodies.append("<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content +
                  "\nendstream");
    bodies.append("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    return buildPdf(bodies);
}

// 用结构化方式（XML 解析器）扫一遍导出的 HTML。
//
// 为什么不用正则：正则分不清 <path> 是标签还是属性值里的字样，
// 一旦导出的结构变了，用例可能「绿着但没在验东西」。
struct HtmlScan {
    bool wellFormed = false;
    QString error;
    int svgCount = 0;
    int pathCount = 0;
    int imageCount = 0;
    int spanCount = 0;
    QStringList pathData;
    QStringList spanStyles;
    QStringList imageSources;
};

HtmlScan scanHtml(const QString& html) {
    HtmlScan scan;
    QXmlStreamReader reader(html);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) continue;
        const QString name = reader.name().toString();
        if (name == QStringLiteral("svg")) {
            ++scan.svgCount;
        } else if (name == QStringLiteral("path")) {
            ++scan.pathCount;
            scan.pathData.append(reader.attributes().value(QStringLiteral("d")).toString());
        } else if (name == QStringLiteral("img")) {
            ++scan.imageCount;
            scan.imageSources.append(reader.attributes().value(QStringLiteral("src")).toString());
        } else if (name == QStringLiteral("span")) {
            ++scan.spanCount;
            scan.spanStyles.append(reader.attributes().value(QStringLiteral("style")).toString());
        }
    }
    if (reader.hasError()) {
        scan.error = reader.errorString();
        return scan;
    }
    scan.wellFormed = true;
    return scan;
}

// 取一页的文本层（PDFium 读出来的）。
TextPage pageTextOf(const QByteArray& pdf, int pageIndex) {
    const auto opened = aipdf::pdf::openDocumentFromBytes(pdf);
    if (!opened.document.isValid()) return {};
    const auto page = opened.document.page(pageIndex);
    if (!page.has_value()) return {};
    return page->extractText().value_or(TextPage());
}

}  // namespace

class Wave21ExportHtmlTests : public QObject {
    Q_OBJECT

private slots:
    // 图形提取
    void graphicsExtractionFindsPathsAndImages();
    void graphicsExtractionFindsNothingOnTextOnlyPage();
    void graphicsExtractionReportsFailureOnInvalidInput();

    // HTML 里的矢量图形
    void htmlContainsOneSvgPathPerSourcePath();
    void svgCoordinatesAreFlippedToTopDown();
    void htmlWithoutGraphicsHasNoSvgAndNoImage();

    // HTML 里的图片
    void htmlEmbedsImageAsDataUri();

    // 字号
    void htmlUsesRealFontSize();
    void htmlFallsBackToEstimatedFontSizeAndSaysSo();

    // 整篇导出
    void exportWholeDocumentCarriesGraphicsImagesAndFonts();
    void exportWithoutStylesKeepsStructureOnly();

    // 真实文件上的健壮性
    void realFixturesStayConsistent();
};

namespace {

// 导出一页（带图形与真实字号）的 HTML。
//
// 这三步就是界面层该做的事：文字走 PDFium，图形与真实字号走内容流。
aipdf::pdf::HtmlExportResult exportRichPage(const QByteArray& pdf, bool withRealSizes,
                                             const HtmlExportOptions& options = HtmlExportOptions{}) {
    HtmlPage page;
    page.text = pageTextOf(pdf, 0);
    page.sizePt = QSizeF(200.0, 100.0);
    const PageGraphics graphics = extractPageGraphics(pdf, 0);
    if (graphics.ok) page.graphics = graphics;
    if (withRealSizes) page.charFontSizes = extractCharFontSizes(pdf, page.text, 0);
    return exportToHtml({page}, options);
}

// 导出一整篇（每页都带上图形与真实字号）。
aipdf::pdf::HtmlExportResult exportWholeDocument(const QByteArray& pdf,
                                                  const HtmlExportOptions& options = HtmlExportOptions{}) {
    QVector<HtmlPage> pages;
    const auto opened = aipdf::pdf::openDocumentFromBytes(pdf);
    if (!opened.document.isValid()) return {};
    for (int index = 0; index < opened.document.pageCount(); ++index) {
        const auto page = opened.document.page(index);
        if (!page.has_value()) continue;
        const TextPage text = page->extractText().value_or(TextPage());
        const QSizeF sizePt(page->widthPoints(), page->heightPoints());
        // 这一行就是界面层该做的事：文字给进去，图形与真实字号自动补齐。
        pages.append(aipdf::pdf::buildHtmlPage(pdf, text, sizePt, index));
    }
    return exportToHtml(pages, options);
}

}  // namespace

// ============================================================ 图形提取 ========

void Wave21ExportHtmlTests::graphicsExtractionFindsPathsAndImages() {
    const QByteArray pdf = richFixturePdf();
    const PageGraphics graphics = extractPageGraphics(pdf, 0);
    QVERIFY2(graphics.ok, qPrintable(graphics.error));
    // 内容流里画了两条路径：一条描边直线、一个填充矩形。
    QCOMPARE(graphics.paths.size(), 2);
    // 一张内嵌位图。
    QCOMPARE(graphics.images.size(), 1);

    const auto& image = graphics.images.first();
    QCOMPARE(image.mime, QStringLiteral("image/png"));
    QCOMPARE(image.pixelWidth, 2);
    QCOMPARE(image.pixelHeight, 2);
    QVERIFY(!image.data.isEmpty());
    // 图片摆在 cm 变换出来的矩形里：x 140..260、y 20..50。
    // 矩形约定与字符框一致：top() 是纵坐标小的那条边，bottom() 是大的那条。
    QVERIFY2(qAbs(image.rect.left() - 140.0) < 0.01, qPrintable(QString::number(image.rect.left())));
    QVERIFY2(qAbs(image.rect.top() - 20.0) < 0.01, qPrintable(QString::number(image.rect.top())));
    QVERIFY2(qAbs(image.rect.bottom() - 50.0) < 0.01, qPrintable(QString::number(image.rect.bottom())));
    QVERIFY2(qAbs(image.rect.width() - 120.0) < 0.01, qPrintable(QString::number(image.rect.width())));
    QVERIFY2(qAbs(image.rect.height() - 30.0) < 0.01, qPrintable(QString::number(image.rect.height())));

    // 描边那条：红色、线宽 2（CTM 没缩放，线宽就是内容流里的值）。
    bool foundStroke = false;
    for (const auto& path : graphics.paths) {
        if (!path.stroked) continue;
        foundStroke = true;
        QCOMPARE(path.lineWidth, 2.0);
        QVERIFY(qAbs(path.strokeColor.r - 1.0) < 0.01);
        QVERIFY(path.strokeColor.g < 0.01);
    }
    QVERIFY(foundStroke);
}

void Wave21ExportHtmlTests::graphicsExtractionFindsNothingOnTextOnlyPage() {
    const QByteArray pdf = textOnlyFixturePdf();
    const PageGraphics graphics = extractPageGraphics(pdf, 0);
    // 没有图形不是失败。
    QVERIFY2(graphics.ok, qPrintable(graphics.error));
    QVERIFY(graphics.paths.isEmpty());
    QVERIFY(graphics.images.isEmpty());
}

void Wave21ExportHtmlTests::graphicsExtractionReportsFailureOnInvalidInput() {
    const PageGraphics graphics = extractPageGraphics(QByteArray("not a pdf"), 0);
    QVERIFY(!graphics.ok);
    QVERIFY2(!graphics.error.isEmpty(), "失败时必须给出原因");
    // 原因要说人话（中文），不能把内部异常直接甩给用户。
    QVERIFY2(graphics.error.contains(QString::fromUtf8("失败")) ||
                 graphics.error.contains(QString::fromUtf8("qpdf")),
             qPrintable(graphics.error));
}

// ====================================================== HTML 里的矢量图形 =====

void Wave21ExportHtmlTests::htmlContainsOneSvgPathPerSourcePath() {
    const QByteArray pdf = richFixturePdf();
    const PageGraphics graphics = extractPageGraphics(pdf, 0);
    QVERIFY2(graphics.ok, qPrintable(graphics.error));

    const auto result = exportRichPage(pdf, true);
    QVERIFY2(result.ok, qPrintable(result.error));
    const HtmlScan scan = scanHtml(result.html);
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.svgCount, 1);
    // SVG 里的路径数必须与源页面里的路径数一致。
    QCOMPARE(scan.pathCount, graphics.paths.size());
    QCOMPARE(result.vectorPathCount, graphics.paths.size());
}

void Wave21ExportHtmlTests::svgCoordinatesAreFlippedToTopDown() {
    const QByteArray pdf = richFixturePdf();
    const auto result = exportRichPage(pdf, true);
    QVERIFY2(result.ok, qPrintable(result.error));
    const HtmlScan scan = scanHtml(result.html);
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QVERIFY(!scan.pathData.isEmpty());

    // 内容流里的直线在 PDF 空间是 y=20（页高 100），SVG 的 y 向下，应该是 80。
    bool foundFlipped = false;
    for (const QString& data : scan.pathData) {
        if (data.contains(QStringLiteral("L 180.00 80.00"))) foundFlipped = true;
    }
    QVERIFY2(foundFlipped, qPrintable(scan.pathData.join(QStringLiteral(" | "))));
}

void Wave21ExportHtmlTests::htmlWithoutGraphicsHasNoSvgAndNoImage() {
    const QByteArray pdf = textOnlyFixturePdf();
    HtmlPage page;
    page.text = pageTextOf(pdf, 0);
    page.sizePt = QSizeF(200.0, 100.0);
    const PageGraphics graphics = extractPageGraphics(pdf, 0);
    QVERIFY(graphics.ok);
    page.graphics = graphics;

    const auto result = exportToHtml({page});
    QVERIFY2(result.ok, qPrintable(result.error));
    const HtmlScan scan = scanHtml(result.html);
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.svgCount, 0);
    QCOMPARE(scan.imageCount, 0);
    QCOMPARE(scan.pathCount, 0);
}

// ========================================================== HTML 里的图片 =====

void Wave21ExportHtmlTests::htmlEmbedsImageAsDataUri() {
    const QByteArray pdf = richFixturePdf();
    const auto result = exportRichPage(pdf, true);
    QVERIFY2(result.ok, qPrintable(result.error));
    const HtmlScan scan = scanHtml(result.html);
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.imageCount, 1);
    QVERIFY(!scan.imageSources.isEmpty());
    QVERIFY2(scan.imageSources.first().startsWith(QStringLiteral("data:image/png;base64,")),
             qPrintable(scan.imageSources.first().left(40)));
    QCOMPARE(result.imageCount, 1);
}

// ================================================================ 字号 ========

void Wave21ExportHtmlTests::htmlUsesRealFontSize() {
    const QByteArray pdf = richFixturePdf();
    const auto result = exportRichPage(pdf, true);
    QVERIFY2(result.ok, qPrintable(result.error));
    // 内容流里的 Tf 是 20pt：真实字号必须被采用。
    QVERIFY2(result.html.contains(QStringLiteral("font-size:20.00pt")), qPrintable(result.html));
    QVERIFY2(!result.fontSizeEstimated, "真实字号取到了就不该说走了估算");
    QVERIFY(result.fontSizeNote.isEmpty());

    const HtmlScan scan = scanHtml(result.html);
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.spanCount, 1);
    QVERIFY(scan.spanStyles.first().contains(QStringLiteral("font-size:20.00pt")));
}

void Wave21ExportHtmlTests::htmlFallsBackToEstimatedFontSizeAndSaysSo() {
    const QByteArray pdf = richFixturePdf();
    // 不给真实字号：只能按字形高度估算，而且必须说清楚。
    const auto result = exportRichPage(pdf, false);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY2(result.fontSizeEstimated, "走了估算就必须标明");
    QVERIFY2(!result.fontSizeNote.isEmpty(), "要走估算就得说明原因");
    // 估算值与真实字号不一样（Courier 的 x 高度只有 0.426 em）。
    QVERIFY2(!result.html.contains(QStringLiteral("font-size:20.00pt")), qPrintable(result.html));
    const HtmlScan scan = scanHtml(result.html);
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.spanCount, 1);
}

// ============================================================ 整篇导出 ========

void Wave21ExportHtmlTests::exportWholeDocumentCarriesGraphicsImagesAndFonts() {
    const QByteArray pdf = richFixturePdf();
    const auto result = exportWholeDocument(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    const HtmlScan scan = scanHtml(result.html);
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.svgCount, 1);
    QCOMPARE(scan.imageCount, 1);
    QVERIFY2(result.html.contains(QStringLiteral("font-size:20.00pt")), qPrintable(result.html));
    QVERIFY2(!result.fontSizeEstimated, qPrintable(result.fontSizeNote));
}

void Wave21ExportHtmlTests::exportWithoutStylesKeepsStructureOnly() {
    const QByteArray pdf = richFixturePdf();
    HtmlExportOptions options;
    options.includeStyles = false;
    const auto result = exportRichPage(pdf, true, options);
    QVERIFY2(result.ok, qPrintable(result.error));
    // 关掉样式段之后只剩结构标签与行内样式。
    QVERIFY2(!result.html.contains(QStringLiteral("<style")), qPrintable(result.html));
    const HtmlScan scan = scanHtml(result.html);
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.svgCount, 1);
    QCOMPARE(scan.imageCount, 1);
    QCOMPARE(scan.pathCount, 2);
}

// ====================================================== 真实文件上的健壮性 =====

void Wave21ExportHtmlTests::realFixturesStayConsistent() {
    // 仓库里真正的夹具逐个过一遍。
    //
    // 为什么要有这条：这些文件是别人（或别的工具）生成的，内容流千奇百怪。
    // 提取图形与字号在它们上面不许崩、不许给出自相矛盾的结果
    // （要么成功且没有错误说明，要么失败且说清中文原因）。
    QDir dir(QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR));
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.pdf"), QDir::Files);
    if (files.isEmpty()) QSKIP("夹具目录不可用");

    for (const QString& name : files) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = file.readAll();
        file.close();

        const PageGraphics graphics = extractPageGraphics(bytes, 0);
        if (graphics.ok) {
            QVERIFY2(graphics.error.isEmpty(),
                     qPrintable(QStringLiteral("%1 成功了就该没有错误说明：%2")
                                    .arg(name, graphics.error)));
        } else {
            QVERIFY2(!graphics.error.isEmpty(),
                     qPrintable(QStringLiteral("%1 失败了就得说清原因").arg(name)));
        }

        // 字号数组的长度必须与字符数一致：调用方是按位置取的。
        const TextPage text = pageTextOf(bytes, 0);
        const QVector<double> sizes = extractCharFontSizes(bytes, text, 0);
        QCOMPARE(sizes.size(), text.chars.size());
    }
}

QTEST_MAIN(Wave21ExportHtmlTests)
#include "Wave21ExportHtmlTests.moc"
