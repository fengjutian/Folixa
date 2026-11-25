#include "app/controllers/ScannerCapture.h"
#include "pdf/convert/ImageToPdf.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QHash>
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
// ---------------------------------------------------------------- TIFF ----
//
// 造 TIFF 的字节：结构与真实文件一致（头 → IFD → 条数据 → 外置数组），
// 这样测试验的是「按 TIFF 规范解」，不是「按我自己写的格式解」。

// TIFF 的字节序。
enum class TiffOrder { Little, Big };

void appendTiff16(QByteArray& out, quint16 value, TiffOrder order) {
    if (order == TiffOrder::Little) {
        out.append(static_cast<char>(value & 0xFF));
        out.append(static_cast<char>((value >> 8) & 0xFF));
    } else {
        out.append(static_cast<char>((value >> 8) & 0xFF));
        out.append(static_cast<char>(value & 0xFF));
    }
}

void appendTiff32(QByteArray& out, quint32 value, TiffOrder order) {
    if (order == TiffOrder::Little) {
        for (int index = 0; index < 4; ++index) {
            out.append(static_cast<char>((value >> (8 * index)) & 0xFF));
        }
    } else {
        for (int index = 3; index >= 0; --index) {
            out.append(static_cast<char>((value >> (8 * index)) & 0xFF));
        }
    }
}

// TIFF 的字段类型：3 = SHORT，4 = LONG。
constexpr quint16 kTiffTypeShort = 3;
constexpr quint16 kTiffTypeLong = 4;

int tiffTypeSize(quint16 type) {
    return type == kTiffTypeShort ? 2 : 4;
}

// 一条要写进 IFD 的标签。
struct TiffTag {
    quint16 tag = 0;
    quint16 type = 0;
    QVector<quint32> values;
};

// PackBits 编码（TIFF / Apple 的 RLE）。
QByteArray packBitsEncode(const QByteArray& raw) {
    QByteArray out;
    int index = 0;
    while (index < raw.size()) {
        int runLength = 1;
        while (index + runLength < raw.size() && runLength < 128 &&
               raw.at(index + runLength) == raw.at(index)) {
            ++runLength;
        }
        if (runLength >= 3) {
            out.append(static_cast<char>(static_cast<signed char>(1 - runLength)));
            out.append(raw.at(index));
            index += runLength;
            continue;
        }
        int literal = 0;
        while (index + literal < raw.size() && literal < 128) {
            if (index + literal + 2 < raw.size() &&
                raw.at(index + literal) == raw.at(index + literal + 1) &&
                raw.at(index + literal) == raw.at(index + literal + 2)) {
                break;
            }
            ++literal;
        }
        out.append(static_cast<char>(static_cast<signed char>(literal - 1)));
        out.append(raw.constData() + index, literal);
        index += literal;
    }
    return out;
}

// TIFF 的 LZW 编码（高位在前、early change）。
//
// 与解码器同一套规则：码宽从 9 位起，清码 256、结束码 257，
// 新码从 258 开始；下一个要分配的码是 512 / 1024 / 2048 时码宽加一。
// （这条门槛是拿 libtiff 写的真实文件对拍定下来的，见
//   scripts/generate-tiff-fixtures.py 与 tests/fixtures/tiff/。）
QByteArray tiffLzwEncode(const QByteArray& raw) {
    QByteArray out;
    int bitBuffer = 0;
    int bitCount = 0;
    int codeWidth = 9;
    // 名字不能叫 emit：那是 Qt 的关键字（展开成空），一叫它这行就编不过。
    const auto writeCode = [&](int code) {
        bitBuffer = (bitBuffer << codeWidth) | code;
        bitCount += codeWidth;
        while (bitCount >= 8) {
            bitCount -= 8;
            out.append(static_cast<char>((bitBuffer >> bitCount) & 0xFF));
            bitBuffer &= (1 << bitCount) - 1;
        }
    };

    QHash<QString, int> dict;
    int nextCode = 258;
    writeCode(256);

    QByteArray prefix;
    for (int index = 0; index < raw.size(); ++index) {
        const char symbol = raw.at(index);
        if (prefix.isEmpty()) {
            prefix = QByteArray(1, symbol);
            continue;
        }
        const QByteArray candidate = prefix + symbol;
        const QString key = QString::fromLatin1(candidate);
        if (dict.contains(key)) {
            prefix = candidate;
            continue;
        }
        writeCode(dict.value(QString::fromLatin1(prefix),
                             static_cast<int>(static_cast<quint8>(prefix.at(0)))));
        dict.insert(key, nextCode);
        ++nextCode;
        if (nextCode > 4094) {
            // 表满了（码用到 4095 就到顶）：按 TIFF 规范发一个清码，
            // 把表和码宽重置，重新开始。
            //
            // 不做这一步会怎样：码号超过 4095 就没有码宽可用了，
            // 解码器读到越界的码会直接报「码流非法」——
            // 也就是说「图够大」就会触发，所以这条不能省。
            writeCode(256);
            dict.clear();
            nextCode = 258;
            codeWidth = 9;
        } else if (nextCode >= (1 << codeWidth) && codeWidth < 12) {
            ++codeWidth;
        }
        prefix = QByteArray(1, symbol);
    }
    if (!prefix.isEmpty()) {
        writeCode(dict.value(QString::fromLatin1(prefix),
                             static_cast<int>(static_cast<quint8>(prefix.at(0)))));
    }
    writeCode(257);
    if (bitCount > 0) {
        out.append(static_cast<char>((bitBuffer << (8 - bitCount)) & 0xFF));
    }
    return out;
}

// 造一个 TIFF 文件。
//
// 为什么自己造：测试要的是「已知像素 → 已知字节 → 已知结果」这条链；
// 用现成的库存一张图，中间就多了一层说不清的变量。
// （TIFF 的真实样本另有一道交叉核对：见 docs/VERSION_AUDIT.md 的本轮记录。）
struct TiffSpec {
    TiffOrder order = TiffOrder::Little;
    int width = 0;
    int height = 0;
    int samplesPerPixel = 1;
    int bitsPerSample = 8;
    // 光度解释：0 = 白为最小，1 = 黑为最小，2 = RGB，3 = 调色板。
    int photometric = 1;
    // 压缩：1 = 未压缩，5 = LZW，8 = Deflate，32773 = PackBits。
    int compression = 1;
    // 每条 strip 几行（0 = 整个图像一条）。
    int rowsPerStrip = 0;
    // 每页的原始像素（未压缩，按行，行内按 TIFF 的取样顺序）。
    QVector<QByteArray> pages;
    // 调色板分量（16 位，先红后绿后蓝）。
    QVector<quint32> colorMap;
};

QByteArray compressTiffStrip(const QByteArray& raw, int compression) {
    switch (compression) {
    case 1:
        return raw;
    case 32773:
        return packBitsEncode(raw);
    case 8: {
        // qCompress 产出的是「4 字节长度 + zlib 流」这个 Qt 自家格式；
        // TIFF 要的是**不带长度头**的 zlib 流，所以把头切掉。
        const QByteArray compressed = qCompress(raw);
        return compressed.size() > 4 ? compressed.mid(4) : compressed;
    }
    case 5:
        return tiffLzwEncode(raw);
    default:
        return raw;
    }
}

QByteArray makeTiff(const TiffSpec& spec) {
    const TiffOrder order = spec.order;
    QByteArray out;
    out.append(order == TiffOrder::Little ? "II" : "MM");
    appendTiff16(out, 42, order);
    const qint64 firstIfdField = out.size();
    appendTiff32(out, 0, order);

    for (int pageIndex = 0; pageIndex < spec.pages.size(); ++pageIndex) {
        const QByteArray& raw = spec.pages.at(pageIndex);
        const int rowsPerStrip = spec.rowsPerStrip > 0 ? spec.rowsPerStrip : spec.height;
        const qint64 rowBytes = (static_cast<qint64>(spec.width) * spec.samplesPerPixel *
                                 spec.bitsPerSample + 7) / 8;

        QVector<QByteArray> strips;
        for (int row = 0; row < spec.height; row += rowsPerStrip) {
            const int rows = qMin(rowsPerStrip, spec.height - row);
            strips.append(compressTiffStrip(
                raw.mid(static_cast<qsizetype>(row * rowBytes),
                        static_cast<qsizetype>(rows * rowBytes)),
                spec.compression));
        }

        QVector<TiffTag> tags;
        tags.append({256, kTiffTypeLong, {static_cast<quint32>(spec.width)}});
        tags.append({257, kTiffTypeLong, {static_cast<quint32>(spec.height)}});
        tags.append({258, kTiffTypeShort,
                     QVector<quint32>(spec.samplesPerPixel,
                                      static_cast<quint32>(spec.bitsPerSample))});
        tags.append({259, kTiffTypeShort, {static_cast<quint32>(spec.compression)}});
        tags.append({262, kTiffTypeShort, {static_cast<quint32>(spec.photometric)}});
        tags.append({273, kTiffTypeLong, {}});  // StripOffsets（稍后回填）
        tags.append({277, kTiffTypeShort, {static_cast<quint32>(spec.samplesPerPixel)}});
        tags.append({278, kTiffTypeLong, {static_cast<quint32>(rowsPerStrip)}});
        tags.append({279, kTiffTypeLong, {}});  // StripByteCounts（稍后回填）
        tags.append({284, kTiffTypeShort, {1}});
        if (!spec.colorMap.isEmpty()) {
            tags.append({320, kTiffTypeShort, spec.colorMap});
        }

        // 布局：IFD 之后先放条数据，再放各标签的外置数组。
        const qint64 ifdOffset = out.size();
        const qint64 ifdSize = 2 + static_cast<qint64>(tags.size()) * 12 + 4;
        qint64 cursor = ifdOffset + ifdSize;
        QVector<quint32> stripOffsets;
        QVector<quint32> stripByteCounts;
        for (const QByteArray& strip : strips) {
            stripOffsets.append(static_cast<quint32>(cursor));
            stripByteCounts.append(static_cast<quint32>(strip.size()));
            cursor += strip.size();
        }
        for (TiffTag& tag : tags) {
            if (tag.tag == 273) {
                tag.values = stripOffsets;
            } else if (tag.tag == 279) {
                tag.values = stripByteCounts;
            }
        }
        QHash<quint16, qint64> dataOffset;
        for (const TiffTag& tag : tags) {
            const qint64 total = static_cast<qint64>(tiffTypeSize(tag.type)) * tag.values.size();
            if (total > 4) {
                dataOffset.insert(tag.tag, cursor);
                cursor += total;
            }
        }
        const qint64 nextIfd = pageIndex + 1 < spec.pages.size() ? cursor : 0;

        // 写 IFD。
        appendTiff16(out, static_cast<quint16>(tags.size()), order);
        for (const TiffTag& tag : tags) {
            appendTiff16(out, tag.tag, order);
            appendTiff16(out, tag.type, order);
            appendTiff32(out, static_cast<quint32>(tag.values.size()), order);
            const qint64 total = static_cast<qint64>(tiffTypeSize(tag.type)) * tag.values.size();
            if (total <= 4) {
                // 内联：值**左对齐**装在 4 字节的 value 字段里，
                // 而且要按**文件的字节序**排（TIFF 6.0 规定）。
                QByteArray field(4, '\0');
                const quint32 value = tag.values.isEmpty() ? 0 : tag.values.first();
                if (order == TiffOrder::Little) {
                    field[0] = static_cast<char>(value & 0xFF);
                    field[1] = static_cast<char>((value >> 8) & 0xFF);
                    if (tiffTypeSize(tag.type) == 4) {
                        field[2] = static_cast<char>((value >> 16) & 0xFF);
                        field[3] = static_cast<char>((value >> 24) & 0xFF);
                    }
                } else {
                    field[0] = static_cast<char>((value >> 8) & 0xFF);
                    field[1] = static_cast<char>(value & 0xFF);
                    if (tiffTypeSize(tag.type) == 4) {
                        field[0] = static_cast<char>((value >> 24) & 0xFF);
                        field[1] = static_cast<char>((value >> 16) & 0xFF);
                        field[2] = static_cast<char>((value >> 8) & 0xFF);
                        field[3] = static_cast<char>(value & 0xFF);
                    }
                }
                out.append(field);
            } else {
                appendTiff32(out, static_cast<quint32>(dataOffset.value(tag.tag)), order);
            }
        }
        appendTiff32(out, static_cast<quint32>(nextIfd), order);

        // 写数据区：条数据在前，外置数组按标签顺序在后。
        for (const QByteArray& strip : strips) {
            out.append(strip);
        }
        for (const TiffTag& tag : tags) {
            const qint64 total = static_cast<qint64>(tiffTypeSize(tag.type)) * tag.values.size();
            if (total <= 4) {
                continue;
            }
            for (const quint32 value : tag.values) {
                if (tiffTypeSize(tag.type) == 2) {
                    appendTiff16(out, static_cast<quint16>(value), order);
                } else {
                    appendTiff32(out, value, order);
                }
            }
        }
    }
    // 回填第一个 IFD 的位置（偏移字段同样是按文件字节序写的）。
    const quint32 firstIfd = static_cast<quint32>(8);
    if (order == TiffOrder::Little) {
        for (int index = 0; index < 4; ++index) {
            out[static_cast<qsizetype>(firstIfdField + index)] =
                static_cast<char>((firstIfd >> (8 * index)) & 0xFF);
        }
    } else {
        for (int index = 0; index < 4; ++index) {
            out[static_cast<qsizetype>(firstIfdField + index)] =
                static_cast<char>((firstIfd >> (8 * (3 - index))) & 0xFF);
        }
    }
    return out;
}

// 造一份默认规格：w×h 的 8 位灰度（黑为最小），像素全 0。
TiffSpec tiffSpec(int width, int height) {
    TiffSpec spec;
    spec.width = width;
    spec.height = height;
    spec.pages.append(QByteArray(width * height, '\0'));
    return spec;
}

// 就地改一个 IFD 条目的内联 SHORT 值（负例用）。
//
// 条目顺序与 makeTiff 里写的顺序一致：
// 0 宽度 / 1 高度 / 2 位深 / 3 压缩 / 4 光度 / 5 条偏移 / 6 通道数 /
// 7 每条行数 / 8 条字节数 / 9 平面配置 / 10 调色板。
void patchTiffInlineShort(QByteArray& tiff, int entryIndex, quint16 value) {
    const qint64 entryOffset = 8 + 2 + static_cast<qint64>(entryIndex) * 12;
    tiff[static_cast<qsizetype>(entryOffset + 8)] = static_cast<char>(value & 0xFF);
    tiff[static_cast<qsizetype>(entryOffset + 9)] = static_cast<char>((value >> 8) & 0xFF);
}

// ---------------------------------------------------------------- 真实夹具 ----
//
// 上面那些用例是「自己造字节、自己解」：写字节的人和读字节的人是同一个，
// 两边一起错也照样全绿。这批夹具由**独立实现**（libtiff）写出，
// 解码器读不懂真实世界的文件时，这里会红。
//
// 夹具是签进仓库的（tests/fixtures/tiff/），构建与测试都不需要 Pillow；
// 需要重新生成：python scripts/generate-tiff-fixtures.py tests/fixtures/tiff

// 没定义夹具目录时（比如把这个文件单独编到别处）用例会跳过，
// 而不是编不过。
#ifndef AIPDF_TIFF_FIXTURES_DIR
#define AIPDF_TIFF_FIXTURES_DIR ""
#endif

// FNV-1a 32 位：拿整幅图的像素算一个校验值。
//
// 为什么用校验值而不是逐像素比对：整幅图有几千个像素，
// 逐个写进断言里既冗长又难读；一个校验值能把「任何一个像素错了」
// 变成一次失败，同时断言本身只有一行。
quint32 fnv1a32(const QByteArray& data) {
    quint32 hash = 0x811c9dc5u;
    for (const char byte : data) {
        hash ^= static_cast<quint8>(byte);
        hash *= 0x01000193u;
    }
    return hash;
}

QByteArray readFixture(const QString& name) {
    QFile file(QDir(QStringLiteral(AIPDF_TIFF_FIXTURES_DIR)).filePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
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
    // —— TIFF（自研解码器：不依赖 Qt 图像插件）——
    void detectsTiffFormat();
    void decodesGrayscaleTiff();
    void decodesWhiteIsZeroTiff();
    void decodesRgbTiff();
    void decodesBigEndianTiff();
    void decodesPalettedTiff();
    void decodesOneBitTiff();
    void decodes16BitTiff();
    void decodesCompressedTiff();
    void decodesMultiPageTiff();
    void tiffBecomesReadablePdf();
    void brokenTiffFails();
    void decodesLzwAcrossCodeWidthChange();
    void decodesRealWorldTiffFixtures();
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
// ---------------------------------------------------------------- TIFF ----

void ImageToPdfTests::detectsTiffFormat() {
    const QByteArray little = makeTiff(tiffSpec(2, 1));
    QCOMPARE(detectImageFormat(little), ImageFormat::Tiff);
    QCOMPARE(describeImageFormat(little), QStringLiteral("TIFF"));

    TiffSpec bigEndianSpec = tiffSpec(2, 1);
    bigEndianSpec.order = TiffOrder::Big;
    const QByteArray big = makeTiff(bigEndianSpec);
    QCOMPARE(detectImageFormat(big), ImageFormat::Tiff);

    // 负例：只有「II」开头、但魔数不对 → 不是 TIFF。
    // 只看两个字母会把恰好以 II / MM 开头的别的文件也当成 TIFF。
    QByteArray broken = little;
    broken[2] = 0x2B;
    QCOMPARE(detectImageFormat(broken), ImageFormat::Unknown);
}

void ImageToPdfTests::decodesGrayscaleTiff() {
    TiffSpec spec = tiffSpec(2, 2);
    spec.pages[0] = QByteArray::fromHex("0040FF80");
    const QByteArray tiff = makeTiff(spec);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(tiff, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.width, 2);
    QCOMPARE(decoded.height, 2);
    QCOMPARE(decoded.rgb.size(), 12);
    // 灰度 → R=G=B；顺序与原图一致（不能上下颠倒、不能红蓝互换）。
    QCOMPARE(decoded.rgb.mid(0, 3), QByteArray::fromHex("000000"));
    QCOMPARE(decoded.rgb.mid(3, 3), QByteArray::fromHex("404040"));
    QCOMPARE(decoded.rgb.mid(6, 3), QByteArray::fromHex("FFFFFF"));
    QCOMPARE(decoded.rgb.mid(9, 3), QByteArray::fromHex("808080"));
}

void ImageToPdfTests::decodesWhiteIsZeroTiff() {
    // 光度解释 = 0（白为最小）：值 0 是白色、255 是黑色，要按规范反相。
    TiffSpec spec = tiffSpec(2, 1);
    spec.photometric = 0;
    spec.pages[0] = QByteArray::fromHex("00FF");
    const QByteArray tiff = makeTiff(spec);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(tiff, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.rgb.mid(0, 3), QByteArray::fromHex("FFFFFF"));
    QCOMPARE(decoded.rgb.mid(3, 3), QByteArray::fromHex("000000"));
}

void ImageToPdfTests::decodesRgbTiff() {
    TiffSpec spec = tiffSpec(2, 1);
    spec.samplesPerPixel = 3;
    spec.photometric = 2;
    spec.pages[0] = QByteArray::fromHex("FF00000000FF");
    const QByteArray tiff = makeTiff(spec);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(tiff, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.rgb.size(), 6);
    QCOMPARE(decoded.rgb.mid(0, 3), QByteArray::fromHex("FF0000"));
    QCOMPARE(decoded.rgb.mid(3, 3), QByteArray::fromHex("0000FF"));
}

void ImageToPdfTests::decodesBigEndianTiff() {
    // 大端（MM）文件必须解出**一样的像素**：字节序只影响文件里的排列，
    // 不影响图像本身。
    TiffSpec littleSpec = tiffSpec(2, 1);
    littleSpec.pages[0] = QByteArray::fromHex("1122");
    TiffSpec bigSpec = littleSpec;
    bigSpec.order = TiffOrder::Big;

    aipdf::pdf::DecodedImage little;
    aipdf::pdf::DecodedImage big;
    QString detail;
    QVERIFY2(decodeImage(makeTiff(littleSpec), &little, &detail), qPrintable(detail));
    QVERIFY2(decodeImage(makeTiff(bigSpec), &big, &detail), qPrintable(detail));
    QCOMPARE(little.rgb, big.rgb);
    QCOMPARE(little.rgb.mid(0, 3), QByteArray::fromHex("111111"));
    QCOMPARE(little.rgb.mid(3, 3), QByteArray::fromHex("222222"));
}

void ImageToPdfTests::decodesPalettedTiff() {
    TiffSpec spec = tiffSpec(2, 1);
    spec.bitsPerSample = 4;
    spec.photometric = 3;
    // 调色板是 16 位分量：先红后绿后蓝。4 位取样 → 每通道 2^4 = 16 档
    // （TIFF 6.0 要求给满 2^位深 档），共 48 个分量。
    spec.colorMap = QVector<quint32>(48, 0x0000);
    spec.colorMap[0] = 0xFF00;     // 红：索引 0
    spec.colorMap[32 + 1] = 0xFF00;  // 蓝：索引 1
    // 4 位取样：第一个像素在高半字节。
    spec.pages[0] = QByteArray::fromHex("01");
    const QByteArray tiff = makeTiff(spec);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(tiff, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.rgb.mid(0, 3), QByteArray::fromHex("FF0000"));
    QCOMPARE(decoded.rgb.mid(3, 3), QByteArray::fromHex("0000FF"));
}

void ImageToPdfTests::decodesOneBitTiff() {
    // 1 位取样：每字节 8 个像素，高位在前。
    TiffSpec spec = tiffSpec(2, 1);
    spec.bitsPerSample = 1;
    spec.pages[0] = QByteArray::fromHex("40");
    const QByteArray tiff = makeTiff(spec);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(tiff, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.rgb.mid(0, 3), QByteArray::fromHex("000000"));
    QCOMPARE(decoded.rgb.mid(3, 3), QByteArray::fromHex("FFFFFF"));
}

void ImageToPdfTests::decodes16BitTiff() {
    TiffSpec spec = tiffSpec(1, 1);
    spec.bitsPerSample = 16;
    spec.pages[0] = QByteArray::fromHex("1234");
    const QByteArray tiff = makeTiff(spec);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(tiff, &decoded, &detail), qPrintable(detail));
    // 16 位取样按高字节折到 8 位。
    QCOMPARE(decoded.rgb, QByteArray::fromHex("121212"));
}

void ImageToPdfTests::decodesCompressedTiff() {
    // 四种压缩都要解出**同样的像素**：压缩只是存法，不许改变图像。
    const QVector<int> compressions{1, 5, 8, 32773};
    for (const int compression : compressions) {
        TiffSpec spec = tiffSpec(4, 2);
        spec.compression = compression;
        spec.pages[0] = QByteArray::fromHex("00112233445566778899AABBCCDDEEFF");
        const QByteArray tiff = makeTiff(spec);

        aipdf::pdf::DecodedImage decoded;
        QString detail;
        QVERIFY2(decodeImage(tiff, &decoded, &detail),
                 qPrintable(QStringLiteral("compression %1: %2").arg(compression).arg(detail)));
        QCOMPARE(decoded.rgb.size(), 24);
        QCOMPARE(decoded.rgb.mid(0, 3), QByteArray::fromHex("000000"));
        QCOMPARE(decoded.rgb.mid(9, 3), QByteArray::fromHex("333333"));
        QCOMPARE(decoded.rgb.mid(21, 3), QByteArray::fromHex("777777"));
    }
}

void ImageToPdfTests::decodesMultiPageTiff() {
    // 多页 TIFF：一个文件两页，**一页都不能少**。
    TiffSpec spec = tiffSpec(2, 1);
    spec.pages[0] = QByteArray::fromHex("00FF");
    spec.pages.append(QByteArray::fromHex("AABB"));
    const QByteArray tiff = makeTiff(spec);

    QVector<aipdf::pdf::DecodedImage> pages;
    QString detail;
    QVERIFY2(aipdf::pdf::decodeImagePages(tiff, &pages, &detail), qPrintable(detail));
    QCOMPARE(pages.size(), 2);
    QCOMPARE(pages.at(0).rgb, QByteArray::fromHex("000000FFFFFF"));
    QCOMPARE(pages.at(1).rgb, QByteArray::fromHex("AAAAAABBBBBB"));

    // 转成 PDF 时按页展开，页序不变。
    ImageToPdfOptions options;
    options.dpi = 72.0;
    const ImageToPdfResult result = imagesToPdf({tiff}, options);
    QVERIFY2(result.ok(), qPrintable(result.detail));
    QCOMPARE(result.pageCount, 2);

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.pdf, QStringLiteral("tiff-multi.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 2);
    // 第二页是「AB」那页：渲染出来第一个像素应是 AA。
    const auto second = opened.document.page(1);
    QVERIFY(second.has_value());
    const QImage rendered = second->render(1.0);
    QCOMPARE(qRed(rendered.pixel(0, 0)), 0xAA);
}

void ImageToPdfTests::tiffBecomesReadablePdf() {
    TiffSpec spec = tiffSpec(2, 1);
    spec.samplesPerPixel = 3;
    spec.photometric = 2;
    spec.pages[0] = QByteArray::fromHex("FF00000000FF");
    const QByteArray tiff = makeTiff(spec);

    ImageToPdfOptions options;
    options.dpi = 72.0;
    const ImageToPdfResult result = imageToPdf(tiff, options);
    QVERIFY2(result.ok(), qPrintable(result.detail));
    QCOMPARE(result.pageCount, 1);
    // TIFF 走 /FlateDecode + /DeviceRGB（解成 RGB 后再压，与 BMP / PNG 同一条路）。
    QVERIFY(result.pdf.contains("/FlateDecode"));
    QVERIFY(result.pdf.contains("/DeviceRGB"));

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.pdf, QStringLiteral("from-tiff.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->widthPoints(), 2.0);

    const QImage rendered = page->render(1.0);
    QCOMPARE(rendered.width(), 2);
    QCOMPARE(qRed(rendered.pixel(0, 0)), 255);
    QCOMPARE(qGreen(rendered.pixel(0, 0)), 0);
    QCOMPARE(qBlue(rendered.pixel(1, 0)), 255);
}

void ImageToPdfTests::brokenTiffFails() {
    TiffSpec spec = tiffSpec(2, 1);
    spec.pages[0] = QByteArray::fromHex("1122");
    const QByteArray good = makeTiff(spec);

    // 负例 1：魔数坏了 → 明确失败，不是「解出一张乱图」。
    QByteArray badMagic = good;
    badMagic[2] = 0x2B;
    QCOMPARE(detectImageFormat(badMagic), ImageFormat::Unknown);

    // 负例 2：文件被截断 → 明确失败。
    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY(!decodeImage(good.left(10), &decoded, &detail));
    QVERIFY(!detail.isEmpty());

    // 负例 3：不支持的压缩（这里造 JPEG-in-TIFF）→ 说清是压缩不支持。
    QByteArray jpegCompressed = good;
    patchTiffInlineShort(jpegCompressed, 3, 7);
    QVERIFY(!decodeImage(jpegCompressed, &decoded, &detail));
    QVERIFY2(detail.contains(QStringLiteral("compression")), qPrintable(detail));

    // 负例 4：平面布局（每个通道分开存）→ 说清是布局不支持。
    QByteArray planar = good;
    patchTiffInlineShort(planar, 9, 2);
    QVERIFY(!decodeImage(planar, &decoded, &detail));
    QVERIFY2(detail.contains(QStringLiteral("planar")), qPrintable(detail));

    // 负例 5：CMYK → 说清是颜色空间不支持。
    QByteArray cmyk = good;
    patchTiffInlineShort(cmyk, 4, 6);
    QVERIFY(!decodeImage(cmyk, &decoded, &detail));
    QVERIFY2(detail.contains(QStringLiteral("CMYK")), qPrintable(detail));

    // 负例 6：转 PDF 时也要明确失败，不能给一份空 PDF。
    const ImageToPdfResult result = imagesToPdf({jpegCompressed});
    QCOMPARE(result.error, ImageToPdfError::DecodeFailed);
    QVERIFY(result.pdf.isEmpty());
    QCOMPARE(result.failedIndex, 0);
}

void ImageToPdfTests::decodesLzwAcrossCodeWidthChange() {
    // LZW 的码宽会随码数增长：9 位 → 10 位 → 11 位。
    //
    // 小图根本跨不过那道坎（要 511 个码才换宽），所以「码宽增长规则」
    // 这段逻辑平时压根没被执行到 —— 一旦写错，解出来的是**一整片噪点**，
    // 而且不会报错。这里用一张够大的图把两道坎都跨过去。
    TiffSpec spec = tiffSpec(64, 64);
    spec.compression = 5;
    spec.samplesPerPixel = 3;
    spec.photometric = 2;
    QByteArray raw;
    raw.reserve(64 * 64 * 3);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            raw.append(static_cast<char>((x * 7 + y * 13) % 256));
            raw.append(static_cast<char>((x * 11 + y * 3) % 256));
            raw.append(static_cast<char>((x * 5 + y * 17) % 256));
        }
    }
    spec.pages[0] = raw;
    const QByteArray tiff = makeTiff(spec);

    aipdf::pdf::DecodedImage decoded;
    QString detail;
    QVERIFY2(decodeImage(tiff, &decoded, &detail), qPrintable(detail));
    QCOMPARE(decoded.width, 64);
    QCOMPARE(decoded.height, 64);
    // 一个字节都不能差：跨过码宽门槛之后，规则差一格就全错。
    QCOMPARE(decoded.rgb.size(), raw.size());
    QCOMPARE(decoded.rgb, raw);
}

void ImageToPdfTests::decodesRealWorldTiffFixtures() {
    if (QStringLiteral(AIPDF_TIFF_FIXTURES_DIR).isEmpty()) {
        QSKIP("TIFF fixture directory is not configured");
    }
    struct Expectation {
        const char* fileName;
        int width;
        int height;
        quint32 checksum;
    };
    // 校验值是「用 libtiff 读出来的像素」的 FNV-1a 32 位值：
    // 由 scripts/generate-tiff-fixtures.py 造夹具时用同一套图案算出。
    const QVector<Expectation> expectations{
        {"rgb-lzw.tif", 64, 48, 0x178c1dc5u},
        {"rgb-deflate.tif", 8, 4, 0x2131b7bdu},
        {"gray-packbits.tif", 8, 4, 0x5126d1edu},
        {"palette.tif", 16, 4, 0x7f893fc5u},
        {"bigendian.tif", 4, 3, 0xc43802c1u},
    };
    for (const Expectation& expectation : expectations) {
        const QString name = QString::fromLatin1(expectation.fileName);
        const QByteArray bytes = readFixture(name);
        const QString missing = QStringLiteral("missing fixture %1").arg(name);
        QVERIFY2(!bytes.isEmpty(), qPrintable(missing));
        QVector<aipdf::pdf::DecodedImage> pages;
        QString detail;
        const bool decoded = aipdf::pdf::decodeImagePages(bytes, &pages, &detail);
        const QString failure = QStringLiteral("%1: %2").arg(name).arg(detail);
        QVERIFY2(decoded, qPrintable(failure));
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages.at(0).width, expectation.width);
        QCOMPARE(pages.at(0).height, expectation.height);
        QCOMPARE(pages.at(0).rgb.size(), expectation.width * expectation.height * 3);
        QCOMPARE(fnv1a32(pages.at(0).rgb), expectation.checksum);
    }

    // 多页 TIFF：两页尺寸还不一样，页序不能串。
    const QByteArray multipage = readFixture(QStringLiteral("multipage.tif"));
    QVERIFY2(!multipage.isEmpty(), "missing fixture multipage.tif");
    QVector<aipdf::pdf::DecodedImage> pages;
    QString detail;
    QVERIFY2(aipdf::pdf::decodeImagePages(multipage, &pages, &detail), qPrintable(detail));
    QCOMPARE(pages.size(), 2);
    QCOMPARE(pages.at(0).width, 3);
    QCOMPARE(pages.at(0).height, 2);
    QCOMPARE(fnv1a32(pages.at(0).rgb), 0x3a5ea820u);
    QCOMPARE(pages.at(1).width, 5);
    QCOMPARE(pages.at(1).height, 2);
    QCOMPARE(fnv1a32(pages.at(1).rgb), 0xa70b7fdau);
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
