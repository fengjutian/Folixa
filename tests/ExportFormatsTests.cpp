#include "pdf/ExportFormats.h"

#include "pdf/PdfiumAdapter.h"

#include <QFile>
#include <QList>
#include <QSize>
#include <QString>
#include <QTest>
#include <QVector>

#include <cmath>

using aipdf::pdf::DxfExportOptions;
using aipdf::pdf::HtmlExportOptions;
using aipdf::pdf::HtmlPage;
using aipdf::pdf::RasterizeOptions;
using aipdf::pdf::TextPage;

namespace {

// 读一份夹具文件。夹具是构建时生成的（pdf-fixtures 目标），读不到就跳过。
QByteArray fixture(const QString& name) {
    QFile file(QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    file.close();
    return bytes;
}

// 造一份最小的 PDF（单页），内容流由调用方给。
//
// 为什么要自己造：现成的夹具都是纯文字的，没有矢量路径，
// 「页面上的路径能不能导成 DXF」这件事用它们验不了。
QByteArray buildPdfWithContent(const QByteArray& content) {
    QVector<QByteArray> bodies;
    bodies.append("<< /Type /Catalog /Pages 2 0 R >>");
    bodies.append("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    bodies.append("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                  "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>");
    bodies.append("<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content +
                  "\nendstream");
    bodies.append("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");

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

// 从生成的 PDF 里抠出第一张 JPEG（图像化 PDF 的页图）。
QByteArray extractFirstJpeg(const QByteArray& pdf) {
    const int marker = pdf.indexOf("/DCTDecode");
    if (marker < 0) return {};
    const int stream = pdf.indexOf("stream\n", marker);
    if (stream < 0) return {};
    const int start = stream + 7;
    const int end = pdf.indexOf("\nendstream", start);
    if (end < 0) return {};
    return pdf.mid(start, end - start);
}

// 读 JPEG 的宽高（SOF 段）。用来验「分辨率真的影响了图像尺寸」。
QSize jpegSize(const QByteArray& bytes) {
    if (bytes.size() < 10) return {};
    if (static_cast<quint8>(bytes.at(0)) != 0xFF || static_cast<quint8>(bytes.at(1)) != 0xD8) {
        return {};
    }
    int i = 2;
    while (i + 9 < bytes.size()) {
        if (static_cast<quint8>(bytes.at(i)) != 0xFF) {
            ++i;
            continue;
        }
        const quint8 marker = static_cast<quint8>(bytes.at(i + 1));
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2;
            continue;
        }
        const int length = (static_cast<quint8>(bytes.at(i + 2)) << 8) |
                           static_cast<quint8>(bytes.at(i + 3));
        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            const int height = (static_cast<quint8>(bytes.at(i + 5)) << 8) |
                               static_cast<quint8>(bytes.at(i + 6));
            const int width = (static_cast<quint8>(bytes.at(i + 7)) << 8) |
                              static_cast<quint8>(bytes.at(i + 8));
            return QSize(width, height);
        }
        i += 2 + length;
    }
    return {};
}

// DXF 里的一个实体（只取用得上的字段）。
struct DxfEntityView {
    QString type;
    QVector<double> coordinates;  // 组码 10/20/11/21
    QString text;                 // 组码 1
    int flags = 0;                // 组码 70
};

// 把 DXF 拆成实体列表。DXF 是「一行组码、一行值」成对的文本格式。
QVector<DxfEntityView> parseDxfEntities(const QByteArray& dxf) {
    QVector<DxfEntityView> entities;
    const QList<QByteArray> lines = dxf.split('\n');
    bool inEntities = false;
    bool pendingSection = false;
    DxfEntityView current;
    bool haveEntity = false;
    for (int i = 0; i + 1 < lines.size(); i += 2) {
        bool ok = false;
        const int code = lines.at(i).trimmed().toInt(&ok);
        if (!ok) continue;
        const QString value = QString::fromLatin1(lines.at(i + 1)).trimmed();
        if (code == 0) {
            if (value == QStringLiteral("SECTION")) {
                pendingSection = true;
                continue;
            }
            if (value == QStringLiteral("ENDSEC")) {
                inEntities = false;
                continue;
            }
            if (value == QStringLiteral("EOF")) break;
            if (inEntities) {
                if (haveEntity) entities.append(current);
                current = DxfEntityView();
                current.type = value;
                haveEntity = true;
            }
            continue;
        }
        if (pendingSection && code == 2) {
            pendingSection = false;
            inEntities = (value == QStringLiteral("ENTITIES"));
            continue;
        }
        if (!inEntities || !haveEntity) continue;
        if (code == 10 || code == 20 || code == 11 || code == 21) {
            current.coordinates.append(value.toDouble());
        } else if (code == 1) {
            current.text = value;
        } else if (code == 70) {
            current.flags = value.toInt();
        }
    }
    if (haveEntity) entities.append(current);
    return entities;
}

// 取 HEADER 段里某个变量（$EXTMAX 之类）的第一个数值。
double dxfHeaderNumber(const QByteArray& dxf, const QByteArray& variable) {
    const int at = dxf.indexOf(variable);
    if (at < 0) return 0.0;
    const QList<QByteArray> lines = dxf.split('\n');
    for (int i = 0; i + 1 < lines.size(); ++i) {
        if (lines.at(i).trimmed() != "10") continue;
        if (!lines.at(i - 1).contains(variable)) continue;
        return lines.at(i + 1).trimmed().toDouble();
    }
    return 0.0;
}

// 取一份 PDF 某一页的文字（PDFium 读出来的）。
QString pageText(const QByteArray& pdf, int pageIndex) {
    const auto opened = aipdf::pdf::openDocumentFromBytes(pdf);
    if (!opened.document.isValid()) return {};
    const auto page = opened.document.page(pageIndex);
    if (!page.has_value()) return {};
    const auto text = page->extractText();
    if (!text.has_value()) return {};
    return text->plainText();
}

// 取一份 PDF 的页数与页面尺寸（点）。
QVector<QSizeF> pageSizes(const QByteArray& pdf) {
    QVector<QSizeF> sizes;
    const auto opened = aipdf::pdf::openDocumentFromBytes(pdf);
    if (!opened.document.isValid()) return sizes;
    for (int i = 0; i < opened.document.pageCount(); ++i) {
        const auto page = opened.document.page(i);
        if (!page.has_value()) continue;
        sizes.append(QSizeF(page->widthPoints(), page->heightPoints()));
    }
    return sizes;
}

}  // namespace

class ExportFormatsTests : public QObject {
    Q_OBJECT

private slots:
    // HTML
    void emptyTextProducesEmptyHtml();
    void htmlContainsPageText();
    void htmlKeepsSpaceBetweenWords();
    void htmlWritesPageSizeIntoStyle();
    void htmlWritesOneDivPerPage();
    void htmlEscapesMarkupCharacters();

    // 图像化 PDF
    void imageBasedPdfKeepsPageCount();
    void imageBasedPdfKeepsPageSize();
    void imageBasedPdfDpiChangesImageSize();
    void imageBasedPdfKeepsTextLayerWhenRequested();
    void imageBasedPdfDropsTextLayerByDefault();
    void imageBasedPdfKeepsChineseTextLayer();
    void imageBasedPdfRejectsNonPdfInput();

    // DXF
    void dxfStartsWithSectionAndEndsWithEof();
    void dxfExportsVectorPathsAsEntities();
    void dxfHeaderCarriesPageSize();
    void dxfScaleEnlargesCoordinates();
    void dxfExportsTextEntitiesWhenAsked();
    void dxfHasNoEntitiesWithoutVectorContent();
    void dxfRejectsInvalidInput();
};

// ============================================================== HTML ========

void ExportFormatsTests::emptyTextProducesEmptyHtml() {
    const TextPage empty;
    const QString html = aipdf::pdf::exportPageToHtml(empty, QSizeF(200.0, 100.0));
    QVERIFY2(html.isEmpty(), qPrintable(html));

    // 多页接口没有页面时报错，而不是给一份空文档。
    const auto result = aipdf::pdf::exportToHtml({});
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void ExportFormatsTests::htmlContainsPageText() {
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 normal.pdf");
    const auto opened = aipdf::pdf::openDocumentFromBytes(bytes);
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const auto text = page->extractText();
    QVERIFY(text.has_value());

    const QString html = aipdf::pdf::exportPageToHtml(*text, QSizeF(200.0, 100.0));
    QVERIFY(!html.isEmpty());
    QVERIFY2(html.contains(QStringLiteral("Hello PDFium")), qPrintable(html));
    QVERIFY(html.contains(QStringLiteral("class=\"page\"")));
    QVERIFY(html.contains(QStringLiteral("class=\"line\"")));
}

void ExportFormatsTests::htmlKeepsSpaceBetweenWords() {
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 normal.pdf");
    const auto opened = aipdf::pdf::openDocumentFromBytes(bytes);
    QVERIFY(opened.document.isValid());
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const auto text = page->extractText();
    QVERIFY(text.has_value());

    const QString html = aipdf::pdf::exportPageToHtml(*text, QSizeF(200.0, 100.0));
    // 词之间的空格不能被吃掉：HTML 默认会折叠空格，所以还要带上 pre-wrap。
    QVERIFY2(html.contains(QStringLiteral("Hello PDFium")), qPrintable(html));
    QVERIFY(!html.contains(QStringLiteral("HelloPDFium")));
    QVERIFY2(html.contains(QStringLiteral("white-space: pre-wrap")), qPrintable(html));
}

void ExportFormatsTests::htmlWritesPageSizeIntoStyle() {
    const TextPage empty;
    // 没有文字时单页接口返回空串，用多页接口才能看到页面盒子。
    TextPage text;
    aipdf::pdf::TextChar character;
    character.text = QStringLiteral("X");
    character.box = QRectF(10.0, 60.0, 8.0, 12.0);
    text.chars.append(character);

    HtmlPage page;
    page.text = text;
    page.sizePt = QSizeF(200.0, 100.0);
    const auto result = aipdf::pdf::exportToHtml({page});
    QVERIFY2(result.ok, qPrintable(result.error));
    // 200 点 = 70.556 毫米，100 点 = 35.278 毫米。
    QVERIFY2(result.html.contains(QStringLiteral("width:70.556mm")), qPrintable(result.html));
    QVERIFY2(result.html.contains(QStringLiteral("height:35.278mm")), qPrintable(result.html));
    Q_UNUSED(empty);
}

void ExportFormatsTests::htmlWritesOneDivPerPage() {
    const QByteArray bytes = fixture(QStringLiteral("text.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 text.pdf");
    const auto opened = aipdf::pdf::openDocumentFromBytes(bytes);
    QVERIFY(opened.document.isValid());
    QVERIFY(opened.document.pageCount() >= 2);

    QVector<HtmlPage> pages;
    for (int i = 0; i < opened.document.pageCount(); ++i) {
        const auto page = opened.document.page(i);
        QVERIFY(page.has_value());
        HtmlPage item;
        item.text = page->extractText().value_or(TextPage());
        item.sizePt = QSizeF(page->widthPoints(), page->heightPoints());
        pages.append(item);
    }

    const auto result = aipdf::pdf::exportToHtml(pages);
    QVERIFY2(result.ok, qPrintable(result.error));
    const int divCount = result.html.count(QStringLiteral("<div class=\"page\""));
    QCOMPARE(divCount, pages.size());
    // 两页的文字都要在里面。
    QVERIFY(result.html.contains(QStringLiteral("Folixa")));
    QVERIFY(result.html.contains(QStringLiteral("second page")));
}

void ExportFormatsTests::htmlEscapesMarkupCharacters() {
    TextPage text;
    aipdf::pdf::TextChar character;
    character.text = QStringLiteral("a<b>&c");
    character.box = QRectF(10.0, 60.0, 30.0, 12.0);
    text.chars.append(character);

    HtmlPage page;
    page.text = text;
    page.sizePt = QSizeF(200.0, 100.0);
    const auto result = aipdf::pdf::exportToHtml({page});
    QVERIFY(result.ok);
    QVERIFY2(result.html.contains(QStringLiteral("a&lt;b&gt;&amp;c")), qPrintable(result.html));
    QVERIFY(!result.html.contains(QStringLiteral("<b>")));
}

// ==================================================== 图像化 PDF ===========

void ExportFormatsTests::imageBasedPdfKeepsPageCount() {
    const QByteArray bytes = fixture(QStringLiteral("text.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 text.pdf");

    QString error;
    const QByteArray output = aipdf::pdf::exportToImageBasedPdf(bytes, RasterizeOptions{}, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    QVERIFY(output.startsWith("%PDF-"));

    const auto reopened = aipdf::pdf::openDocumentFromBytes(output);
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));
    QCOMPARE(reopened.document.pageCount(), 2);
}

void ExportFormatsTests::imageBasedPdfKeepsPageSize() {
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 normal.pdf");

    QString error;
    const QByteArray output = aipdf::pdf::exportToImageBasedPdf(bytes, RasterizeOptions{}, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<QSizeF> before = pageSizes(bytes);
    const QVector<QSizeF> after = pageSizes(output);
    QCOMPARE(after.size(), before.size());
    for (int i = 0; i < before.size(); ++i) {
        QVERIFY(qAbs(after.at(i).width() - before.at(i).width()) < 0.5);
        QVERIFY(qAbs(after.at(i).height() - before.at(i).height()) < 0.5);
    }
}

void ExportFormatsTests::imageBasedPdfDpiChangesImageSize() {
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 normal.pdf");

    QString error;
    RasterizeOptions low;
    low.dpi = 72.0;
    const QByteArray lowPdf = aipdf::pdf::exportToImageBasedPdf(bytes, low, &error);
    QVERIFY2(!lowPdf.isEmpty(), qPrintable(error));
    RasterizeOptions high;
    high.dpi = 300.0;
    const QByteArray highPdf = aipdf::pdf::exportToImageBasedPdf(bytes, high, &error);
    QVERIFY2(!highPdf.isEmpty(), qPrintable(error));

    const QSize lowSize = jpegSize(extractFirstJpeg(lowPdf));
    const QSize highSize = jpegSize(extractFirstJpeg(highPdf));
    QVERIFY(!lowSize.isEmpty());
    QVERIFY(!highSize.isEmpty());
    // 200×100 点的页面：72 DPI → 200×100 像素，300 DPI → 833×417 像素。
    QCOMPARE(lowSize.width(), 200);
    QCOMPARE(lowSize.height(), 100);
    QCOMPARE(highSize.width(), 833);
    QCOMPARE(highSize.height(), 417);
    QVERIFY(highSize.width() > lowSize.width());
    QVERIFY(highSize.height() > lowSize.height());
}

void ExportFormatsTests::imageBasedPdfKeepsTextLayerWhenRequested() {
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 normal.pdf");

    QString error;
    RasterizeOptions options;
    options.keepTextLayer = true;
    const QByteArray output = aipdf::pdf::exportToImageBasedPdf(bytes, options, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QString text = pageText(output, 0);
    QVERIFY2(text.contains(QStringLiteral("Hello PDFium")), qPrintable(text));
}

void ExportFormatsTests::imageBasedPdfDropsTextLayerByDefault() {
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 normal.pdf");

    QString error;
    const QByteArray output = aipdf::pdf::exportToImageBasedPdf(bytes, RasterizeOptions{}, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QString text = pageText(output, 0);
    QVERIFY2(text.trimmed().isEmpty(), qPrintable(text));
}

void ExportFormatsTests::imageBasedPdfKeepsChineseTextLayer() {
    const QByteArray bytes = fixture(QStringLiteral("cjk.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 cjk.pdf");

    QString error;
    RasterizeOptions options;
    options.keepTextLayer = true;
    const QByteArray output = aipdf::pdf::exportToImageBasedPdf(bytes, options, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    // 不可见文字层用的是 Identity-H + ToUnicode，中文要能被原样抽出来。
    const QString text = pageText(output, 0);
    QVERIFY2(text.contains(QStringLiteral("中文")), qPrintable(text));
}

void ExportFormatsTests::imageBasedPdfRejectsNonPdfInput() {
    QString error;
    const QByteArray output =
        aipdf::pdf::exportToImageBasedPdf(QByteArray("not a pdf at all"), RasterizeOptions{}, &error);
    QVERIFY(output.isEmpty());
    QVERIFY2(!error.isEmpty(), "失败时必须给出原因");
    // 领域层的错误说明是中文硬编码。
    QVERIFY2(error.contains(QStringLiteral("PDF")), qPrintable(error));

    error.clear();
    QVERIFY(aipdf::pdf::exportToImageBasedPdf(QByteArray(), RasterizeOptions{}, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

// ============================================================== DXF ========

void ExportFormatsTests::dxfStartsWithSectionAndEndsWithEof() {
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 normal.pdf");

    QString error;
    const QByteArray dxf = aipdf::pdf::exportPdfToDxf(bytes, DxfExportOptions{}, &error);
    QVERIFY2(!dxf.isEmpty(), qPrintable(error));
    QVERIFY2(dxf.startsWith("  0\nSECTION"), dxf.left(40).constData());
    QVERIFY2(dxf.trimmed().endsWith("EOF"), dxf.right(40).constData());
    QVERIFY(dxf.contains("\nENTITIES\n"));
}

void ExportFormatsTests::dxfExportsVectorPathsAsEntities() {
    // 造一份带矢量路径的 PDF：一条只有两个点的折线（直线）+ 一个闭合三角形。
    const QByteArray content =
        "10 20 m 110 20 l S\n"
        "20 10 m 80 10 l 80 60 l 20 60 l h f\n";
    const QByteArray pdf = buildPdfWithContent(content);

    QString error;
    const QByteArray dxf = aipdf::pdf::exportPdfToDxf(pdf, DxfExportOptions{}, &error);
    QVERIFY2(!dxf.isEmpty(), qPrintable(error));

    const QVector<DxfEntityView> entities = parseDxfEntities(dxf);
    QVERIFY2(!entities.isEmpty(), dxf.constData());

    bool hasLine = false;
    bool hasPolyline = false;
    bool polylineClosed = false;
    for (const DxfEntityView& entity : entities) {
        if (entity.type == QStringLiteral("LINE")) hasLine = true;
        if (entity.type == QStringLiteral("LWPOLYLINE")) {
            hasPolyline = true;
            // 70 组：位 1 = 闭合。
            polylineClosed = (entity.flags & 1) != 0;
        }
    }
    QVERIFY2(hasLine, dxf.constData());
    QVERIFY2(hasPolyline, dxf.constData());
    QVERIFY2(polylineClosed, "闭合的多边形要带上 70 组闭合标志");
}

void ExportFormatsTests::dxfHeaderCarriesPageSize() {
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 normal.pdf");

    QString error;
    const QByteArray dxf = aipdf::pdf::exportPdfToDxf(bytes, DxfExportOptions{}, &error);
    QVERIFY2(!dxf.isEmpty(), qPrintable(error));
    QVERIFY(dxf.contains("$EXTMIN"));
    QVERIFY(dxf.contains("$EXTMAX"));
    // normal.pdf 的页面是 200×100 点。
    QVERIFY(qAbs(dxfHeaderNumber(dxf, "$EXTMAX") - 200.0) < 0.5);
    QVERIFY(qAbs(dxfHeaderNumber(dxf, "$EXTMIN") - 0.0) < 0.5);
}

void ExportFormatsTests::dxfScaleEnlargesCoordinates() {
    const QByteArray content = "10 20 m 110 20 l 110 90 l S\n";
    const QByteArray pdf = buildPdfWithContent(content);
    QString error;
    const QByteArray normal = aipdf::pdf::exportPdfToDxf(pdf, DxfExportOptions{}, &error);
    QVERIFY2(!normal.isEmpty(), qPrintable(error));
    DxfExportOptions doubled;
    doubled.scale = 2.0;
    const QByteArray scaled = aipdf::pdf::exportPdfToDxf(pdf, doubled, &error);
    QVERIFY2(!scaled.isEmpty(), qPrintable(error));

    const QVector<DxfEntityView> normalEntities = parseDxfEntities(normal);
    const QVector<DxfEntityView> scaledEntities = parseDxfEntities(scaled);
    QVERIFY(!normalEntities.isEmpty());
    QVERIFY(!scaledEntities.isEmpty());
    QCOMPARE(scaledEntities.size(), normalEntities.size());

    double normalMax = 0.0;
    double scaledMax = 0.0;
    for (const DxfEntityView& entity : normalEntities) {
        for (const double value : entity.coordinates) normalMax = qMax(normalMax, qAbs(value));
    }
    for (const DxfEntityView& entity : scaledEntities) {
        for (const double value : entity.coordinates) scaledMax = qMax(scaledMax, qAbs(value));
    }
    QVERIFY(normalMax > 0.0);
    QVERIFY2(qAbs(scaledMax - normalMax * 2.0) < 0.5,
             qPrintable(QStringLiteral("normal=%1 scaled=%2").arg(normalMax).arg(scaledMax)));
    // HEADER 里的图形范围也跟着放大。
    QVERIFY(qAbs(dxfHeaderNumber(scaled, "$EXTMAX") - dxfHeaderNumber(normal, "$EXTMAX") * 2.0) <
            0.5);
}

void ExportFormatsTests::dxfExportsTextEntitiesWhenAsked() {
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 normal.pdf");

    QString error;
    DxfExportOptions options;
    options.includeText = true;
    const QByteArray dxf = aipdf::pdf::exportPdfToDxf(bytes, options, &error);
    QVERIFY2(!dxf.isEmpty(), qPrintable(error));

    const QVector<DxfEntityView> entities = parseDxfEntities(dxf);
    bool hasText = false;
    for (const DxfEntityView& entity : entities) {
        if (entity.type == QStringLiteral("TEXT") && entity.text.contains(QStringLiteral("Hello"))) {
            hasText = true;
        }
    }
    QVERIFY2(hasText, dxf.constData());
}

void ExportFormatsTests::dxfHasNoEntitiesWithoutVectorContent() {
    // text.pdf 只有文字，没有矢量路径：不开 includeText 时 ENTITIES 段应当是空的，
    // 不能拿别的东西冒充路径。
    const QByteArray bytes = fixture(QStringLiteral("text.pdf"));
    if (bytes.isEmpty()) QSKIP("缺少夹具 text.pdf");

    QString error;
    const QByteArray dxf = aipdf::pdf::exportPdfToDxf(bytes, DxfExportOptions{}, &error);
    QVERIFY2(!dxf.isEmpty(), qPrintable(error));
    QVERIFY(parseDxfEntities(dxf).isEmpty());
    QVERIFY(dxf.contains("\nENTITIES\n"));
}

void ExportFormatsTests::dxfRejectsInvalidInput() {
    QString error;
    const QByteArray dxf =
        aipdf::pdf::exportPdfToDxf(QByteArray("this is not a pdf"), DxfExportOptions{}, &error);
    QVERIFY(dxf.isEmpty());
    QVERIFY2(!error.isEmpty(), "失败时必须给出原因");
    QVERIFY2(error.contains(QStringLiteral("PDF")), qPrintable(error));

    error.clear();
    QVERIFY(aipdf::pdf::exportPdfToDxf(QByteArray(), DxfExportOptions{}, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(ExportFormatsTests)
#include "ExportFormatsTests.moc"
