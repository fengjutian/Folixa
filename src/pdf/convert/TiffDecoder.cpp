#include "pdf/convert/TiffDecoder.h"

#include <QHash>
#include <QtGlobal>

#include <cstring>

namespace aipdf::pdf {

namespace {

// 像素数上限：1 亿像素。
//
// 与 ImageToPdf 那一层的上限保持一致：一张声称 30000×30000 的 TIFF
// 不该让程序去申请 2.7 GB 内存。
constexpr qint64 kMaxPixels = 100000000LL;

// 页数上限：一次转换最多几百页。
constexpr int kMaxPages = 1000;

// TIFF 的标签号。
enum Tag : quint16 {
    kTagImageWidth = 256,
    kTagImageLength = 257,
    kTagBitsPerSample = 258,
    kTagCompression = 259,
    kTagPhotometric = 262,
    kTagFillOrder = 266,
    kTagStripOffsets = 273,
    kTagSamplesPerPixel = 277,
    kTagRowsPerStrip = 278,
    kTagStripByteCounts = 279,
    kTagPlanarConfig = 284,
    kTagColorMap = 320,
    kTagTileWidth = 322,
    kTagTileLength = 323,
    kTagTileOffsets = 324,
    kTagTileByteCounts = 325,
};

// 字段类型。
enum FieldType : quint16 {
    kTypeByte = 1,
    kTypeAscii = 2,
    kTypeShort = 3,
    kTypeLong = 4,
    kTypeRational = 5,
    kTypeSByte = 6,
    kTypeUndefined = 7,
    kTypeSShort = 8,
    kTypeSLong = 9,
    kTypeSRational = 10,
    kTypeFloat = 11,
    kTypeDouble = 12,
};

// 压缩方式。
enum Compression : quint16 {
    kCompressionNone = 1,
    kCompressionCcittRle = 2,
    kCompressionCcittFax3 = 3,
    kCompressionCcittFax4 = 4,
    kCompressionLzw = 5,
    kCompressionOldJpeg = 6,
    kCompressionJpeg = 7,
    kCompressionDeflate = 8,
    kCompressionPackBits = 32773,
    kCompressionDeflateLegacy = 32946,
};

// 光度解释。
enum Photometric : quint16 {
    kPhotometricWhiteIsZero = 0,
    kPhotometricBlackIsZero = 1,
    kPhotometricRgb = 2,
    kPhotometricPalette = 3,
    kPhotometricCmyk = 6,
};

// ------------------------------------------------------------- 字节读取 ----

// 按 TIFF 的字节序读整数。
//
// 为什么不能像 BMP 那样直接 reinterpret_cast：TIFF 有**两种**字节序
// （II 小端、MM 大端），写死一种就会把另一种的文件读成天文数字，
// 而且错得很安静 —— 尺寸变大、页面白掉，没有任何报错。
class TiffReader {
public:
    TiffReader(const QByteArray& data, bool littleEndian) : data_(data), little_(littleEndian) {}

    const QByteArray& data() const { return data_; }

    bool has(qint64 offset, qint64 count) const {
        return offset >= 0 && count >= 0 && offset + count <= data_.size();
    }

    quint8 u8(qint64 offset) const {
        return static_cast<quint8>(data_.at(static_cast<qsizetype>(offset)));
    }

    quint16 u16(qint64 offset) const {
        const quint8 a = u8(offset);
        const quint8 b = u8(offset + 1);
        return little_ ? static_cast<quint16>(a | static_cast<quint16>(b << 8))
                       : static_cast<quint16>((static_cast<quint16>(a) << 8) | b);
    }

    quint32 u32(qint64 offset) const {
        quint32 value = 0;
        if (little_) {
            for (int index = 0; index < 4; ++index) {
                value |= static_cast<quint32>(u8(offset + index)) << (8 * index);
            }
        } else {
            for (int index = 0; index < 4; ++index) {
                value = (value << 8) | static_cast<quint32>(u8(offset + index));
            }
        }
        return value;
    }

private:
    const QByteArray& data_;
    bool little_ = true;
};

// 字段类型占几个字节。
int fieldTypeSize(quint16 type) {
    switch (type) {
    case kTypeByte:
    case kTypeAscii:
    case kTypeSByte:
    case kTypeUndefined:
        return 1;
    case kTypeShort:
    case kTypeSShort:
        return 2;
    case kTypeLong:
    case kTypeSLong:
    case kTypeFloat:
        return 4;
    case kTypeRational:
    case kTypeSRational:
    case kTypeDouble:
        return 8;
    default:
        return 0;
    }
}

// 一个 IFD 条目。
struct IfdEntry {
    quint16 type = 0;
    quint32 count = 0;
    // value 字段在文件里的位置（内联值就装在这 4 个字节里）。
    qint64 valueFieldOffset = 0;
};

// 读条目里的第 index 个值。
//
// 内联规则：值的大小不超过 4 字节时，值**左对齐**装在 value 字段里
// （不论字节序都是从前两个字节开始读）；超过 4 字节时那里装的是偏移。
quint64 readEntryValue(const TiffReader& reader, const IfdEntry& entry, quint32 index,
                        bool* ok) {
    const int size = fieldTypeSize(entry.type);
    if (size == 0) {
        *ok = false;
        return 0;
    }
    const quint64 total = static_cast<quint64>(size) * entry.count;
    qint64 base = entry.valueFieldOffset;
    if (total > 4) {
        base = static_cast<qint64>(reader.u32(entry.valueFieldOffset));
    }
    const qint64 at = base + static_cast<qint64>(index) * size;
    if (!reader.has(at, size)) {
        *ok = false;
        return 0;
    }
    switch (entry.type) {
    case kTypeByte:
    case kTypeAscii:
    case kTypeUndefined:
        return reader.u8(at);
    case kTypeShort:
        return reader.u16(at);
    case kTypeLong:
        return reader.u32(at);
    default:
        *ok = false;
        return 0;
    }
}

// --------------------------------------------------------------- 解压 ----

// PackBits（Apple 的 RLE）。
//
// 一条控制字节 n：
//   n >= 0    → 后面 n+1 个字节原样抄；
//   n = -128  → 什么都不做（填充）；
//   其它负数 → 下一个字节重复 (-n)+1 次。
bool decodePackBits(const QByteArray& in, QByteArray* out) {
    int index = 0;
    while (index < in.size()) {
        const int control = static_cast<signed char>(in.at(index));
        ++index;
        if (control >= 0) {
            const int count = control + 1;
            if (index + count > in.size()) {
                return false;
            }
            out->append(in.constData() + index, count);
            index += count;
        } else if (control != -128) {
            const int count = 1 - control;
            if (index >= in.size()) {
                return false;
            }
            const char value = in.at(index);
            ++index;
            out->append(count, value);
        }
    }
    return true;
}

// TIFF 的 LZW。
//
// 与 GIF 的 LZW 不同：位是**高位在前**，码宽从 9 位起，清码 256、
// 结束码 257，新码从 258 开始。
//
// 两条容易写错的规则：
//
//   1) 表的下标就是**码号**。0..255 是单字节字面量，256（清码）与
//      257（结束码）没有对应字串，但必须**占着两个位置**。
//      少占这两个位置，新建字串会落在 256 而不是 258，码号与表下标
//      错开两格：轻则解出错图，重则读到不存在的表项直接崩。
//
//   2) 码宽增长的门槛。编码器建新表项用的是「上一个串」，解码器要等
//      下一个码才知道，所以**解码器永远比编码器少一个表项**：
//      编码器在下一个要分配的码是 512 / 1024 / 2048 时换宽，
//      解码器就要在 nextCode 到达 511 / 1023 / 2047 时换宽。
//      门槛差一格就会整条流错位——症状是「解出来的图全是噪点」，
//      而且不会报错，所以只能拿真实文件（libtiff 写的）来定标。
//      不 early change 的旧做法门槛再晚一格。
bool decodeLzw(const QByteArray& in, bool earlyChange, QByteArray* out) {
    QVector<QByteArray> table;
    table.reserve(4096);
    auto resetTable = [&table]() {
        table.clear();
        for (int code = 0; code < 256; ++code) {
            table.append(QByteArray(1, static_cast<char>(code)));
        }
        table.append(QByteArray());  // 256 = Clear（占位）
        table.append(QByteArray());  // 257 = EOI（占位）
    };
    resetTable();

    int bitPosition = 0;
    const int totalBits = in.size() * 8;
    int codeWidth = 9;
    int nextCode = 258;
    int oldCode = -1;

    auto readCode = [&](int width) -> int {
        if (bitPosition + width > totalBits) {
            return -1;
        }
        int value = 0;
        for (int bit = 0; bit < width; ++bit) {
            const int index = bitPosition + bit;
            const quint8 byte = static_cast<quint8>(in.at(index / 8));
            const int bitValue = (byte >> (7 - (index % 8))) & 1;
            value = (value << 1) | bitValue;
        }
        bitPosition += width;
        return value;
    };

    while (true) {
        const int code = readCode(codeWidth);
        if (code < 0) {
            break;
        }
        if (code == 257) {  // EOI
            break;
        }
        if (code == 256) {  // Clear
            resetTable();
            codeWidth = 9;
            nextCode = 258;
            oldCode = -1;
            continue;
        }
        QByteArray entry;
        if (code < nextCode) {
            entry = table.at(code);
        } else if (code == nextCode && oldCode >= 0 && oldCode < table.size()) {
            // KwKwK 情形：码就是「上一个串 + 上一个串的头」。
            entry = table.at(oldCode);
            entry.append(entry.at(0));
        } else {
            return false;
        }
        out->append(entry);
        if (oldCode >= 0 && nextCode < 4096) {
            QByteArray added = table.at(oldCode);
            added.append(entry.at(0));
            table.append(added);
            ++nextCode;
        }
        oldCode = code;

        // 码宽增长：门槛比编码器早一格（见上面的说明）。
        const int threshold = earlyChange ? (1 << codeWidth) - 1 : (1 << codeWidth);
        if (nextCode >= threshold && codeWidth < 12) {
            ++codeWidth;
        }
    }
    return true;
}

// Deflate（zlib 流）。
//
// 用 qUncompress：它认的是「4 字节长度 + zlib 流」这个 Qt 自家格式，
// 所以先把长度头补上。长度是**算得出来的**：一条 strip 展开后有多少字节，
// 由行宽与该 strip 的行数决定。
bool decodeDeflate(const QByteArray& in, qint64 expectedSize, QByteArray* out) {
    if (expectedSize <= 0 || expectedSize > 256LL * 1024 * 1024) {
        return false;
    }
    QByteArray sized;
    sized.append(static_cast<char>((expectedSize >> 24) & 0xFF));
    sized.append(static_cast<char>((expectedSize >> 16) & 0xFF));
    sized.append(static_cast<char>((expectedSize >> 8) & 0xFF));
    sized.append(static_cast<char>(expectedSize & 0xFF));
    sized.append(in);
    const QByteArray raw = qUncompress(sized);
    if (raw.size() != expectedSize) {
        return false;
    }
    out->append(raw);
    return true;
}

// ------------------------------------------------------------- 页面解析 ----

// 一个 IFD 解析出来的页面参数。
struct TiffPageInfo {
    quint32 width = 0;
    quint32 height = 0;
    quint16 compression = kCompressionNone;
    quint16 photometric = 0;
    quint16 samplesPerPixel = 1;
    quint16 bitsPerSample = 1;
    quint16 rowsPerStrip = 0;
    QVector<quint32> stripOffsets;
    QVector<quint32> stripByteCounts;
    // 每个通道的位深（SamplesPerPixel 个）。
    QVector<quint16> bitsPerSampleList;
    // 调色板原始数据（ColorMap 标签的内容）。
    QByteArray colorMap;
};

// 从一个 IFD 里读出页面参数。
//
// 失败时 detail 里写技术事实（ASCII），由调用方拼人话。
bool readPageInfo(const TiffReader& reader, qint64 ifdOffset, TiffPageInfo* info,
                  QString* detail) {
    const auto fail = [detail](const QString& reason) {
        if (detail != nullptr) {
            *detail = reason;
        }
        return false;
    };
    if (!reader.has(ifdOffset, 2)) {
        return fail(QStringLiteral("TIFF IFD offset is out of range"));
    }
    const quint16 entryCount = reader.u16(ifdOffset);
    if (entryCount == 0) {
        return fail(QStringLiteral("TIFF IFD has no entries"));
    }
    if (!reader.has(ifdOffset + 2, static_cast<qint64>(entryCount) * 12 + 4)) {
        return fail(QStringLiteral("TIFF IFD is truncated"));
    }

    QHash<quint16, IfdEntry> entries;
    for (quint16 index = 0; index < entryCount; ++index) {
        const qint64 entryOffset = ifdOffset + 2 + static_cast<qint64>(index) * 12;
        IfdEntry entry;
        entry.type = reader.u16(entryOffset + 2);
        entry.count = reader.u32(entryOffset + 4);
        entry.valueFieldOffset = entryOffset + 8;
        entries.insert(reader.u16(entryOffset), entry);
    }

    bool ok = true;
    const auto readOne = [&](quint16 tag, quint64* value) {
        const auto it = entries.constFind(tag);
        if (it == entries.constEnd()) {
            return false;
        }
        *value = readEntryValue(reader, it.value(), 0, &ok);
        return ok;
    };
    const auto readList = [&](quint16 tag, QVector<quint64>* values) {
        const auto it = entries.constFind(tag);
        if (it == entries.constEnd()) {
            return false;
        }
        const IfdEntry& entry = it.value();
        values->reserve(static_cast<int>(entry.count));
        for (quint32 index = 0; index < entry.count; ++index) {
            const quint64 value = readEntryValue(reader, entry, index, &ok);
            if (!ok) {
                return false;
            }
            // 注意：值本身可以是 0（合法的数据），所以判断成功与否只能看
            // ok 标志，不能拿返回值当真假 —— 那样会把「读到 0」误当成
            // 「读失败」，把位深、条偏移这类标签全读成 0。
            values->append(value);
        }
        return ok;
    };

    // 分块（tiled）的 TIFF 本程序没有对应的读法：明确失败，不给半张图。
    if (entries.contains(kTagTileOffsets) || entries.contains(kTagTileWidth)) {
        return fail(QStringLiteral("TIFF tiled images are not supported"));
    }
    // 平面布局（每个通道分开存）同样不认。
    quint64 planar = 1;
    if (readOne(kTagPlanarConfig, &planar) && planar != 1) {
        return fail(QStringLiteral("TIFF planar configuration %1 is not supported").arg(planar));
    }
    quint64 fillOrder = 1;
    if (readOne(kTagFillOrder, &fillOrder) && fillOrder != 1) {
        return fail(QStringLiteral("TIFF fill order %1 is not supported").arg(fillOrder));
    }

    quint64 width = 0;
    if (!readOne(kTagImageWidth, &width) || width == 0) {
        return fail(QStringLiteral("TIFF has no valid image width"));
    }
    quint64 height = 0;
    if (!readOne(kTagImageLength, &height) || height == 0) {
        return fail(QStringLiteral("TIFF has no valid image length"));
    }
    if (width > 100000u || height > 100000u) {
        return fail(QStringLiteral("TIFF dimensions out of range %1x%2").arg(width).arg(height));
    }
    if (static_cast<qint64>(width) * height > kMaxPixels) {
        return fail(QStringLiteral("TIFF pixel count too large"));
    }
    info->width = static_cast<quint32>(width);
    info->height = static_cast<quint32>(height);

    quint64 compression = kCompressionNone;
    if (readOne(kTagCompression, &compression)) {
        info->compression = static_cast<quint16>(compression);
    }
    quint64 samples = 1;
    if (readOne(kTagSamplesPerPixel, &samples)) {
        info->samplesPerPixel = static_cast<quint16>(samples);
    }
    if (info->samplesPerPixel == 0 || info->samplesPerPixel > 4) {
        return fail(QStringLiteral("TIFF samples per pixel %1 is not supported")
                        .arg(info->samplesPerPixel));
    }
    quint64 rowsPerStrip = height;
    if (readOne(kTagRowsPerStrip, &rowsPerStrip) && rowsPerStrip > 0) {
        info->rowsPerStrip = static_cast<quint16>(rowsPerStrip);
    }
    if (info->rowsPerStrip == 0) {
        info->rowsPerStrip = static_cast<quint16>(qMin<quint64>(height, 0xFFFFu));
    }

    // 每通道的位深：只有一个通道时 BitsPerSample 可以是标量，
    // 多通道时必须给一个数组。
    QVector<quint64> bitsList;
    if (readList(kTagBitsPerSample, &bitsList) && !bitsList.isEmpty()) {
        for (const quint64 value : bitsList) {
            info->bitsPerSampleList.append(static_cast<quint16>(value));
        }
    }
    if (info->bitsPerSampleList.isEmpty()) {
        quint64 bits = 1;
        if (readOne(kTagBitsPerSample, &bits)) {
            info->bitsPerSampleList.append(static_cast<quint16>(bits));
        }
    }
    if (info->bitsPerSampleList.isEmpty()) {
        return fail(QStringLiteral("TIFF has no bits per sample"));
    }
    info->bitsPerSample = info->bitsPerSampleList.first();

    // 光度解释：没写就按通道数猜（1 通道 = 灰度，3 通道 = RGB）。
    quint64 photometric = 0;
    if (readOne(kTagPhotometric, &photometric)) {
        info->photometric = static_cast<quint16>(photometric);
    } else {
        info->photometric =
            info->samplesPerPixel >= 3 ? kPhotometricRgb : kPhotometricBlackIsZero;
    }
    if (info->photometric == kPhotometricCmyk) {
        return fail(QStringLiteral("TIFF CMYK images are not supported"));
    }

    QVector<quint64> offsets;
    if (!readList(kTagStripOffsets, &offsets) || offsets.isEmpty()) {
        return fail(QStringLiteral("TIFF has no strip offsets"));
    }
    QVector<quint64> counts;
    if (!readList(kTagStripByteCounts, &counts) || counts.isEmpty()) {
        return fail(QStringLiteral("TIFF has no strip byte counts"));
    }
    for (const quint64 value : offsets) {
        info->stripOffsets.append(static_cast<quint32>(value));
    }
    for (const quint64 value : counts) {
        info->stripByteCounts.append(static_cast<quint32>(value));
    }
    if (info->stripOffsets.size() != info->stripByteCounts.size()) {
        return fail(QStringLiteral("TIFF strip offsets and byte counts do not match"));
    }

    const auto colorMapEntry = entries.constFind(kTagColorMap);
    if (colorMapEntry != entries.constEnd()) {
        const IfdEntry& entry = colorMapEntry.value();
        const qint64 size = static_cast<qint64>(fieldTypeSize(entry.type)) * entry.count;
        const qint64 base = size <= 4 ? entry.valueFieldOffset
                                      : static_cast<qint64>(reader.u32(entry.valueFieldOffset));
        if (!reader.has(base, size)) {
            return fail(QStringLiteral("TIFF color map is out of range"));
        }
        info->colorMap = reader.data().mid(static_cast<qsizetype>(base),
                                           static_cast<qsizetype>(size));
    }
    // 读任何一个条目失败都要当错误：静默当成「标签不存在」会把坏文件
    // 解成一张尺寸乱掉的图，而不是明确失败。
    if (!ok) {
        return fail(QStringLiteral("TIFF entry data is out of range"));
    }
    return true;
}

// ------------------------------------------------------------- 像素展开 ----

// 读一行里的第 x 个取样（位深小于 8 时按位取，高位在前）。
quint32 readSample(const quint8* row, int x, int bitDepth) {
    switch (bitDepth) {
    case 1:
        return (row[x / 8] >> (7 - (x % 8))) & 0x01u;
    case 2:
        return (row[x / 4] >> (6 - 2 * (x % 4))) & 0x03u;
    case 4:
        return (row[x / 2] >> (4 - 4 * (x % 2))) & 0x0Fu;
    case 8:
        return row[x];
    case 16:
        return (static_cast<quint32>(row[x * 2]) << 8) | row[x * 2 + 1];
    default:
        return 0;
    }
}

// 把一个取样缩到 8 位。
quint8 scaleSample(quint32 value, int bitDepth) {
    switch (bitDepth) {
    case 1:
        return static_cast<quint8>(value * 255u);
    case 2:
        return static_cast<quint8>(value * 85u);
    case 4:
        return static_cast<quint8>(value * 17u);
    case 16:
        return static_cast<quint8>(value >> 8);
    default:
        return static_cast<quint8>(value);
    }
}

// 调色板：ColorMap 里是「先全部红、再全部绿、再全部蓝」，
// 每个分量 16 位；索引对应的那一档要 >> 8 取高字节。
//
// 档数按文件里**实际给了多少**算，而不是按 2^位深 算：规范要求给满
// 2^位深 档，但确实有写程序只存了用得上的那几档。按实际档数排布，
// 遇到越界的索引再明确失败。
bool paletteColor(const QByteArray& colorMap, int bitDepth, quint32 index, quint8* rgb,
                  bool littleEndian) {
    if (bitDepth < 1 || bitDepth > 16) {
        return false;
    }
    const qint64 entries = colorMap.size() / 6;  // 每档 3 个 16 位分量
    if (entries <= 0 || index >= static_cast<quint64>(entries)) {
        return false;
    }
    const auto read16 = [&](qint64 offset) -> quint32 {
        const quint8 a = static_cast<quint8>(colorMap.at(static_cast<qsizetype>(offset)));
        const quint8 b = static_cast<quint8>(colorMap.at(static_cast<qsizetype>(offset + 1)));
        return littleEndian ? (static_cast<quint32>(a) | (static_cast<quint32>(b) << 8))
                            : ((static_cast<quint32>(a) << 8) | b);
    };
    const qint64 redAt = static_cast<qint64>(index) * 2;
    const qint64 greenAt = static_cast<qint64>(entries + index) * 2;
    const qint64 blueAt = static_cast<qint64>(2 * entries + index) * 2;
    rgb[0] = static_cast<quint8>(read16(redAt) >> 8);
    rgb[1] = static_cast<quint8>(read16(greenAt) >> 8);
    rgb[2] = static_cast<quint8>(read16(blueAt) >> 8);
    return true;
}

// 把解压后的行数据展开成 RGB。
bool expandToRgb(const TiffPageInfo& info, const QByteArray& raw, bool littleEndian,
                 QByteArray* rgb, QString* detail) {
    const auto fail = [detail](const QString& reason) {
        if (detail != nullptr) {
            *detail = reason;
        }
        return false;
    };
    const int width = static_cast<int>(info.width);
    const int height = static_cast<int>(info.height);
    const int samples = info.samplesPerPixel;
    const int bitDepth = info.bitsPerSample;

    // 每个通道的位深必须一致（TIFF 允许不等，但本程序不认这种布局）。
    for (const quint16 bits : info.bitsPerSampleList) {
        if (bits != info.bitsPerSample) {
            return fail(QStringLiteral("TIFF samples with different bit depths are not supported"));
        }
    }

    const qint64 bitsPerPixel = static_cast<qint64>(samples) * bitDepth;
    const qint64 rowBytes = (static_cast<qint64>(width) * bitsPerPixel + 7) / 8;
    if (raw.size() < rowBytes * height) {
        return fail(QStringLiteral("TIFF image data is truncated (expected %1 bytes, got %2)")
                        .arg(rowBytes * height)
                        .arg(raw.size()));
    }

    rgb->resize(static_cast<qsizetype>(static_cast<qint64>(width) * height * 3));
    char* target = rgb->data();

    for (int y = 0; y < height; ++y) {
        const quint8* row = reinterpret_cast<const quint8*>(raw.constData() +
                                                             static_cast<qint64>(y) * rowBytes);
        char* out = target + static_cast<qint64>(y) * width * 3;
        for (int x = 0; x < width; ++x) {
            quint8 red = 0;
            quint8 green = 0;
            quint8 blue = 0;
            if (info.photometric == kPhotometricRgb) {
                red = scaleSample(readSample(row, x * samples, bitDepth), bitDepth);
                green = scaleSample(readSample(row, x * samples + 1, bitDepth), bitDepth);
                blue = scaleSample(readSample(row, x * samples + 2, bitDepth), bitDepth);
            } else if (info.photometric == kPhotometricPalette) {
                const quint32 index = readSample(row, x, bitDepth);
                quint8 color[3] = {0, 0, 0};
                if (!paletteColor(info.colorMap, bitDepth, index, color, littleEndian)) {
                    return fail(QStringLiteral("TIFF palette is missing or too short"));
                }
                red = color[0];
                green = color[1];
                blue = color[2];
            } else {
                // 灰度：白为最小（PhotometricInterpretation = 0）要按规范反相。
                quint8 gray = scaleSample(readSample(row, x, bitDepth), bitDepth);
                if (info.photometric == kPhotometricWhiteIsZero) {
                    gray = static_cast<quint8>(255 - gray);
                }
                red = gray;
                green = gray;
                blue = gray;
            }
            out[x * 3] = static_cast<char>(red);
            out[x * 3 + 1] = static_cast<char>(green);
            out[x * 3 + 2] = static_cast<char>(blue);
        }
    }
    return true;
}

// 解一页：把各条 strip 解压、拼起来，再展开成 RGB。
bool decodePage(const TiffReader& reader, const TiffPageInfo& info, bool littleEndian,
                TiffPage* page, QString* detail) {
    const auto fail = [detail](const QString& reason) {
        if (detail != nullptr) {
            *detail = reason;
        }
        return false;
    };
    const int width = static_cast<int>(info.width);
    const int height = static_cast<int>(info.height);
    const int samples = info.samplesPerPixel;
    const int bitDepth = info.bitsPerSample;
    const qint64 bitsPerPixel = static_cast<qint64>(samples) * bitDepth;
    const qint64 rowBytes = (static_cast<qint64>(width) * bitsPerPixel + 7) / 8;

    QByteArray raw;
    raw.reserve(static_cast<qsizetype>(rowBytes * height));
    int rowCursor = 0;
    for (int strip = 0; strip < info.stripOffsets.size(); ++strip) {
        const qint64 offset = info.stripOffsets.at(strip);
        const qint64 count = info.stripByteCounts.at(strip);
        if (!reader.has(offset, count)) {
            return fail(QStringLiteral("TIFF strip %1 is out of range").arg(strip));
        }
        const QByteArray compressed =
            reader.data().mid(static_cast<qsizetype>(offset), static_cast<qsizetype>(count));
        const int rowsInStrip = qMin<int>(info.rowsPerStrip, height - rowCursor);
        if (rowsInStrip <= 0) {
            break;
        }
        const qint64 expected = rowBytes * rowsInStrip;
        QByteArray plain;
        bool decoded = true;
        switch (info.compression) {
        case kCompressionNone:
            plain = compressed;
            if (plain.size() < expected) {
                return fail(QStringLiteral("TIFF strip %1 is truncated").arg(strip));
            }
            break;
        case kCompressionPackBits:
            decoded = decodePackBits(compressed, &plain);
            break;
        case kCompressionLzw: {
            // 先按 TIFF 的 early change 解；解不出来或不够长，再按
            // 「不 early change」的旧做法解一遍。
            QByteArray early;
            const bool earlyOk = decodeLzw(compressed, true, &early);
            QByteArray legacy;
            const bool legacyOk = decodeLzw(compressed, false, &legacy);
            if (earlyOk && early.size() >= expected) {
                plain = early;
            } else if (legacyOk && legacy.size() >= expected) {
                plain = legacy;
            } else {
                decoded = false;
            }
            break;
        }
        case kCompressionDeflate:
        case kCompressionDeflateLegacy:
            decoded = decodeDeflate(compressed, expected, &plain);
            break;
        default:
            return fail(QStringLiteral("TIFF compression %1 is not supported")
                            .arg(info.compression));
        }
        if (!decoded) {
            return fail(QStringLiteral("TIFF strip %1 could not be decompressed").arg(strip));
        }
        if (plain.size() < expected) {
            return fail(QStringLiteral("TIFF strip %1 is shorter than expected").arg(strip));
        }
        raw.append(plain.constData(), static_cast<qsizetype>(expected));
        rowCursor += rowsInStrip;
        if (rowCursor >= height) {
            break;
        }
    }
    if (rowCursor < height) {
        return fail(QStringLiteral("TIFF strips cover %1 of %2 rows").arg(rowCursor).arg(height));
    }

    page->width = width;
    page->height = height;
    if (!expandToRgb(info, raw, littleEndian, &page->rgb, detail)) {
        return false;
    }
    return true;
}

}  // namespace

bool decodeTiff(const QByteArray& bytes, QVector<TiffPage>* out, QString* detail) {
    const auto fail = [detail](const QString& reason) {
        if (detail != nullptr) {
            *detail = reason;
        }
        return false;
    };
    if (out == nullptr) {
        return fail(QStringLiteral("no output buffer"));
    }
    if (bytes.size() < 8) {
        return fail(QStringLiteral("TIFF is too short"));
    }
    const quint8 marker0 = static_cast<quint8>(bytes.at(0));
    const quint8 marker1 = static_cast<quint8>(bytes.at(1));
    bool littleEndian = false;
    if (marker0 == 'I' && marker1 == 'I') {
        littleEndian = true;
    } else if (marker0 == 'M' && marker1 == 'M') {
        littleEndian = false;
    } else {
        return fail(QStringLiteral("TIFF missing byte order marker"));
    }
    TiffReader reader(bytes, littleEndian);
    const quint16 magic = reader.u16(2);
    if (magic == 43) {
        return fail(QStringLiteral("BigTIFF is not supported"));
    }
    if (magic != 42) {
        return fail(QStringLiteral("TIFF magic number is %1, expected 42").arg(magic));
    }

    QVector<TiffPage> pages;
    qint64 ifdOffset = reader.u32(4);
    int guard = 0;
    while (ifdOffset != 0) {
        if (++guard > kMaxPages) {
            return fail(QStringLiteral("TIFF has too many pages"));
        }
        if (pages.size() >= kMaxPages) {
            return fail(QStringLiteral("TIFF has too many pages"));
        }
        if (!reader.has(ifdOffset, 2)) {
            return fail(QStringLiteral("TIFF IFD offset is out of range"));
        }
        TiffPageInfo info;
        if (!readPageInfo(reader, ifdOffset, &info, detail)) {
            return false;
        }
        TiffPage page;
        if (!decodePage(reader, info, littleEndian, &page, detail)) {
            return false;
        }
        pages.append(page);

        // 下一个 IFD：IFD 结尾的 4 个字节。
        const quint16 entryCount = reader.u16(ifdOffset);
        const qint64 nextField = ifdOffset + 2 + static_cast<qint64>(entryCount) * 12;
        if (!reader.has(nextField, 4)) {
            return fail(QStringLiteral("TIFF IFD chain is truncated"));
        }
        const quint32 next = reader.u32(nextField);
        if (next == ifdOffset) {
            return fail(QStringLiteral("TIFF IFD chain points to itself"));
        }
        ifdOffset = next;
    }
    if (pages.isEmpty()) {
        return fail(QStringLiteral("TIFF has no pages"));
    }
    out->append(pages);
    return true;
}

}  // namespace aipdf::pdf
