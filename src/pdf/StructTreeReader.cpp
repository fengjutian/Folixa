#include "pdf/StructTreeReader.h"

#include "pdf/ContentStream.h"

#include <QHash>
#include <QSet>

#include <algorithm>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <memory>
#include <string>
#include <vector>
#endif

namespace aipdf::pdf {
namespace {

// ---------------------------------------------------------------------------
// 字符串与编码
// ---------------------------------------------------------------------------

// 字面量字符串（带括号）还原成字节。
//
// 为什么自己解而不是用现成的：ContentStream 里的 decodeLiteral 只能解成
// Latin-1 的字符串，遇到嵌入字体（多字节编码）就废了；
// 这里要的是**原始字节**，编码的还原交给字体的解码器。
QByteArray decodeLiteralBytes(const QByteArray& raw) {
    QByteArray out;
    for (int i = 1; i + 1 < raw.size(); ++i) {
        const char c = raw.at(i);
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i + 1 >= raw.size()) break;
        const char escaped = raw.at(i);
        if (escaped == 'n') out += '\n';
        else if (escaped == 'r') out += '\r';
        else if (escaped == 't') out += '\t';
        else if (escaped == 'b') out += '\b';
        else if (escaped == 'f') out += '\f';
        else if (escaped == '\n') continue;
        else if (escaped == '\r') {
            if (i + 1 < raw.size() && raw.at(i + 1) == '\n') ++i;
        } else if (escaped >= '0' && escaped <= '7') {
            int value = escaped - '0';
            for (int n = 0; n < 2 && i + 1 < raw.size() - 1 && raw.at(i + 1) >= '0' && raw.at(i + 1) <= '7'; ++n) {
                value = value * 8 + (raw.at(++i) - '0');
            }
            out += static_cast<char>(value);
        } else {
            out += escaped;
        }
    }
    return out;
}

// WinAnsi 编码里 0x80-0x9F 这一段与 Latin-1 不同。
struct WinAnsiSpecial {
    int code;
    ushort unicode;
};

const WinAnsiSpecial kWinAnsiSpecials[] = {
    {0x80, 0x20AC}, {0x82, 0x201A}, {0x83, 0x0192}, {0x84, 0x201E}, {0x85, 0x2026},
    {0x86, 0x2020}, {0x87, 0x2021}, {0x88, 0x02C6}, {0x89, 0x2030}, {0x8A, 0x0160},
    {0x8B, 0x2039}, {0x8C, 0x0152}, {0x8E, 0x017D}, {0x91, 0x2018}, {0x92, 0x2019},
    {0x93, 0x201C}, {0x94, 0x201D}, {0x95, 0x2022}, {0x96, 0x2013}, {0x97, 0x2014},
    {0x98, 0x02DC}, {0x99, 0x2122}, {0x9A, 0x0161}, {0x9B, 0x203A}, {0x9C, 0x0153},
    {0x9E, 0x017E}, {0x9F, 0x0178},
};

// 单个字节的码 → Unicode（按 WinAnsi）。
QString winAnsiToUnicode(int code) {
    if (code >= 0x20 && code <= 0x7E) return QString(QChar(static_cast<char16_t>(code)));
    if (code >= 0xA0 && code <= 0xFF) return QString(QChar(static_cast<char16_t>(code)));
    for (const WinAnsiSpecial& special : kWinAnsiSpecials) {
        if (special.code == code) return QString(QChar(special.unicode));
    }
    return QString(QChar('?'));
}

// Unicode 码点 → 字符串（超过基本平面时用代理对）。
QString codePointToString(uint code) {
    if (code <= 0xFFFF) return QString(QChar(static_cast<char16_t>(code)));
    const uint value = code - 0x10000;
    const char16_t high = static_cast<char16_t>(0xD800 + (value >> 10));
    const char16_t low = static_cast<char16_t>(0xDC00 + (value & 0x3FF));
    return QString(QChar(high)) + QString(QChar(low));
}

// ---------------------------------------------------------------------------
// 字形名 → Unicode
// ---------------------------------------------------------------------------

struct GlyphNameEntry {
    const char* name;
    ushort unicode;
};

// 只收常用的一批字形名。
//
// 为什么不全收：完整的 Adobe Glyph List 有四千多条，
// 而实际用得到的是拉丁字母、数字与常用标点；
// 剩下的走 /uniXXXX 约定或单字符字形名兜底。
const GlyphNameEntry kGlyphNames[] = {
    {"space", 0x0020}, {"exclam", 0x0021}, {"quotedbl", 0x0022}, {"numbersign", 0x0023},
    {"dollar", 0x0024}, {"percent", 0x0025}, {"ampersand", 0x0026}, {"quotesingle", 0x0027},
    {"parenleft", 0x0028}, {"parenright", 0x0029}, {"asterisk", 0x002A}, {"plus", 0x002B},
    {"comma", 0x002C}, {"hyphen", 0x002D}, {"period", 0x002E}, {"slash", 0x002F},
    {"colon", 0x003A}, {"semicolon", 0x003B}, {"less", 0x003C}, {"equal", 0x003D},
    {"greater", 0x003E}, {"question", 0x003F}, {"at", 0x0040},
    {"bracketleft", 0x005B}, {"backslash", 0x005C}, {"bracketright", 0x005D},
    {"asciicircum", 0x005E}, {"underscore", 0x005F}, {"grave", 0x0060},
    {"braceleft", 0x007B}, {"bar", 0x007C}, {"braceright", 0x007D}, {"asciitilde", 0x007E},
    {"exclamdown", 0x00A1}, {"cent", 0x00A2}, {"sterling", 0x00A3}, {"currency", 0x00A4},
    {"yen", 0x00A5}, {"florin", 0x0192}, {"section", 0x00A7}, {"periodcentered", 0x00B7},
    {"paragraph", 0x00B6}, {"germandbls", 0x00DF}, {"eacute", 0x00E9}, {"egrave", 0x00E8},
    {"ecircumflex", 0x00EA}, {"edieresis", 0x00EB}, {"agrave", 0x00E0}, {"aacute", 0x00E1},
    {"acircumflex", 0x00E2}, {"adieresis", 0x00E4}, {"atilde", 0x00E3}, {"aring", 0x00E5},
    {"ccedilla", 0x00E7}, {"cacute", 0x0107}, {"ccircumflex", 0x010D}, {"ntilde", 0x00F1},
    {"oacute", 0x00F3}, {"ocircumflex", 0x00F4}, {"odieresis", 0x00F6}, {"otilde", 0x00F5},
    {"uacute", 0x00FA}, {"ucircumflex", 0x00FB}, {"udieresis", 0x00FC}, {"uumlaut", 0x00FF},
    {"yacute", 0x00FD}, {"ydieresis", 0x00FF}, {"zacute", 0x017A}, {"zcaron", 0x017E},
    {"bullet", 0x2022}, {"ellipsis", 0x2026}, {"endash", 0x2013}, {"emdash", 0x2014},
    {"quotedblleft", 0x201C}, {"quotedblright", 0x201D}, {"quotesinglbase", 0x201A},
    {"fi", 0xFB01}, {"fl", 0xFB02}, {"dagger", 0x2020}, {"daggerdbl", 0x2021},
    {"fraction", 0x2044}, {"perthousand", 0x2030}, {"guillemotleft", 0x00AB},
    {"guillemotright", 0x00BB}, {"guilsinglleft", 0x2039}, {"guilsinglright", 0x203A},
    {"trademark", 0x2122}, {"Euro", 0x20AC}, {"AE", 0x00C6}, {"ae", 0x00E6},
    {"OE", 0x0152}, {"oe", 0x0153}, {"Oslash", 0x00D8}, {"oslash", 0x00F8},
    {"questiondown", 0x00BF}, {"exclamdown", 0x00A1}, {"quoteleft", 0x2018},
    {"quoteright", 0x2019}, {"minus", 0x2212}, {"nbspace", 0x00A0}
};

// 字形名 → Unicode。
QString glyphNameToUnicode(const QByteArray& name) {
    if (name.isEmpty()) return QString();
    // /uniXXXX 与 /uXXXX 约定：直接就是码点。
    if (name.startsWith("uni") && name.size() >= 7) {
        bool ok = false;
        const uint code = name.mid(3).toUInt(&ok, 16);
        if (ok && code > 0) return codePointToString(code);
    }
    if (name.startsWith("u") && name.size() >= 5 && name.size() <= 7) {
        bool ok = false;
        const uint code = name.mid(1).toUInt(&ok, 16);
        if (ok && code > 0) return codePointToString(code);
    }
    // 单字符的字形名（例如 /A）。
    if (name.size() == 1) {
        return QString(QChar(static_cast<unsigned char>(name.at(0))));
    }
    for (const GlyphNameEntry& entry : kGlyphNames) {
        if (name == entry.name) return QString(QChar(entry.unicode));
    }
    return QString();
}

// ---------------------------------------------------------------------------
// ToUnicode CMap
// ---------------------------------------------------------------------------

// 读一个 <...> 记号；返回空表示当前位置不是记号。
QByteArray readHexToken(const QByteArray& text, int& position) {
    while (position < text.size()) {
        const char c = text.at(position);
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            ++position;
            continue;
        }
        break;
    }
    if (position >= text.size() || text.at(position) != '<') return QByteArray();
    const int end = text.indexOf('>', position);
    if (end < 0) return QByteArray();
    const QByteArray hex = text.mid(position + 1, end - position - 1);
    position = end + 1;
    return hex;
}

// UTF-16BE 十六进制 → 字符串。
QString decodeUtf16Hex(const QByteArray& hex) {
    const QByteArray bytes = QByteArray::fromHex(hex);
    QString out;
    for (int i = 0; i + 1 < bytes.size(); i += 2) {
        const ushort unit = static_cast<ushort>((static_cast<unsigned char>(bytes.at(i)) << 8) |
                                                static_cast<unsigned char>(bytes.at(i + 1)));
        out += QChar(unit);
    }
    return out;
}

void parseBfChars(const QByteArray& section, QHash<int, QString>& out) {
    int position = 0;
    while (true) {
        const QByteArray source = readHexToken(section, position);
        if (source.isEmpty()) break;
        const QByteArray target = readHexToken(section, position);
        if (target.isEmpty()) break;
        out.insert(source.toInt(nullptr, 16), decodeUtf16Hex(target));
    }
}

void parseBfRanges(const QByteArray& section, QHash<int, QString>& out) {
    int position = 0;
    while (position < section.size()) {
        const QByteArray low = readHexToken(section, position);
        if (low.isEmpty()) {
            ++position;
            continue;
        }
        const QByteArray high = readHexToken(section, position);
        if (high.isEmpty()) break;
        while (position < section.size() && section.at(position) != '[' && section.at(position) != '<') {
            ++position;
        }
        if (position < section.size() && section.at(position) == '[') {
            // 数组形式：<低> <高> [<码1> <码2> …]
            ++position;
            int offset = 0;
            while (position < section.size() && section.at(position) != ']') {
                const QByteArray value = readHexToken(section, position);
                if (value.isEmpty()) {
                    ++position;
                    continue;
                }
                out.insert(low.toInt(nullptr, 16) + offset, decodeUtf16Hex(value));
                ++offset;
            }
            if (position < section.size()) ++position;
            continue;
        }
        const QByteArray base = readHexToken(section, position);
        if (base.isEmpty()) break;
        const int lowCode = low.toInt(nullptr, 16);
        const int highCode = high.toInt(nullptr, 16);
        const int baseCode = base.toInt(nullptr, 16);
        for (int code = lowCode; code <= highCode && code - lowCode < 65536; ++code) {
            out.insert(code, codePointToString(static_cast<uint>(baseCode + (code - lowCode))));
        }
    }
}

// 解析一张 ToUnicode CMap。
void parseToUnicode(const QByteArray& cmap, QHash<int, QString>& out) {
    int position = 0;
    while (true) {
        const int charStart = cmap.indexOf("beginbfchar", position);
        const int rangeStart = cmap.indexOf("beginbfrange", position);
        if (charStart < 0 && rangeStart < 0) break;
        if (charStart >= 0 && (rangeStart < 0 || charStart < rangeStart)) {
            const int end = cmap.indexOf("endbfchar", charStart);
            if (end < 0) break;
            // "beginbfchar" 是 11 个字符：按 10 切会把末尾的 'r' 留在段首，
            // readHexToken 读不懂它就直接放弃 —— 整张 ToUnicode 表会解析成空。
            // "beginbfchar" 是 11 个字符：按 10 切会把末尾的 'r' 留在段首，
            // readHexToken 读不懂它就直接放弃 —— 整张 ToUnicode 表会解析成空。
            parseBfChars(cmap.mid(charStart + 11, end - charStart - 11), out);
            position = end + 9;
        } else {
            const int end = cmap.indexOf("endbfrange", rangeStart);
            if (end < 0) break;
            // "beginbfrange" 是 12 个字符，同理不能按 11 切。
            // "beginbfrange" 是 12 个字符，同理不能按 11 切。
            parseBfRanges(cmap.mid(rangeStart + 12, end - rangeStart - 12), out);
            position = end + 10;
        }
    }
}

// ---------------------------------------------------------------------------
// 字体解码
// ---------------------------------------------------------------------------

// 一个字体的解码规则。
struct FontDecoder {
    // 一个字符占几个字节：简单字体 1，Type0（Identity-H 之类）2。
    int codeBytes = 1;
    QHash<int, QString> toUnicode;
    QHash<int, QByteArray> differences;

    QString decodeCode(int code) const {
        const auto mapped = toUnicode.constFind(code);
        if (mapped != toUnicode.constEnd()) return mapped.value();
        const auto difference = differences.constFind(code);
        if (difference != differences.constEnd()) {
            const QString unicode = glyphNameToUnicode(difference.value());
            if (!unicode.isEmpty()) return unicode;
        }
        if (codeBytes == 1) return winAnsiToUnicode(code);
        // 多字节编码又没有 ToUnicode：只能承认读不出来。
        return QString(QChar('?'));
    }

    QString decodeBytes(const QByteArray& bytes) const {
        QString out;
        int i = 0;
        while (i < bytes.size()) {
            int code = 0;
            if (codeBytes >= 2) {
                if (i + 1 >= bytes.size()) break;
                code = (static_cast<unsigned char>(bytes.at(i)) << 8) |
                       static_cast<unsigned char>(bytes.at(i + 1));
                i += 2;
            } else {
                code = static_cast<unsigned char>(bytes.at(i));
                i += 1;
            }
            out += decodeCode(code);
        }
        return out;
    }
};

// 遍历的防环与规模上限。
//
// 为什么要有上限：坏文件里的标签树可能有环（/K 指回祖先），
// 不设限就会把自己绕死在一个死循环里。
constexpr int kMaxTreeDepth = 64;
constexpr int kMaxTreeElements = 100000;

}  // namespace

#ifdef AIPDF_WITH_QPDF
namespace {

// 从字体字典造一个解码器。
FontDecoder buildDecoder(const QPDFObjectHandle& font) {
    FontDecoder decoder;
    if (!font.isDictionary()) return decoder;
    const QPDFObjectHandle subtype = font.getKey("/Subtype");
    if (subtype.isName() && subtype.getName() == "/Type0") {
        // Type0 字体（Identity-H、UniGB-UCS2-H 之类）的码是多字节的。
        decoder.codeBytes = 2;
    }
    const QPDFObjectHandle toUnicode = font.getKey("/ToUnicode");
    if (toUnicode.isStream()) {
        QPDFObjectHandle stream = toUnicode;
        const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
        if (data) {
            const QByteArray cmap(reinterpret_cast<const char*>(data->getBuffer()),
                                  static_cast<qsizetype>(data->getSize()));
            parseToUnicode(cmap, decoder.toUnicode);
        }
    }
    const QPDFObjectHandle encoding = font.getKey("/Encoding");
    if (encoding.isDictionary()) {
        const QPDFObjectHandle differences = encoding.getKey("/Differences");
        if (differences.isArray()) {
            const std::vector<QPDFObjectHandle> items = differences.getArrayAsVector();
            int code = 0;
            for (const QPDFObjectHandle& item : items) {
                if (item.isNumber()) {
                    code = static_cast<int>(item.getIntValue());
                } else if (item.isName()) {
                    const std::string name = item.getName();
                    decoder.differences.insert(code, QByteArray(name.c_str() + 1));
                    ++code;
                }
            }
        }
    }
    return decoder;
}

// 页面上的字体资源 → 解码器。
QHash<QByteArray, FontDecoder> pageFontDecoders(const QPDFObjectHandle& page) {
    QHash<QByteArray, FontDecoder> fonts;
    const QPDFObjectHandle resources = page.getKey("/Resources");
    if (!resources.isDictionary()) return fonts;
    const QPDFObjectHandle fontDict = resources.getKey("/Font");
    if (!fontDict.isDictionary()) return fonts;
    for (const std::string& key : fontDict.getKeys()) {
        fonts.insert(QByteArray(key.c_str()), buildDecoder(fontDict.getKey(key)));
    }
    return fonts;
}

// 一页的内容流（多个流拼起来）。
QByteArray pageContent(QPDFObjectHandle page) {
    QByteArray content;
    const std::vector<QPDFObjectHandle> contents = page.getPageContents();
    for (const QPDFObjectHandle& item : contents) {
        if (!item.isStream()) continue;
        // qpdf 的取流数据接口不是 const 方法，这里按值取一份句柄。
        QPDFObjectHandle stream = item;
        const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
        if (!data) continue;
        content += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                              static_cast<qsizetype>(data->getSize()));
        content += '\n';
    }
    return content;
}

// 内容流里的一段标记内容。
struct MarkedItem {
    int mcid = -1;
    QString text;
};

// 在 token 区间里找字典的某个键对应的值记号。
int findDictValue(const QVector<ContentToken>& tokens, int start, int end, const QByteArray& key) {
    for (int i = start; i < end; ++i) {
        if (tokens.at(i).type != ContentTokenType::Name) continue;
        if (tokens.at(i).raw != key) continue;
        for (int j = i + 1; j < end; ++j) {
            if (tokens.at(j).type == ContentTokenType::Whitespace) continue;
            if (tokens.at(j).type == ContentTokenType::Comment) continue;
            return j;
        }
        return -1;
    }
    return -1;
}

int previousSignificant(const QVector<ContentToken>& tokens, int index) {
    for (int i = index - 1; i >= 0; --i) {
        if (tokens.at(i).type == ContentTokenType::Whitespace) continue;
        if (tokens.at(i).type == ContentTokenType::Comment) continue;
        return i;
    }
    return -1;
}

// 扫一页的内容流，取出标记内容里的文字。
QVector<MarkedItem> scanMarkedContent(const QByteArray& content,
                                       const QHash<QByteArray, FontDecoder>& fonts,
                                       QString* error) {
    QVector<MarkedItem> items;
    const ContentParseResult parsed = ContentStream::tokenize(content);
    if (!parsed.isValid()) {
        if (error) *error = parsed.error;
        return items;
    }
    const QVector<ContentToken>& tokens = parsed.tokens;

    struct Context {
        int mcid = -1;
        QString text;
    };
    QVector<Context> stack;
    FontDecoder currentFont;

    for (int i = 0; i < tokens.size(); ++i) {
        const ContentToken& token = tokens.at(i);
        if (token.type != ContentTokenType::Operator) continue;
        const QByteArray& op = token.raw;

        if (op == "Tf") {
            const int sizeIndex = previousSignificant(tokens, i);
            const int nameIndex = sizeIndex >= 0 ? previousSignificant(tokens, sizeIndex) : -1;
            if (nameIndex >= 0 && tokens.at(nameIndex).type == ContentTokenType::Name) {
                currentFont = fonts.value(tokens.at(nameIndex).raw);
            }
            continue;
        }

        if (op == "BDC" || op == "BMC") {
            int mcid = -1;
            if (op == "BDC") {
                const int propsIndex = previousSignificant(tokens, i);
                if (propsIndex >= 0 && tokens.at(propsIndex).type == ContentTokenType::DictionaryEnd) {
                    // 往前找这个字典的起点。
                    int depth = 0;
                    int dictStart = -1;
                    for (int j = propsIndex; j >= 0; --j) {
                        if (tokens.at(j).type == ContentTokenType::DictionaryEnd) {
                            ++depth;
                        } else if (tokens.at(j).type == ContentTokenType::DictionaryStart) {
                            --depth;
                            if (depth == 0) {
                                dictStart = j;
                                break;
                            }
                        }
                    }
                    if (dictStart >= 0) {
                        const int mcidIndex = findDictValue(tokens, dictStart + 1, propsIndex, "/MCID");
                        if (mcidIndex >= 0 && tokens.at(mcidIndex).type == ContentTokenType::Number) {
                            mcid = tokens.at(mcidIndex).raw.toInt();
                        }
                    }
                }
            }
            stack.append({mcid, QString()});
            continue;
        }

        if (op == "EMC") {
            if (!stack.isEmpty()) {
                const Context context = stack.takeLast();
                if (context.mcid >= 0) {
                    items.append({context.mcid, context.text});
                }
            }
            continue;
        }

        if (op == "Tj" || op == "'" || op == "\"") {
            const int operand = previousSignificant(tokens, i);
            if (operand < 0) continue;
            const ContentToken& value = tokens.at(operand);
            if (value.type != ContentTokenType::LiteralString && value.type != ContentTokenType::HexString) {
                continue;
            }
            QByteArray bytes;
            if (value.type == ContentTokenType::LiteralString) {
                bytes = decodeLiteralBytes(value.raw);
            } else {
                bytes = QByteArray::fromHex(value.raw.mid(1, value.raw.size() - 2));
            }
            if (!stack.isEmpty()) stack.last().text += currentFont.decodeBytes(bytes);
            continue;
        }

        if (op == "TJ") {
            const int endIndex = previousSignificant(tokens, i);
            if (endIndex < 0 || tokens.at(endIndex).type != ContentTokenType::ArrayEnd) continue;
            int depth = 0;
            for (int j = endIndex; j >= 0; --j) {
                if (tokens.at(j).type == ContentTokenType::ArrayEnd) {
                    ++depth;
                } else if (tokens.at(j).type == ContentTokenType::ArrayStart) {
                    --depth;
                    if (depth == 0) {
                        QString text;
                        for (int k = j + 1; k < endIndex; ++k) {
                            const ContentToken& item = tokens.at(k);
                            if (item.type == ContentTokenType::LiteralString) {
                                text += currentFont.decodeBytes(decodeLiteralBytes(item.raw));
                            } else if (item.type == ContentTokenType::HexString) {
                                text += currentFont.decodeBytes(
                                    QByteArray::fromHex(item.raw.mid(1, item.raw.size() - 2)));
                            } else if (item.type == ContentTokenType::Number) {
                                // 数组里的数字是字距调整：调整得很大说明中间本来就有空隙。
                                if (item.raw.toDouble() <= -200.0) text += QLatin1Char(' ');
                            }
                        }
                        if (!stack.isEmpty()) stack.last().text += text;
                        break;
                    }
                }
            }
            continue;
        }
    }
    return items;
}

// 遍历标签树时用的上下文。
struct TreeContext {
    QHash<int, int> pageIndexById;                       // 页面对象号 → 页码
    QHash<QPair<int, int>, QString> markedText;           // (页码, MCID) → 文字
    QHash<int, QVector<QPair<int, QString>>> textByMcid;   // MCID → [(页码, 文字)]
    QHash<int, QVector<int>> childrenByParent;            // 父对象号 → 子对象号
    QHash<int, QPDFObjectHandle> structElems;             // 对象号 → 元素
    QSet<int> visited;                                    // 已访问过的元素（防环）
    QVector<StructElement>* elements = nullptr;
    QString error;
    bool cycle = false;
};

void traverseElement(TreeContext& context, const QPDFObjectHandle& node, int parentIndex, int depth,
                     int inheritedPage) {
    if (!node.isDictionary()) return;
    if (context.cycle) return;
    if (depth > kMaxTreeDepth) {
        if (context.error.isEmpty()) {
            context.error = QStringLiteral("标签树层级超过 %1 层，遍历已停止").arg(kMaxTreeDepth);
        }
        return;
    }
    if (context.elements && context.elements->size() >= kMaxTreeElements) {
        if (context.error.isEmpty()) {
            context.error = QStringLiteral("标签树元素超过 %1 个，遍历已停止").arg(kMaxTreeElements);
        }
        return;
    }

    const bool indirect = node.isIndirect();
    const int id = indirect ? node.getObjectID() : 0;
    if (indirect) {
        if (context.visited.contains(id)) {
            // 同一个元素又被引用了一次：结构树里有环。
            context.cycle = true;
            context.error = QStringLiteral("标签树里有环：元素 %1 被重复引用，遍历已停止").arg(id);
            return;
        }
        context.visited.insert(id);
    }

    StructElement element;
    element.depth = depth;
    element.parentIndex = parentIndex;

    const QPDFObjectHandle tag = node.getKey("/S");
    if (tag.isName()) {
        element.tag = QString::fromLatin1(tag.getName().c_str() + 1);
    } else if (tag.isArray()) {
        // 有些文件把 /S 写成数组（一个元素多个标签），取第一个。
        const std::vector<QPDFObjectHandle> items = tag.getArrayAsVector();
        for (const QPDFObjectHandle& item : items) {
            if (!item.isName()) continue;
            element.tag = QString::fromLatin1(item.getName().c_str() + 1);
            break;
        }
    }

    const QPDFObjectHandle alt = node.getKey("/Alt");
    if (alt.isString()) element.alt = QString::fromUtf8(alt.getUTF8Value().c_str());
    const QPDFObjectHandle language = node.getKey("/Lang");
    if (language.isString()) element.language = QString::fromUtf8(language.getUTF8Value().c_str());
    const QPDFObjectHandle actualText = node.getKey("/ActualText");
    if (actualText.isString()) {
        element.actualText = QString::fromUtf8(actualText.getUTF8Value().c_str());
    }
    const QPDFObjectHandle title = node.getKey("/T");
    if (title.isString()) element.title = QString::fromUtf8(title.getUTF8Value().c_str());

    // 所属页面：自己的 /Pg 优先，没有就继承父元素的。
    int page = inheritedPage;
    const QPDFObjectHandle pageRef = node.getKey("/Pg");
    if (pageRef.isDictionary() && pageRef.isIndirect()) {
        const int found = context.pageIndexById.value(pageRef.getObjectID(), -1);
        if (found >= 0) page = found;
    }
    element.pageIndex = page;

    const int index = context.elements ? static_cast<int>(context.elements->size()) : -1;
    if (context.elements) context.elements->append(element);

    // /K 有三种可能：结构元素、MCID 数字、数组（三种混着也行）。
    const QPDFObjectHandle kids = node.getKey("/K");
    bool hasKids = false;
    if (kids.isDictionary()) {
        hasKids = true;
        traverseElement(context, kids, index, depth + 1, page);
    } else if (kids.isNumber()) {
        hasKids = true;
        element.mcids.append(static_cast<int>(kids.getIntValue()));
    } else if (kids.isArray()) {
        hasKids = true;
        const std::vector<QPDFObjectHandle> items = kids.getArrayAsVector();
        for (const QPDFObjectHandle& item : items) {
            if (item.isDictionary()) {
                traverseElement(context, item, index, depth + 1, page);
            } else if (item.isNumber()) {
                element.mcids.append(static_cast<int>(item.getIntValue()));
            }
        }
    }

    // 没有 /K 的元素：有些文件只靠 /Parent 串层级，用反向索引把它的孩子找回来。
    if (!hasKids && indirect) {
        const QVector<int> childIds = context.childrenByParent.value(id);
        for (int childId : childIds) {
            const QPDFObjectHandle child = context.structElems.value(childId);
            if (!child.isDictionary()) continue;
            traverseElement(context, child, index, depth + 1, page);
        }
    }

    // 把 MCID 对应的文字找出来；页码未知时按「哪个页面有这个 MCID」推。
    if (context.elements && index >= 0) {
        QString text;
        int resolvedPage = element.pageIndex;
        for (int mcid : element.mcids) {
            const QVector<QPair<int, QString>> candidates = context.textByMcid.value(mcid);
            bool matched = false;
            for (const QPair<int, QString>& candidate : candidates) {
                if (resolvedPage >= 0 && candidate.first != resolvedPage) continue;
                text += candidate.second;
                if (resolvedPage < 0) resolvedPage = candidate.first;
                matched = true;
            }
            if (!matched && resolvedPage >= 0) {
                // 这一页没有这个 MCID：如实不写文字，但保留 MCID 本身。
                continue;
            }
        }
        (*context.elements)[index].mcids = element.mcids;
        (*context.elements)[index].text = text;
        if ((*context.elements)[index].pageIndex < 0) {
            (*context.elements)[index].pageIndex = resolvedPage;
        }
    }
}

}  // namespace
#endif  // AIPDF_WITH_QPDF

StructTreeReport readStructTree(const QByteArray& pdfBytes) {
    StructTreeReport report;
    if (pdfBytes.isEmpty()) {
        report.error = QStringLiteral("输入的 PDF 内容为空");
        return report;
    }

#ifndef AIPDF_WITH_QPDF
    report.error = QStringLiteral("当前构建没有启用 qpdf，无法读取标签树");
    return report;
#else
    try {
        QPDF pdf;
        pdf.processMemoryFile("struct-tree.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            report.error = QStringLiteral("PDF 的根对象不是字典");
            return report;
        }

        // 文档级信息。
        const QPDFObjectHandle language = catalog.getKey("/Lang");
        if (language.isString()) {
            report.documentLanguage = QString::fromUtf8(language.getUTF8Value().c_str());
            report.hasDocumentLanguage = true;
        }
        const QPDFObjectHandle displayTitle = catalog.getKey("/DisplayDocTitle");
        if (displayTitle.isBool()) {
            report.displayDocTitle = displayTitle.getBoolValue();
            report.hasDisplayDocTitle = true;
        }

        const std::vector<QPDFObjectHandle> allPages = pdf.getAllPages();
        TreeContext context;
        for (size_t i = 0; i < allPages.size(); ++i) {
            if (allPages.at(i).isIndirect()) {
                context.pageIndexById.insert(allPages.at(i).getObjectID(), static_cast<int>(i));
            }
        }

        // 每一页的标记内容：MCID → 文字。
        for (size_t i = 0; i < allPages.size(); ++i) {
            QPDFObjectHandle page = allPages.at(i);
            const QByteArray content = pageContent(page);
            if (content.isEmpty()) continue;
            const QHash<QByteArray, FontDecoder> fonts = pageFontDecoders(page);
            QString scanError;
            const QVector<MarkedItem> items = scanMarkedContent(content, fonts, &scanError);
            if (!scanError.isEmpty()) {
                report.notes.append(QStringLiteral("第 %1 页的内容流无法解析：%2")
                                        .arg(static_cast<int>(i) + 1)
                                        .arg(scanError));
            }
            for (const MarkedItem& item : items) {
                const int pageIndex = static_cast<int>(i);
                report.markedContent.append({pageIndex, item.mcid, item.text});
                context.markedText.insert({pageIndex, item.mcid}, item.text);
                context.textByMcid[item.mcid].append({pageIndex, item.text});
            }
        }

        const QPDFObjectHandle root = catalog.getKey("/StructTreeRoot");
        if (!root.isDictionary()) {
            report.notes.append(QStringLiteral("这份 PDF 没有标签树（根对象里没有 /StructTreeRoot）"));
            if (!report.hasDocumentLanguage) {
                report.notes.append(QStringLiteral("文档没有声明语言（根对象里没有 /Lang）"));
            }
            if (!report.hasDisplayDocTitle) {
                report.notes.append(QStringLiteral("文档没有声明 /DisplayDocTitle"));
            }
            return report;
        }
        report.hasStructTree = true;

        // 扫一遍文档里的结构元素，按 /Parent 建索引：
        // 有些文件（例如某些 Word 导出）只靠 /Parent 串起层级，/K 里是空的。
        for (const QPDFObjectHandle& object : pdf.getAllObjects()) {
            if (!object.isDictionary() || !object.isIndirect()) continue;
            const QPDFObjectHandle type = object.getKey("/Type");
            const bool typedStructElem = type.isName() && type.getName() == "/StructElem";
            const bool looksLikeStructElem =
                !type.isName() && object.getKey("/S").isName() && object.getKey("/Parent").isDictionary();
            if (!typedStructElem && !looksLikeStructElem) continue;
            const int id = object.getObjectID();
            context.structElems.insert(id, object);
            const QPDFObjectHandle parent = object.getKey("/Parent");
            if (parent.isIndirect()) {
                context.childrenByParent[parent.getObjectID()].append(id);
            }
        }

        context.elements = &report.elements;
        const QPDFObjectHandle kids = root.getKey("/K");
        QVector<int> rootMcids;
        if (kids.isDictionary()) {
            traverseElement(context, kids, -1, 0, -1);
        } else if (kids.isArray()) {
            const std::vector<QPDFObjectHandle> items = kids.getArrayAsVector();
            for (const QPDFObjectHandle& item : items) {
                if (item.isDictionary()) {
                    traverseElement(context, item, -1, 0, -1);
                } else if (item.isNumber()) {
                    rootMcids.append(static_cast<int>(item.getIntValue()));
                }
            }
        } else if (kids.isNumber()) {
            rootMcids.append(static_cast<int>(kids.getIntValue()));
        }
        if (!rootMcids.isEmpty()) {
            report.notes.append(QStringLiteral("标签树根节点的 /K 里有 %1 个直接的 MCID，已记在标记内容列表里")
                                    .arg(rootMcids.size()));
        }

        if (!context.error.isEmpty()) {
            report.error = context.error;
        }
        if (!report.hasDocumentLanguage) {
            report.notes.append(QStringLiteral("文档没有声明语言（根对象里没有 /Lang）"));
        }
        return report;
    } catch (const std::exception& failure) {
        report.error = QStringLiteral("读取标签树失败：%1").arg(QString::fromUtf8(failure.what()));
        return report;
    }
#endif
}

}  // namespace aipdf::pdf
