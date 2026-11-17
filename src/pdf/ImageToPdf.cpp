#include "pdf/ImageToPdf.h"

#include <QtGlobal>

#include <cmath>
#include <cstring>
#include <limits>

namespace aipdf::pdf {

namespace {

// 页面上限：14400 点 = 200 英寸。
//
// 更大的尺寸没有实际意义（打印机根本吐不出这种纸），
// 留着只会让扫描件按纸张尺寸缩放时算出荒唐的比例。
constexpr double kMaxPageDimensionPoints = 14400.0;

// 单次转换的图片张数上限。
//
// 一次扫描最多几百页；不设上限的话，调用方一个循环写错了就会
// 生成一份几百万页的文件。
constexpr int kMaxImages = 1000;

// 像素数上限：1 亿像素。
//
// 3200 万像素的照片 RGB 就要 96 MB，再大就一定会把内存吃穿。
// 正常扫描件（300 DPI 的 A4）只有 250 万像素左右，离这个上限很远。
constexpr qint64 kMaxPixels = 100000000LL;

// --------------------------------------------------------------- 字节读取 ----

// 按小端读 BMP 里的整数。
//
// 为什么自己写：BMP 的整数一律小端，直接 reinterpret_cast 到 quint32*
// 会在大端机器上读出完全错误的值，而且错得很安静（尺寸变成天文数字）。
class ByteReader {
public:
    explicit ByteReader(const QByteArray& data) : data_(data) {}

    qint64 size() const { return data_.size(); }

    bool has(qint64 offset, qint64 count) const {
        return offset >= 0 && count >= 0 && offset + count <= data_.size();
    }

    quint8 u8(qint64 offset) const {
        return static_cast<quint8>(data_.at(static_cast<qsizetype>(offset)));
    }

    quint16 u16(qint64 offset) const {
        return static_cast<quint16>(static_cast<quint16>(u8(offset)) |
                                    static_cast<quint16>(u8(offset + 1) << 8));
    }

    quint32 u32(qint64 offset) const {
        return static_cast<quint32>(u8(offset)) |
               (static_cast<quint32>(u8(offset + 1)) << 8) |
               (static_cast<quint32>(u8(offset + 2)) << 16) |
               (static_cast<quint32>(u8(offset + 3)) << 24);
    }

    qint32 s32(qint64 offset) const { return static_cast<qint32>(u32(offset)); }

private:
    const QByteArray& data_;
};

// ------------------------------------------------------------- 格式识别 ----

// 把认得出的格式说成人能看懂的名字（写进技术细节里）。
//
// 为什么要专门说这个：用户拿 TIFF 回来时，提示只写「格式不支持」等于让他自己猜；
// 直接点名「TIFF」，他立刻知道是设备设置的问题还是程序的问题。
QString describeFormat(const QByteArray& bytes) {
    if (bytes.size() >= 8 && static_cast<quint8>(bytes.at(0)) == 0x89 &&
        bytes.startsWith(QByteArrayLiteral("\x89PNG\r\n\x1a\n"))) {
        return QStringLiteral("PNG");
    }
    if (bytes.size() >= 4 &&
        (bytes.startsWith(QByteArrayLiteral("II\x2a\x00")) ||
         bytes.startsWith(QByteArrayLiteral("MM\x00\x2a")))) {
        return QStringLiteral("TIFF");
    }
    if (bytes.size() >= 4 && bytes.startsWith(QByteArrayLiteral("GIF8"))) {
        return QStringLiteral("GIF");
    }
    if (bytes.size() >= 12 && bytes.startsWith(QByteArrayLiteral("RIFF"))) {
        return QStringLiteral("WebP");
    }
    if (bytes.size() >= 2 && static_cast<quint8>(bytes.at(0)) == 0x42 &&
        static_cast<quint8>(bytes.at(1)) == 0x4d) {
        return QStringLiteral("BMP");
    }
    return QStringLiteral("unknown");
}

// ------------------------------------------------------------- JPEG 头部 ----

// JPEG 头里读出来的信息。
struct JpegInfo {
    int width = 0;
    int height = 0;
    int components = 0;
    // Adobe APP14 段里的 transform 值（-1 表示没有 APP14）。
    int adobeTransform = -1;
};

// 读 JPEG 里的 16 位大端数。
//
// JPEG 的整数一律**大端**（与 BMP 相反）：按小端读会把宽 16 读成 4096，
// 页面尺寸跟着全错，而且不报错，只是图变成方的。
quint16 readBigEndianU16(const ByteReader& reader, qint64 offset) {
    return static_cast<quint16>((static_cast<quint16>(reader.u8(offset)) << 8) |
                                 reader.u8(offset + 1));
}

// JPEG 必须以 EOI（FFD9）结尾。
//
// 为什么查这个：只看头的话，被截断的图（扫描仪中途断线、驱动崩了）
// 会被当成好图塞进 PDF，结果是页面上半张图下半张空白，而且**没有任何提示**。
bool hasEndOfImage(const QByteArray& bytes) {
    const qint64 tail = qMin<qint64>(bytes.size(), 16);
    for (qint64 i = bytes.size() - tail; i + 1 < bytes.size(); ++i) {
        if (static_cast<quint8>(bytes.at(static_cast<qsizetype>(i))) == 0xFF &&
            static_cast<quint8>(bytes.at(static_cast<qsizetype>(i + 1))) == 0xD9) {
            return true;
        }
    }
    return false;
}

// 这些是 SOF（Start of Frame）标记。
//
// 为什么排除 0xC4/0xC8/0xCC：它们分别属于 DHT（霍夫曼表）、保留位和 DAC，
// 结构跟 SOF 一样带长度字段，但不是图像尺寸。
bool isStartOfFrame(unsigned char marker) {
    if (marker < 0xC0 || marker > 0xCF) return false;
    return marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
}

// 只解析到 SOF 为止。
//
// 为什么够用：PDF 的 /DCTDecode 只需要宽高与分量数，压缩数据原样带过去；
// 而熵编码数据里可能出现 0xFF 字节，往后扫会踩进压缩流里出不来的坑。
bool parseJpegHeader(const QByteArray& bytes, JpegInfo* out, QString* detail) {
    const ByteReader reader(bytes);
    if (reader.size() < 4) {
        *detail = QStringLiteral("JPEG too short");
        return false;
    }
    if (reader.u8(0) != 0xFF || reader.u8(1) != 0xD8) {
        *detail = QStringLiteral("JPEG missing SOI marker");
        return false;
    }

    qint64 pos = 2;
    bool foundFrame = false;
    while (pos + 1 < reader.size()) {
        if (reader.u8(pos) != 0xFF) {
            ++pos;
            continue;
        }
        // 0xFF 可以连续出现：填充字节跳过，后面的才是真正的标记。
        while (pos < reader.size() && reader.u8(pos) == 0xFF) {
            ++pos;
        }
        if (pos >= reader.size()) break;
        const unsigned char marker = reader.u8(pos);
        ++pos;
        // 没有长度字段的独立标记。
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        if (marker == 0xD9) break;  // EOI：后面没有 SOF 了
        if (!reader.has(pos, 2)) {
            *detail = QStringLiteral("JPEG truncated segment header");
            return false;
        }
        // 段长是**大端**的（BMP 才是小端）：写错字节序会把 16 字节的 APP0
        // 读成 4096，然后段尾跑到文件外，整张图都解不出来。
        const qint64 segmentLength = (static_cast<qint64>(reader.u8(pos)) << 8) | reader.u8(pos + 1);
        if (segmentLength < 2) {
            *detail = QStringLiteral("JPEG invalid segment length");
            return false;
        }
        const qint64 segmentStart = pos + 2;
        const qint64 segmentEnd = pos + segmentLength;
        if (segmentEnd > reader.size()) {
            *detail = QStringLiteral("JPEG truncated segment");
            return false;
        }
        if (marker == 0xEE && segmentEnd - segmentStart >= 12 &&
            std::memcmp(bytes.constData() + segmentStart, "Adobe", 5) == 0) {
            // APP14 的最后一个字节就是 transform。
            out->adobeTransform = reader.u8(segmentEnd - 1);
        } else if (isStartOfFrame(marker)) {
            if (segmentEnd - segmentStart < 6) {
                *detail = QStringLiteral("JPEG truncated SOF segment");
                return false;
            }
            out->height = readBigEndianU16(reader, segmentStart + 1);
            out->width = readBigEndianU16(reader, segmentStart + 3);
            out->components = reader.u8(segmentStart + 5);
            foundFrame = true;
            break;
        }
        pos = segmentEnd;
    }

    if (!foundFrame) {
        *detail = QStringLiteral("JPEG has no SOF marker");
        return false;
    }
    if (out->width <= 0 || out->height <= 0) {
        *detail = QStringLiteral("JPEG has empty dimensions");
        return false;
    }
    if (out->components != 1 && out->components != 3 && out->components != 4) {
        *detail = QStringLiteral("JPEG has unsupported component count %1").arg(out->components);
        return false;
    }
    if (!hasEndOfImage(bytes)) {
        *detail = QStringLiteral("JPEG is truncated (no EOI marker)");
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ BMP ----

// 一个颜色通道在像素里的位置。
struct ChannelSpec {
    quint32 mask = 0;
    int shift = 0;
    int bits = 0;
    quint64 maxValue = 0;
};

// 从掩码推出「位移 + 位数」。
ChannelSpec makeChannel(quint32 mask) {
    ChannelSpec spec;
    spec.mask = mask;
    if (mask == 0) return spec;
    quint32 rest = mask;
    while ((rest & 1u) == 0u) {
        rest >>= 1;
        ++spec.shift;
    }
    while ((rest & 1u) != 0u) {
        rest >>= 1;
        ++spec.bits;
    }
    spec.maxValue = spec.bits >= 64 ? 0xFFFFFFFFULL : ((1ULL << spec.bits) - 1ULL);
    return spec;
}

// 按掩码取一个通道并拉到 0..255。
int scaleChannel(quint32 pixel, const ChannelSpec& spec) {
    if (spec.bits == 0) return 0;
    const quint64 value = static_cast<quint64>((pixel & spec.mask) >> spec.shift);
    return static_cast<int>((value * 255ULL + spec.maxValue / 2ULL) / spec.maxValue);
}

// 自己解 BMP。
//
// 为什么不直接用 QImage：本机部署的 Qt 图像插件只有 gif/ico/jpeg/svg，
// 虽然 BMP 恰好是 Qt 内置的，但把「能不能读某种格式」押在部署环境上不稳；
// 自己解还能顺手把「头里的尺寸是不是真的」验一遍——
// 一张声称 30000×30000 的 BMP 不该让程序去申请 2.7 GB 内存。
bool parseBmp(const QByteArray& bytes, int* width, int* height, QByteArray* rgb,
              QString* detail) {
    const ByteReader reader(bytes);
    if (reader.size() < 14) {
        *detail = QStringLiteral("BMP too short");
        return false;
    }
    if (reader.u8(0) != 0x42 || reader.u8(1) != 0x4d) {
        *detail = QStringLiteral("BMP missing BM signature");
        return false;
    }
    const quint32 pixelOffset = reader.u32(10);
    const quint32 headerSize = reader.u32(14);
    if (headerSize < 12) {
        *detail = QStringLiteral("BMP invalid header size %1").arg(headerSize);
        return false;
    }
    if (!reader.has(14, headerSize)) {
        *detail = QStringLiteral("BMP header truncated");
        return false;
    }

    qint32 rawWidth = 0;
    qint32 rawHeight = 0;
    int bitsPerPixel = 0;
    quint32 compression = 0;
    quint32 paletteCount = 0;
    int paletteEntrySize = 4;

    if (headerSize == 12) {
        // BITMAPCOREHEADER：宽高与色深都是 16 位，调色板项是 3 字节 RGB。
        rawWidth = reader.u16(18);
        rawHeight = reader.u16(20);
        bitsPerPixel = reader.u16(24);
        paletteEntrySize = 3;
        compression = 0;
    } else {
        rawWidth = reader.s32(18);
        rawHeight = reader.s32(22);
        bitsPerPixel = reader.u16(28);
        compression = reader.u32(30);
        if (headerSize >= 40) {
            paletteCount = reader.u32(46);
        }
    }

    if (rawWidth <= 0 || rawHeight == 0) {
        *detail = QStringLiteral("BMP invalid dimensions %1x%2").arg(rawWidth).arg(rawHeight);
        return false;
    }
    const bool bottomUp = rawHeight > 0;
    const qint64 absHeight = rawHeight < 0 ? -static_cast<qint64>(rawHeight)
                                           : static_cast<qint64>(rawHeight);
    const qint64 imageWidth = rawWidth;
    if (imageWidth > 100000 || absHeight > 100000) {
        *detail = QStringLiteral("BMP dimensions out of range %1x%2")
                      .arg(imageWidth)
                      .arg(absHeight);
        return false;
    }
    if (imageWidth * absHeight > kMaxPixels) {
        *detail = QStringLiteral("BMP pixel count too large");
        return false;
    }

    // 颜色掩码：BI_BITFIELDS 自己带掩码，其余按位深用默认掩码。
    quint32 redMask = 0;
    quint32 greenMask = 0;
    quint32 blueMask = 0;
    if (headerSize >= 52) {
        // BITMAPV2/V4/V5：掩码在头里。
        redMask = reader.u32(54);
        greenMask = reader.u32(58);
        blueMask = reader.u32(62);
    } else if (compression == 3) {
        // BITMAPINFOHEADER + BI_BITFIELDS：掩码紧跟在头后面。
        redMask = reader.u32(54);
        greenMask = reader.u32(58);
        blueMask = reader.u32(62);
    } else {
        switch (bitsPerPixel) {
        case 16:
            redMask = 0x7C00u;
            greenMask = 0x03E0u;
            blueMask = 0x001Fu;
            break;
        case 24:
            redMask = 0xFF0000u;
            greenMask = 0x00FF00u;
            blueMask = 0x0000FFu;
            break;
        case 32:
            redMask = 0x00FF0000u;
            greenMask = 0x0000FF00u;
            blueMask = 0x000000FFu;
            break;
        default:
            break;
        }
    }

    if (compression != 0 && compression != 3) {
        // 1 = RLE8，2 = RLE4。游程压缩的 BMP 很罕见，但真遇到也不能瞎解。
        *detail = QStringLiteral("BMP compression %1 not supported").arg(compression);
        return false;
    }
    if (bitsPerPixel != 1 && bitsPerPixel != 4 && bitsPerPixel != 8 &&
        bitsPerPixel != 16 && bitsPerPixel != 24 && bitsPerPixel != 32) {
        *detail = QStringLiteral("BMP bit depth %1 not supported").arg(bitsPerPixel);
        return false;
    }

    // 调色板。
    QVector<QByteArray> palette;
    if (bitsPerPixel <= 8) {
        const quint32 entries =
            paletteCount != 0 ? paletteCount : (1u << bitsPerPixel);
        const qint64 paletteOffset = 14 + headerSize;
        if (!reader.has(paletteOffset, static_cast<qint64>(entries) * paletteEntrySize)) {
            *detail = QStringLiteral("BMP palette truncated");
            return false;
        }
        palette.reserve(static_cast<int>(entries));
        for (quint32 i = 0; i < entries; ++i) {
            const qint64 entry = paletteOffset + static_cast<qint64>(i) * paletteEntrySize;
            // 调色板项是 BGR(A) 顺序。
            const quint8 blue = reader.u8(entry);
            const quint8 green = reader.u8(entry + 1);
            const quint8 red = reader.u8(entry + 2);
            QByteArray color;
            color.append(static_cast<char>(red));
            color.append(static_cast<char>(green));
            color.append(static_cast<char>(blue));
            palette.append(color);
        }
    }

    // 行距：BMP 每行按 4 字节对齐。
    const qint64 stride = ((imageWidth * bitsPerPixel + 31) / 32) * 4;
    const qint64 pixelBytes = stride * absHeight;
    if (!reader.has(pixelOffset, pixelBytes)) {
        *detail = QStringLiteral("BMP pixel data truncated");
        return false;
    }

    const ChannelSpec red = makeChannel(redMask);
    const ChannelSpec green = makeChannel(greenMask);
    const ChannelSpec blue = makeChannel(blueMask);

    QByteArray out;
    out.resize(static_cast<qsizetype>(imageWidth * absHeight * 3));
    char* outData = out.data();

    for (qint64 row = 0; row < absHeight; ++row) {
        // 负高度 = 从上往下存，正高度 = 从下往上存；输出统一从上往下。
        const qint64 sourceRow = bottomUp ? (absHeight - 1 - row) : row;
        const qint64 rowStart = pixelOffset + sourceRow * stride;
        char* target = outData + row * imageWidth * 3;
        for (qint64 x = 0; x < imageWidth; ++x) {
            quint8 r = 0;
            quint8 g = 0;
            quint8 b = 0;
            if (bitsPerPixel <= 8) {
                const qint64 byteOffset = rowStart + x * bitsPerPixel / 8;
                const quint8 value = reader.u8(byteOffset);
                int index = 0;
                if (bitsPerPixel == 8) {
                    index = value;
                } else if (bitsPerPixel == 4) {
                    // 4 位：一个字节两个像素，高位在前。
                    index = (x % 2 == 0) ? ((value >> 4) & 0x0F) : (value & 0x0F);
                } else {
                    // 1 位：MSB 在前。
                    index = (value >> (7 - (x % 8))) & 0x01;
                }
                if (index < palette.size()) {
                    r = static_cast<quint8>(palette[index].at(0));
                    g = static_cast<quint8>(palette[index].at(1));
                    b = static_cast<quint8>(palette[index].at(2));
                }
            } else {
                const int bytesPerPixel = bitsPerPixel / 8;
                const qint64 pixelOffsetInRow = rowStart + x * bytesPerPixel;
                quint32 pixel = 0;
                for (int i = 0; i < bytesPerPixel; ++i) {
                    pixel |= static_cast<quint32>(reader.u8(pixelOffsetInRow + i)) << (8 * i);
                }
                r = static_cast<quint8>(scaleChannel(pixel, red));
                g = static_cast<quint8>(scaleChannel(pixel, green));
                b = static_cast<quint8>(scaleChannel(pixel, blue));
            }
            target[x * 3 + 0] = static_cast<char>(r);
            target[x * 3 + 1] = static_cast<char>(g);
            target[x * 3 + 2] = static_cast<char>(b);
        }
    }

    *width = static_cast<int>(imageWidth);
    *height = static_cast<int>(absHeight);
    *rgb = out;
    return true;
}

// ------------------------------------------------------------- PDF 拼装 ----

// qCompress 的输出是「4 字节未压缩长度 + zlib 数据」，
// 而 PDF 的 /FlateDecode 只认后半段 zlib 数据；把 Qt 私有长度头带进去会让严格阅读器拒收。
QByteArray pdfFlate(const QByteArray& raw) {
    QByteArray compressed = qCompress(raw, 6);
    if (compressed.size() >= 4) {
        compressed.remove(0, 4);
    }
    return compressed;
}

// 把尺寸写成 PDF 里的数字。
QByteArray formatPoints(double value) {
    const double rounded = qRound64(value * 10000.0) / 10000.0;
    const qlonglong asInteger = qRound64(rounded);
    if (std::fabs(rounded - static_cast<double>(asInteger)) < 1e-9) {
        return QByteArray::number(asInteger);
    }
    return QByteArray::number(rounded, 'f', 4);
}

// 一页要写进 PDF 的图像。
struct PageImage {
    // 像素宽高。
    int width = 0;
    int height = 0;
    // 页面物理尺寸（PDF 点）。
    double widthPoints = 0.0;
    double heightPoints = 0.0;
    // 图像数据（已压缩或原样）。
    QByteArray stream;
    // 图像过滤器（/DCTDecode 或 /FlateDecode）。
    QByteArray filter;
    // 颜色空间。
    QByteArray colorSpace;
    // 额外的字典项（例如 CMYK 的 /Decode）。
    QByteArray extra;
};

// 拼一份「每页一张图」的 PDF。
//
// 为什么自己拼字节而不借 qpdf：这里只需要最小结构（Catalog / Pages / 每页三个对象），
// 借 qpdf 反而要把「可选依赖」变成硬依赖——没装 qpdf 的构建就编不了了。
QByteArray buildPdf(const QVector<PageImage>& pages) {
    const int objectCount = 2 + pages.size() * 3;
    QByteArray out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    QVector<qint64> offsets(objectCount + 1, 0);

    const auto appendObject = [&out, &offsets](int number, const QByteArray& body) {
        offsets[number] = out.size();
        out += QByteArray::number(number) + " 0 obj\n" + body + "\nendobj\n";
    };

    // 对象 1 = Catalog，2 = Pages；第 i 页（0 基）用 3+i*3 / 4+i*3 / 5+i*3。
    appendObject(1, "<< /Type /Catalog /Pages 2 0 R >>");

    QByteArray kids;
    for (int i = 0; i < pages.size(); ++i) {
        if (i > 0) kids += ' ';
        kids += QByteArray::number(5 + i * 3) + " 0 R";
    }
    appendObject(2, "<< /Type /Pages /Kids [" + kids + "] /Count " +
                       QByteArray::number(pages.size()) + " >>");

    for (int i = 0; i < pages.size(); ++i) {
        const PageImage& page = pages.at(i);
        const int contentId = 3 + i * 3;
        const int imageId = 4 + i * 3;
        const int pageId = 5 + i * 3;

        // 内容流：把图铺满整页。
        const QByteArray content = "q\n" + formatPoints(page.widthPoints) + " 0 0 " +
                                   formatPoints(page.heightPoints) + " 0 0 cm\n/Im0 Do\nQ\n";
        QByteArray contentBody = "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n";
        contentBody += content;
        contentBody += "\nendstream";
        appendObject(contentId, contentBody);

        QByteArray imageDict = "<< /Type /XObject /Subtype /Image /Width " +
                               QByteArray::number(page.width) + " /Height " +
                               QByteArray::number(page.height) + " /ColorSpace " + page.colorSpace +
                               " /BitsPerComponent 8 ";
        if (!page.filter.isEmpty()) {
            imageDict += "/Filter " + page.filter + " ";
        }
        if (!page.extra.isEmpty()) {
            imageDict += page.extra + " ";
        }
        imageDict += "/Length " + QByteArray::number(page.stream.size()) + " >>\nstream\n";
        imageDict += page.stream;
        imageDict += "\nendstream";
        appendObject(imageId, imageDict);

        const QByteArray mediaBox = "[0 0 " + formatPoints(page.widthPoints) + " " +
                                    formatPoints(page.heightPoints) + "]";
        appendObject(pageId, "<< /Type /Page /Parent 2 0 R /MediaBox " + mediaBox +
                                 " /Resources << /XObject << /Im0 " +
                                 QByteArray::number(imageId) + " 0 R >> >> /Contents " +
                                 QByteArray::number(contentId) + " 0 R >>");
    }

    const qint64 xrefOffset = out.size();
    out += "xref\n0 " + QByteArray::number(objectCount + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (int number = 1; number <= objectCount; ++number) {
        out += QByteArray::number(offsets.at(number), 10).rightJustified(10, '0') +
               " 00000 n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(objectCount + 1) + " /Root 1 0 R >>\n";
    out += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return out;
}

}  // namespace

// ------------------------------------------------------------ 公开接口 ----

ImageFormat detectImageFormat(const QByteArray& bytes) {
    if (bytes.size() >= 3 && static_cast<quint8>(bytes.at(0)) == 0xFF &&
        static_cast<quint8>(bytes.at(1)) == 0xD8 && static_cast<quint8>(bytes.at(2)) == 0xFF) {
        return ImageFormat::Jpeg;
    }
    if (bytes.size() >= 2 && static_cast<quint8>(bytes.at(0)) == 0x42 &&
        static_cast<quint8>(bytes.at(1)) == 0x4d) {
        return ImageFormat::Bmp;
    }
    return ImageFormat::Unknown;
}

bool decodeImage(const QByteArray& bytes, DecodedImage* out, QString* detail) {
    const auto fail = [detail](const QString& reason) {
        if (detail != nullptr) {
            *detail = reason;
        }
        return false;
    };
    if (out == nullptr) {
        return fail(QStringLiteral("no output buffer"));
    }
    if (bytes.isEmpty()) {
        return fail(QStringLiteral("image bytes are empty"));
    }

    switch (detectImageFormat(bytes)) {
    case ImageFormat::Jpeg: {
        JpegInfo info;
        if (!parseJpegHeader(bytes, &info, detail)) {
            return false;
        }
        out->width = info.width;
        out->height = info.height;
        out->jpeg = bytes;
        out->jpegComponents = info.components;
        // 4 分量 + Adobe transform 2 = YCCK，值是反的，写进 PDF 要 /Decode 反相。
        out->jpegInverted = info.components == 4;
        return true;
    }
    case ImageFormat::Bmp: {
        int width = 0;
        int height = 0;
        QByteArray rgb;
        if (!parseBmp(bytes, &width, &height, &rgb, detail)) {
            return false;
        }
        out->width = width;
        out->height = height;
        out->rgb = rgb;
        return true;
    }
    case ImageFormat::Unknown:
    default:
        return fail(QStringLiteral("unsupported image format: %1").arg(describeFormat(bytes)));
    }
}

ImageToPdfResult imageToPdf(const QByteArray& imageBytes, const ImageToPdfOptions& options) {
    return imagesToPdf(QVector<QByteArray>{imageBytes}, options);
}

ImageToPdfResult imagesToPdf(const QVector<QByteArray>& images,
                             const ImageToPdfOptions& options) {
    ImageToPdfResult result;
    if (images.isEmpty()) {
        result.error = ImageToPdfError::EmptyInput;
        result.detail = QStringLiteral("no images given");
        return result;
    }
    if (images.size() > kMaxImages) {
        result.error = ImageToPdfError::EncodeFailed;
        result.detail = QStringLiteral("too many images: %1").arg(images.size());
        return result;
    }
    if (!(options.dpi > 0.0) || !std::isfinite(options.dpi) || options.dpi > 10000.0) {
        result.error = ImageToPdfError::InvalidPageSize;
        result.detail = QStringLiteral("invalid dpi: %1").arg(options.dpi);
        return result;
    }

    QVector<PageImage> pages;
    pages.reserve(images.size());
    for (int i = 0; i < images.size(); ++i) {
        // 空字节单独一类：不是「格式不对」，是压根没拿到图
        // （扫描仪断线、设备被抢走时最常见的就是这个）。
        if (images.at(i).isEmpty()) {
            result.error = ImageToPdfError::EmptyInput;
            result.failedIndex = i;
            result.detail = QStringLiteral("image bytes are empty");
            return result;
        }
        // 格式先自己认一遍：认不出来是「格式不支持」，
        // 认出来了但解不开才是「数据坏了」——这两种要分开报，
        // 前者多半是设备设置的问题，后者是数据传坏了。
        if (detectImageFormat(images.at(i)) == ImageFormat::Unknown) {
            result.error = ImageToPdfError::UnsupportedFormat;
            result.failedIndex = i;
            result.detail =
                QStringLiteral("unsupported image format: %1").arg(describeFormat(images.at(i)));
            return result;
        }
        DecodedImage decoded;
        QString detail;
        if (!decodeImage(images.at(i), &decoded, &detail)) {
            result.error = ImageToPdfError::DecodeFailed;
            result.failedIndex = i;
            result.detail = detail;
            return result;
        }
        const double widthPoints = decoded.width * 72.0 / options.dpi;
        const double heightPoints = decoded.height * 72.0 / options.dpi;
        if (!(widthPoints > 0.0) || !(heightPoints > 0.0) ||
            widthPoints > kMaxPageDimensionPoints || heightPoints > kMaxPageDimensionPoints) {
            result.error = ImageToPdfError::InvalidPageSize;
            result.failedIndex = i;
            result.detail = QStringLiteral("page size out of range for %1x%2 pixels")
                                .arg(decoded.width)
                                .arg(decoded.height);
            return result;
        }

        PageImage page;
        page.width = decoded.width;
        page.height = decoded.height;
        page.widthPoints = widthPoints;
        page.heightPoints = heightPoints;
        if (decoded.isJpeg()) {
            // JPEG 原样塞进去：不重编码，一个字节都不动。
            page.filter = "/DCTDecode";
            page.stream = decoded.jpeg;
            if (decoded.jpegComponents == 1) {
                page.colorSpace = "/DeviceGray";
            } else if (decoded.jpegComponents == 4) {
                page.colorSpace = "/DeviceCMYK";
                page.extra = "/Decode [1 0 1 0 1 0 1 0]";
            } else {
                page.colorSpace = "/DeviceRGB";
            }
        } else {
            page.filter = "/FlateDecode";
            page.stream = pdfFlate(decoded.rgb);
            page.colorSpace = "/DeviceRGB";
        }
        pages.append(page);
    }

    const QByteArray bytes = buildPdf(pages);
    if (bytes.isEmpty()) {
        result.error = ImageToPdfError::EncodeFailed;
        result.detail = QStringLiteral("failed to assemble PDF bytes");
        return result;
    }
    result.error = ImageToPdfError::None;
    result.pdf = bytes;
    result.pageCount = pages.size();
    return result;
}

}  // namespace aipdf::pdf
