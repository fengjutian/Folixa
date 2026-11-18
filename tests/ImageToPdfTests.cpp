#include "app/ScannerCapture.h"
#include "pdf/convert/ImageToPdf.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageWriter>
#include <QtTest>

#include <cstring>

using aipdf::app::ScanErrorKind;
using aipdf::app::ScanOutcome;
using aipdf::app::ScannerCapture;
using aipdf::app::ScannerCaptureDialog;
using aipdf::app::ScannerCaptureOptions;
using aipdf::app::ScannerDevice;
using aipdf::app::scanErrorMessage;
using aipdf::pdf::ImageFormat;
using aipdf::pdf::ImageToPdfError;
using aipdf::pdf::ImageToPdfOptions;
using aipdf::pdf::ImageToPdfResult;
using aipdf::pdf::decodeImage;
using aipdf::pdf::describeImageFormat;
using aipdf::pdf::detectImageFormat;
using aipdf::pdf::imageToPdf;
using aipdf::pdf::imagesToPdf;

namespace {

// --------------------------------------------------------- 造测试用的字节 ----

void appendU16(QByteArray& out, quint16 value) {
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
}

void appendU32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 24) & 0xFF));
}

// 造一张 24 位 BMP。
//
// pixelsTopDown 是自上而下、每像素 3 字节 RGB 的原始像素；
// 造的时候翻成 BMP 的存储方式（自下而上 + BGR），
// 这样测试才能验出「解码后上下颠倒 / 红蓝互换」这类错误。
//
// 为什么自己造而不是用 QImage 存 BMP：测试要的是
// 「已知像素 → 已知字节 → 已知结果」这条链，
// 中间多一层 QImage 就多一个说不清的变量。
QByteArray make24BitBmp(int width, int height, const QByteArray& pixelsTopDown,
                        bool topDown = false) {
    const int stride = ((width * 24 + 31) / 32) * 4;
    QByteArray pixels;
    pixels.resize(stride * height);
    std::memset(pixels.data(), 0, static_cast<size_t>(pixels.size()));

    for (int row = 0; row < height; ++row) {
        // BMP 默认自下而上；负高度才是自上而下。
        const int sourceRow = topDown ? row : (height - 1 - row);
        const char* source = pixelsTopDown.constData() + sourceRow * width * 3;
        char* target = pixels.data() + row * stride;
        for (int x = 0; x < width; ++x) {
            target[x * 3 + 0] = source[x * 3 + 2];  // B
            target[x * 3 + 1] = source[x * 3 + 1];  // G
            target[x * 3 + 2] = source[x * 3 + 0];  // R
        }
    }

    QByteArray header;
    header.append("BM");
    const int pixelOffset = 14 + 40;
    appendU32(header, static_cast<quint32>(pixelOffset + pixels.size()));
    appendU16(header, 0);
    appendU16(header, 0);
    appendU32(header, static_cast<quint32>(pixelOffset));
    appendU32(header, 40);
    appendU32(header, static_cast<quint32>(width));
    appendU32(header, static_cast<quint32>(topDown ? -height : height));
    appendU16(header, 1);
    appendU16(header, 24);
    appendU32(header, 0);  // BI_RGB
    appendU32(header, static_cast<quint32>(pixels.size()));
    appendU32(header, 2835);
    appendU32(header, 2835);
    appendU32(header, 0);
    appendU32(header, 0);
    return header + pixels;
}

// 造一张 8 位调色板 BMP。
QByteArray make8BitPalettedBmp(int width, int height, const QByteArray& indicesTopDown,
                               const QVector<QByteArray>& palette) {
    const int stride = ((width * 8 + 31) / 32) * 4;
    QByteArray pixels;
    pixels.resize(stride * height);
    std::memset(pixels.data(), 0, static_cast<size_t>(pixels.size()));
    for (int row = 0; row < height; ++row) {
        const int sourceRow = height - 1 - row;
        const char* source = indicesTopDown.constData() + sourceRow * width;
        char* target = pixels.data() + row * stride;
        std::memcpy(target, source, static_cast<size_t>(width));
    }

    QByteArray header;
    header.append("BM");
    const int paletteOffset = 14 + 40;
    const int pixelOffset = paletteOffset + palette.size() * 4;
    appendU32(header, static_cast<quint32>(pixelOffset + pixels.size()));
    appendU16(header, 0);
    appendU16(header, 0);
    appendU32(header, static_cast<quint32>(pixelOffset));
    appendU32(header, 40);
    appendU32(header, static_cast<quint32>(width));
    appendU32(header, static_cast<quint32>(height));
    appendU16(header, 1);
    appendU16(header, 8);
    appendU32(header, 0);  // BI_RGB
    appendU32(header, static_cast<quint32>(pixels.size()));
    appendU32(header, 2835);
    appendU32(header, 2835);
    appendU32(header, static_cast<quint32>(palette.size()));
    appendU32(header, 0);

    for (const QByteArray& entry : palette) {
        // 调色板项是 BGR(A) 顺序。
        header.append(entry.at(2));
        header.append(entry.at(1));
        header.append(entry.at(0));
        header.append(static_cast<char>(0));
    }
    return header + pixels;
}

// 4×2 的已知像素：上面一行红绿蓝白，下面一行黑黄品青。
QByteArray knownPixels() {
    QByteArray pixels;
    const char rows[2][12] = {
        {static_cast<char>(255), 0, 0, 0, static_cast<char>(255), 0, 0, 0, static_cast<char>(255),
         static_cast<char>(255), static_cast<char>(255), static_cast<char>(255)},
        {0, 0, 0, static_cast<char>(255), static_cast<char>(255), 0, static_cast<char>(255), 0,
         static_cast<char>(255), 0, static_cast<char>(255), static_cast<char>(255)}};
    pixels.append(rows[0], 12);
    pixels.append(rows[1], 12);
    return pixels;
}

// 用 Qt 写一张 JPEG（真 JPEG，不是假头）。
QByteArray makeJpeg(int width, int height) {
    QImage image(width, height, QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixelColor(x, y, QColor((x * 17) % 256, (y * 23) % 256, 128));
        }
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {};
    }
    QImageWriter writer(&buffer, QByteArrayLiteral("JPEG"));
    writer.setQuality(90);
    if (!writer.write(image)) {
        return {};
    }
    buffer.close();
    return bytes;
}

// ------------------------------------------------- 造 PNG 用的工具 ----
//
// 为什么自己造 PNG 而不是用 QImageWriter 写一张：
// 部署包不一定带 Qt 的 PNG 图像插件（scripts/package.ps1 只保证 gif/ico/jpeg/svg），
// 夹具要是靠插件才造得出来，CI 上就会随机红。
// 像素一律用 PNG 的「原始扫描行」格式：每行前面一个过滤器字节，默认 0（None）。

// PNG 块的 CRC-32。
//
// 故意用**按位**版本，与解码器里的查表版本写法不同：
// 两边独立实现，测试才有资格发现解码器把 CRC 算错了
// （两边写成一模一样的话，一起错就永远测不出来）。
quint32 testCrc32(const QByteArray& data) {
    quint32 crc = 0xFFFFFFFFu;
    for (char ch : data) {
        crc ^= static_cast<quint8>(ch);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) != 0u ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void appendBE32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>((value >> 24) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

void appendPngChunk(QByteArray& png, const char* type, const QByteArray& data) {
    appendBE32(png, static_cast<quint32>(data.size()));
    const QByteArray typed = QByteArray(type, 4) + data;
    png.append(typed);
    appendBE32(png, testCrc32(typed));
}

// 造一张 PNG。
//
// idatPayload 非空时直接用它当 IDAT 的内容（用来造「压缩流被截断」这类坏图）；
// 否则把 rawRows 压成 zlib 流。qCompress 的输出前面 4 字节是长度头，
// 后面那段才是标准 zlib 流（带 0x78 头与 adler32），正好是 PNG 要的。
QByteArray makePng(int width, int height, int colorType, int bitDepth,
                   const QByteArray& rawRows, const QByteArray& palette = QByteArray(),
                   const QByteArray& transparency = QByteArray(),
                   const QByteArray& idatPayload = QByteArray()) {
    QByteArray png;
    png.append("\x89PNG\r\n\x1a\n", 8);
    QByteArray ihdr;
    appendBE32(ihdr, static_cast<quint32>(width));
    appendBE32(ihdr, static_cast<quint32>(height));
    ihdr.append(static_cast<char>(bitDepth));
    ihdr.append(static_cast<char>(colorType));
    ihdr.append(static_cast<char>(0));  // compression method
    ihdr.append(static_cast<char>(0));  // filter method
    ihdr.append(static_cast<char>(0));  // interlace
    appendPngChunk(png, "IHDR", ihdr);
    if (!palette.isEmpty()) {
        appendPngChunk(png, "PLTE", palette);
    }
    if (!transparency.isEmpty()) {
        appendPngChunk(png, "tRNS", transparency);
    }
    const QByteArray zlib =
        idatPayload.isEmpty() ? qCompress(rawRows).mid(4) : idatPayload;
    appendPngChunk(png, "IDAT", zlib);
    appendPngChunk(png, "IEND", QByteArray());
    return png;
}

// 一行「过滤器 0（None）」的原始扫描行。
QByteArray pngRow(const QByteArray& pixels) {
    QByteArray row;
    row.append(static_cast<char>(0));
    row.append(pixels);
    return row;
}

// 测试侧的 Paeth 预测器（与解码器里的那份各写各的，免得一起错）。
int testPaeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = qAbs(p - a);
    const int pb = qAbs(p - b);
    const int pc = qAbs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// 给一行原始像素套上过滤器，返回「过滤器字节 + 过滤后的数据」。
//
// 为什么测试要自己算过滤器：反滤波是最容易写错的一段，
// 测试得能精确地喂进去「用某种过滤器编码过的行」。
QByteArray pngFilteredRow(int filterType, const QByteArray& pixels, const QByteArray& previous,
                           int bytesPerPixel) {
    QByteArray out;
    out.append(static_cast<char>(filterType));
    for (int i = 0; i < pixels.size(); ++i) {
        const int x = static_cast<quint8>(pixels.at(i));
        const int a = i >= bytesPerPixel ? static_cast<quint8>(pixels.at(i - bytesPerPixel)) : 0;
        const int b = previous.isEmpty() ? 0 : static_cast<quint8>(previous.at(i));
        const int c =
            (previous.isEmpty() || i < bytesPerPixel) ? 0 : static_cast<quint8>(previous.at(i - bytesPerPixel));
        int value = x;
        switch (filterType) {
        case 1:
            value = x - a;
            break;
        case 2:
            value = x - b;
            break;
        case 3:
            value = x - ((a + b) / 2);
            break;
        case 4:
            value = x - testPaeth(a, b, c);
            break;
        default:
            break;
        }
        out.append(static_cast<char>(static_cast<quint8>(value)));
    }
    return out;
}
}  // namespace

class ImageToPdfTests final : public QObject {
    Q_OBJECT

private slots:
    void detectsImageFormat();
    void decodesBmpPixelsExactly();
    void decodesTopDownBmpPixelsExactly();
    void decodesPalettedBmp();
    void bmpBecomesReadablePdf();
    void jpegBecomesReadablePdf();
    void detectsPngFormat();
    void decodesGrayscalePng();
    void decodesRgbPng();
    void decodesRgbaPng();
    void decodesPalettedPng();
    void decodes16BitPng();
    void decodesPngWithRowFilters();
    void pngBecomesReadablePdf();
    void brokenPngFails();
    void tiffIsExplicitlyUnsupported();
    void multiPageKeepsOrder();
    void emptyInputFails();
    void garbageBytesFail();
    void truncatedImagesFail();
    void invalidDpiFails();
    void oversizedImageIsRejected();
    void scanErrorMessagesAreDistinct();
    void deviceEnumerationEitherWorksOrExplains();
    void deviceEnumerationIsStableAcrossRepeatedCalls();
    void captureWithUnknownDeviceFails();
    void scanToPdfFailsWithoutScanner();
    void dialogCancelIsReportedAsUserCancelled();
};

// ------------------------------------------------------------- 格式识别 ----

void ImageToPdfTests::detectsImageFormat() {
    QCOMPARE(detectImageFormat(make24BitBmp(2, 2, knownPixels())), ImageFormat::Bmp);
    QCOMPARE(detectImageFormat(makeJpeg(8, 8)), ImageFormat::Jpeg);

    // 负例：随手一段字节不能被当成图像。
    QCOMPARE(detectImageFormat(QByteArray("not an image at all")), ImageFormat::Unknown);
    QCOMPARE(detectImageFormat(QByteArray()), ImageFormat::Unknown);
}

// ------------------------------------------------------------- BMP 解码 ----

void ImageToPdfTests::decodesBmpPixelsExactly() {
    const QByteArray bmp = make24BitBmp(4, 2, knownPixels());
    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(bmp, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.width, 4);
    QCOMPARE(decoded.height, 2);
    // 解出来的必须是**自上而下**的 RGB：上下颠倒与红蓝互换都要能验出来。
    QCOMPARE(decoded.rgb, knownPixels());
}

void ImageToPdfTests::decodesTopDownBmpPixelsExactly() {
    const QByteArray bmp = make24BitBmp(4, 2, knownPixels(), /*topDown=*/true);
    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(bmp, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.width, 4);
    QCOMPARE(decoded.height, 2);
    QCOMPARE(decoded.rgb, knownPixels());
}

void ImageToPdfTests::decodesPalettedBmp() {
    // 4×1，调色板：0 = 红，1 = 蓝。
    QVector<QByteArray> palette;
    palette.append(QByteArray::fromHex("FF0000"));
    palette.append(QByteArray::fromHex("0000FF"));
    QByteArray indices;
    indices.append(static_cast<char>(1));
    indices.append(static_cast<char>(0));

    const QByteArray bmp = make8BitPalettedBmp(2, 1, indices, palette);
    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(bmp, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.width, 2);
    QCOMPARE(decoded.height, 1);
    QCOMPARE(decoded.rgb.size(), 6);
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(0)), static_cast<quint8>(0));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(1)), static_cast<quint8>(0));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(2)), static_cast<quint8>(255));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(3)), static_cast<quint8>(255));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(4)), static_cast<quint8>(0));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(5)), static_cast<quint8>(0));
}

// --------------------------------------------------------------- 转 PDF ----

void ImageToPdfTests::bmpBecomesReadablePdf() {
    ImageToPdfOptions options;
    options.dpi = 72.0;  // 72 DPI 时 1 像素 = 1 点
    const ImageToPdfResult result = imageToPdf(make24BitBmp(4, 2, knownPixels()), options);
    QVERIFY2(result.ok(), qPrintable(result.detail));
    QCOMPARE(result.pageCount, 1);
    QVERIFY(!result.pdf.isEmpty());

    // 读回验证：生成的 PDF 必须能被 PDFium 重新打开。
    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.pdf, QStringLiteral("from-bmp.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);

    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->widthPoints(), 4.0);
    QCOMPARE(page->heightPoints(), 2.0);

    // 渲染出来还得是那个颜色：BMP 走的是无损的 FlateDecode，颜色必须一模一样。
    const QImage rendered = page->render(1.0);
    QVERIFY(!rendered.isNull());
    QCOMPARE(rendered.width(), 4);
    QCOMPARE(rendered.height(), 2);
    QCOMPARE(qRed(rendered.pixel(0, 0)), 255);
    QCOMPARE(qGreen(rendered.pixel(0, 0)), 0);
    QCOMPARE(qBlue(rendered.pixel(0, 0)), 0);
    QCOMPARE(qRed(rendered.pixel(2, 0)), 0);
    QCOMPARE(qGreen(rendered.pixel(2, 0)), 0);
    QCOMPARE(qBlue(rendered.pixel(2, 0)), 255);
}

void ImageToPdfTests::jpegBecomesReadablePdf() {
    const QByteArray jpeg = makeJpeg(16, 12);
    QVERIFY(!jpeg.isEmpty());
    QCOMPARE(detectImageFormat(jpeg), ImageFormat::Jpeg);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(jpeg, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.width, 16);
    QCOMPARE(decoded.height, 12);
    QCOMPARE(decoded.jpegComponents, 3);

    // 72 DPI 时 1 像素 = 1 点，页面尺寸就等于像素尺寸，好核对。
    ImageToPdfOptions options;
    options.dpi = 72.0;
    const ImageToPdfResult result = imageToPdf(jpeg, options);
    QVERIFY2(result.ok(), qPrintable(result.detail));
    QCOMPARE(result.pageCount, 1);
    // JPEG 必须**原样**塞进 PDF（/DCTDecode），一个字节都不许重编码。
    QVERIFY2(result.pdf.contains(jpeg), "JPEG bytes were re-encoded instead of embedded as-is");
    QVERIFY(result.pdf.contains("/DCTDecode"));

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.pdf, QStringLiteral("from-jpeg.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->widthPoints(), 16.0);
    QCOMPARE(page->heightPoints(), 12.0);
    const QImage rendered = page->render(1.0);
    QVERIFY(!rendered.isNull());
    QCOMPARE(rendered.width(), 16);
    QCOMPARE(rendered.height(), 12);
}

// ------------------------------------------------------------- PNG 解码 ----

void ImageToPdfTests::detectsPngFormat() {
    const QByteArray png = makePng(2, 1, 0, 8, pngRow(QByteArray::fromHex("1020")));
    QCOMPARE(detectImageFormat(png), ImageFormat::Png);
    QCOMPARE(describeImageFormat(png), QStringLiteral("PNG"));

    // 负例：签名坏掉的字节不能被当成 PNG（否则会拿一段垃圾去解）。
    QByteArray brokenSignature = png;
    brokenSignature[1] = 'X';
    QCOMPARE(detectImageFormat(brokenSignature), ImageFormat::Unknown);
    QCOMPARE(detectImageFormat(QByteArray()), ImageFormat::Unknown);
}

void ImageToPdfTests::decodesGrayscalePng() {
    // 2×2 灰度：每行两个样本。
    QByteArray rows = pngRow(QByteArray::fromHex("0A14"));
    rows.append(pngRow(QByteArray::fromHex("C8FF")));
    const QByteArray png = makePng(2, 2, 0, 8, rows);
    QCOMPARE(detectImageFormat(png), ImageFormat::Png);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(png, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.width, 2);
    QCOMPARE(decoded.height, 2);
    QCOMPARE(decoded.rgb.size(), 12);
    // 灰度要三通道重复：错了就会变成「只有红通道」。
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(0)), static_cast<quint8>(0x0A));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(1)), static_cast<quint8>(0x0A));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(2)), static_cast<quint8>(0x0A));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(3)), static_cast<quint8>(0x14));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(4)), static_cast<quint8>(0x14));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(5)), static_cast<quint8>(0x14));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(6)), static_cast<quint8>(0xC8));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(7)), static_cast<quint8>(0xC8));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(8)), static_cast<quint8>(0xC8));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(9)), static_cast<quint8>(0xFF));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(11)), static_cast<quint8>(0xFF));
}

void ImageToPdfTests::decodesRgbPng() {
    // 2×1 RGB：红、蓝。
    QByteArray rows = pngRow(QByteArray::fromHex("FF00000000FF"));
    const QByteArray png = makePng(2, 1, 2, 8, rows);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(png, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.width, 2);
    QCOMPARE(decoded.height, 1);
    QCOMPARE(decoded.rgb.size(), 6);
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(0)), static_cast<quint8>(255));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(1)), static_cast<quint8>(0));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(2)), static_cast<quint8>(0));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(3)), static_cast<quint8>(0));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(4)), static_cast<quint8>(0));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(5)), static_cast<quint8>(255));
}

void ImageToPdfTests::decodesRgbaPng() {
    // 2×1 RGBA：不透明的红 + 全透明的蓝。
    QByteArray rows = pngRow(QByteArray::fromHex("FF0000FF0000FF00"));
    const QByteArray png = makePng(2, 1, 6, 8, rows);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(png, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.rgb.size(), 6);
    // 不透明的像素：颜色必须原样保留。
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(0)), static_cast<quint8>(255));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(1)), static_cast<quint8>(0));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(2)), static_cast<quint8>(0));
    // 全透明的像素：合成到白底上（PDF 页面本来就是白纸；
    // 直接丢 alpha 会把这里变成黑色）。
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(3)), static_cast<quint8>(255));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(4)), static_cast<quint8>(255));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(5)), static_cast<quint8>(255));
}

void ImageToPdfTests::decodesPalettedPng() {
    // 调色板：0 = 红、1 = 绿、2 = 蓝；tRNS 让 1 号（绿）全透明。
    const QByteArray palette = QByteArray::fromHex("FF000000FF000000FF");
    const QByteArray transparency = QByteArray::fromHex("FF00FF");
    const QByteArray rows = pngRow(QByteArray::fromHex("000102"));
    const QByteArray png = makePng(3, 1, 3, 8, rows, palette, transparency);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(png, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.width, 3);
    QCOMPARE(decoded.height, 1);
    QCOMPARE(decoded.rgb.size(), 9);
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(0)), static_cast<quint8>(255));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(1)), static_cast<quint8>(0));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(2)), static_cast<quint8>(0));
    // 透明的绿 → 白底
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(3)), static_cast<quint8>(255));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(4)), static_cast<quint8>(255));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(5)), static_cast<quint8>(255));
    // 蓝
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(6)), static_cast<quint8>(0));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(7)), static_cast<quint8>(0));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(8)), static_cast<quint8>(255));
}

void ImageToPdfTests::decodes16BitPng() {
    // 16 位灰度：0x1234 → 0x12，0xFFFF → 0xFF（取高字节）。
    QByteArray rows = pngRow(QByteArray::fromHex("1234FFFF"));
    const QByteArray gray = makePng(2, 1, 0, 16, rows);
    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(gray, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.rgb.size(), 6);
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(0)), static_cast<quint8>(0x12));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(1)), static_cast<quint8>(0x12));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(2)), static_cast<quint8>(0x12));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(5)), static_cast<quint8>(0xFF));

    // 16 位 RGB：0x1234 / 0xABCD / 0x00FF → 0x12 / 0xAB / 0x00。
    QByteArray rgbRows = pngRow(QByteArray::fromHex("1234ABCD00FF"));
    const QByteArray rgbPng = makePng(1, 1, 2, 16, rgbRows);
    QVERIFY2(decodeImage(rgbPng, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.rgb.size(), 3);
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(0)), static_cast<quint8>(0x12));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(1)), static_cast<quint8>(0xAB));
    QCOMPARE(static_cast<quint8>(decoded.rgb.at(2)), static_cast<quint8>(0x00));
}

void ImageToPdfTests::decodesPngWithRowFilters() {
    // 5 行，每行用一种不同的行过滤器（0/1/2/3/4）。
    // 反滤波那段是最容易写错的地方，必须真的把五种都走一遍。
    constexpr int kWidth = 3;
    constexpr int kHeight = 5;
    QByteArray rawRows;
    QByteArray expected;
    QByteArray previous;
    for (int row = 0; row < kHeight; ++row) {
        QByteArray pixels;
        for (int x = 0; x < kWidth; ++x) {
            pixels.append(static_cast<char>((row * 37 + x * 11) & 0xFF));
            pixels.append(static_cast<char>((row * 53 + x * 7) & 0xFF));
            pixels.append(static_cast<char>((row * 91 + x * 5) & 0xFF));
        }
        expected.append(pixels);
        rawRows.append(pngFilteredRow(row % 5, pixels, previous, 3));
        previous = pixels;
    }
    const QByteArray png = makePng(kWidth, kHeight, 2, 8, rawRows);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(png, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.width, kWidth);
    QCOMPARE(decoded.height, kHeight);
    QCOMPARE(decoded.rgb.size(), kWidth * kHeight * 3);
    QCOMPARE(decoded.rgb, expected);
}

void ImageToPdfTests::pngBecomesReadablePdf() {
    const QByteArray rows = pngRow(QByteArray::fromHex("FF00000000FF"));
    const QByteArray png = makePng(2, 1, 2, 8, rows);

    ImageToPdfOptions options;
    options.dpi = 72.0;
    const ImageToPdfResult result = imageToPdf(png, options);
    QVERIFY2(result.ok(), qPrintable(result.detail));
    QCOMPARE(result.pageCount, 1);
    // PNG 走 /FlateDecode + /DeviceRGB（与 BMP 同一条路）。
    QVERIFY(result.pdf.contains("/FlateDecode"));
    QVERIFY(result.pdf.contains("/DeviceRGB"));
    QVERIFY(!result.pdf.contains("/DCTDecode"));

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.pdf, QStringLiteral("from-png.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->widthPoints(), 2.0);
    QCOMPARE(page->heightPoints(), 1.0);

    // 渲染出来的颜色必须与原图一致（FlateDecode 是无损的）。
    const QImage rendered = page->render(1.0);
    QVERIFY(!rendered.isNull());
    QCOMPARE(rendered.width(), 2);
    QCOMPARE(rendered.height(), 1);
    QCOMPARE(qRed(rendered.pixel(0, 0)), 255);
    QCOMPARE(qGreen(rendered.pixel(0, 0)), 0);
    QCOMPARE(qBlue(rendered.pixel(0, 0)), 0);
    QCOMPARE(qRed(rendered.pixel(1, 0)), 0);
    QCOMPARE(qGreen(rendered.pixel(1, 0)), 0);
    QCOMPARE(qBlue(rendered.pixel(1, 0)), 255);
}

void ImageToPdfTests::brokenPngFails() {
    const QByteArray rows = pngRow(QByteArray::fromHex("1020"));
    const QByteArray good = makePng(2, 1, 0, 8, rows);

    // 负例 1：签名坏掉 → 连格式都认不出来（不是「解不开」，是「不是 PNG」）。
    QByteArray brokenSignature = good;
    brokenSignature[0] = 'X';
    QCOMPARE(detectImageFormat(brokenSignature), ImageFormat::Unknown);
    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY(!decodeImage(brokenSignature, &decoded, &detail));
    QVERIFY(!detail.isEmpty());

    // 负例 2：文件被砍掉一截 → 块长度对不上，必须失败而不是崩溃。
    QByteArray truncated = good;
    truncated.chop(12);
    QVERIFY(!decodeImage(truncated, &decoded, &detail));
    QVERIFY(detail.contains(QStringLiteral("PNG")));

    // 负例 3：IHDR 说宽 0 → 尺寸非法。
    const QByteArray zeroWidth = makePng(0, 1, 0, 8, rows);
    QVERIFY(!decodeImage(zeroWidth, &decoded, &detail));
    QVERIFY(detail.contains(QStringLiteral("PNG")));

    // 负例 4：IHDR 里的字节被改过 → CRC 对不上。
    QByteArray badCrc = good;
    badCrc[20] = static_cast<char>(badCrc.at(20) ^ 0xFF);
    QVERIFY(!decodeImage(badCrc, &decoded, &detail));
    QVERIFY(detail.contains(QStringLiteral("CRC")));

    // 负例 5：zlib 流本身被截断（块的 CRC 是对的，只是数据少了一半）。
    const QByteArray zlib = qCompress(rows).mid(4);
    const QByteArray halfStream = makePng(2, 1, 0, 8, rows, QByteArray(), QByteArray(),
                                           zlib.left(zlib.size() / 2));
    QVERIFY(!decodeImage(halfStream, &decoded, &detail));
    QVERIFY(detail.contains(QStringLiteral("PNG")));

    // 负例 6：走整条链时，坏 PNG 必须报「数据坏了」，不能报「格式不支持」
    // ——前者多半是文件传坏了，后者是格式根本不对，这两件事要分开。
    const ImageToPdfResult bad = imagesToPdf({truncated});
    QCOMPARE(bad.error, ImageToPdfError::DecodeFailed);
    QVERIFY(bad.pdf.isEmpty());
    QCOMPARE(bad.failedIndex, 0);
    QVERIFY(bad.detail.contains(QStringLiteral("PNG")));
}
void ImageToPdfTests::tiffIsExplicitlyUnsupported() {
    // 负例：TIFF 明确不支持，而且必须**点名 TIFF**——
    // 只写「格式不支持」等于让用户自己猜是设备设置的问题还是程序的问题。
    const QByteArray tiff = QByteArray::fromHex("49492A0000000000");
    QCOMPARE(detectImageFormat(tiff), ImageFormat::Unknown);
    QCOMPARE(describeImageFormat(tiff), QStringLiteral("TIFF"));

    const ImageToPdfResult result = imagesToPdf({tiff});
    QCOMPARE(result.error, ImageToPdfError::UnsupportedFormat);
    QCOMPARE(result.failedIndex, 0);
    QVERIFY(result.pdf.isEmpty());
    QVERIFY2(result.detail.contains(QStringLiteral("TIFF")), qPrintable(result.detail));
}

void ImageToPdfTests::multiPageKeepsOrder() {
    // 三张宽度不同的图：页序必须按给的顺序，一页一张。
    QVector<QByteArray> images;
    images.append(make24BitBmp(2, 1, QByteArray::fromHex("FF0000")));
    images.append(make24BitBmp(4, 1, QByteArray::fromHex("00FF00")));
    images.append(make24BitBmp(6, 1, QByteArray::fromHex("0000FF")));

    ImageToPdfOptions options;
    options.dpi = 72.0;
    const ImageToPdfResult result = imagesToPdf(images, options);
    QVERIFY2(result.ok(), qPrintable(result.detail));
    QCOMPARE(result.pageCount, 3);

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.pdf, QStringLiteral("multi.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 3);

    const QVector<double> expected{2.0, 4.0, 6.0};
    for (int i = 0; i < 3; ++i) {
        const auto page = opened.document.page(i);
        QVERIFY(page.has_value());
        QCOMPARE(page->widthPoints(), expected.at(i));
    }
}

// ------------------------------------------------------------- 负例 ----

void ImageToPdfTests::emptyInputFails() {
    // 负例：一张图都没有 → 必须明确失败，不能生成一份空 PDF。
    const ImageToPdfResult result = imagesToPdf({});
    QCOMPARE(result.error, ImageToPdfError::EmptyInput);
    QVERIFY(result.pdf.isEmpty());
    QCOMPARE(result.pageCount, 0);
    QVERIFY(!result.detail.isEmpty());

    // 负例：单张空字节 → 也是明确失败（不是「没读到东西就当成功」）。
    const ImageToPdfResult single = imageToPdf(QByteArray());
    QCOMPARE(single.error, ImageToPdfError::EmptyInput);
    QVERIFY(single.pdf.isEmpty());
    QVERIFY(!single.detail.isEmpty());
}

void ImageToPdfTests::garbageBytesFail() {
    // 负例：不是图像的字节 → 报「格式不支持」，并且点名是什么格式。
    const ImageToPdfResult result = imagesToPdf({QByteArray("this is definitely not an image")});
    QCOMPARE(result.error, ImageToPdfError::UnsupportedFormat);
    QVERIFY(result.pdf.isEmpty());
    QCOMPARE(result.failedIndex, 0);
    QVERIFY(result.detail.contains(QStringLiteral("unsupported image format")));

    // 负例：PNG 现在是支持的格式，但这串字节只是一张 PNG 的头（后面的块全没了），
    // 所以报的是「数据坏了」（DecodeFailed），不是「格式不支持」——
    // 这两件事要分开：前者多半是文件传坏了，后者是格式根本不对。
    const QByteArray pngHeader = QByteArray::fromHex("89504E470D0A1A0A0000000D49484452");
    const ImageToPdfResult png = imagesToPdf({pngHeader});
    QCOMPARE(png.error, ImageToPdfError::DecodeFailed);
    QVERIFY(png.detail.contains(QStringLiteral("PNG")));
}

void ImageToPdfTests::truncatedImagesFail() {
    // 负例：BMP 头说 4×2，但像素数据被砍了 → 必须失败，不能按头里的尺寸瞎解。
    QByteArray bmp = make24BitBmp(4, 2, knownPixels());
    bmp.chop(20);
    const ImageToPdfResult bmpResult = imagesToPdf({bmp});
    QCOMPARE(bmpResult.error, ImageToPdfError::DecodeFailed);
    QVERIFY(bmpResult.pdf.isEmpty());
    QVERIFY(!bmpResult.detail.isEmpty());

    // 负例：JPEG 头被砍断 → 同样必须失败。
    QByteArray jpeg = makeJpeg(16, 12);
    jpeg.chop(jpeg.size() / 2);
    const ImageToPdfResult jpegResult = imagesToPdf({jpeg});
    QCOMPARE(jpegResult.error, ImageToPdfError::DecodeFailed);
    QVERIFY(jpegResult.pdf.isEmpty());

    // 负例：多张图里第 2 张坏了 → 要指出是哪一张，不能默默跳过。
    QVector<QByteArray> mixed;
    mixed.append(make24BitBmp(2, 1, QByteArray::fromHex("FF0000")));
    mixed.append(QByteArray("broken"));
    mixed.append(make24BitBmp(2, 1, QByteArray::fromHex("00FF00")));
    const ImageToPdfResult mixedResult = imagesToPdf(mixed);
    QCOMPARE(mixedResult.error, ImageToPdfError::UnsupportedFormat);
    QCOMPARE(mixedResult.failedIndex, 1);
    QVERIFY(mixedResult.pdf.isEmpty());
}

void ImageToPdfTests::invalidDpiFails() {
    const QByteArray bmp = make24BitBmp(4, 2, knownPixels());
    ImageToPdfOptions options;
    options.dpi = 0.0;
    QCOMPARE(imageToPdf(bmp, options).error, ImageToPdfError::InvalidPageSize);
    options.dpi = -300.0;
    QCOMPARE(imageToPdf(bmp, options).error, ImageToPdfError::InvalidPageSize);
    options.dpi = 1e9;
    QCOMPARE(imageToPdf(bmp, options).error, ImageToPdfError::InvalidPageSize);
}

void ImageToPdfTests::oversizedImageIsRejected() {
    // 负例：头里写一个 100000×100000 的图，但文件里没有那么多像素。
    // 这样的头不能被当成真图（否则会申请几个 GB 内存）。
    QByteArray bmp;
    bmp.append("BM");
    appendU32(bmp, 54);
    appendU16(bmp, 0);
    appendU16(bmp, 0);
    appendU32(bmp, 54);
    appendU32(bmp, 40);
    appendU32(bmp, 100000);
    appendU32(bmp, 100000);
    appendU16(bmp, 1);
    appendU16(bmp, 24);
    appendU32(bmp, 0);
    appendU32(bmp, 0);
    appendU32(bmp, 0);
    appendU32(bmp, 0);
    appendU32(bmp, 0);
    appendU32(bmp, 0);

    const ImageToPdfResult result = imagesToPdf({bmp});
    QVERIFY(!result.ok());
    QCOMPARE(result.error, ImageToPdfError::DecodeFailed);
    QVERIFY(result.pdf.isEmpty());
    QVERIFY(!result.detail.isEmpty());
}

// ---------------------------------------------------------- 应用层 ----

void ImageToPdfTests::scanErrorMessagesAreDistinct() {
    const QVector<ScanErrorKind> kinds{
        ScanErrorKind::None,          ScanErrorKind::EnumerationFailed,
        ScanErrorKind::NoDevice,      ScanErrorKind::UserCancelled,
        ScanErrorKind::TransferFailed, ScanErrorKind::UnsupportedImage,
        ScanErrorKind::PdfFailed};

    QVector<QString> messages;
    for (ScanErrorKind kind : kinds) {
        const QString message = scanErrorMessage(kind);
        QVERIFY(!message.isEmpty());
        messages.append(message);
    }
    // 每一种失败都要有不同的说法：全都叫「扫描失败」等于没分类。
    for (int i = 0; i < messages.size(); ++i) {
        for (int j = i + 1; j < messages.size(); ++j) {
            QVERIFY2(messages.at(i) != messages.at(j),
                     qPrintable(QStringLiteral("duplicate message: %1").arg(messages.at(i))));
        }
    }
    // 带技术细节时要把细节带上。
    QVERIFY(scanErrorMessage(ScanErrorKind::NoDevice, QStringLiteral("0 devices")).contains(
        QStringLiteral("0 devices")));
}

void ImageToPdfTests::deviceEnumerationEitherWorksOrExplains() {
    QString error;
    const QVector<ScannerDevice> devices = ScannerCapture().listDevices(&error);
    if (devices.isEmpty()) {
        // 本机没有扫描仪：必须给出**能看懂**的原因，不能是空的。
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(QStringLiteral("扫描仪")) || error.contains(QStringLiteral("WIA")));
        return;
    }
    // 有设备：每一台都得有能回头找到它的 ID。
    for (const ScannerDevice& device : devices) {
        QVERIFY(!device.deviceId.isEmpty());
        QVERIFY(!device.displayName().isEmpty());
    }
}

void ImageToPdfTests::deviceEnumerationIsStableAcrossRepeatedCalls() {
    // 为什么必须重复 30 次：枚举偶发段错误的单次崩溃率只有约 27%
    // （实测修复前连跑 40 次崩 11 次），跑一次抓不住这种偶发问题；
    // 连着 30 次在旧实现下几乎必崩，这条用例才有守门价值。
    //
    // 断言的不是「设备数量」（设备本来就可能增减），而是：
    //   1. 每次调用都活着回来（不崩）；
    //   2. 每次都给出说得清的结论——要么设备列表合理，要么原因非空且能看懂。
    constexpr int kCalls = 30;
    for (int i = 0; i < kCalls; ++i) {
        QString error;
        const QVector<ScannerDevice> devices = ScannerCapture().listDevices(&error);
        if (devices.isEmpty()) {
            QVERIFY2(!error.isEmpty(),
                     qPrintable(QStringLiteral("call %1 returned no device and no reason")
                                    .arg(i + 1)));
            QVERIFY2(error.contains(QStringLiteral("扫描仪")) ||
                         error.contains(QStringLiteral("WIA")),
                     qPrintable(QStringLiteral("call %1 gave an unclear reason: %2")
                                    .arg(i + 1)
                                    .arg(error)));
            continue;
        }
        for (const ScannerDevice& device : devices) {
            QVERIFY2(!device.deviceId.isEmpty(),
                     qPrintable(QStringLiteral("call %1 returned a device without id").arg(i + 1)));
            QVERIFY2(!device.displayName().isEmpty(),
                     qPrintable(QStringLiteral("call %1 returned a device without name").arg(i + 1)));
        }
    }
}

void ImageToPdfTests::captureWithUnknownDeviceFails() {
    ScannerDevice fake;
    fake.deviceId = QStringLiteral("\\?\\usb#fake-device-that-does-not-exist");
    fake.name = QStringLiteral("fake");
    fake.deviceType = 1;

    ScannerCaptureOptions options;
    options.dpi = 300;
    const ScanOutcome outcome = ScannerCapture().capture(fake, options);
    // 负例：不存在的设备必须明确失败，不能静默返回「成功但没有图」。
    QVERIFY(!outcome.ok);
    QVERIFY(outcome.frames.isEmpty());
    QVERIFY(outcome.errorKind == ScanErrorKind::NoDevice ||
            outcome.errorKind == ScanErrorKind::TransferFailed);
    QVERIFY(!outcome.errorMessage.isEmpty());
}

void ImageToPdfTests::scanToPdfFailsWithoutScanner() {
    QByteArray pdf;
    ScannerCaptureOptions options;
    options.dpi = 300;
    const ScanOutcome outcome = ScannerCapture().scanToPdf(options, &pdf);
    if (outcome.ok) {
        // 本机真有扫描仪：产出的必须是能打开的多页 PDF。
        QVERIFY(!pdf.isEmpty());
        const aipdf::pdf::PdfOpenResult opened =
            aipdf::pdf::openDocumentFromBytes(pdf, QStringLiteral("scan.pdf"));
        QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
        QCOMPARE(opened.document.pageCount(), outcome.pageCount());
        return;
    }
    // 本机没有扫描仪：必须明确失败并给出原因，PDF 也不能是半成品。
    QVERIFY(pdf.isEmpty());
    QVERIFY(outcome.errorKind != ScanErrorKind::None);
    QVERIFY(!outcome.errorMessage.isEmpty());
}

void ImageToPdfTests::dialogCancelIsReportedAsUserCancelled() {
    ScannerCaptureDialog dialog;
    QVERIFY(dialog.pageCount() == 0);
    QVERIFY(dialog.pdfBytes().isEmpty());
    dialog.reject();
    // 负例：用户自己关掉窗口 → 原因必须是「用户取消」，不能当成扫描失败。
    QCOMPARE(dialog.cancelReason(), ScanErrorKind::UserCancelled);
}

QTEST_MAIN(ImageToPdfTests)
#include "ImageToPdfTests.moc"
