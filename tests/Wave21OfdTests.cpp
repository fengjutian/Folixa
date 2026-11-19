// 2026-11-19 wave21：OFD 导出补上图形与图片（矢量路径 → 路径对象，位图 → 图像对象）。
//
// 为什么要单独一组用例：OFD 以前只导文字，图形与图片整块丢掉，
// 而丢的时候**一声不响**——包还是合法的 OFD，打开也是一页白纸加几行字，
// 光看「导出成功」发现不了。每一件新东西都得单独钉死：
//   路径对象数、图像对象数与源页面一致；
//   贝塞尔还是贝塞尔（没被偷偷打散成折线）；
//   颜色与线宽没丢；
//   文字还在；
//   只传文字的老入口仍然能用。
//
// 另外：OFD 的页面图元（路径/图像/文字）按国标写在**每页的 Content.xml** 里，
// Document.xml 只列页面（BaseLoc 指向 Content.xml）。
// 所以「从 Document.xml 数出路径与图像对象」的正确做法是：
// 先从 Document.xml 找到每页的 BaseLoc，再去那一份 Content.xml 里数。
#include "doc/export/OfdExport.h"
#include "doc/export/OfficeExport.h"
#include "doc/export/ZipWriter.h"
#include "pdf/content/PageGraphics.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>
#include <QXmlStreamReader>

using aipdf::doc::OfdExportOptions;
using aipdf::doc::OfdExportResult;
using aipdf::doc::OfdPageContent;
using aipdf::pdf::PageGraphics;
using aipdf::pdf::TextPage;

namespace {

// 手写一份最小 PDF 的骨架：对象、xref、trailer。
//
// 为什么自己造：现成的夹具都是纯文字的，验不了「图形与图片有没有进 OFD」。
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

// 一页里同时有文字、三条矢量路径（其中一条是三次贝塞尔）和一张内嵌位图的 PDF。
//
// 页面 200×100 点（1 点 = 25.4/72 毫米，页面就是 70.556 × 35.278 毫米）。
//
// 三条路径各自要能单独认出来：
//   1. 红色描边直线（线宽 2）：最简单的一条，用来看线宽与描边色；
//   2. 蓝色填充矩形：看填充色；
//   3. 深蓝描边的三次贝塞尔：看曲线有没有被偷偷打散成折线。
QByteArray richFixturePdf() {
    const QByteArray content =
        "BT /F1 20 Tf 20 60 Td (hello graph) Tj ET\n"
        "1 0 0 RG 2 w 20 20 m 180 20 l S\n"
        "0 0 1 rg 20 40 60 30 re f\n"
        "0 0 0.5 RG 3 w 40 50 m 60 70 90 30 120 80 c S\n"
        "q 50 0 0 30 140 20 cm /Im1 Do Q\n";
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

// 两页：第一页有文字与图形，第二页只有文字。
//
// 注意对象号：页面字典里的引用必须对得上（/Contents 5 0 R → 第 5 号对象）。
// 号写错了 PDFium 照样能打开（它只看 /Type /Page），但内容流读不出来，
// 图形与文字就整块消失——这种错很难一眼看出来。
QByteArray twoPageFixturePdf() {
    const QByteArray firstContent =
        "BT /F1 20 Tf 20 60 Td (page one) Tj ET\n"
        "1 0 0 RG 2 w 20 20 m 180 20 l S\n"
        "q 50 0 0 30 140 20 cm /Im1 Do Q\n";
    const QByteArray secondContent = "BT /F1 20 Tf 20 60 Td (page two) Tj ET\n";
    const QByteArray pixels = twoByTwoPixels();

    QVector<QByteArray> bodies;
    bodies.append("<< /Type /Catalog /Pages 2 0 R >>");                                  // 1
    bodies.append("<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");                    // 2
    bodies.append("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "                // 3
                  "/Resources << /Font << /F1 6 0 R >> /XObject << /Im1 7 0 R >> >> "
                  "/Contents 5 0 R >>");
    bodies.append("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "                // 4
                  "/Resources << /Font << /F1 6 0 R >> >> /Contents 8 0 R >>");
    bodies.append("<< /Length " + QByteArray::number(firstContent.size()) + " >>\nstream\n" +
                  firstContent + "\nendstream");                                          // 5
    bodies.append("<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>");               // 6
    bodies.append("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB "
                  "/BitsPerComponent 8 /Length " +
                  QByteArray::number(pixels.size()) + " >>\nstream\n" + pixels + "\nendstream");  // 7
    bodies.append("<< /Length " + QByteArray::number(secondContent.size()) + " >>\nstream\n" +
                  secondContent + "\nendstream");                                         // 8
    return buildPdf(bodies);
}

// 取一页的文本层（PDFium 读出来的）。
TextPage pageTextOf(const QByteArray& pdf, int pageIndex) {
    const auto opened = aipdf::pdf::openDocumentFromBytes(pdf);
    if (!opened.document.isValid()) return {};
    const auto page = opened.document.page(pageIndex);
    if (!page.has_value()) return {};
    return page->extractText().value_or(TextPage());
}

// 一页的图形（矢量路径 + 内嵌位图）。
PageGraphics pageGraphicsOf(const QByteArray& pdf, int pageIndex) {
    return aipdf::pdf::extractPageGraphics(pdf, pageIndex);
}

// 用结构化方式（XML 解析器）扫一遍一页的 Content.xml。
//
// 为什么不用正则：正则分不清 <ofd:PathObject 是标签还是属性值里的字样，
// 一旦导出的结构变了，用例可能「绿着但没在验东西」。
struct OfdPageScan {
    bool wellFormed = false;
    QString error;
    int pathCount = 0;
    int imageCount = 0;
    int textCount = 0;
    // 文本对象里的文字（TextCode 的内容）。
    QStringList textCodes;
    // 路径对象的路径描述（AbbreviatedData 的内容）。
    QStringList pathData;
    QStringList pathBoundaries;
    QStringList pathStrokeColors;
    QStringList pathFillColors;
    QStringList pathLineWidths;
    // 图像对象。
    QStringList imageBoundaries;
    QStringList imageResourceIds;
};

// 去掉命名空间前缀：OFD 里写的是 ofd:PathObject。
QString localName(const QString& qualified) {
    const int index = qualified.lastIndexOf(QLatin1Char(':'));
    return index < 0 ? qualified : qualified.mid(index + 1);
}

OfdPageScan scanPageContent(const QByteArray& content) {
    OfdPageScan scan;
    QXmlStreamReader reader(content);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) continue;
        const QString name = localName(reader.name().toString());
        const QXmlStreamAttributes attributes = reader.attributes();
        if (name == QStringLiteral("PathObject")) {
            ++scan.pathCount;
            scan.pathBoundaries.append(attributes.value(QStringLiteral("Boundary")).toString());
            scan.pathLineWidths.append(attributes.value(QStringLiteral("LineWidth")).toString());
        } else if (name == QStringLiteral("ImageObject")) {
            ++scan.imageCount;
            scan.imageBoundaries.append(attributes.value(QStringLiteral("Boundary")).toString());
            scan.imageResourceIds.append(attributes.value(QStringLiteral("ResourceID")).toString());
        } else if (name == QStringLiteral("TextObject")) {
            ++scan.textCount;
        } else if (name == QStringLiteral("TextCode")) {
            scan.textCodes.append(reader.readElementText());
        } else if (name == QStringLiteral("StrokeColor")) {
            scan.pathStrokeColors.append(attributes.value(QStringLiteral("Value")).toString());
        } else if (name == QStringLiteral("FillColor")) {
            scan.pathFillColors.append(attributes.value(QStringLiteral("Value")).toString());
        } else if (name == QStringLiteral("AbbreviatedData")) {
            scan.pathData.append(reader.readElementText());
        }
    }
    if (reader.hasError()) {
        scan.error = reader.errorString();
        return scan;
    }
    scan.wellFormed = true;
    return scan;
}

// 从 Document.xml 出发，数出整篇文档里的路径对象与图像对象。
//
// 为什么绕这一圈：OFD 的页面图元写在每页的 Content.xml 里，
// Document.xml 只列页面（BaseLoc 指向 Content.xml）。
// 独立校验一个 OFD 包时，入口就是 Document.xml——
// 「能不能从入口一路数到图元」本身就是要验的事。
struct OfdDocumentScan {
    bool ok = false;
    QString error;
    int pageCount = 0;
    int pathCount = 0;
    int imageCount = 0;
    int textCount = 0;
    QStringList contentPaths;
};

OfdDocumentScan scanDocument(const QByteArray& package) {
    OfdDocumentScan scan;
    const QByteArray document = aipdf::doc::zip::readEntry(package, QStringLiteral("Doc_0/Document.xml"));
    if (document.isEmpty()) {
        scan.error = QStringLiteral("包里没有 Doc_0/Document.xml");
        return scan;
    }
    QStringList pageLocations;
    QXmlStreamReader reader(document);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) continue;
        if (localName(reader.name().toString()) != QStringLiteral("Page")) continue;
        const QString baseLoc = reader.attributes().value(QStringLiteral("BaseLoc")).toString();
        if (baseLoc.isEmpty()) continue;
        pageLocations.append(QStringLiteral("Doc_0/") + baseLoc);
    }
    if (reader.hasError()) {
        scan.error = reader.errorString();
        return scan;
    }
    scan.pageCount = pageLocations.size();
    for (const QString& location : pageLocations) {
        const QByteArray content = aipdf::doc::zip::readEntry(package, location);
        if (content.isEmpty()) {
            scan.error = QStringLiteral("页面内容读不出来：%1").arg(location);
            return scan;
        }
        const OfdPageScan page = scanPageContent(content);
        if (!page.wellFormed) {
            scan.error = page.error;
            return scan;
        }
        scan.pathCount += page.pathCount;
        scan.imageCount += page.imageCount;
        scan.textCount += page.textCount;
        scan.contentPaths.append(location);
    }
    scan.ok = true;
    return scan;
}

// 导出一页（文字 + 图形）。
OfdExportResult exportRichPage(const QByteArray& pdf, int pageIndex = 0) {
    OfdPageContent content;
    content.text = pageTextOf(pdf, pageIndex);
    content.graphics = pageGraphicsOf(pdf, pageIndex);
    return aipdf::doc::exportPagesToOfd(QVector<OfdPageContent>{content}, QSizeF(200.0, 100.0));
}

// 导出的包里第一页的 Content.xml。
QByteArray firstPageContent(const QByteArray& package) {
    return aipdf::doc::zip::readEntry(package, QStringLiteral("Doc_0/Pages/Page_0/Content.xml"));
}

}  // namespace

class Wave21OfdTests : public QObject {
    Q_OBJECT

private slots:
    // 图形提取（底座）
    void fixtureProvidesPathsAndImages();

    // 包结构
    void ofdPackageKeepsDocumentEntryPoints();
    void ofdPackageXmlIsWellFormed();

    // 图形与图片进包
    void ofdPageCarriesOnePathObjectPerSourcePath();
    void ofdPageCarriesOneImageObjectPerSourceImage();
    void ofdObjectsAreCountableFromDocumentXml();

    // 路径本身
    void ofdBezierStaysBezier();
    void ofdPathCarriesColorAndLineWidth();
    void ofdPathCoordinatesUsePageSpace();

    // 图片本身
    void ofdImageIsPackagedAsPageResource();

    // 文字不许丢
    void ofdKeepsTextObjects();

    // 兼容
    void textOnlyExportStillWorks();
    void exportWithoutGraphicsKeepsOnlyText();
    void multiPageExportKeepsPerPageObjects();
};

void Wave21OfdTests::fixtureProvidesPathsAndImages() {
    const QByteArray pdf = richFixturePdf();
    const PageGraphics graphics = pageGraphicsOf(pdf, 0);
    QVERIFY2(graphics.ok, qPrintable(graphics.error));
    // 三条路径：一条直线、一个矩形、一条三次贝塞尔。
    QCOMPARE(graphics.paths.size(), 3);
    // 一张内嵌位图。
    QCOMPARE(graphics.images.size(), 1);
    // 底座自己也钉一遍「贝塞尔是贝塞尔」：源数据里必须有曲线段，
    // 否则后面「OFD 里还是曲线」这条断言就没有对照。
    int curveCount = 0;
    for (const auto& path : graphics.paths) {
        for (const auto& segment : path.segments) {
            if (segment.verb == aipdf::pdf::GraphicsVerb::Curve) ++curveCount;
        }
    }
    QCOMPARE(curveCount, 1);
}

void Wave21OfdTests::ofdPackageKeepsDocumentEntryPoints() {
    const auto result = exportRichPage(richFixturePdf());
    QVERIFY2(result.ok, qPrintable(result.error));
    const QStringList names = aipdf::doc::zip::listEntryNames(result.bytes);
    QVERIFY(names.contains(QStringLiteral("OFD.xml")));
    QVERIFY(names.contains(QStringLiteral("Doc_0/Document.xml")));
    QVERIFY(names.contains(QStringLiteral("Doc_0/Pages/Page_0/Content.xml")));

    // OFD.xml 指向 Document.xml，Document.xml 指向每页的 Content.xml。
    const QByteArray ofd = aipdf::doc::zip::readEntry(result.bytes, QStringLiteral("OFD.xml"));
    QVERIFY(ofd.contains("Doc_0/Document.xml"));
    const QByteArray document =
        aipdf::doc::zip::readEntry(result.bytes, QStringLiteral("Doc_0/Document.xml"));
    QVERIFY(document.contains("Pages/Page_0/Content.xml"));
}

void Wave21OfdTests::ofdPackageXmlIsWellFormed() {
    const auto result = exportRichPage(richFixturePdf());
    QVERIFY2(result.ok, qPrintable(result.error));
    for (const QString& name : aipdf::doc::zip::listEntryNames(result.bytes)) {
        if (!name.endsWith(QStringLiteral(".xml"))) continue;
        const QByteArray content = aipdf::doc::zip::readEntry(result.bytes, name);
        QXmlStreamReader reader(content);
        while (!reader.atEnd()) {
            reader.readNext();
        }
        QVERIFY2(!reader.hasError(),
                 qPrintable(QStringLiteral("%1 不是合法 XML：%2").arg(name, reader.errorString())));
    }
}

void Wave21OfdTests::ofdPageCarriesOnePathObjectPerSourcePath() {
    const QByteArray pdf = richFixturePdf();
    const PageGraphics graphics = pageGraphicsOf(pdf, 0);
    QVERIFY2(graphics.ok, qPrintable(graphics.error));

    const auto result = exportRichPage(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    const OfdPageScan scan = scanPageContent(firstPageContent(result.bytes));
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    // 路径对象数必须与源页面的路径数一致：少一个就是图形丢了。
    QCOMPARE(scan.pathCount, graphics.paths.size());
    QCOMPARE(result.pathCount, graphics.paths.size());
}

void Wave21OfdTests::ofdPageCarriesOneImageObjectPerSourceImage() {
    const QByteArray pdf = richFixturePdf();
    const PageGraphics graphics = pageGraphicsOf(pdf, 0);
    QVERIFY2(graphics.ok, qPrintable(graphics.error));

    const auto result = exportRichPage(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    const OfdPageScan scan = scanPageContent(firstPageContent(result.bytes));
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.imageCount, graphics.images.size());
    QCOMPARE(result.imageCount, graphics.images.size());
    // 图像对象必须引用一个资源 ID：没有资源 ID 的图像对象是空壳。
    QCOMPARE(scan.imageResourceIds.size(), graphics.images.size());
    for (const QString& resourceId : scan.imageResourceIds) {
        QVERIFY(!resourceId.isEmpty());
    }
}

void Wave21OfdTests::ofdObjectsAreCountableFromDocumentXml() {
    const QByteArray pdf = richFixturePdf();
    const PageGraphics graphics = pageGraphicsOf(pdf, 0);
    QVERIFY2(graphics.ok, qPrintable(graphics.error));

    const auto result = exportRichPage(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));

    // 独立校验的入口是 Document.xml：从它一路数到页面里的图元。
    const OfdDocumentScan scan = scanDocument(result.bytes);
    QVERIFY2(scan.ok, qPrintable(scan.error));
    QCOMPARE(scan.pageCount, 1);
    QCOMPARE(scan.pathCount, graphics.paths.size());
    QCOMPARE(scan.imageCount, graphics.images.size());
    QVERIFY(scan.textCount > 0);
}

void Wave21OfdTests::ofdBezierStaysBezier() {
    const QByteArray pdf = richFixturePdf();
    const auto result = exportRichPage(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    const OfdPageScan scan = scanPageContent(firstPageContent(result.bytes));
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.pathData.size(), 3);

    // OFD 的路径描述里，三次贝塞尔是 **B**（不是 SVG 的 C；OFD 的 C 是「闭合」）。
    bool foundBezier = false;
    for (const QString& data : scan.pathData) {
        if (!data.contains(QStringLiteral(" B "))) continue;
        foundBezier = true;
        // 控制点必须真的在文件里：B 后面是 6 个数（两个控制点 + 终点）。
        const QStringList parts = data.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        int bezierOperandCount = 0;
        for (int i = 0; i < parts.size(); ++i) {
            if (parts.at(i) != QStringLiteral("B")) continue;
            bezierOperandCount = parts.size() - i - 1;
            break;
        }
        QCOMPARE(bezierOperandCount, 6);
        // 曲线那条路径里不能多出折线段：打散成折线会让 L 的数量变多。
        int lineCount = 0;
        for (const QString& part : parts) {
            if (part == QStringLiteral("L")) ++lineCount;
        }
        QCOMPARE(lineCount, 0);
    }
    QVERIFY2(foundBezier, qPrintable(scan.pathData.join(QStringLiteral(" | "))));
}

void Wave21OfdTests::ofdPathCarriesColorAndLineWidth() {
    const QByteArray pdf = richFixturePdf();
    const auto result = exportRichPage(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    const OfdPageScan scan = scanPageContent(firstPageContent(result.bytes));
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));

    // 红色描边（1 0 0 RG）、蓝色填充（0 0 1 rg）都要写进去。
    QVERIFY2(scan.pathStrokeColors.contains(QStringLiteral("255 0 0")),
             qPrintable(scan.pathStrokeColors.join(QStringLiteral(" | "))));
    QVERIFY2(scan.pathFillColors.contains(QStringLiteral("0 0 255")),
             qPrintable(scan.pathFillColors.join(QStringLiteral(" | "))));

    // 线宽：内容流里是 2 点 → 0.706 毫米（2 × 25.4/72）。
    bool foundWidth = false;
    for (const QString& width : scan.pathLineWidths) {
        if (qAbs(width.toDouble() - 0.706) < 0.001) foundWidth = true;
    }
    QVERIFY2(foundWidth, qPrintable(scan.pathLineWidths.join(QStringLiteral(" | "))));
}

void Wave21OfdTests::ofdPathCoordinatesUsePageSpace() {
    const QByteArray pdf = richFixturePdf();
    const auto result = exportRichPage(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    const OfdPageScan scan = scanPageContent(firstPageContent(result.bytes));
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.pathBoundaries.size(), 3);

    // OFD 的页面空间是左上原点、y 向下（GB/T 33190-2016）。
    // 蓝色矩形在 PDF 里是 x 20..80、y 40..70（页面 200×100 点）：
    //   x = 20 点 = 7.056 毫米；
    //   上边 = 页面顶往下 (100 - 70) 点 = 10.583 毫米；
    //   宽 = 60 点 = 21.167 毫米；高 = 30 点 = 10.583 毫米。
    bool foundRect = false;
    for (const QString& boundary : scan.pathBoundaries) {
        const QStringList parts = boundary.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QCOMPARE(parts.size(), 4);
        if (qAbs(parts.at(0).toDouble() - 7.056) > 0.002) continue;
        if (qAbs(parts.at(1).toDouble() - 10.583) > 0.002) continue;
        if (qAbs(parts.at(2).toDouble() - 21.167) > 0.002) continue;
        if (qAbs(parts.at(3).toDouble() - 10.583) > 0.002) continue;
        foundRect = true;
    }
    QVERIFY2(foundRect, qPrintable(scan.pathBoundaries.join(QStringLiteral(" | "))));
}

void Wave21OfdTests::ofdImageIsPackagedAsPageResource() {
    const QByteArray pdf = richFixturePdf();
    const PageGraphics graphics = pageGraphicsOf(pdf, 0);
    QVERIFY2(graphics.ok, qPrintable(graphics.error));
    QCOMPARE(graphics.images.size(), 1);
    const auto& image = graphics.images.first();

    const auto result = exportRichPage(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));

    // 图片字节必须原样进包（PNG 头还在），并且资源表指向它。
    const QStringList names = aipdf::doc::zip::listEntryNames(result.bytes);
    QString mediaEntry;
    for (const QString& name : names) {
        if (name.startsWith(QStringLiteral("Doc_0/Pages/Page_0/Res/Res_0/Image_"))) {
            mediaEntry = name;
            break;
        }
    }
    QVERIFY2(!mediaEntry.isEmpty(), qPrintable(names.join(QStringLiteral(" | "))));
    const QByteArray media = aipdf::doc::zip::readEntry(result.bytes, mediaEntry);
    QCOMPARE(media, image.data);

    // 页面资源表要登记这张图。
    const QByteArray res =
        aipdf::doc::zip::readEntry(result.bytes, QStringLiteral("Doc_0/Pages/Page_0/Res/Res_0.xml"));
    QVERIFY2(!res.isEmpty(), "页面资源文件不在包里");
    QVERIFY(res.contains("MultiMedia"));
    QVERIFY(res.contains(mediaEntry.section(QLatin1Char('/'), -1).toUtf8()));

    // 图像对象要引用资源表里的 ID。
    const OfdPageScan scan = scanPageContent(firstPageContent(result.bytes));
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.imageResourceIds.size(), 1);
    QVERIFY2(res.contains(QStringLiteral("ID=\"%1\"").arg(scan.imageResourceIds.first()).toUtf8()),
             qPrintable(res));
}

void Wave21OfdTests::ofdKeepsTextObjects() {
    const QByteArray pdf = richFixturePdf();
    const TextPage text = pageTextOf(pdf, 0);
    QVERIFY(!text.chars.isEmpty());

    const auto result = exportRichPage(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    const OfdPageScan scan = scanPageContent(firstPageContent(result.bytes));
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));

    // 文字对象数必须与排出来的行数一致：加了图形之后文字不能被挤掉。
    const QVector<aipdf::doc::TextExportLine> lines = aipdf::doc::buildTextLines(text);
    QVector<QString> expectedTexts;
    for (const aipdf::doc::TextExportLine& line : lines) {
        if (!line.text.isEmpty()) expectedTexts.append(line.text);
    }
    QCOMPARE(scan.textCount, expectedTexts.size());
    // 写进 OFD 的文字必须就是排版出来的那一行（一个字符都不能少）。
    QCOMPARE(scan.textCodes.size(), expectedTexts.size());
    for (int i = 0; i < expectedTexts.size(); ++i) {
        QCOMPARE(scan.textCodes.at(i), expectedTexts.at(i));
    }
    QVERIFY(!expectedTexts.isEmpty());
    QVERIFY(!expectedTexts.first().isEmpty());
}

void Wave21OfdTests::textOnlyExportStillWorks() {
    // 老入口：只传文字的 exportPagesToOfd 仍然能用（界面层还没换接线之前）。
    const QByteArray pdf = richFixturePdf();
    const QVector<TextPage> pages{pageTextOf(pdf, 0)};
    const OfdExportResult result = aipdf::doc::exportPagesToOfd(pages, QSizeF(200.0, 100.0));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.bytes.isEmpty());

    const OfdPageScan scan = scanPageContent(firstPageContent(result.bytes));
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QVERIFY(scan.textCount > 0);
    // 没有图形就没有图形对象——不能凭空多出路径或图像。
    QCOMPARE(scan.pathCount, 0);
    QCOMPARE(scan.imageCount, 0);
    QCOMPARE(result.pathCount, 0);
    QCOMPARE(result.imageCount, 0);
}

void Wave21OfdTests::exportWithoutGraphicsKeepsOnlyText() {
    // 图形层没取到（ok=false）不算失败：文字照导，图形当没有。
    const QByteArray pdf = richFixturePdf();
    OfdPageContent content;
    content.text = pageTextOf(pdf, 0);
    content.graphics = pageGraphicsOf(pdf, 0);
    content.graphics.ok = false;
    content.graphics.paths.clear();
    content.graphics.images.clear();

    const OfdExportResult result =
        aipdf::doc::exportPagesToOfd(QVector<OfdPageContent>{content}, QSizeF(200.0, 100.0));
    QVERIFY2(result.ok, qPrintable(result.error));
    const OfdPageScan scan = scanPageContent(firstPageContent(result.bytes));
    QVERIFY2(scan.wellFormed, qPrintable(scan.error));
    QCOMPARE(scan.pathCount, 0);
    QCOMPARE(scan.imageCount, 0);
    QVERIFY(scan.textCount > 0);
}

void Wave21OfdTests::multiPageExportKeepsPerPageObjects() {
    const QByteArray pdf = twoPageFixturePdf();
    QVector<OfdPageContent> contents;
    for (int index = 0; index < 2; ++index) {
        OfdPageContent content;
        content.text = pageTextOf(pdf, index);
        content.graphics = pageGraphicsOf(pdf, index);
        contents.append(content);
    }
    const OfdExportResult result =
        aipdf::doc::exportPagesToOfd(contents, QSizeF(200.0, 100.0));
    QVERIFY2(result.ok, qPrintable(result.error));

    const OfdDocumentScan scan = scanDocument(result.bytes);
    QVERIFY2(scan.ok, qPrintable(scan.error));
    QCOMPARE(scan.pageCount, 2);
    // 第一页有一条路径、一张图，第二页什么都没有。
    QCOMPARE(scan.pathCount, 1);
    QCOMPARE(scan.imageCount, 1);
    QCOMPARE(result.pathCount, 1);
    QCOMPARE(result.imageCount, 1);

    const OfdPageScan second = scanPageContent(
        aipdf::doc::zip::readEntry(result.bytes, QStringLiteral("Doc_0/Pages/Page_1/Content.xml")));
    QVERIFY2(second.wellFormed, qPrintable(second.error));
    QCOMPARE(second.pathCount, 0);
    QCOMPARE(second.imageCount, 0);
    QVERIFY(second.textCount > 0);
}

QTEST_MAIN(Wave21OfdTests)
#include "Wave21OfdTests.moc"
