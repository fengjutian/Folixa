#include "pdf/content/TrueTypeSubset.h"

#include <QHash>
#include <QVector>

#include <algorithm>

namespace aipdf::pdf {

namespace {

constexpr quint32 kTagHead = 0x68656164;  // 'head'
constexpr quint32 kTagMaxp = 0x6D617870;  // 'maxp'
constexpr quint32 kTagLoca = 0x6C6F6361;  // 'loca'
constexpr quint32 kTagGlyf = 0x676C7966;  // 'glyf'
constexpr quint32 kTagHhea = 0x68686561;  // 'hhea'
constexpr quint32 kTagHmtx = 0x686D7478;  // 'hmtx'
constexpr quint32 kTagCmap = 0x636D6170;  // 'cmap'
constexpr quint32 kTagVhea = 0x76686561;  // 'vhea'
constexpr quint32 kTagVmtx = 0x766D7478;  // 'vmtx'
constexpr quint32 kTagSfnt = 0x00010000;  // TrueType 1.0
constexpr quint32 kTagTrue = 0x74727565;  // 'true'
constexpr quint32 kTagTtc = 0x74746366;   // 'ttcf'
constexpr quint32 kTagOttO = 0x4F54544F;  // 'OTTO'（CFF 轮廓，不支持）

quint16 readU16(const QByteArray& data, int offset) {
    if (offset < 0 || offset + 2 > data.size()) {
        return 0;
    }
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData()) + offset;
    return static_cast<quint16>((static_cast<quint16>(bytes[0]) << 8) | bytes[1]);
}

quint32 readU32(const QByteArray& data, int offset) {
    if (offset < 0 || offset + 4 > data.size()) {
        return 0;
    }
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData()) + offset;
    return (static_cast<quint32>(bytes[0]) << 24) | (static_cast<quint32>(bytes[1]) << 16) |
           (static_cast<quint32>(bytes[2]) << 8) | static_cast<quint32>(bytes[3]);
}

void appendU16(QByteArray& out, quint16 value) {
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

void appendU32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>((value >> 24) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

// 一张表在文件里的位置。
struct TableRecord {
    quint32 tag = 0;
    int offset = 0;
    int length = 0;
};

// 一份 sfnt 字体（.ttf 或 .ttc 里的一份）。
struct FontFile {
    quint32 version = 0;
    QVector<TableRecord> tables;

    const TableRecord* find(quint32 tag) const {
        for (const TableRecord& table : tables) {
            if (table.tag == tag) {
                return &table;
            }
        }
        return nullptr;
    }
};

// 解析 sfnt 目录。ttc 取第一份字体。
bool parseFontFile(const QByteArray& data, FontFile* font, QString* error) {
    if (data.size() < 12) {
        *error = QStringLiteral("字体资源无效：文件太小，不像是字体");
        return false;
    }
    quint32 version = readU32(data, 0);
    int base = 0;
    if (version == kTagTtc) {
        const quint32 fontCount = readU32(data, 8);
        if (fontCount == 0) {
            *error = QStringLiteral("字体资源无效：字体集合是空的");
            return false;
        }
        base = static_cast<int>(readU32(data, 12));
        if (base < 0 || base + 12 > data.size()) {
            *error = QStringLiteral("字体资源无效：字体集合的索引越界");
            return false;
        }
        version = readU32(data, base);
    }
    if (version == kTagOttO) {
        *error = QStringLiteral("字体资源无效：OpenType/CFF 字体没有 glyf/loca，无法子集化");
        return false;
    }
    if (version != kTagSfnt && version != kTagTrue) {
        *error = QStringLiteral("字体资源无效：不是 TrueType 字体");
        return false;
    }
    font->version = version;

    const int tableCount = readU16(data, base + 4);
    for (int index = 0; index < tableCount; ++index) {
        const int entry = base + 12 + index * 16;
        if (entry + 16 > data.size()) {
            break;
        }
        TableRecord record;
        record.tag = readU32(data, entry);
        record.offset = static_cast<int>(readU32(data, entry + 8));
        record.length = static_cast<int>(readU32(data, entry + 12));
        if (record.offset < 0 || record.length < 0 ||
            record.offset + record.length > data.size()) {
            continue;
        }
        font->tables.append(record);
    }
    return !font->tables.isEmpty();
}

// 表的校验和：按大端 32 位累加，末尾不足 4 字节补零。
quint32 tableChecksum(const QByteArray& data) {
    quint32 sum = 0;
    const int count = data.size() / 4;
    for (int index = 0; index < count; ++index) {
        sum += readU32(data, index * 4);
    }
    const int remainder = data.size() % 4;
    if (remainder != 0) {
        quint32 tail = 0;
        for (int index = 0; index < remainder; ++index) {
            tail = (tail << 8) |
                   static_cast<quint32>(static_cast<quint8>(data.at(count * 4 + index)));
        }
        sum += tail;
    }
    return sum;
}

// 读 loca 表：短格式存的是「偏移 / 2」，长格式存真实偏移。
QVector<quint32> readLoca(const QByteArray& data, const TableRecord& loca, int glyphCount,
                           bool longFormat, QString* error) {
    QVector<quint32> offsets(glyphCount + 1, 0);
    for (int gid = 0; gid <= glyphCount; ++gid) {
        if (longFormat) {
            const int at = loca.offset + gid * 4;
            if (at + 4 > data.size()) {
                *error = QStringLiteral("字体资源无效：loca 表越界");
                return {};
            }
            offsets[gid] = readU32(data, at);
        } else {
            const int at = loca.offset + gid * 2;
            if (at + 2 > data.size()) {
                *error = QStringLiteral("字体资源无效：loca 表越界");
                return {};
            }
            offsets[gid] = static_cast<quint32>(readU16(data, at)) * 2;
        }
    }
    return offsets;
}

// 有符号的 16 位读法。
//
// 复合字形的 numberOfContours 是 **-1**；用无符号读法会变成 65535，
// 于是「>= 0」永远成立，复合字形会被误判成简单字形、组件也就没被带上。
qint16 readS16(const QByteArray& data, int offset) {
    return static_cast<qint16>(readU16(data, offset));
}

// 复合字形里的组件字形号；组件自己可能也是复合字形，所以要递归。
//
// glyf 条目：numberOfContours < 0 就是复合字形，后面是一串「标志 + 字形号 + 参数」。
void collectComponents(const QByteArray& data, const QVector<quint32>& loca, const TableRecord& glyf,
                       int gid, QSet<int>* keep) {
    if (gid < 0 || gid + 1 >= loca.size()) {
        return;
    }
    const int start = glyf.offset + static_cast<int>(loca.at(gid));
    const int end = glyf.offset + static_cast<int>(loca.at(gid + 1));
    if (start < 0 || end <= start || end > data.size()) {
        return;
    }
    if (readS16(data, start) >= 0) {
        return;  // 简单字形：没有组件
    }
    int at = start + 10;  // numberOfContours(2) + xMin/yMin/xMax/yMax(8)
    while (at + 4 <= end) {
        const quint16 flags = readU16(data, at);
        const int componentGid = readU16(data, at + 2);
        at += 4;
        at += (flags & 0x0001) ? 4 : 2;  // ARG_1_AND_2_ARE_WORDS
        if (flags & 0x0008) {
            at += 2;  // WE_HAVE_A_SCALE
        } else if (flags & 0x0040) {
            at += 4;  // WE_HAVE_AN_X_AND_Y_SCALE
        } else if (flags & 0x0080) {
            at += 8;  // WE_HAVE_A_TWO_BY_TWO
        }
        if (componentGid >= 0 && !keep->contains(componentGid)) {
            keep->insert(componentGid);
            collectComponents(data, loca, glyf, componentGid, keep);
        }
        if ((flags & 0x0020) == 0) {
            break;  // MORE_COMPONENTS
        }
    }
}

// 把一批表写成一份新的 sfnt 文件。
//
// 表目录必须按 tag 排序（TrueType 规范要求），表数据按 4 字节对齐。
QByteArray writeFontFile(quint32 version, const QVector<QPair<quint32, QByteArray>>& tables) {
    QVector<QPair<quint32, QByteArray>> sorted = tables;
    std::sort(sorted.begin(), sorted.end(),
              [](const QPair<quint32, QByteArray>& left, const QPair<quint32, QByteArray>& right) {
                  return left.first < right.first;
              });

    const int tableCount = sorted.size();
    int selector = 0;
    while ((1 << (selector + 1)) <= tableCount) {
        ++selector;
    }
    const int searchRange = 16 * (1 << selector);
    const int rangeShift = tableCount * 16 - searchRange;

    const int headerSize = 12 + tableCount * 16;
    QVector<int> offsets(tableCount, 0);
    int cursor = headerSize;
    for (int index = 0; index < tableCount; ++index) {
        offsets[index] = cursor;
        cursor += ((sorted.at(index).second.size() + 3) / 4) * 4;
    }

    QByteArray out;
    out.reserve(cursor);
    appendU32(out, version);
    appendU16(out, static_cast<quint16>(tableCount));
    appendU16(out, static_cast<quint16>(searchRange));
    appendU16(out, static_cast<quint16>(selector));
    appendU16(out, static_cast<quint16>(rangeShift));
    for (int index = 0; index < tableCount; ++index) {
        appendU32(out, sorted.at(index).first);
        appendU32(out, tableChecksum(sorted.at(index).second));
        appendU32(out, static_cast<quint32>(offsets.at(index)));
        appendU32(out, static_cast<quint32>(sorted.at(index).second.size()));
    }
    for (int index = 0; index < tableCount; ++index) {
        QByteArray payload = sorted.at(index).second;
        while (payload.size() % 4 != 0) {
            payload.append('\0');
        }
        out.append(payload);
    }
    return out;
}

}  // namespace

// 读一份字体的竖排度量（vhea / vmtx）。
//
// 表结构（TrueType 规范）：
//   * vhea 与 hhea 一样是 36 字节的定长表，字段顺序也一致：
//     版本(2+2) / ascender(4) / descender(6) / lineGap(8) / …
//     / metricDataFormat(32) / numberOfVMetrics(34)。
//   * vmtx：前 numberOfVMetrics 个字形各占 4 字节（advanceHeight uint16 +
//     topSideBearing int16）；剩下的字形只留 advanceHeight，
//     **沿用最后一条记录的 advance**（规范如此），尾部的 int16 数组是 topSideBearing。
//
// 竖排 advance 是「字身框的高度」，不是横排宽度：中文竖排里一个字占满一个 em，
// 标点与全角字的框高一致，所以竖排 advance 基本都是 1 em。
TrueTypeVerticalMetrics readTrueTypeVerticalMetrics(const QByteArray& fontBytes) {
    TrueTypeVerticalMetrics result;

    FontFile font;
    QString error;
    if (!parseFontFile(fontBytes, &font, &error)) {
        result.error = error;
        return result;
    }

    const TableRecord* head = font.find(kTagHead);
    const TableRecord* maxp = font.find(kTagMaxp);
    const TableRecord* vhea = font.find(kTagVhea);
    const TableRecord* vmtx = font.find(kTagVmtx);
    if (vhea == nullptr || vmtx == nullptr) {
        result.error = QStringLiteral("该字体没有竖排度量：缺少 vhea / vmtx 表");
        return result;
    }
    if (head == nullptr || head->length < 54) {
        result.error = QStringLiteral("字体资源无效：head 表不完整，读不出 unitsPerEm");
        return result;
    }
    if (vhea->length < 36) {
        result.error = QStringLiteral("字体资源无效：vhea 表太短，读不出竖排度量");
        return result;
    }

    result.unitsPerEm = readU16(fontBytes, head->offset + 18);
    if (result.unitsPerEm <= 0) {
        result.unitsPerEm = 1000;
    }
    result.ascent = readS16(fontBytes, vhea->offset + 4);
    result.descent = readS16(fontBytes, vhea->offset + 6);
    result.lineGap = readS16(fontBytes, vhea->offset + 8);
    result.numberOfVMetrics = readU16(fontBytes, vhea->offset + 34);

    if (maxp != nullptr && maxp->length >= 6) {
        result.glyphCount = readU16(fontBytes, maxp->offset + 4);
    }
    if (result.glyphCount <= 0) {
        result.error = QStringLiteral("字体资源无效：maxp 表里没有字形数");
        return result;
    }
    if (result.numberOfVMetrics <= 0 || result.numberOfVMetrics > result.glyphCount) {
        result.error = QStringLiteral("字体资源无效：vhea 里的 numberOfVMetrics 非法");
        return result;
    }
    if (vmtx->length < result.numberOfVMetrics * 4) {
        result.error = QStringLiteral("字体资源无效：vmtx 表太短，放不下竖排度量");
        return result;
    }

    // 前 numberOfVMetrics 个字形各自有 advanceHeight；其余沿用最后一条。
    QVector<int> heights(result.glyphCount, 0);
    int lastAdvance = 0;
    for (int gid = 0; gid < result.numberOfVMetrics; ++gid) {
        const int advance = readU16(fontBytes, vmtx->offset + gid * 4);
        heights[gid] = advance;
        lastAdvance = advance;
    }
    for (int gid = result.numberOfVMetrics; gid < result.glyphCount; ++gid) {
        heights[gid] = lastAdvance;
    }

    result.advanceHeights = heights;
    result.hasVerticalMetrics = true;
    return result;
}

TrueTypeSubsetResult subsetTrueTypeFont(const QByteArray& fontBytes, const QSet<int>& usedGids) {
    TrueTypeSubsetResult result;
    result.originalBytes = fontBytes.size();

    FontFile font;
    QString error;
    if (!parseFontFile(fontBytes, &font, &error)) {
        result.error = error;
        return result;
    }
    const TableRecord* head = font.find(kTagHead);
    const TableRecord* maxp = font.find(kTagMaxp);
    const TableRecord* locaTable = font.find(kTagLoca);
    const TableRecord* glyfTable = font.find(kTagGlyf);
    if (head == nullptr || maxp == nullptr || locaTable == nullptr || glyfTable == nullptr) {
        result.error = QStringLiteral("字体资源无效：缺少 head / maxp / loca / glyf 表，无法子集化");
        return result;
    }

    const int glyphCount = readU16(fontBytes, maxp->offset + 4);
    if (glyphCount <= 0) {
        result.error = QStringLiteral("字体资源无效：字体里没有字形");
        return result;
    }
    result.glyphCount = glyphCount;

    // head 里的 indexToLocFormat：0 = 短格式（偏移 / 2），1 = 长格式。
    //
    // 只认 0 与 1。读到别的值说明 head 表本身不可信（表目录对不齐、文件被截过），
    // 这时宁可报错让调用方退回「整份嵌入」，也不要按猜出来的格式去读偏移——
    // 读错偏移会让子集里少几个字形，中文直接画成空白。
    if (head->length < 54) {
        result.error = QStringLiteral("字体资源无效：head 表不完整，无法确定 loca 格式");
        return result;
    }
    const qint16 indexToLocFormat = readS16(fontBytes, head->offset + 50);
    if (indexToLocFormat != 0 && indexToLocFormat != 1) {
        result.error = QStringLiteral("字体资源无效：head 表里的 loca 格式标记非法，字体可能已损坏");
        return result;
    }
    const bool longLoca = indexToLocFormat == 1;
    const QVector<quint32> loca = readLoca(fontBytes, *locaTable, glyphCount, longLoca, &error);
    if (loca.isEmpty()) {
        result.error = error;
        return result;
    }

    // 要保留的字形：.notdef（0）永远留着，用到的那些，加上复合字形的组件。
    QSet<int> keep;
    keep.insert(0);
    for (int gid : usedGids) {
        if (gid > 0 && gid < glyphCount) {
            keep.insert(gid);
        }
    }
    const QVector<int> initiallyKept = keep.values();
    for (int gid : initiallyKept) {
        collectComponents(fontBytes, loca, *glyfTable, gid, &keep);
    }

    // 新的 glyf：按字形号顺序把要保留的字形数据搬过去（每个补齐到偶数字节）。
    QByteArray newGlyf;
    QVector<quint32> newOffsets(glyphCount + 1, 0);
    for (int gid = 0; gid < glyphCount; ++gid) {
        newOffsets[gid] = static_cast<quint32>(newGlyf.size());
        if (!keep.contains(gid)) {
            continue;
        }
        const int start = glyfTable->offset + static_cast<int>(loca.at(gid));
        const int end = glyfTable->offset + static_cast<int>(loca.at(gid + 1));
        if (start < 0 || end < start || end > fontBytes.size()) {
            continue;
        }
        QByteArray glyph = fontBytes.mid(start, end - start);
        while (glyph.size() % 2 != 0) {
            glyph.append('\0');
        }
        newGlyf.append(glyph);
    }
    newOffsets[glyphCount] = static_cast<quint32>(newGlyf.size());
    // 没保留的字形，其 loca 指向下一个保留字形的起点 → 长度为零的空字形。
    quint32 nextKept = newOffsets[glyphCount];
    for (int gid = glyphCount - 1; gid >= 0; --gid) {
        if (keep.contains(gid)) {
            nextKept = newOffsets.at(gid);
        } else {
            newOffsets[gid] = nextKept;
        }
    }

    // 新的 loca：新 glyf 超过 128 KB 时必须用长格式（短格式存不下偏移）。
    const bool useLongLoca = newGlyf.size() > 0x1FFFE;
    QByteArray newLoca;
    for (int gid = 0; gid <= glyphCount; ++gid) {
        if (useLongLoca) {
            appendU32(newLoca, newOffsets.at(gid));
        } else {
            appendU16(newLoca, static_cast<quint16>(newOffsets.at(gid) / 2));
        }
    }

    // 新的 head：indexToLocFormat 要跟着换，checkSumAdjustment 先清零（最后再算）。
    QByteArray newHead = fontBytes.mid(head->offset, head->length);
    if (newHead.size() >= 54) {
        newHead[50] = static_cast<char>(useLongLoca ? 1 : 0);
        newHead[51] = '\0';
        for (int index = 8; index < 12; ++index) {
            newHead[index] = '\0';
        }
    } else {
        result.error = QStringLiteral("字体资源无效：head 表太短");
        return result;
    }

    // 其余表原样搬过去。
    //
    // vhea / vmtx 也在其中：子集化**不改动字形号**（没用的字形留成空壳），
    // 所以 vmtx 里按字形号记的竖排 advance 依然对得上，不必重写。
    QVector<QPair<quint32, QByteArray>> tables;
    for (const TableRecord& record : font.tables) {
        if (record.tag == kTagGlyf || record.tag == kTagLoca || record.tag == kTagHead) {
            continue;
        }
        tables.append(qMakePair(record.tag, fontBytes.mid(record.offset, record.length)));
    }
    tables.append(qMakePair(kTagHead, newHead));
    tables.append(qMakePair(kTagLoca, newLoca));
    tables.append(qMakePair(kTagGlyf, newGlyf));

    QByteArray output = writeFontFile(font.version, tables);

    // head.checkSumAdjustment = 0xB1B0AFBA - 整份字体的校验和。
    //
    // 不算也不会影响 PDFium，但算了才是**合法**的 TrueType 文件：
    // 有些工具（字体校验器、其它阅读器）会核对这一项。
    //
    // 做法：重新读一遍刚写出的文件的表目录，找到 head 的数据偏移，把调整量写进去。
    FontFile written;
    QString parseError;
    if (parseFontFile(output, &written, &parseError)) {
        const TableRecord* writtenHead = written.find(kTagHead);
        if (writtenHead != nullptr && writtenHead->length >= 12) {
            const quint32 adjustment = 0xB1B0AFBAu - tableChecksum(output);
            const int at = writtenHead->offset + 8;
            output[at] = static_cast<char>((adjustment >> 24) & 0xFF);
            output[at + 1] = static_cast<char>((adjustment >> 16) & 0xFF);
            output[at + 2] = static_cast<char>((adjustment >> 8) & 0xFF);
            output[at + 3] = static_cast<char>(adjustment & 0xFF);
        }
    }

    result.keptGlyphCount = keep.size();
    result.bytes = output;
    result.ok = !output.isEmpty();
    if (!result.ok) {
        result.error = QStringLiteral("子集化失败：写出的字体是空的");
    }
    return result;
}

}  // namespace aipdf::pdf
