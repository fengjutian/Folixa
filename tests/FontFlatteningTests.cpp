// 字体转曲的**栅格化模式**（旧行为）的测试。
//
// 注意：这里测的是**视觉等价**的转曲（渲染成位图再写回 PDF），
// 不是轮廓化转曲。断言 accordingly：转曲后文字取不到了，
// 但页面尺寸、页数必须保持。
//
// 为什么显式写 mode = Raster：转曲的默认模式已经改成矢量轮廓化，
// 这组用例守的是旧行为，必须自己把模式钉死，不能跟着默认值漂移。
// 轮廓化模式的用例在 tests/Wave21FlattenTests.cpp。

#include <QtTest>

#include <QRegularExpression>

#include <cmath>
#include <optional>

#include "doc/model/BlankDocument.h"
#include "doc/convert/FontFlattening.h"
#include "pdf/kernel/PdfiumAdapter.h"

using aipdf::doc::FlattenMode;
using aipdf::doc::FlattenOptions;

namespace {

// 旧行为（栅格化）的参数。
FlattenOptions rasterOptions() {
    FlattenOptions options;
    options.mode = FlattenMode::Raster;
    return options;
}

// 造一份带文字层的夹具 PDF。
//
// 为什么自己造而不是用 tests/fixtures 里的文件：这份夹具必须
// 「一页、尺寸固定、有可提取文字」，自己造才能把这三件事都钉死，
// 测试才不受别的夹具变化影响。
QByteArray makeTextPdf(int pageCount = 1, double widthPoints = 200.0,
                       double heightPoints = 150.0) {
    aipdf::doc::BlankDocumentOptions options;
    options.size = aipdf::doc::PageSize::Custom;
    options.widthPt = widthPoints;
    options.heightPt = heightPoints;
    options.pageCount = pageCount;

    QByteArray bytes = aipdf::doc::createBlankDocument(options);
    if (bytes.isEmpty()) {
        return QByteArray();
    }
    aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) {
        return QByteArray();
    }
    for (int i = 0; i < pageCount; ++i) {
        std::optional<aipdf::pdf::PdfPage> page = opened.document.page(i);
        if (!page.has_value()) {
            return QByteArray();
        }
        if (!page->addTextLayer(QStringLiteral("Flatten me %1").arg(i + 1), 20.0, 60.0, 14.0,
                                Qt::black)) {
            return QByteArray();
        }
    }
    return opened.document.serialize();
}

// 从 PDF 字节里找出图像字典里最大的 /Height（像素）。
//
// 转曲后的 PDF 里唯一的图像就是转曲位图，所以它的像素高度
// 就是「这张图有多大」的证据。
int maxImageHeight(const QByteArray& pdfBytes) {
    const QString text = QString::fromLatin1(pdfBytes);
    QRegularExpression expression(QStringLiteral("/Height\\s+(\\d+)"));
    int largest = 0;
    QRegularExpressionMatchIterator iterator = expression.globalMatch(text);
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        largest = qMax(largest, match.captured(1).toInt());
    }
    return largest;
}

// 去掉 PDF 尾部 trailer 里的 /ID[...]。
//
// PDFium 每次写文件都会生成一个新的 /ID（PDF 规范允许它每次都变），
// 它不代表内容变了。比较两次导出时必须先把它去掉，否则永远不相等。
QByteArray stripPdfId(const QByteArray& pdfBytes) {
    QRegularExpression expression(QStringLiteral("/ID\\[[^\\]]*\\]"));
    QString text = QString::fromLatin1(pdfBytes);
    text.remove(expression);
    return text.toLatin1();
}

// 两张图的平均绝对差（0..255）。用来判断「看起来是不是同一张」。
double meanAbsoluteDifference(const QImage& left, const QImage& right) {
    if (left.isNull() || right.isNull() || left.size() != right.size()) {
        return 255.0;
    }
    const QImage a = left.convertToFormat(QImage::Format_Grayscale8);
    const QImage b = right.convertToFormat(QImage::Format_Grayscale8);
    double total = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* rowA = a.constScanLine(y);
        const uchar* rowB = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x) {
            total += std::abs(static_cast<int>(rowA[x]) - static_cast<int>(rowB[x]));
        }
    }
    return total / static_cast<double>(a.width() * a.height());
}

}  // namespace

class FontFlatteningTests : public QObject {
    Q_OBJECT

private slots:
    void outputIsValidPdfWithSamePageCount();
    void textIsGoneWhenTextLayerIsRemoved();
    void textStaysWhenTextLayerIsKept();
    void hasExtractableTextDistinguishesFixtureFromResult();
    void higherDpiProducesBiggerImage();
    void nonPdfInputFailsWithChineseReason();
    void emptyInputFails();
    void pageSizeMatchesOriginal();
    void repeatedFlattenKeepsSameBytes();
    void rotatedPageStillLooksTheSame();
};

void FontFlatteningTests::outputIsValidPdfWithSamePageCount() {
    const QByteArray source = makeTextPdf(2);
    QVERIFY(!source.isEmpty());

    QString error;
    const QByteArray output =
        aipdf::doc::flattenFontsToOutlines(source, rasterOptions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    QVERIFY(output.startsWith("%PDF"));

    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(output);
    QVERIFY(opened.document.isValid());
    QCOMPARE(opened.document.pageCount(), 2);
}

void FontFlatteningTests::textIsGoneWhenTextLayerIsRemoved() {
    const QByteArray source = makeTextPdf(1);
    QVERIFY(!source.isEmpty());
    QVERIFY(aipdf::doc::hasExtractableText(source));

    FlattenOptions options = rasterOptions();
    options.removeTextLayer = true;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, options);
    QVERIFY(!output.isEmpty());
    QVERIFY2(!aipdf::doc::hasExtractableText(output), "转曲后仍然能取到文字");
}

void FontFlatteningTests::textStaysWhenTextLayerIsKept() {
    const QByteArray source = makeTextPdf(1);
    QVERIFY(!source.isEmpty());

    FlattenOptions options = rasterOptions();
    options.removeTextLayer = false;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, options);
    QVERIFY(!output.isEmpty());
    QVERIFY2(aipdf::doc::hasExtractableText(output), "保留文本层时文字应当还能取到");
}

void FontFlatteningTests::hasExtractableTextDistinguishesFixtureFromResult() {
    const QByteArray source = makeTextPdf(1);
    QVERIFY(!source.isEmpty());
    QVERIFY(aipdf::doc::hasExtractableText(source));

    FlattenOptions options = rasterOptions();
    options.removeTextLayer = true;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, options);
    QVERIFY(!output.isEmpty());
    QVERIFY(!aipdf::doc::hasExtractableText(output));

    // 空字节与坏字节都不是「有文字」。
    QVERIFY(!aipdf::doc::hasExtractableText(QByteArray()));
    QVERIFY(!aipdf::doc::hasExtractableText(QByteArrayLiteral("not a pdf")));
}

void FontFlatteningTests::higherDpiProducesBiggerImage() {
    const QByteArray source = makeTextPdf(1);
    QVERIFY(!source.isEmpty());

    FlattenOptions low = rasterOptions();
    low.dpi = 72.0;
    FlattenOptions high = rasterOptions();
    high.dpi = 288.0;

    const QByteArray lowOutput = aipdf::doc::flattenFontsToOutlines(source, low);
    const QByteArray highOutput = aipdf::doc::flattenFontsToOutlines(source, high);
    QVERIFY(!lowOutput.isEmpty());
    QVERIFY(!highOutput.isEmpty());

    const int lowHeight = maxImageHeight(lowOutput);
    const int highHeight = maxImageHeight(highOutput);
    QVERIFY2(lowHeight > 0, "低分辨率输出里没找到图像");
    QVERIFY2(highHeight > lowHeight,
             qPrintable(QStringLiteral("高分辨率的图像没有更大：%1 vs %2")
                            .arg(highHeight)
                            .arg(lowHeight)));
}

void FontFlatteningTests::nonPdfInputFailsWithChineseReason() {
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(
        QByteArrayLiteral("this is not a pdf at all"), rasterOptions(), &error);
    QVERIFY(output.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("字体转曲失败")), qPrintable(error));
}

void FontFlatteningTests::emptyInputFails() {
    QString error;
    const QByteArray output =
        aipdf::doc::flattenFontsToOutlines(QByteArray(), rasterOptions(), &error);
    QVERIFY(output.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("字体转曲失败")), qPrintable(error));
}

void FontFlatteningTests::pageSizeMatchesOriginal() {
    const QByteArray source = makeTextPdf(1, 200.0, 150.0);
    QVERIFY(!source.isEmpty());

    const aipdf::pdf::PdfOpenResult original = aipdf::pdf::openDocumentFromBytes(source);
    QVERIFY(original.document.isValid());
    const std::optional<aipdf::pdf::PdfPage> originalPage = original.document.page(0);
    QVERIFY(originalPage.has_value());

    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, rasterOptions());
    QVERIFY(!output.isEmpty());
    const aipdf::pdf::PdfOpenResult flattened = aipdf::pdf::openDocumentFromBytes(output);
    QVERIFY(flattened.document.isValid());
    const std::optional<aipdf::pdf::PdfPage> flattenedPage = flattened.document.page(0);
    QVERIFY(flattenedPage.has_value());

    QCOMPARE(flattenedPage->widthPoints(), originalPage->widthPoints());
    QCOMPARE(flattenedPage->heightPoints(), originalPage->heightPoints());
}

void FontFlatteningTests::repeatedFlattenKeepsSameBytes() {
    const QByteArray source = makeTextPdf(1);
    QVERIFY(!source.isEmpty());

    FlattenOptions options = rasterOptions();
    options.dpi = 96.0;
    const QByteArray first = aipdf::doc::flattenFontsToOutlines(source, options);
    const QByteArray second = aipdf::doc::flattenFontsToOutlines(source, options);
    QVERIFY(!first.isEmpty());
    // 同一份输入导出两次必须得到同样的内容，否则没法拿它做缓存或比对。
    // （/ID 除外：那是 PDFium 每次写文件都会重新生成的文档标识。）
    QCOMPARE(stripPdfId(first), stripPdfId(second));
}

void FontFlatteningTests::rotatedPageStillLooksTheSame() {
    // 带 /Rotate 90 的页面：显示方向是竖的，页面坐标方向是横的。
    // 转曲时把位图放回页面坐标方向，方向搞反的话这一页会转 180 度。
    aipdf::doc::BlankDocumentOptions blankOptions;
    blankOptions.size = aipdf::doc::PageSize::Custom;
    blankOptions.widthPt = 200.0;
    blankOptions.heightPt = 150.0;
    blankOptions.pageCount = 1;

    aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(aipdf::doc::createBlankDocument(blankOptions));
    QVERIFY(opened.document.isValid());
    std::optional<aipdf::pdf::PdfPage> sourcePage = opened.document.page(0);
    QVERIFY(sourcePage.has_value());
    QVERIFY(sourcePage->addTextLayer(QStringLiteral("Rotate me"), 20.0, 60.0, 14.0, Qt::black));
    QVERIFY(opened.document.setPageRotation(0, 1));  // 90°
    const QByteArray source = opened.document.serialize();
    QVERIFY(!source.isEmpty());

    FlattenOptions options = rasterOptions();
    options.removeTextLayer = false;
    options.dpi = 96.0;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, options);
    QVERIFY(!output.isEmpty());

    const aipdf::pdf::PdfOpenResult originalOpened = aipdf::pdf::openDocumentFromBytes(source);
    const aipdf::pdf::PdfOpenResult flattenedOpened = aipdf::pdf::openDocumentFromBytes(output);
    QVERIFY(originalOpened.document.isValid());
    QVERIFY(flattenedOpened.document.isValid());

    const std::optional<aipdf::pdf::PdfPage> originalPage = originalOpened.document.page(0);
    const std::optional<aipdf::pdf::PdfPage> flattenedPage = flattenedOpened.document.page(0);
    QVERIFY(originalPage.has_value());
    QVERIFY(flattenedPage.has_value());

    const QImage originalImage = originalPage->render(1.0);
    const QImage flattenedImage = flattenedPage->render(1.0);
    QVERIFY(!originalImage.isNull());
    QVERIFY(!flattenedImage.isNull());
    QCOMPARE(originalImage.size(), flattenedImage.size());

    const double difference = meanAbsoluteDifference(originalImage, flattenedImage);
    QVERIFY2(difference < 12.0,
             qPrintable(QStringLiteral("转曲后的页面和原页面差太多：平均差 %1").arg(difference)));
}

QTEST_GUILESS_MAIN(FontFlatteningTests)

#include "FontFlatteningTests.moc"
