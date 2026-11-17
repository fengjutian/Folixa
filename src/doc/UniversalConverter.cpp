#include "doc/UniversalConverter.h"

#include "pdf/ContentStream.h"

#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace aipdf::doc {

namespace {

// ============================================================ 字宽表 =========
//
// 标准 14 号字体的字宽（单位：1/1000 em），只覆盖 ASCII 32..126。
//
// 为什么自己存表而不用 QFontMetricsF：QFontMetricsF 量的是**本机**字体，
// 同一段文字在开发机与用户机器上量出来的宽度不一样，折行位置就会漂移；
// 折行位置是这份 PDF 可重复性的一部分，不能跟着机器变。
// 嵌入字体走的是字体自己的 hmtx（见 TrueTypeFont），不查这两张表。

const short kHelveticaWidths[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584};

const short kHelveticaBoldWidths[95] = {
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611,
    975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556,
    333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
    611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584};

// Courier 是等宽字体：所有字形都是 600/1000 em。
constexpr short kCourierWidth = 600;

// 标题字号相对正文的倍数（按层级递减）。
double headingScaleFor(int level) {
    switch (level) {
    case 1:
        return 1.60;
    case 2:
        return 1.35;
    case 3:
        return 1.20;
    case 4:
        return 1.10;
    case 5:
        return 1.05;
    default:
        return 1.00;
    }
}

// ==================================================== WinAnsi 编码表 ========
//
// 标准 14 号字体走 WinAnsiEncoding（CP1252）。
// 0x00..0x7F 与 0xA0..0xFF 的编码与码位一致；0x80..0x9F 是 CP1252 特有的。
// 别的字符（中文、日文、emoji……）标准 14 字体根本没有字形。

struct Cp1252Special {
    char byte;
    ushort code;
};

const Cp1252Special kCp1252Specials[] = {
    {'\x80', 0x20AC}, {'\x82', 0x201A}, {'\x83', 0x0192}, {'\x84', 0x201E},
    {'\x85', 0x2026}, {'\x86', 0x2020}, {'\x87', 0x2021}, {'\x88', 0x02C6},
    {'\x89', 0x2030}, {'\x8A', 0x0160}, {'\x8B', 0x2039}, {'\x8C', 0x0152},
    {'\x8E', 0x017D}, {'\x91', 0x2018}, {'\x92', 0x2019}, {'\x93', 0x201C},
    {'\x94', 0x201D}, {'\x95', 0x2022}, {'\x96', 0x2013}, {'\x97', 0x2014},
    {'\x98', 0x02DC}, {'\x99', 0x2122}, {'\x9A', 0x0161}, {'\x9B', 0x203A},
    {'\x9C', 0x0153}, {'\x9E', 0x017E}, {'\x9F', 0x0178},
};

// 这个字符能不能用 WinAnsi 表示；能则写出字节码。
bool winAnsiByteFor(QChar ch, int* byte) {
    const ushort code = ch.unicode();
    if (code >= 0x20 && code <= 0x7E) {
        if (byte != nullptr) *byte = code;
        return true;
    }
    if (code >= 0xA0 && code <= 0xFF) {
        if (byte != nullptr) *byte = code;
        return true;
    }
    for (const Cp1252Special& special : kCp1252Specials) {
        if (special.code == code) {
            if (byte != nullptr) *byte = static_cast<unsigned char>(special.byte);
            return true;
        }
    }
    return false;
}

// ====================================================== UTF-8 解码 ==========

// 手工解 UTF-8：非法序列换成 U+FFFD。
//
// 为什么不直接用 QStringDecoder：不同 Qt 版本的「非法字节怎么处理」
// 选项不完全一致，自己解一遍才能保证「同样的字节永远得到同样的文字」。
QString decodeUtf8(const QByteArray& bytes, bool* hadInvalid) {
    QString out;
    out.reserve(bytes.size());
    bool invalid = false;
    int index = 0;
    const int size = bytes.size();
    while (index < size) {
        const auto lead = static_cast<unsigned char>(bytes.at(index));
        if (lead < 0x80) {
            out.append(QChar(static_cast<ushort>(lead)));
            ++index;
            continue;
        }
        int extra = 0;
        uint code = 0;
        if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            code = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            code = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            code = lead & 0x07u;
        } else {
            out.append(QChar(0xFFFD));
            invalid = true;
            ++index;
            continue;
        }
        if (index + extra >= size) {
            out.append(QChar(0xFFFD));
            invalid = true;
            ++index;
            continue;
        }
        bool wellFormed = true;
        for (int step = 1; step <= extra; ++step) {
            const auto continuation = static_cast<unsigned char>(bytes.at(index + step));
            if ((continuation & 0xC0) != 0x80) {
                wellFormed = false;
                break;
            }
            code = (code << 6) | (continuation & 0x3Fu);
        }
        const bool overlong = (extra == 1 && code < 0x80) || (extra == 2 && code < 0x800) ||
                              (extra == 3 && code < 0x10000);
        if (!wellFormed || overlong || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) {
            out.append(QChar(0xFFFD));
            invalid = true;
            ++index;
            continue;
        }
        if (code > 0xFFFF) {
            const uint adjusted = code - 0x10000;
            out.append(QChar(static_cast<ushort>(0xD800 + (adjusted >> 10))));
            out.append(QChar(static_cast<ushort>(0xDC00 + (adjusted & 0x3FF))));
        } else {
            out.append(QChar(static_cast<ushort>(code)));
        }
        index += extra + 1;
    }
    if (hadInvalid != nullptr) {
        *hadInvalid = invalid;
    }
    return out;
}

// ====================================================== 嵌入字体解析 ========

quint16 readU16(const QByteArray& bytes, int offset) {
    if (offset < 0 || offset + 2 > bytes.size()) {
        return 0;
    }
    return static_cast<quint16>((static_cast<quint8>(bytes.at(offset)) << 8) |
                                static_cast<quint8>(bytes.at(offset + 1)));
}

qint16 readS16(const QByteArray& bytes, int offset) {
    return static_cast<qint16>(readU16(bytes, offset));
}

quint32 readU32(const QByteArray& bytes, int offset) {
    if (offset < 0 || offset + 4 > bytes.size()) {
        return 0;
    }
    return (static_cast<quint32>(static_cast<quint8>(bytes.at(offset))) << 24) |
           (static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 1))) << 16) |
           (static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 2))) << 8) |
           static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 3)));
}

// 一份 TrueType 字体里我们用得上的东西。
//
// 只解析 cmap（码位 → 字形号）、hmtx（字宽）、head/hhea（度量）。
// 字形数据（glyf/loca）不解析也不改动：整份字体原样嵌进 PDF，
// 由阅读器自己去解析字形——本模块不做子集化。
struct TrueTypeFont {
    bool ok = false;
    QString error;
    QByteArray bytes;
    QString postScriptName;
    int unitsPerEm = 1000;
    int ascender = 800;
    int descender = -200;
    int capHeight = 700;
    int bbox[4] = {0, -200, 1000, 900};
    QHash<int, int> unicodeToGid;
    QVector<int> advances;

    int gidForChar(QChar ch) const {
        return unicodeToGid.value(ch.unicode(), 0);
    }

    // 字宽，归一化到 1/1000 em（PDF 的 /W 数组用的就是这个单位）。
    int widthOfGid(int gid) const {
        if (gid < 0 || gid >= advances.size()) {
            return 1000;
        }
        const int scale = unitsPerEm > 0 ? unitsPerEm : 1000;
        return static_cast<int>(static_cast<double>(advances.at(gid)) * 1000.0 / scale + 0.5);
    }
};

// 解析 cmap 子表（格式 4：BMP；格式 12：完整 Unicode）。
void parseCmapSubtable(const QByteArray& bytes, int offset, TrueTypeFont* font) {
    const int format = readU16(bytes, offset);
    if (format == 4) {
        const int segCountX2 = readU16(bytes, offset + 6);
        const int segCount = segCountX2 / 2;
        const int endCodes = offset + 14;
        const int startCodes = endCodes + segCountX2 + 2;
        const int idDeltas = startCodes + segCountX2;
        const int idRangeOffsets = idDeltas + segCountX2;
        const int glyphArray = idRangeOffsets + segCountX2;
        for (int segment = 0; segment < segCount; ++segment) {
            const int endCode = readU16(bytes, endCodes + segment * 2);
            const int startCode = readU16(bytes, startCodes + segment * 2);
            const qint16 idDelta = readS16(bytes, idDeltas + segment * 2);
            const int idRangeOffset = readU16(bytes, idRangeOffsets + segment * 2);
            if (startCode > endCode || startCode == 0xFFFF) {
                continue;
            }
            for (int code = startCode; code <= endCode && code != 0xFFFF; ++code) {
                int gid = 0;
                if (idRangeOffset == 0) {
                    gid = (code + idDelta) & 0xFFFF;
                } else {
                    // idRangeOffset 是相对本段自己位置的偏移，要换算成
                    // glyphIdArray 里的下标。
                    const int index = (idRangeOffset + 2 * (code - startCode) +
                                       (segCount - segment - 1) * 2) / 2;
                    const int at = glyphArray + index * 2;
                    if (at + 2 > bytes.size()) {
                        continue;
                    }
                    gid = readU16(bytes, at);
                    if (gid != 0) {
                        gid = (gid + idDelta) & 0xFFFF;
                    }
                }
                if (gid != 0) {
                    font->unicodeToGid.insert(code, gid);
                }
            }
        }
    } else if (format == 12) {
        const quint32 groupCount = readU32(bytes, offset + 12);
        for (quint32 group = 0; group < groupCount; ++group) {
            const int at = offset + 16 + static_cast<int>(group) * 12;
            if (at + 12 > bytes.size()) {
                break;
            }
            const quint32 startChar = readU32(bytes, at);
            const quint32 endChar = readU32(bytes, at + 4);
            const quint32 startGid = readU32(bytes, at + 8);
            if (endChar < startChar) {
                continue;
            }
            const quint32 span = endChar - startChar;
            if (span > 0x20000) {
                continue;
            }
            for (quint32 code = startChar; code <= endChar; ++code) {
                font->unicodeToGid.insert(static_cast<int>(code),
                                           static_cast<int>(startGid + (code - startChar)));
            }
        }
    }
}

// 从 name 表里取 PostScript 名（nameID 6）。取不到就返回空串。
QString readPostScriptName(const QByteArray& bytes, int offset) {
    if (offset <= 0 || offset + 6 > bytes.size()) {
        return QString();
    }
    const int count = readU16(bytes, offset + 2);
    const int stringOffset = offset + readU16(bytes, offset + 4);
    for (int record = 0; record < count; ++record) {
        const int at = offset + 6 + record * 12;
        if (at + 12 > bytes.size()) {
            break;
        }
        if (readU16(bytes, at + 6) != 6) {
            continue;
        }
        const int platformId = readU16(bytes, at);
        const int nameLength = readU16(bytes, at + 8);
        const int nameOffset = stringOffset + readU16(bytes, at + 10);
        if (nameOffset + nameLength > bytes.size()) {
            continue;
        }
        const QByteArray raw = bytes.mid(nameOffset, nameLength);
        if (platformId == 3 || platformId == 0) {
            // UTF-16BE。
            QString decoded;
            for (int index = 0; index + 1 < raw.size(); index += 2) {
                decoded.append(QChar(static_cast<ushort>((static_cast<quint8>(raw.at(index)) << 8) |
                                                         static_cast<quint8>(raw.at(index + 1)))));
            }
            if (!decoded.isEmpty()) {
                return decoded;
            }
        } else {
            const QString decoded = QString::fromLatin1(raw);
            if (!decoded.isEmpty()) {
                return decoded;
            }
        }
    }
    return QString();
}

// 解析一份 TrueType 字体。
TrueTypeFont parseTrueTypeFont(const QByteArray& bytes) {
    TrueTypeFont font;
    font.bytes = bytes;
    if (bytes.size() < 12) {
        font.error = QStringLiteral("字体资源无效：文件太小，不像是字体");
        return font;
    }

    quint32 tag = readU32(bytes, 0);
    int base = 0;
    if (tag == 0x74746366) {  // 'ttcf'：字体集合，取第一份。
        const quint32 fontCount = readU32(bytes, 8);
        if (fontCount == 0) {
            font.error = QStringLiteral("字体资源无效：字体集合是空的");
            return font;
        }
        base = static_cast<int>(readU32(bytes, 12));
        if (base < 0 || base + 12 > bytes.size()) {
            font.error = QStringLiteral("字体资源无效：字体集合的索引越界");
            return font;
        }
        tag = readU32(bytes, base);
    }
    if (tag == 0x4F54544F) {  // 'OTTO'：CFF 轮廓的 OpenType。
        font.error = QStringLiteral("暂不支持 OpenType/CFF 字体（.otf），请提供 TrueType（.ttf）字体");
        return font;
    }
    if (tag != 0x00010000 && tag != 0x74727565) {  // 0x00010000 或 'true'
        font.error = QStringLiteral("字体资源无效：不是 TrueType 字体");
        return font;
    }

    const int tableCount = readU16(bytes, base + 4);
    QHash<QString, QPair<int, int>> tables;
    for (int index = 0; index < tableCount; ++index) {
        const int entry = base + 12 + index * 16;
        if (entry + 16 > bytes.size()) {
            break;
        }
        const QString name = QString::fromLatin1(bytes.mid(entry, 4));
        const int offset = static_cast<int>(readU32(bytes, entry + 8));
        const int length = static_cast<int>(readU32(bytes, entry + 12));
        if (offset >= 0 && length >= 0 && offset + length <= bytes.size()) {
            tables.insert(name, qMakePair(offset, length));
        }
    }

    if (!tables.contains("head") || !tables.contains("hhea") || !tables.contains("maxp")) {
        font.error = QStringLiteral("字体资源无效：缺少 head / hhea / maxp 表");
        return font;
    }
    if (!tables.contains("cmap")) {
        font.error = QStringLiteral("字体资源无效：缺少 cmap 表，无法把字符映射到字形");
        return font;
    }

    const int headOffset = tables.value("head").first;
    font.unitsPerEm = readU16(bytes, headOffset + 18);
    if (font.unitsPerEm <= 0) {
        font.unitsPerEm = 1000;
    }
    font.bbox[0] = readS16(bytes, headOffset + 36);
    font.bbox[1] = readS16(bytes, headOffset + 38);
    font.bbox[2] = readS16(bytes, headOffset + 40);
    font.bbox[3] = readS16(bytes, headOffset + 42);

    const int hheaOffset = tables.value("hhea").first;
    font.ascender = readS16(bytes, hheaOffset + 4);
    font.descender = readS16(bytes, hheaOffset + 6);
    font.capHeight = font.ascender;

    const int maxpOffset = tables.value("maxp").first;
    const int glyphCount = readU16(bytes, maxpOffset + 4);
    if (glyphCount <= 0) {
        font.error = QStringLiteral("字体资源无效：字体里没有字形");
        return font;
    }

    // 字宽。
    font.advances.resize(glyphCount);
    const int metricCount = readU16(bytes, hheaOffset + 34);
    if (tables.contains("hmtx")) {
        const int hmtxOffset = tables.value("hmtx").first;
        int lastAdvance = 0;
        for (int gid = 0; gid < glyphCount; ++gid) {
            if (gid < metricCount) {
                lastAdvance = readU16(bytes, hmtxOffset + gid * 4);
            }
            font.advances[gid] = lastAdvance;
        }
    } else {
        font.advances.fill(font.unitsPerEm);
    }

    // 码位 → 字形号。
    const int cmapOffset = tables.value("cmap").first;
    const int subtableCount = readU16(bytes, cmapOffset + 2);
    int chosen = -1;
    int chosenScore = -1;
    for (int index = 0; index < subtableCount; ++index) {
        const int record = cmapOffset + 4 + index * 8;
        if (record + 8 > bytes.size()) {
            break;
        }
        const int platformId = readU16(bytes, record);
        const int encodingId = readU16(bytes, record + 2);
        const int offset = cmapOffset + static_cast<int>(readU32(bytes, record + 4));
        if (offset + 4 > bytes.size()) {
            continue;
        }
        const int format = readU16(bytes, offset);
        int score = -1;
        if (platformId == 3 && encodingId == 10 && format == 12) {
            score = 5;
        } else if (platformId == 0 && format == 12) {
            score = 4;
        } else if (platformId == 3 && encodingId == 1 && format == 4) {
            score = 3;
        } else if (platformId == 0 && format == 4) {
            score = 2;
        } else if (format == 4 || format == 12) {
            score = 1;
        }
        if (score > chosenScore) {
            chosenScore = score;
            chosen = offset;
        }
    }
    if (chosen < 0) {
        font.error = QStringLiteral("字体资源无效：cmap 里没有可用的 Unicode 子表");
        return font;
    }
    parseCmapSubtable(bytes, chosen, &font);
    if (font.unicodeToGid.isEmpty()) {
        font.error = QStringLiteral("字体资源无效：cmap 里没有能映射的字符");
        return font;
    }

    if (tables.contains("name")) {
        font.postScriptName = readPostScriptName(bytes, tables.value("name").first);
    }
    if (font.postScriptName.isEmpty()) {
        font.postScriptName = QStringLiteral("AipdfEmbedded");
    }
    // PostScript 名里不能有分隔符。
    static const QString kForbidden = QStringLiteral(" ()/<>[]");
    for (const QChar forbidden : kForbidden) {
        font.postScriptName.remove(forbidden);
    }
    if (font.postScriptName.isEmpty()) {
        font.postScriptName = QStringLiteral("AipdfEmbedded");
    }

    font.ok = true;
    return font;
}

// ====================================================== 字面与编码 ==========

// 一个可用的字面：要么标准 14 字体，要么嵌入字体。
struct FontFace {
    bool embedded = false;
    bool monospace = false;
    // 标准 14 的种类：0=Helvetica 1=Helvetica-Bold 2=Courier 3=Courier-Bold。
    int standardKind = 0;
    const TrueTypeFont* ttf = nullptr;

    // 这个字符的宽度（1/1000 em）。
    int widthOf(QChar ch) const {
        if (embedded && ttf != nullptr) {
            const int gid = ttf->gidForChar(ch);
            return ttf->widthOfGid(gid > 0 ? gid : 0);
        }
        if (monospace) {
            return kCourierWidth;
        }
        const ushort code = ch.unicode();
        if (code >= 32 && code <= 126) {
            return standardKind == 1 ? kHelveticaBoldWidths[code - 32]
                                     : kHelveticaWidths[code - 32];
        }
        // 表外的拉丁字母（重音字母等）按 556 估：估宽一点宁可折早一点。
        return 556;
    }
};

// 一段文字在 PDF 里的编码结果。
struct EncodedText {
    QByteArray bytes;
    int replaced = 0;
    // 用到的「字形号 → 码位」，只有嵌入字体路径需要（ToUnicode 用）。
    QHash<int, int> gidToUnicode;
};

// 标准 14 字体路径：WinAnsi 字面串，显示不了的字符换成 '?'。
EncodedText encodeWinAnsi(const QString& text) {
    EncodedText encoded;
    for (const QChar ch : text) {
        int byte = 0;
        if (winAnsiByteFor(ch, &byte)) {
            encoded.bytes.append(static_cast<char>(byte));
        } else {
            encoded.bytes.append('?');
            ++encoded.replaced;
        }
    }
    return encoded;
}

// 嵌入字体路径：Identity-H 的双字节字形号。
EncodedText encodeGlyphIds(const QString& text, const TrueTypeFont& font) {
    EncodedText encoded;
    for (const QChar ch : text) {
        const int gid = font.gidForChar(ch);
        if (gid <= 0) {
            // 字体里没有这个字形：用 .notdef，并记成一次替换。
            encoded.bytes.append('\0');
            encoded.bytes.append('\0');
            ++encoded.replaced;
            continue;
        }
        encoded.bytes.append(static_cast<char>((gid >> 8) & 0xFF));
        encoded.bytes.append(static_cast<char>(gid & 0xFF));
        if (!encoded.gidToUnicode.contains(gid)) {
            encoded.gidToUnicode.insert(gid, ch.unicode());
        }
    }
    return encoded;
}

// ====================================================== 字体集 ==============

// 本次转换用的字体。
struct FontSet {
    bool embedded = false;
    TrueTypeFont regular;
    TrueTypeFont bold;
    bool hasBold = false;

    // 资源名（F1…）：标准 14 与嵌入字体各有一套映射。
    //
    // 名字带前导斜杠：内容流里写的是资源名（/F1），不是字典键名（F1）。
    QByteArray resourceName(int faceKind) const {
        if (embedded) {
            // 嵌入字体没有等宽版：等宽内容也用同一份嵌入字体。
            return (faceKind == 1 || faceKind == 3) ? QByteArrayLiteral("/F2")
                                                    : QByteArrayLiteral("/F1");
        }
        switch (faceKind) {
        case 1:
            return QByteArrayLiteral("/F2");
        case 2:
            return QByteArrayLiteral("/F3");
        case 3:
            return QByteArrayLiteral("/F4");
        default:
            return QByteArrayLiteral("/F1");
        }
    }

    // 某个字面对应哪份嵌入字体（0=常规 1=粗体）。
    int embeddedIndexFor(int faceKind) const {
        if ((faceKind == 1 || faceKind == 3) && hasBold) {
            return 1;
        }
        return 0;
    }

    const TrueTypeFont* embeddedFontFor(int faceKind) const {
        return embeddedIndexFor(faceKind) == 1 ? &bold : &regular;
    }

    // 量字宽用的字面。
    FontFace faceFor(int faceKind) const {
        FontFace face;
        face.embedded = embedded;
        face.monospace = !embedded && (faceKind == 2 || faceKind == 3);
        face.standardKind = (faceKind == 1 || faceKind == 3) ? 1 : 0;
        if (embedded) {
            face.ttf = embeddedFontFor(faceKind);
        }
        return face;
    }
};

// ============================================================ 文本解析 ======

// 把文本切成行。
//
// 末尾的换行是「终止最后一行」，不是「另起一行空行」：
// "a\n" 是一个文件一行，不是两行。多算一行会让页数凭空多一页。
QStringList splitIntoLines(const QString& text) {
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty() && text.endsWith(QLatin1Char('\n'))) {
        lines.removeLast();
    }
    return lines;
}

// 显示宽度（等宽字体里一个汉字占两格）。
int displayWidthOf(const QString& text) {
    int width = 0;
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        const bool wide = (code >= 0x1100 && code <= 0x115F) ||
                          (code >= 0x2E80 && code <= 0xA4CF) ||
                          (code >= 0xAC00 && code <= 0xD7A3) ||
                          (code >= 0xF900 && code <= 0xFAFF) ||
                          (code >= 0xFE30 && code <= 0xFE6F) ||
                          (code >= 0xFF00 && code <= 0xFF60) ||
                          (code >= 0xFFE0 && code <= 0xFFE6);
        width += wide ? 2 : 1;
    }
    return width;
}

// 把匹配到的片段换成它的第一个捕获组（保留内容，只去掉标记）。
//
// 为什么自己写：QString::remove(正则) 删掉的是**整个匹配**，
// "**粗体**" 会被删成 "" 而不是 "粗体"；QString::replace 又没有「回调」重载。
QString replaceWithFirstCapture(const QString& text, const QRegularExpression& expression) {
    QVector<QPair<int, QPair<int, QString>>> replacements;
    QRegularExpressionMatchIterator iterator = expression.globalMatch(text);
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        replacements.append(qMakePair(
            match.capturedStart(), qMakePair(match.capturedLength(), match.captured(1))));
    }
    QString result = text;
    // 从后往前替换：前面那次的替换不会影响后面已经算好的偏移。
    for (int index = static_cast<int>(replacements.size()) - 1; index >= 0; --index) {
        const QPair<int, QPair<int, QString>>& item = replacements.at(index);
        result.replace(item.first, item.second.first, item.second.second);
    }
    return result;
}

// 去掉行内 Markdown 标记（只去成对的 ** 与 `，单星号不动：
// 「2 * 3」这种算式不该被吃掉）。
QString stripInlineMarkdown(const QString& text) {
    static const QRegularExpression kBold(QStringLiteral("\\*\\*(.+?)\\*\\*"));
    static const QRegularExpression kCode(QStringLiteral("`([^`]*)`"));
    return replaceWithFirstCapture(replaceWithFirstCapture(text, kBold), kCode);
}

// Markdown → 排版行。
QVector<UniversalTextLine> parseMarkdownLines(const QString& text) {
    QVector<UniversalTextLine> lines;
    bool inCodeFence = false;
    const QStringList rawLines = splitIntoLines(text);
    for (const QString& rawLine : rawLines) {
        QString line = rawLine;
        line.remove(QLatin1Char('\r'));
        const QString trimmed = line.trimmed();

        // 代码块围栏：进出的那一行本身不产出内容。
        if (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"))) {
            inCodeFence = !inCodeFence;
            continue;
        }
        if (inCodeFence) {
            UniversalTextLine lineInfo;
            lineInfo.text = line;
            lineInfo.monospace = true;
            lines.append(lineInfo);
            continue;
        }
        if (trimmed.isEmpty()) {
            lines.append(UniversalTextLine());
            continue;
        }

        // 标题：# 到 ######，后面必须跟空格。
        int hashes = 0;
        while (hashes < trimmed.size() && trimmed.at(hashes) == QLatin1Char('#')) {
            ++hashes;
        }
        if (hashes >= 1 && hashes <= 6 && hashes < trimmed.size() &&
            trimmed.at(hashes) == QLatin1Char(' ')) {
            UniversalTextLine lineInfo;
            lineInfo.headingLevel = hashes;
            lineInfo.bold = true;
            lineInfo.text = stripInlineMarkdown(trimmed.mid(hashes + 1).trimmed());
            lines.append(lineInfo);
            continue;
        }

        // 引用块。
        if (trimmed.startsWith(QLatin1Char('>'))) {
            UniversalTextLine lineInfo;
            lineInfo.indentLevel = 1;
            lineInfo.text = stripInlineMarkdown(trimmed.mid(1).trimmed());
            lines.append(lineInfo);
            continue;
        }

        // 前导空白 → 缩进层级（两个空格一级）。
        int spaces = 0;
        while (spaces < line.size() &&
               (line.at(spaces) == QLatin1Char(' ') || line.at(spaces) == QLatin1Char('\t'))) {
            ++spaces;
        }
        const int indentLevel = spaces / 2;
        QString rest = line.mid(spaces);

        // 列表项：- / * / + / 1. / 1)
        bool isListItem = false;
        if (rest.startsWith(QLatin1String("- ")) || rest.startsWith(QLatin1String("* ")) ||
            rest.startsWith(QLatin1String("+ "))) {
            isListItem = true;
            rest = rest.mid(2);
        } else {
            int digits = 0;
            while (digits < rest.size() && rest.at(digits).isDigit()) {
                ++digits;
            }
            if (digits > 0 && digits + 1 < rest.size() &&
                (rest.at(digits) == QLatin1Char('.') || rest.at(digits) == QLatin1Char(')')) &&
                rest.at(digits + 1) == QLatin1Char(' ')) {
                isListItem = true;
                rest = rest.mid(digits + 2);
            }
        }
        if (isListItem) {
            UniversalTextLine lineInfo;
            lineInfo.listItem = true;
            lineInfo.indentLevel = indentLevel;
            lineInfo.text = stripInlineMarkdown(rest.trimmed());
            lines.append(lineInfo);
            continue;
        }

        UniversalTextLine lineInfo;
        lineInfo.indentLevel = indentLevel;
        lineInfo.text = stripInlineMarkdown(rest.trimmed());
        lines.append(lineInfo);
    }
    return lines;
}

// 按逗号切一行 CSV（支持双引号包裹与 "" 转义）。
QStringList splitCsvRow(const QString& row) {
    QStringList fields;
    QString current;
    bool inQuotes = false;
    for (int index = 0; index < row.size(); ++index) {
        const QChar ch = row.at(index);
        if (inQuotes) {
            if (ch == QLatin1Char('"')) {
                if (index + 1 < row.size() && row.at(index + 1) == QLatin1Char('"')) {
                    current.append(QLatin1Char('"'));
                    ++index;
                    continue;
                }
                inQuotes = false;
                continue;
            }
            current.append(ch);
            continue;
        }
        if (ch == QLatin1Char('"') && current.isEmpty()) {
            inQuotes = true;
            continue;
        }
        if (ch == QLatin1Char(',')) {
            fields.append(current);
            current.clear();
            continue;
        }
        current.append(ch);
    }
    fields.append(current);
    return fields;
}

// CSV → 排版行（等宽、按列宽对齐）。
QVector<UniversalTextLine> parseCsvLines(const QString& text) {
    QVector<UniversalTextLine> lines;
    const QStringList rawLines = splitIntoLines(text);
    QVector<QStringList> rows;
    for (const QString& rawLine : rawLines) {
        QString row = rawLine;
        row.remove(QLatin1Char('\r'));
        if (row.trimmed().isEmpty()) {
            continue;
        }
        rows.append(splitCsvRow(row));
    }
    if (rows.isEmpty()) {
        return lines;
    }

    // 每列的最宽显示宽度（按等宽字体的格数算）。
    int columnCount = 0;
    for (const QStringList& row : rows) {
        columnCount = std::max(columnCount, static_cast<int>(row.size()));
    }
    QVector<int> columnWidths(columnCount, 0);
    for (const QStringList& row : rows) {
        for (int column = 0; column < row.size(); ++column) {
            columnWidths[column] =
                std::max(columnWidths[column], displayWidthOf(row.at(column)));
        }
    }

    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const QStringList& row = rows.at(rowIndex);
        QStringList padded;
        for (int column = 0; column < columnCount; ++column) {
            const QString cell = column < row.size() ? row.at(column) : QString();
            const int width = displayWidthOf(cell);
            padded.append(cell + QString(columnWidths.at(column) - width, QLatin1Char(' ')));
        }
        UniversalTextLine lineInfo;
        lineInfo.monospace = true;
        // 第一行当表头：加粗。
        lineInfo.bold = (rowIndex == 0);
        lineInfo.text = padded.join(QStringLiteral("  ")).trimmed();
        lines.append(lineInfo);
    }
    return lines;
}

// HTML 实体解码：&amp; &lt; &gt; &quot; &apos; &nbsp; 以及数字实体。
QString decodeHtmlEntities(const QString& text) {
    static const QHash<QString, QString> kEntities = {
        {QStringLiteral("amp"), QStringLiteral("&")},
        {QStringLiteral("lt"), QStringLiteral("<")},
        {QStringLiteral("gt"), QStringLiteral(">")},
        {QStringLiteral("quot"), QStringLiteral("\"")},
        {QStringLiteral("apos"), QStringLiteral("'")},
        {QStringLiteral("nbsp"), QStringLiteral(" ")},
        {QStringLiteral("hellip"), QStringLiteral("…")},
        {QStringLiteral("mdash"), QStringLiteral("—")},
        {QStringLiteral("ndash"), QStringLiteral("–")},
    };

    QString out;
    out.reserve(text.size());
    int index = 0;
    while (index < text.size()) {
        const QChar ch = text.at(index);
        if (ch != QLatin1Char('&')) {
            out.append(ch);
            ++index;
            continue;
        }
        const int semicolon = text.indexOf(QLatin1Char(';'), index + 1);
        // 实体名不会太长；超了就当普通 '&' 处理。
        if (semicolon < 0 || semicolon - index > 12) {
            out.append(ch);
            ++index;
            continue;
        }
        const QString token = text.mid(index + 1, semicolon - index - 1);
        QString decoded;
        if (token.startsWith(QLatin1Char('#'))) {
            const bool hex = token.size() > 1 &&
                             (token.at(1) == QLatin1Char('x') || token.at(1) == QLatin1Char('X'));
            const QString digits = hex ? token.mid(2) : token.mid(1);
            bool ok = false;
            const uint code = digits.toUInt(&ok, hex ? 16 : 10);
            if (ok && code > 0 && code <= 0x10FFFF) {
                const char32_t value = static_cast<char32_t>(code);
                decoded = QString::fromUcs4(&value, 1);
            }
        } else {
            const auto it = kEntities.constFind(token.toLower());
            if (it != kEntities.constEnd()) {
                decoded = it.value();
            }
        }
        if (decoded.isEmpty()) {
            out.append(ch);
            ++index;
            continue;
        }
        out.append(decoded);
        index = semicolon + 1;
    }
    return out;
}

// HTML → 排版行（去标签留文字）。
//
// 为什么不引 HTML 解析库：引入就多一份依赖，而这里的诉求只是
// 「把标签去掉、把块级标签换成换行」，正则足够。
QVector<UniversalTextLine> parseHtmlLines(const QString& text) {
    QString working = text;

    // script / style 整段丢掉：里面的内容不是给人看的正文。
    working.remove(QRegularExpression(
        QStringLiteral("<script[^>]*>.*?</script>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    working.remove(QRegularExpression(
        QStringLiteral("<style[^>]*>.*?</style>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    working.remove(QRegularExpression(
        QStringLiteral("<!--.*?-->"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));

    // 块级标签：开始与结束都换成换行，免得两段挤成一段。
    working.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"),
                                       QRegularExpression::CaseInsensitiveOption),
                    QStringLiteral("\n"));
    working.replace(QRegularExpression(QStringLiteral("<hr\\s*/?>"),
                                       QRegularExpression::CaseInsensitiveOption),
                    QStringLiteral("\n"));
    working.replace(QRegularExpression(QStringLiteral("<li[^>]*>"),
                                       QRegularExpression::CaseInsensitiveOption),
                    QStringLiteral("\n• "));
    working.replace(QRegularExpression(
                        QStringLiteral("</(p|div|tr|table|section|article|ul|ol|dl|h[1-6]|li|blockquote|pre)>"),
                        QRegularExpression::CaseInsensitiveOption),
                    QStringLiteral("\n"));
    working.replace(QRegularExpression(
                        QStringLiteral("<(p|div|tr|table|section|article|ul|ol|dl|h[1-6]|blockquote|pre)[^>]*>"),
                        QRegularExpression::CaseInsensitiveOption),
                    QStringLiteral("\n"));

    // 剩下的标签一律去掉。
    working.remove(QRegularExpression(QStringLiteral("<[^>]*>")));

    // 实体解码：只认常见的那几个，够用且不会把别的文本改坏。
    //
    // 手工扫一遍而不是用正则回调：Qt 的 QString::replace 没有「回调」重载，
    // 用正则回调得配 QRegularExpressionMatchIterator，代码长一倍。
    working = decodeHtmlEntities(working);

    // 连续空行压成一行空行：省纸，也更像正文。
    working.replace(QRegularExpression(QStringLiteral("\n{3,}")), QStringLiteral("\n\n"));

    QVector<UniversalTextLine> lines;
    const QStringList rawLines = splitIntoLines(working);
    for (const QString& rawLine : rawLines) {
        QString line = rawLine;
        line.remove(QLatin1Char('\r'));
        UniversalTextLine lineInfo;
        lineInfo.text = line.trimmed();
        lines.append(lineInfo);
    }
    return lines;
}

// JSON → 排版行（缩进输出）。
QVector<UniversalTextLine> parseJsonLines(const QString& text) {
    QVector<UniversalTextLine> lines;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    const bool isJson = !document.isNull();
    const QString source =
        isJson ? QString::fromUtf8(document.toJson(QJsonDocument::Indented)) : text;
    const QStringList rawLines = splitIntoLines(source);
    for (const QString& rawLine : rawLines) {
        QString line = rawLine;
        line.remove(QLatin1Char('\r'));
        line.remove(QLatin1Char('\t'));
        UniversalTextLine lineInfo;
        lineInfo.text = line;
        lines.append(lineInfo);
    }
    return lines;
}

// 纯文本 → 排版行。
QVector<UniversalTextLine> parsePlainLines(const QString& text) {
    QVector<UniversalTextLine> lines;
    const QStringList rawLines = splitIntoLines(text);
    for (const QString& rawLine : rawLines) {
        QString line = rawLine;
        line.remove(QLatin1Char('\r'));
        UniversalTextLine lineInfo;
        lineInfo.text = line;
        lines.append(lineInfo);
    }
    return lines;
}

// 已解码的文本 → 排版行（按文件类型分派）。
//
// 分派只留这一处：解析规则与「转 PDF」共用同一套，
// 两处各写一份 switch 的话，迟早会漂移（测试测的是 parse 那份，生成用的却是另一份）。
QVector<UniversalTextLine> parseDecodedText(const QString& text, UniversalFileKind kind) {
    switch (kind) {
    case UniversalFileKind::Markdown:
        return parseMarkdownLines(text);
    case UniversalFileKind::Csv:
        return parseCsvLines(text);
    case UniversalFileKind::Json:
        return parseJsonLines(text);
    case UniversalFileKind::Html:
        return parseHtmlLines(text);
    case UniversalFileKind::PlainText:
    case UniversalFileKind::Unknown:
        break;
    }
    return parsePlainLines(text);
}

// ============================================================ 排版 ==========

// 排好的一行（已经折行、算好缩进与字号）。
struct LaidOutLine {
    QString text;
    double fontSize = 11.0;
    double indent = 0.0;
    int faceKind = 0;  // 0=常规 1=粗体 2=等宽 3=等宽粗体
};

// 折行：按可用宽度把一行拆成若干行。
//
// 规则：能断在空格处就断在空格处；断不了（长单词、中文）就硬断。
// 中文没有空格，硬断是唯一合理的办法——排版引擎那套「避头尾」不在本模块范围。
QVector<QString> wrapText(const QString& text, double availableWidth, const FontFace& face,
                           double fontSize) {
    QVector<QString> result;
    if (text.isEmpty()) {
        result.append(QString());
        return result;
    }
    const double scale = fontSize / 1000.0;
    const auto measure = [&face, scale](const QString& sample) {
        double width = 0.0;
        for (const QChar ch : sample) {
            width += face.widthOf(ch) * scale;
        }
        return width;
    };

    QString current;
    double currentWidth = 0.0;
    for (const QChar ch : text) {
        const double charWidth = face.widthOf(ch) * scale;
        if (!current.isEmpty() && currentWidth + charWidth > availableWidth) {
            const int space = current.lastIndexOf(QLatin1Char(' '));
            if (space > 0) {
                result.append(current.left(space));
                current = current.mid(space + 1);
                currentWidth = measure(current);
            } else {
                result.append(current);
                current.clear();
                currentWidth = 0.0;
            }
        }
        current.append(ch);
        currentWidth += charWidth;
    }
    result.append(current);
    return result;
}

// 把解析出来的行排成可绘制的行（折行 + 缩进 + 字号）。
QVector<LaidOutLine> buildLayout(const QVector<UniversalTextLine>& parsed,
                                  const UniversalConverterOptions& options, const FontSet& fonts,
                                  double usableWidth) {
    QVector<LaidOutLine> result;
    for (const UniversalTextLine& line : parsed) {
        const int faceKind = line.monospace ? (line.bold ? 3 : 2) : (line.bold ? 1 : 0);
        const FontFace face = fonts.faceFor(faceKind);
        const double scale =
            line.headingLevel > 0 ? headingScaleFor(line.headingLevel) : 1.0;
        const double fontSize = options.fontSizePt * scale;

        double indent = line.indentLevel * options.listIndentPt;
        QString text = line.text;
        if (line.listItem) {
            // 列表项：项目符号占半格缩进，文字跟在符号后面。
            indent += options.listIndentPt * 0.5;
            text = QStringLiteral("• ") + text;
        }
        const double available = std::max(usableWidth - indent, 1.0);
        const QVector<QString> wrapped = wrapText(text, available, face, fontSize);
        for (const QString& piece : wrapped) {
            LaidOutLine laid;
            laid.text = piece;
            laid.fontSize = fontSize;
            laid.indent = indent;
            laid.faceKind = faceKind;
            result.append(laid);
        }
    }
    return result;
}

// ============================================================ PDF 拼装 ======

// 拼 PDF 时的中间状态：正文 + 每个对象的起始偏移。
//
// 与 BlankDocument 里那份是同一个道理：PDF 的交叉引用表写的是每个对象的
// **字节位置**，边写边记偏移，否则别的阅读器会认为文件损坏。
class PdfBuilder {
public:
    explicit PdfBuilder(int objectCount) {
        offsets_.resize(objectCount + 1);
        // 文件头必须先写：交叉引用表里的偏移是**相对文件开头**的字节位置，
        // 头写在后面的话所有偏移都会错位，阅读器会判定文件损坏。
        bytes_.append("%PDF-1.7\n");
        // 二进制标记行：告诉别的程序这个文件里有非 ASCII 字节，
        // 免得有人用文本模式搬运时把它改坏。
        bytes_.append(QByteArrayLiteral("%\xE2\xE3\xCF\xD3\n"));
    }

    void setObject(int number, const QByteArray& body) {
        if (number <= 0 || number >= offsets_.size()) {
            return;
        }
        offsets_[number] = bytes_.size();
        bytes_.append(QStringLiteral("%1 0 obj\n").arg(number).toUtf8());
        bytes_.append(body);
        bytes_.append("\nendobj\n");
    }

    QByteArray finish(int rootNumber) {
        const qint64 xrefOffset = bytes_.size();
        QString xref = QStringLiteral("xref\n0 %1\n").arg(offsets_.size());
        xref += QStringLiteral("0000000000 65535 f \n");
        for (int number = 1; number < offsets_.size(); ++number) {
            xref += QStringLiteral("%1 00000 n \n")
                        .arg(offsets_.at(number), 10, 10, QLatin1Char('0'));
        }
        bytes_.append(xref.toUtf8());
        bytes_.append(QStringLiteral("trailer\n<< /Size %1 /Root %2 0 R >>\nstartxref\n%3\n%%EOF\n")
                          .arg(offsets_.size())
                          .arg(rootNumber)
                          .arg(xrefOffset)
                          .toUtf8());
        return bytes_;
    }

private:
    QByteArray bytes_;
    QVector<qint64> offsets_;
};

// PDF 里的数字：整数就写整数，带小数保留 4 位。
QString formatPoints(double value) {
    const double rounded = qRound64(value * 10000.0) / 10000.0;
    const qlonglong asInteger = qRound64(rounded);
    if (qAbs(rounded - static_cast<double>(asInteger)) < 1e-9) {
        return QString::number(asInteger);
    }
    return QString::number(rounded, 'f', 4);
}

// 字节数组 → PDF 十六进制串。
QByteArray toHexString(const QByteArray& bytes) {
    static const char* kDigits = "0123456789ABCDEF";
    QByteArray out;
    out.reserve(bytes.size() * 2 + 2);
    out.append('<');
    for (const char raw : bytes) {
        const auto value = static_cast<unsigned char>(raw);
        out.append(kDigits[(value >> 4) & 0x0F]);
        out.append(kDigits[value & 0x0F]);
    }
    out.append('>');
    return out;
}

// 字面串 → PDF 字面串（转义括号与反斜杠）。
QByteArray toLiteralString(const QByteArray& bytes) {
    QByteArray out;
    out.reserve(bytes.size() + 2);
    out.append('(');
    for (const char raw : bytes) {
        if (raw == '(' || raw == ')' || raw == '\\') {
            out.append('\\');
        }
        out.append(raw);
    }
    out.append(')');
    return out;
}

// 流对象。
QByteArray streamObject(const QByteArray& dictionaryEntries, const QByteArray& data) {
    QByteArray out = "<< ";
    out.append(dictionaryEntries);
    if (!dictionaryEntries.isEmpty()) {
        out.append(' ');
    }
    out.append(QStringLiteral("/Length %1 >>\nstream\n").arg(data.size()).toUtf8());
    out.append(data);
    out.append("\nendstream");
    return out;
}

// ToUnicode CMap：把「字形号 → 码位」写进去，抽取文字时靠它还原原文。
QByteArray buildToUnicodeCMap(const QHash<int, int>& gidToUnicode) {
    QVector<int> gids = gidToUnicode.keys();
    std::sort(gids.begin(), gids.end());

    QByteArray out;
    out.append("/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n");
    out.append("/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n");
    out.append("/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n");
    out.append("1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n");
    // 一块最多 100 条 bfchar（规范的上限），超了就再开一块。
    const int perBlock = 100;
    for (int start = 0; start < gids.size(); start += perBlock) {
        const int end = std::min(start + perBlock, static_cast<int>(gids.size()));
        out.append(QStringLiteral("%1 beginbfchar\n").arg(end - start).toUtf8());
        for (int index = start; index < end; ++index) {
            const int gid = gids.at(index);
            const int code = gidToUnicode.value(gid);
            out.append(QStringLiteral("<%1> <%2>\n")
                           .arg(gid, 4, 16, QLatin1Char('0'))
                           .arg(code, 4, 16, QLatin1Char('0'))
                           .toUtf8());
        }
        out.append("endbfchar\n");
    }
    out.append("endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n");
    return out;
}

// /W 数组：CID（这里就是字形号）→ 字宽。
QByteArray buildWidthArray(const QVector<int>& gids, const TrueTypeFont& font) {
    QByteArray out = " [";
    int index = 0;
    while (index < gids.size()) {
        const int firstGid = gids.at(index);
        const int firstWidth = font.widthOfGid(firstGid);
        int last = index;
        // 连续且字宽相同的一段可以写成区间，读起来短很多。
        while (last + 1 < gids.size() && gids.at(last + 1) == gids.at(last) + 1 &&
               font.widthOfGid(gids.at(last + 1)) == firstWidth) {
            ++last;
        }
        if (last > index) {
            out.append(QStringLiteral(" %1 %2 %3").arg(firstGid).arg(gids.at(last)).arg(firstWidth)
                           .toUtf8());
        } else {
            out.append(QStringLiteral(" %1 [ %2 ]").arg(firstGid).arg(firstWidth).toUtf8());
        }
        index = last + 1;
    }
    out.append(" ]");
    return out;
}

// 一份嵌入字体在 PDF 里的全部对象。
//
// 为什么是 Type0 + Identity-H：标准 14 字体放不下中文；
// Type0 让「一个字符 → 一个 CID」变成显式映射，配合 ToUnicode，
// 文字既能画出来，也能被抽回原文。
struct EmbeddedFontObjects {
    QByteArray fontObject;
    QByteArray descendantObject;
    QByteArray descriptorObject;
    QByteArray toUnicodeObject;
    QByteArray fontFileObject;
    QVector<int> gids;
};

// 生成一份嵌入字体的各对象（对象号由调用方给定）。
EmbeddedFontObjects buildEmbeddedFontObjects(const TrueTypeFont& font, const QString& baseFontName,
                                              int descendantNumber, int descriptorNumber,
                                              int toUnicodeNumber, const QHash<int, int>& gidToUnicode) {
    EmbeddedFontObjects objects;
    objects.fontObject =
        QStringLiteral("<< /Type /Font /Subtype /Type0 /BaseFont /%1 /Encoding /Identity-H "
                       "/DescendantFonts [%2 0 R] /ToUnicode %3 0 R >>")
            .arg(baseFontName)
            .arg(descendantNumber)
            .arg(toUnicodeNumber)
            .toUtf8();

    QVector<int> gids = gidToUnicode.keys();
    std::sort(gids.begin(), gids.end());
    objects.gids = gids;

    objects.descendantObject =
        QStringLiteral("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /%1 "
                       "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
                       "/FontDescriptor %2 0 R /DW 1000 /CIDToGIDMap /Identity /W%3 >>")
            .arg(baseFontName)
            .arg(descriptorNumber)
            .arg(QString::fromLatin1(buildWidthArray(gids, font)))
            .toUtf8();

    const int scale = font.unitsPerEm > 0 ? font.unitsPerEm : 1000;
    objects.descriptorObject =
        QStringLiteral("<< /Type /FontDescriptor /FontName /%1 /Flags 4 /FontBBox [%2 %3 %4 %5] "
                       "/ItalicAngle 0 /Ascent %6 /Descent %7 /CapHeight %8 /StemV 80 >>")
            .arg(baseFontName)
            .arg(font.bbox[0] * 1000 / scale)
            .arg(font.bbox[1] * 1000 / scale)
            .arg(font.bbox[2] * 1000 / scale)
            .arg(font.bbox[3] * 1000 / scale)
            .arg(font.ascender * 1000 / scale)
            .arg(font.descender * 1000 / scale)
            .arg(font.capHeight * 1000 / scale)
            .toUtf8();

    objects.toUnicodeObject = streamObject(QByteArray(), buildToUnicodeCMap(gidToUnicode));
    objects.fontFileObject =
        streamObject(QStringLiteral("/Length1 %1").arg(font.bytes.size()).toUtf8(), font.bytes);
    return objects;
}

}  // namespace

// ============================================================ 公开接口 ======

UniversalFileKind universalFileKindFor(const QString& fileName) {
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) {
        return UniversalFileKind::Unknown;
    }
    const QString suffix = fileName.mid(dot + 1).toLower();
    if (suffix == QLatin1String("txt") || suffix == QLatin1String("text") ||
        suffix == QLatin1String("log")) {
        return UniversalFileKind::PlainText;
    }
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) {
        return UniversalFileKind::Markdown;
    }
    if (suffix == QLatin1String("csv")) {
        return UniversalFileKind::Csv;
    }
    if (suffix == QLatin1String("json")) {
        return UniversalFileKind::Json;
    }
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm")) {
        return UniversalFileKind::Html;
    }
    return UniversalFileKind::Unknown;
}

bool isSupportedUniversalFile(const QString& fileName) {
    return universalFileKindFor(fileName) != UniversalFileKind::Unknown;
}

QString universalFileKindLabel(UniversalFileKind kind) {
    switch (kind) {
    case UniversalFileKind::PlainText:
        return QStringLiteral("纯文本");
    case UniversalFileKind::Markdown:
        return QStringLiteral("Markdown");
    case UniversalFileKind::Csv:
        return QStringLiteral("CSV");
    case UniversalFileKind::Json:
        return QStringLiteral("JSON");
    case UniversalFileKind::Html:
        return QStringLiteral("HTML");
    case UniversalFileKind::Unknown:
        break;
    }
    return QStringLiteral("未知");
}

QString suggestedPdfFileName(const QString& sourceFileName) {
    if (sourceFileName.trimmed().isEmpty()) {
        return QStringLiteral("文档.pdf");
    }
    return sourceFileName + QStringLiteral(".pdf");
}

int linesPerPageFor(const UniversalConverterOptions& options) {
    const QSizeF size = pageSizeInPoints(options.pageSize, options.orientation);
    const double lineHeight = options.fontSizePt * options.lineSpacing;
    if (lineHeight <= 0.0 || size.height() <= 0.0) {
        return 0;
    }
    const double usable = size.height() - 2.0 * options.marginPt;
    if (usable <= 0.0) {
        return 0;
    }
    return static_cast<int>(usable / lineHeight);
}

QVector<UniversalTextLine> parseUniversalText(const QByteArray& content, UniversalFileKind kind,
                                               QString* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (kind == UniversalFileKind::Unknown) {
        if (error != nullptr) {
            *error = QStringLiteral("不支持的文件类型");
        }
        return {};
    }
    bool hadInvalid = false;
    const QString text = decodeUtf8(content, &hadInvalid);
    Q_UNUSED(hadInvalid)
    return parseDecodedText(text, kind);
}

UniversalConversionResult convertToPdf(const QByteArray& content, const QString& fileName,
                                        const UniversalConverterOptions& options) {
    return convertToPdf(content, universalFileKindFor(fileName), fileName, options);
}

UniversalConversionResult convertToPdf(const QByteArray& content, UniversalFileKind kind,
                                        const QString& fileName,
                                        const UniversalConverterOptions& options) {
    UniversalConversionResult result;
    result.suggestedFileName = suggestedPdfFileName(fileName);

    if (kind == UniversalFileKind::Unknown) {
        const QString suffix = fileName.contains(QLatin1Char('.'))
                                   ? fileName.mid(fileName.lastIndexOf(QLatin1Char('.')) + 1)
                                   : QString();
        result.error = suffix.isEmpty()
                           ? QStringLiteral("不支持的文件类型：无法从文件名判断（缺少扩展名）")
                           : QStringLiteral("不支持的文件类型：%1").arg(suffix);
        return result;
    }
    if (content.isEmpty()) {
        result.error = QStringLiteral("文件内容为空，没有可转换的文字");
        return result;
    }

    // 非法 UTF-8：按参数决定是失败还是替换。
    bool hadInvalid = false;
    const QString text = decodeUtf8(content, &hadInvalid);
    if (hadInvalid && options.invalidUtf8 == InvalidUtf8Policy::Fail) {
        result.error = QStringLiteral("文件不是合法的 UTF-8 文本：含有无效的字节序列，无法转换");
        return result;
    }

    // 字体：调用方给了就用嵌入字体，没给就用标准 14。
    FontSet fonts;
    if (!options.regularFont.isEmpty()) {
        fonts.regular = parseTrueTypeFont(options.regularFont.bytes);
        if (!fonts.regular.ok) {
            result.error = QStringLiteral("正文字体资源无效：%1").arg(fonts.regular.error);
            return result;
        }
        fonts.embedded = true;
        if (!options.boldFont.isEmpty()) {
            fonts.bold = parseTrueTypeFont(options.boldFont.bytes);
            if (!fonts.bold.ok) {
                result.error = QStringLiteral("标题字体资源无效：%1").arg(fonts.bold.error);
                return result;
            }
            fonts.hasBold = true;
        }
    } else if (!options.boldFont.isEmpty()) {
        // 只给了粗体：正文用它，标题也用它（字号更大）。
        fonts.regular = parseTrueTypeFont(options.boldFont.bytes);
        if (!fonts.regular.ok) {
            result.error = QStringLiteral("标题字体资源无效：%1").arg(fonts.regular.error);
            return result;
        }
        fonts.embedded = true;
    }
    result.usedEmbeddedFont = fonts.embedded;

    // 解析 → 排版。
    const QVector<UniversalTextLine> parsed = parseDecodedText(text, kind);

    const QSizeF pageSize = pageSizeInPoints(options.pageSize, options.orientation);
    const double pageWidth = pageSize.width();
    const double pageHeight = pageSize.height();
    const double usableWidth = pageWidth - 2.0 * options.marginPt;
    if (usableWidth <= 0.0 || pageHeight - 2.0 * options.marginPt <= 0.0) {
        result.error = QStringLiteral("页面参数无效：页边距过大，没有可排版的区域");
        return result;
    }

    const QVector<LaidOutLine> layout = buildLayout(parsed, options, fonts, usableWidth);

    // 排版 → 分页。编码在这一步做（要知道每页用到哪些字形）。
    QVector<QByteArray> pageStreams;
    QByteArray currentStream;
    QHash<int, int> regularGids;
    QHash<int, int> boldGids;
    int replacedCount = 0;
    double top = pageHeight - options.marginPt;

    const auto emitLine = [&](const LaidOutLine& line) {
        const EncodedText encoded = fonts.embedded
                                        ? encodeGlyphIds(line.text, *fonts.embeddedFontFor(line.faceKind))
                                        : encodeWinAnsi(line.text);
        replacedCount += encoded.replaced;
        if (fonts.embedded) {
            QHash<int, int>& target =
                fonts.embeddedIndexFor(line.faceKind) == 1 ? boldGids : regularGids;
            for (auto it = encoded.gidToUnicode.constBegin(); it != encoded.gidToUnicode.constEnd();
                 ++it) {
                target.insert(it.key(), it.value());
            }
        }
        const double baseline = top - line.fontSize * 0.8;
        QByteArray out = "BT\n";
        out.append(fonts.resourceName(line.faceKind));
        out.append(' ');
        out.append(formatPoints(line.fontSize).toUtf8());
        out.append(" Tf\n1 0 0 1 ");
        out.append(formatPoints(options.marginPt + line.indent).toUtf8());
        out.append(' ');
        out.append(formatPoints(baseline).toUtf8());
        out.append(" Tm\n");
        out.append(fonts.embedded ? toHexString(encoded.bytes) : toLiteralString(encoded.bytes));
        out.append(" Tj\nET\n");
        currentStream.append(out);
    };

    for (const LaidOutLine& line : layout) {
        const double lineHeight = line.fontSize * options.lineSpacing;
        if (top - lineHeight < options.marginPt) {
            pageStreams.append(currentStream);
            currentStream.clear();
            top = pageHeight - options.marginPt;
        }
        emitLine(line);
        top -= lineHeight;
    }
    pageStreams.append(currentStream);
    result.pageCount = pageStreams.size();

    if (replacedCount > 0) {
        if (fonts.embedded) {
            result.warning = QStringLiteral(
                                  "有 %1 个字符在字体里找不到对应字形，已按空白占位处理")
                                  .arg(replacedCount);
        } else {
            result.warning = QStringLiteral(
                                  "有 %1 个字符（中文等）无法用标准 14 字体显示，已替换成 '?'；"
                                  "如需正确显示，请提供中文字体资源")
                                  .arg(replacedCount);
        }
    }
    result.replacedCharacterCount = replacedCount;

    // 内容流自检：BT/ET 配对坏掉的话，PDFium 可能照收不误、别的阅读器却打不开。
    for (const QByteArray& stream : pageStreams) {
        const pdf::ContentParseResult parsedStream = pdf::ContentStream::tokenize(stream);
        if (!parsedStream.isValid()) {
            result.error = QStringLiteral("生成的内容流不合法：%1").arg(parsedStream.error);
            result.pageCount = 0;
            return result;
        }
    }

    // 拼对象。
    //
    // 对象编号必须连续：3 + 2*i 是第 i 页，4 + 2*i 是第 i 页的内容流，
    // 字体对象从「最后一个页面对象 + 1」开始。留空号会让交叉引用表指向
    // 偏移 0（PDF 头），阅读器会判定文件损坏。
    const int pageCount = pageStreams.size();
    const int firstFontObject = 3 + 2 * pageCount;
    // 粗体字体只在**真的用到**时才单独写一份对象：一份没人用的字体对象
    // 会带着空的 ToUnicode 与空的 /W 数组，白白多占一份字体文件体积。
    const bool useBoldFont = fonts.hasBold && !boldGids.isEmpty();
    // 字体对象：标准 14 是 4 个；嵌入字体每个 4 个对象（Type0 / 后代 / 描述符 /
    // ToUnicode）+ 1 个 FontFile2。
    const int fontObjectCount = fonts.embedded ? (useBoldFont ? 10 : 5) : 4;
    const int objectCount = firstFontObject + fontObjectCount - 1;

    PdfBuilder builder(objectCount);
    builder.setObject(1, QByteArrayLiteral("<< /Type /Catalog /Pages 2 0 R >>"));

    QStringList kids;
    for (int index = 0; index < pageCount; ++index) {
        kids.append(QStringLiteral("%1 0 R").arg(3 + 2 * index));
    }
    builder.setObject(2, QStringLiteral("<< /Type /Pages /Kids [%1] /Count %2 >>")
                           .arg(kids.join(QLatin1Char(' ')))
                           .arg(pageCount)
                           .toUtf8());

    // 字体资源表。
    QString fontResources;
    int regularType0 = 0;
    int boldType0 = 0;
    if (fonts.embedded) {
        regularType0 = firstFontObject;
        boldType0 = useBoldFont ? firstFontObject + 5 : 0;
        fontResources += QStringLiteral("/F1 %1 0 R ").arg(regularType0);
        if (boldType0 > 0) {
            fontResources += QStringLiteral("/F2 %1 0 R ").arg(boldType0);
        } else {
            // 没有独立的粗体：F2 指向同一份字体（标题靠字号区分）。
            fontResources += QStringLiteral("/F2 %1 0 R ").arg(regularType0);
        }
    } else {
        fontResources += QStringLiteral("/F1 %1 0 R /F2 %2 0 R /F3 %3 0 R /F4 %4 0 R ")
                            .arg(firstFontObject + 0)
                            .arg(firstFontObject + 1)
                            .arg(firstFontObject + 2)
                            .arg(firstFontObject + 3);
    }

    const QString mediaBox = QStringLiteral("[0 0 %1 %2]")
                                 .arg(formatPoints(pageWidth), formatPoints(pageHeight));
    for (int index = 0; index < pageCount; ++index) {
        const int pageNumber = 3 + 2 * index;
        const int contentNumber = 4 + 2 * index;
        builder.setObject(
            pageNumber,
            QStringLiteral("<< /Type /Page /Parent 2 0 R /MediaBox %1 "
                           "/Resources << /Font << %2 >> >> /Contents %3 0 R >>")
                .arg(mediaBox, fontResources)
                .arg(contentNumber)
                .toUtf8());
        builder.setObject(contentNumber, streamObject(QByteArray(), pageStreams.at(index)));
    }

    // 字体对象。
    if (fonts.embedded) {
        const QString regularName =
            options.regularFont.postScriptName.isEmpty()
                ? fonts.regular.postScriptName
                : options.regularFont.postScriptName;
        const EmbeddedFontObjects regularObjects =
            buildEmbeddedFontObjects(fonts.regular, regularName, firstFontObject + 1,
                                     firstFontObject + 2, firstFontObject + 3, regularGids);
        builder.setObject(firstFontObject + 0, regularObjects.fontObject);
        builder.setObject(firstFontObject + 1, regularObjects.descendantObject);
        builder.setObject(firstFontObject + 2, regularObjects.descriptorObject);
        builder.setObject(firstFontObject + 3, regularObjects.toUnicodeObject);
        builder.setObject(firstFontObject + 4, regularObjects.fontFileObject);
        if (useBoldFont) {
            const QString boldName = options.boldFont.postScriptName.isEmpty()
                                         ? fonts.bold.postScriptName
                                         : options.boldFont.postScriptName;
            const EmbeddedFontObjects boldObjects =
                buildEmbeddedFontObjects(fonts.bold, boldName, firstFontObject + 6,
                                         firstFontObject + 7, firstFontObject + 8, boldGids);
            builder.setObject(firstFontObject + 5, boldObjects.fontObject);
            builder.setObject(firstFontObject + 6, boldObjects.descendantObject);
            builder.setObject(firstFontObject + 7, boldObjects.descriptorObject);
            builder.setObject(firstFontObject + 8, boldObjects.toUnicodeObject);
            builder.setObject(firstFontObject + 9, boldObjects.fontFileObject);
        }
    } else {
        builder.setObject(firstFontObject + 0,
                          QByteArrayLiteral("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                            "/Encoding /WinAnsiEncoding >>"));
        builder.setObject(firstFontObject + 1,
                          QByteArrayLiteral("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold "
                                            "/Encoding /WinAnsiEncoding >>"));
        builder.setObject(firstFontObject + 2,
                          QByteArrayLiteral("<< /Type /Font /Subtype /Type1 /BaseFont /Courier "
                                            "/Encoding /WinAnsiEncoding >>"));
        builder.setObject(firstFontObject + 3,
                          QByteArrayLiteral("<< /Type /Font /Subtype /Type1 /BaseFont /Courier-Bold "
                                            "/Encoding /WinAnsiEncoding >>"));
    }

    result.bytes = builder.finish(1);
    result.ok = !result.bytes.isEmpty();
    if (!result.ok) {
        result.error = QStringLiteral("生成 PDF 失败：没有得到任何字节");
    }
    return result;
}

}  // namespace aipdf::doc
