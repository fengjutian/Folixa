#include "pdf/compliance/TagTreeWriter.h"

#include "i18n/Translate.h"

#include "pdf/content/ContentStream.h"

#include <QHash>
#include <QPointF>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#ifdef AIPDF_WITH_QPDF

#include <qpdf/Buffer.hh>
#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#endif

namespace aipdf::pdf {
namespace {

// marked content 的标签名。
//
// 为什么固定成 /Tag：写标签树时，标签名理论上应当与结构元素的类型一致
// （结构元素是 /Span 就写 /Span）。这里先用中性的 /Tag，
// 让「有没有 marked content」这件事与「结构元素叫什么」解耦——
// 以后要按类型细分，改这一个常量 + 结构元素的 /S 就行。
const char* const kMarkedContentTag = "/Tag";

// 往前找上一个「有意义的」token（跳过空白与注释）。
int previousSignificant(const QVector<ContentToken>& tokens, int index) {
    for (int i = index; i >= 0; --i) {
        const ContentTokenType type = tokens.at(i).type;
        if (type != ContentTokenType::Whitespace && type != ContentTokenType::Comment) return i;
    }
    return -1;
}

#ifdef AIPDF_WITH_QPDF

// 字典里有没有 /MCID（只有带 MCID 的 BDC 才是「打过的标签」）。
bool hasMcidProperty(const QVector<ContentToken>& tokens, int dictStart, int dictEnd) {
    for (int i = dictStart + 1; i < dictEnd; ++i) {
        if (tokens.at(i).type == ContentTokenType::Name && tokens.at(i).raw == "/MCID") return true;
    }
    return false;
}

// 摘掉上一轮打过的标记（`/Tag <</MCID n>> BDC` 与配对的 EMC）。
//
// 为什么需要：同一份文档可能被反复「应用区域顺序」。不先摘掉旧标记的话，
// 同一个显示操作符会被套上两层 BDC，MCID 也就重复了——内容流与 ParentTree
// 对不上，读屏软件读出来的顺序就是错的。
//
// 只动带 /MCID 的标记序列：别的 BMC/BDC（例如 /Artifact）原样保留。
// 嵌套的标记序列用栈配对，EMC 只在它配对的那个 BDC 带 MCID 时才被摘掉。
//
// 走两遍：第一遍找出「哪些 token 要摘掉」，第二遍照着摘。
// 一遍做完的写法会在「BDC 前面还隔着空白 token」时数错要摘几个。
QVector<ContentToken> stripMarkedContent(const QVector<ContentToken>& tokens) {
    // 一个标记序列的起点：标签名下标 + BDC 下标；不是我们打的标记就是 -1。
    struct MarkedSequence {
        int nameIndex = -1;
        int bdcIndex = -1;
    };
    QVector<MarkedSequence> stack;
    QVector<bool> dropped(tokens.size(), false);

    for (int i = 0; i < tokens.size(); ++i) {
        const ContentToken& token = tokens.at(i);
        if (token.type != ContentTokenType::Operator) continue;

        if (token.raw == "BDC" || token.raw == "BMC") {
            MarkedSequence sequence;
            if (token.raw == "BDC") {
                // BDC 前面隔着一个空白，所以要从「上一个有意义的 token」找属性字典。
                const int dictEnd = previousSignificant(tokens, i - 1);
                if (dictEnd >= 0 && tokens.at(dictEnd).type == ContentTokenType::DictionaryEnd) {
                    int depth = 0;
                    int dictStart = -1;
                    for (int j = dictEnd; j >= 0; --j) {
                        const ContentTokenType type = tokens.at(j).type;
                        if (type == ContentTokenType::DictionaryEnd) {
                            ++depth;
                        } else if (type == ContentTokenType::DictionaryStart && --depth == 0) {
                            dictStart = j;
                            break;
                        }
                    }
                    if (dictStart > 0) {
                        const int candidate = previousSignificant(tokens, dictStart - 1);
                        if (candidate >= 0 && tokens.at(candidate).type == ContentTokenType::Name &&
                            hasMcidProperty(tokens, dictStart, dictEnd)) {
                            sequence.nameIndex = candidate;
                            sequence.bdcIndex = i;
                        }
                    }
                }
            }
            stack.append(sequence);
            continue;
        }

        if (token.raw == "EMC") {
            if (stack.isEmpty()) continue;
            const MarkedSequence sequence = stack.takeLast();
            if (sequence.bdcIndex < 0) continue;
            // 摘掉「标签名 + 属性字典 + BDC」与配对的这个 EMC。
            for (int j = sequence.nameIndex; j <= sequence.bdcIndex; ++j) dropped[j] = true;
            dropped[i] = true;
        }
    }

    QVector<ContentToken> result;
    result.reserve(tokens.size());
    for (int i = 0; i < tokens.size(); ++i) {
        if (dropped.at(i)) continue;
        result.append(tokens.at(i));
    }
    return result;
}


// 线段（P0→P1）与矩形的距离：相交时为 0。
//
// 注意坐标系：PDF user space 的 y 向上，而 QRectF 的 top() 是**小**的 y、
// bottom() 是**大**的 y。调用方给的矩形是 QRectF(x, y, 宽, 高)，
// 覆盖 [x, x+宽] × [y, y+高]，所以这里直接用 Qt 的 top()/bottom()，
// 但上下关系要按「y 越大越靠上」来理解。
double distanceToRect(const QPointF& origin, const QPointF& end, const QRectF& rect) {
    const QRectF box = rect.normalized();
    if (box.isEmpty()) {
        const double dx = origin.x() - box.center().x();
        const double dy = origin.y() - box.center().y();
        return std::hypot(dx, dy);
    }
    const double left = std::min(origin.x(), end.x());
    const double right = std::max(origin.x(), end.x());
    const double bottom = std::min(origin.y(), end.y());
    const double top = std::max(origin.y(), end.y());
    if (left <= box.right() && right >= box.left() && bottom <= box.bottom() && top >= box.top()) {
        return 0.0;
    }
    const double dx = std::max({box.left() - right, left - box.right(), 0.0});
    const double dy = std::max({box.top() - top, bottom - box.top(), 0.0});
    return std::hypot(dx, dy);
}

#endif  // AIPDF_WITH_QPDF


// --------------------------------------------------------------- 矩阵 ----

// PDF 的二维仿射矩阵 [a b c d e f]。
struct Matrix {
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;
    double e = 0.0;
    double f = 0.0;
};

// left × right：先作用 right，再作用 left。
//
// PDF 的矩阵连乘就是这个方向（PDF 32000-1 8.3.3）：后来的变换乘在左边。
Matrix multiply(const Matrix& left, const Matrix& right) {
    Matrix result;
    result.a = left.a * right.a + left.b * right.c;
    result.b = left.a * right.b + left.b * right.d;
    result.c = left.c * right.a + left.d * right.c;
    result.d = left.c * right.b + left.d * right.d;
    result.e = left.c * right.e + left.d * right.f + left.e;
    result.f = left.f * right.c + left.f * right.d + right.f;
    return result;
}

QPointF transformPoint(const Matrix& matrix, const QPointF& point) {
    return {matrix.a * point.x() + matrix.c * point.y() + matrix.e,
            matrix.b * point.x() + matrix.d * point.y() + matrix.f};
}

Matrix translation(double tx, double ty) {
    Matrix result;
    result.e = tx;
    result.f = ty;
    return result;
}


// ------------------------------------------------------------ 文本状态 ----

// 文本状态（PDF 32000-1 9.3）。
struct TextState {
    double charSpacing = 0.0;
    double wordSpacing = 0.0;
    double horizontalScale = 1.0;  // Tz 是百分数，这里存小数
    double leading = 0.0;
    double rise = 0.0;
    double fontSize = 0.0;
};

// 一个字体的宽度信息。
//
// 为什么需要它：同一行常常有多个显示操作符，后一个的 x 起点要靠前一个的
// 推进量算出来。没有字宽就只能瞎猜，区域匹配会跟着偏。
// 简单字体（Type1/TrueType）从 /Widths 取；取不到就用 0.5 em 估。
// 复合字体（Type0）每个字形按 1 em 估——CJK 字宽本来就是整 em。
struct FontMetrics {
    bool composite = false;  // Type0：两字节码
    int firstChar = 0;
    QVector<double> widths;  // 单位 1/1000 em
    double fallbackWidth = 0.5;

    double widthOf(int code) const {
        if (composite) return 1.0;
        const int index = code - firstChar;
        if (index < 0 || index >= widths.size()) return fallbackWidth;
        return widths.at(index);
    }
};

// 一个字体的解码信息：把内容流里的字节还原成文字。
//
// 为什么需要：语义标签树要靠文字判断「这是不是标题」「这是不是列表项」，
// 读屏顺序测试也要拿文字做断言。字形码本身不是文字。
struct FontDecoder {
    bool composite = false;
    int codeBytes = 1;
    // /ToUnicode 里的映射（码 → 文字）。
    QHash<int, QString> toUnicode;
    // /Encoding /Differences 里的映射（码 → 字形名）。
    QHash<int, QByteArray> differences;
    // 字体名里带 Bold / Black 之类（粗体启发式）。
    bool bold = false;
};

// 十六进制串 → 整数。
int hexToInt(const QByteArray& hex) {
    int value = 0;
    for (char c : hex) {
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit < 0) continue;
        value = value * 16 + digit;
    }
    return value;
}

// 十六进制串 → 文字（ToUnicode 的目标值是 UTF-16BE，一字节时按 Latin-1）。
QString hexToText(const QByteArray& hex) {
    if (hex.isEmpty()) return QString();
    if (hex.size() == 1) {
        return QString(QChar(static_cast<char16_t>(static_cast<unsigned char>(hex.at(0)))));
    }
    QString out;
    for (int i = 0; i + 1 < hex.size(); i += 2) {
        const int code = (static_cast<unsigned char>(hex.at(i)) << 8) |
                         static_cast<unsigned char>(hex.at(i + 1));
        out += QString(QChar(static_cast<char16_t>(code)));
    }
    return out;
}

// 从一段文本里依次取出 <...> 记号（十六进制串）。
bool nextHexToken(const QByteArray& data, int* pos, QByteArray* out) {
    while (*pos < data.size() && data.at(*pos) != '<') ++(*pos);
    if (*pos >= data.size()) return false;
    const int end = data.indexOf('>', *pos);
    if (end < 0) return false;
    *out = data.mid(*pos + 1, end - *pos - 1);
    *pos = end + 1;
    return true;
}

// 解析一段 beginbfchar…endbfchar。
void parseBfChars(const QByteArray& section, QHash<int, QString>* out) {
    int pos = 0;
    while (true) {
        QByteArray source;
        QByteArray target;
        if (!nextHexToken(section, &pos, &source)) break;
        if (!nextHexToken(section, &pos, &target)) break;
        out->insert(hexToInt(source), hexToText(target));
    }
}

// 解析一段 beginbfrange…endbfrange（同时支持 <起点> <终点> <目标>
// 与 <起点> <终点> [ <目标1> <目标2> … ] 两种写法）。
void parseBfRanges(const QByteArray& section, QHash<int, QString>* out) {
    int pos = 0;
    while (true) {
        QByteArray low;
        QByteArray high;
        if (!nextHexToken(section, &pos, &low)) break;
        if (!nextHexToken(section, &pos, &high)) break;
        while (pos < section.size() &&
               (section.at(pos) == ' ' || section.at(pos) == '\n' || section.at(pos) == '\r' ||
                section.at(pos) == '\t')) {
            ++pos;
        }
        if (pos < section.size() && section.at(pos) == '[') {
            ++pos;
            int offset = 0;
            while (pos < section.size() && section.at(pos) != ']') {
                QByteArray item;
                if (!nextHexToken(section, &pos, &item)) break;
                out->insert(hexToInt(low) + offset, hexToText(item));
                ++offset;
            }
            if (pos < section.size()) ++pos;
            continue;
        }
        QByteArray target;
        if (!nextHexToken(section, &pos, &target)) break;
        const int start = hexToInt(low);
        const int end = hexToInt(high);
        const QString base = hexToText(target);
        if (base.size() != 1) continue;
        const ushort baseCode = base.at(0).unicode();
        for (int code = start; code <= end && code - start < 65536; ++code) {
            out->insert(code,
                        QString(QChar(static_cast<char16_t>(baseCode + (code - start)))));
        }
    }
}

// 解析一张 ToUnicode CMap（只取 bfchar / bfrange 两段）。
void parseToUnicode(const QByteArray& cmap, QHash<int, QString>* out) {
    int position = 0;
    while (true) {
        const int charStart = cmap.indexOf("beginbfchar", position);
        const int rangeStart = cmap.indexOf("beginbfrange", position);
        if (charStart < 0 && rangeStart < 0) break;
        if (charStart >= 0 && (rangeStart < 0 || charStart < rangeStart)) {
            const int end = cmap.indexOf("endbfchar", charStart);
            if (end < 0) break;
            parseBfChars(cmap.mid(charStart + 11, end - charStart - 11), out);
            position = end + 9;
        } else {
            const int end = cmap.indexOf("endbfrange", rangeStart);
            if (end < 0) break;
            parseBfRanges(cmap.mid(rangeStart + 12, end - rangeStart - 12), out);
            position = end + 10;
        }
    }
}

// 字形名 → 文字（常见的那一批；认不出来的返回空串）。
QString glyphNameToUnicode(const QByteArray& name) {
    if (name.isEmpty()) return QString();
    // /uniXXXX 与 /uXXXX 约定：直接就是码点。
    if ((name.startsWith("uni") || name.startsWith("u")) && name.size() >= 5) {
        bool ok = false;
        const int code = name.mid(name.startsWith("uni") ? 3 : 1, 4).toInt(&ok, 16);
        if (ok && code > 0) return QString(QChar(static_cast<char16_t>(code)));
    }
    static const QHash<QByteArray, ushort> table = {
        {QByteArrayLiteral("space"), 0x0020},   {QByteArrayLiteral("exclam"), 0x0021},
        {QByteArrayLiteral("quotedbl"), 0x0022},{QByteArrayLiteral("numbersign"), 0x0023},
        {QByteArrayLiteral("dollar"), 0x0024},  {QByteArrayLiteral("percent"), 0x0025},
        {QByteArrayLiteral("ampersand"), 0x0026},{QByteArrayLiteral("quotesingle"), 0x0027},
        {QByteArrayLiteral("parenleft"), 0x0028},{QByteArrayLiteral("parenright"), 0x0029},
        {QByteArrayLiteral("asterisk"), 0x002A},{QByteArrayLiteral("plus"), 0x002B},
        {QByteArrayLiteral("comma"), 0x002C},   {QByteArrayLiteral("hyphen"), 0x002D},
        {QByteArrayLiteral("period"), 0x002E},  {QByteArrayLiteral("slash"), 0x002F},
        {QByteArrayLiteral("colon"), 0x003A},   {QByteArrayLiteral("semicolon"), 0x003B},
        {QByteArrayLiteral("less"), 0x003C},    {QByteArrayLiteral("equal"), 0x003D},
        {QByteArrayLiteral("greater"), 0x003E}, {QByteArrayLiteral("question"), 0x003F},
        {QByteArrayLiteral("at"), 0x0040},      {QByteArrayLiteral("bracketleft"), 0x005B},
        {QByteArrayLiteral("backslash"), 0x005C},{QByteArrayLiteral("bracketright"), 0x005D},
        {QByteArrayLiteral("asciicircum"), 0x005E},{QByteArrayLiteral("underscore"), 0x005F},
        {QByteArrayLiteral("grave"), 0x0060},   {QByteArrayLiteral("braceleft"), 0x007B},
        {QByteArrayLiteral("bar"), 0x007C},     {QByteArrayLiteral("braceright"), 0x007D},
        {QByteArrayLiteral("asciitilde"), 0x007E},{QByteArrayLiteral("bullet"), 0x2022},
        {QByteArrayLiteral("endash"), 0x2013},  {QByteArrayLiteral("emdash"), 0x2014},
        {QByteArrayLiteral("quotedblleft"), 0x201C},{QByteArrayLiteral("quotedblright"), 0x201D},
    };
    const auto found = table.constFind(name);
    if (found != table.constEnd()) return QString(QChar(found.value()));
    // 数字与字母：名字就是字符本身（zero…nine、A…Z、a…z）。
    if (name.size() == 1) {
        const char c = name.at(0);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            return QString(QChar(static_cast<char16_t>(static_cast<unsigned char>(c))));
        }
    }
    return QString();
}

// 单字节码 → 文字（没有 ToUnicode / Differences 时的兜底：WinAnsi 近似）。
QString winAnsiToUnicode(int code) {
    if (code >= 0x20 && code <= 0x7E) return QString(QChar(static_cast<char16_t>(code)));
    if (code >= 0xA0 && code <= 0xFF) return QString(QChar(static_cast<char16_t>(code)));
    return QString(QChar('?'));
}

// 一个码 → 文字。
QString decodeSingleCode(int code, const FontDecoder& decoder) {
    const auto mapped = decoder.toUnicode.constFind(code);
    if (mapped != decoder.toUnicode.constEnd()) return mapped.value();
    const auto difference = decoder.differences.constFind(code);
    if (difference != decoder.differences.constEnd()) {
        const QString unicode = glyphNameToUnicode(difference.value());
        if (!unicode.isEmpty()) return unicode;
    }
    if (decoder.codeBytes == 1) return winAnsiToUnicode(code);
    // 多字节编码又没有 ToUnicode：只能承认读不出来。
    return QString(QChar('?'));
}

// 内容流里的字节 → 文字。
QString decodeFontBytes(const QByteArray& bytes, const FontDecoder& decoder) {
    QString out;
    const int size = decoder.codeBytes;
    for (int i = 0; i + size <= bytes.size(); i += size) {
        int code = 0;
        for (int b = 0; b < size; ++b) {
            code = (code << 8) | static_cast<unsigned char>(bytes.at(i + b));
        }
        out += decodeSingleCode(code, decoder);
    }
    return out;
}

// 页资源字典里的一个 XObject。
struct ImageResource {
    bool isImage = false;
    ObjectRef reference;
};

// ----------------------------------------------------------- 文本片段 ----

// 内容流里的一个显示操作符（Tj / TJ / ' / "）。
struct TextRun {
    int streamIndex = 0;    // 属于本页的第几条内容流
    int operatorToken = -1; // 显示操作符在 token 列表里的下标
    int operandToken = -1;  // 第一个操作数的下标
    QPointF origin;         // 基线起点（页面用户空间）
    QPointF end;            // 基线终点（按推进量估算）
    double baseline = 0.0;
    double fontSize = 0.0;
    bool bold = false;
    QString text;           // 显示出来的文字
    int streamOrder = 0;    // 内容流里的先后次序（稳定排序用）
};

// 把内容流里的字符串操作数解成原始字节。
//
// 为什么要自己解一份：算推进量要知道有几个字形、每个字形的码是多少。
// ContentStream 里的解码函数是那个文件的私有实现，这里重写一份最小版本，
// 免得为一个只读的小工具去动公共模块。
QByteArray decodeStringBytes(const QByteArray& raw, ContentTokenType type) {
    if (type == ContentTokenType::HexString) {
        QByteArray hex = raw.mid(1, raw.size() - 2);
        QByteArray out;
        int value = 0;
        bool half = false;
        for (char c : hex) {
            int digit = -1;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            if (digit < 0) continue;
            value = value * 16 + digit;
            if (!half) {
                half = true;
                continue;
            }
            out += static_cast<char>(value & 0xFF);
            value = 0;
            half = false;
        }
        if (half) out += static_cast<char>((value << 4) & 0xFF);
        return out;
    }

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

// 扫描一条内容流，找出全部显示操作符并算出它们在页面上的大致位置。
//
// 为什么自己走一遍内容流：要把「一个显示操作符」对应到「调用方给的矩形区域」，
// 就得知道它画在哪儿。PDFium 的字符框给的是渲染后的结果，反推回内容流的
// 操作符对不上号；而文本矩阵（Tm/Td/T*/TL…）是内容流自己的坐标系，
// 直接算出来最省事，也最不容易错。
//
// 算出来的位置是「基线起点 + 估算的推进量」：字宽能查到就是准的，
// 查不到就用 0.5 em 估。位置只用来做区域就近匹配，不参与渲染，
// 估得糙一点不影响输出文件的正确性。
class RunScanner {
public:
    RunScanner(const QVector<ContentToken>& tokens, const QHash<QString, FontMetrics>& fonts,
               const QHash<QString, FontDecoder>& decoders,
               const QHash<QString, ImageResource>& imageResources)
        : tokens_(tokens), fonts_(fonts), decoders_(decoders), imageResources_(imageResources) {}

    // 扫描出这一条内容流里的全部显示操作符，结果追加到 runs。
    void scan(QVector<TextRun>* runs, QVector<ScannedImage>* images) {
        for (int i = 0; i < tokens_.size(); ++i) {
            const ContentToken& token = tokens_.at(i);
            if (token.type != ContentTokenType::Operator) continue;
            const QByteArray& op = token.raw;

            if (op == "q") {
                graphicsStack_.append(ctm_);
            } else if (op == "Q") {
                if (!graphicsStack_.isEmpty()) ctm_ = graphicsStack_.takeLast();
            } else if (op == "cm") {
                QVector<double> values;
                if (readNumbersBefore(i, 6, &values)) {
                    Matrix matrix;
                    matrix.a = values.at(0);
                    matrix.b = values.at(1);
                    matrix.c = values.at(2);
                    matrix.d = values.at(3);
                    matrix.e = values.at(4);
                    matrix.f = values.at(5);
                    ctm_ = multiply(matrix, ctm_);
                }
            } else if (op == "BT") {
                textMatrix_ = Matrix();
                lineMatrix_ = Matrix();
            } else if (op == "Tf") {
                // Tf 的两个操作数是「字体名 字号」，字号紧挨着操作符（PDF 32000-1 9.7）。
                //
                // 注意顺序：名字在**前**，字号在**后**。读反了会拿到别的数字，
                // 字号就成了 0——行聚类与标题判断全都靠它。
                const int sizeIndex = previousSignificant(tokens_, i - 1);
                if (sizeIndex >= 0 && tokens_.at(sizeIndex).type == ContentTokenType::Number) {
                    bool ok = false;
                    const double size = tokens_.at(sizeIndex).raw.toDouble(&ok);
                    if (ok) text_.fontSize = size;
                }
                const int nameIndex =
                    sizeIndex >= 0 ? previousSignificant(tokens_, sizeIndex - 1) : -1;
                if (nameIndex >= 0 && tokens_.at(nameIndex).type == ContentTokenType::Name) {
                    currentFont_ = QString::fromLatin1(tokens_.at(nameIndex).raw.mid(1));
                }
            } else if (op == "TL") {
                QVector<double> values;
                if (readNumbersBefore(i, 1, &values)) text_.leading = values.at(0);
            } else if (op == "Tc") {
                QVector<double> values;
                if (readNumbersBefore(i, 1, &values)) text_.charSpacing = values.at(0);
            } else if (op == "Tw") {
                QVector<double> values;
                if (readNumbersBefore(i, 1, &values)) text_.wordSpacing = values.at(0);
            } else if (op == "Tz") {
                QVector<double> values;
                if (readNumbersBefore(i, 1, &values)) text_.horizontalScale = values.at(0) / 100.0;
            } else if (op == "Ts") {
                QVector<double> values;
                if (readNumbersBefore(i, 1, &values)) text_.rise = values.at(0);
            } else if (op == "Tm") {
                QVector<double> values;
                if (readNumbersBefore(i, 6, &values)) {
                    Matrix matrix;
                    matrix.a = values.at(0);
                    matrix.b = values.at(1);
                    matrix.c = values.at(2);
                    matrix.d = values.at(3);
                    matrix.e = values.at(4);
                    matrix.f = values.at(5);
                    textMatrix_ = matrix;
                    lineMatrix_ = matrix;
                }
            } else if (op == "Td" || op == "TD") {
                QVector<double> values;
                if (readNumbersBefore(i, 2, &values)) {
                    if (op == "TD") text_.leading = -values.at(1);
                    lineMatrix_ = multiply(translation(values.at(0), values.at(1)), lineMatrix_);
                    textMatrix_ = lineMatrix_;
                }
            } else if (op == "T*") {
                lineMatrix_ = multiply(translation(0.0, -text_.leading), lineMatrix_);
                textMatrix_ = lineMatrix_;
            } else if (op == "Tj" || op == "TJ" || op == "'" || op == "\"") {
                showText(i, op, runs);
            } else if (op == "Do") {
                recordImage(i, images);
            }
        }
    }

private:
    // 往前读 count 个数字操作数（返回 true 表示形状对得上）。
    bool readNumbersBefore(int index, int count, QVector<double>* values) const {
        values->clear();
        int cursor = index - 1;
        for (int n = 0; n < count; ++n) {
            cursor = previousSignificant(tokens_, cursor);
            if (cursor < 0) return false;
            const ContentToken& token = tokens_.at(cursor);
            if (token.type != ContentTokenType::Number) return false;
            bool ok = false;
            const double value = token.raw.toDouble(&ok);
            if (!ok) return false;
            values->prepend(value);
            --cursor;
        }
        return true;
    }

    // 找出显示操作符的第一个操作数：Tj / ' 是字符串，TJ 是数组。
    // 认不出来就返回 -1（这一处不标记），宁可少标也不要标错位置。
    int operandTokenFor(int operatorIndex, const QByteArray& op) const {
        const int previous = previousSignificant(tokens_, operatorIndex - 1);
        if (previous < 0) return -1;
        const ContentTokenType type = tokens_.at(previous).type;
        if (type == ContentTokenType::LiteralString || type == ContentTokenType::HexString) return previous;
        if (type == ContentTokenType::ArrayEnd) {
            int depth = 0;
            for (int i = previous; i >= 0; --i) {
                const ContentTokenType inner = tokens_.at(i).type;
                if (inner == ContentTokenType::ArrayEnd) ++depth;
                else if (inner == ContentTokenType::ArrayStart && --depth == 0) return i;
            }
        }
        Q_UNUSED(op)
        return -1;
    }

    void accumulateString(const QByteArray& bytes, const FontMetrics& metrics, double* emTotal,
                           int* glyphs, int* spaces) const {
        const int codeSize = metrics.composite ? 2 : 1;
        const int count = bytes.size() / codeSize;
        for (int i = 0; i < count; ++i) {
            int code = 0;
            if (codeSize == 2) {
                code = (static_cast<unsigned char>(bytes.at(i * 2)) << 8) |
                       (static_cast<unsigned char>(bytes.at(i * 2 + 1)));
            } else {
                code = static_cast<unsigned char>(bytes.at(i));
            }
            *emTotal += metrics.widthOf(code);
            ++(*glyphs);
            if (!metrics.composite && code == 32) ++(*spaces);
        }
    }

    double computeAdvance(int operandIndex, int operatorIndex, const QByteArray& op, double charSpacing,
                          double wordSpacing) const {
        const FontMetrics metrics = fonts_.value(currentFont_);
        double emTotal = 0.0;
        int glyphs = 0;
        int spaces = 0;

        if (op == "TJ") {
            for (int i = operandIndex; i < operatorIndex; ++i) {
                const ContentToken& token = tokens_.at(i);
                if (token.type == ContentTokenType::LiteralString ||
                    token.type == ContentTokenType::HexString) {
                    accumulateString(decodeStringBytes(token.raw, token.type), metrics, &emTotal, &glyphs,
                                     &spaces);
                } else if (token.type == ContentTokenType::Number) {
                    bool ok = false;
                    const double kerning = token.raw.toDouble(&ok);
                    if (ok) emTotal -= kerning / 1000.0;
                }
            }
        } else if (operandIndex >= 0 && operandIndex < tokens_.size()) {
            const ContentToken& token = tokens_.at(operandIndex);
            accumulateString(decodeStringBytes(token.raw, token.type), metrics, &emTotal, &glyphs, &spaces);
        }

        return (emTotal * text_.fontSize + charSpacing * glyphs + wordSpacing * spaces) *
               text_.horizontalScale;
    }

    // 这一处显示操作符显示出来的文字。
    QString textOf(int operandIndex, int operatorIndex, const QByteArray& op) const {
        const FontDecoder decoder = decoders_.value(currentFont_);
        QString out;
        if (op == "TJ") {
            for (int i = operandIndex; i < operatorIndex; ++i) {
                const ContentToken& token = tokens_.at(i);
                if (token.type == ContentTokenType::LiteralString ||
                    token.type == ContentTokenType::HexString) {
                    out += decodeFontBytes(decodeStringBytes(token.raw, token.type), decoder);
                }
            }
            return out;
        }
        if (operandIndex >= 0 && operandIndex < tokens_.size()) {
            const ContentToken& token = tokens_.at(operandIndex);
            out += decodeFontBytes(decodeStringBytes(token.raw, token.type), decoder);
        }
        return out;
    }

    void showText(int operatorIndex, const QByteArray& op, QVector<TextRun>* runs) {
        const int operandIndex = operandTokenFor(operatorIndex, op);
        if (operandIndex < 0) return;

        // ' 与 " 先换行再显示文字。
        if (op == "'" || op == "\"") {
            lineMatrix_ = multiply(translation(0.0, -text_.leading), lineMatrix_);
            textMatrix_ = lineMatrix_;
        }

        // " 的两个额外操作数（词间距、字符间距）只在这一次显示里生效。
        double charSpacing = text_.charSpacing;
        double wordSpacing = text_.wordSpacing;
        if (op == "\"") {
            const int stringIndex = operandIndex;
            const int spacingIndex = previousSignificant(tokens_, stringIndex - 1);
            const int wordIndex = spacingIndex >= 0 ? previousSignificant(tokens_, spacingIndex - 1) : -1;
            bool okWord = false;
            bool okChar = false;
            const double wordValue =
                wordIndex >= 0 ? tokens_.at(wordIndex).raw.toDouble(&okWord) : 0.0;
            const double charValue =
                spacingIndex >= 0 ? tokens_.at(spacingIndex).raw.toDouble(&okChar) : 0.0;
            if (okWord) wordSpacing = wordValue;
            if (okChar) charSpacing = charValue;
        }

        const Matrix rendering = multiply(textMatrix_, ctm_);
        QPointF origin(rendering.e, rendering.f);
        if (text_.rise != 0.0) {
            origin += transformPoint(rendering, QPointF(0.0, text_.rise));
        }

        const double advance = computeAdvance(operandIndex, operatorIndex, op, charSpacing, wordSpacing);

        TextRun run;
        run.operatorToken = operatorIndex;
        run.operandToken = operandIndex;
        run.origin = origin;
        run.baseline = origin.y();
        run.fontSize = text_.fontSize;
        run.bold = decoders_.value(currentFont_).bold;
        run.text = textOf(operandIndex, operatorIndex, op);
        run.streamOrder = runs->size();
        const double directionX = rendering.a;
        const double directionY = rendering.b;
        const double directionLength = std::hypot(directionX, directionY);
        if (directionLength > 1e-9) {
            run.end = origin + QPointF(directionX / directionLength * advance,
                                       directionY / directionLength * advance);
        } else {
            run.end = origin + QPointF(advance, 0.0);
        }
        runs->append(run);

        // 显示完文字，文本矩阵按推进量往前走。
        textMatrix_ = multiply(translation(advance, 0.0), textMatrix_);
    }

    // 内容流里的 Do：画一个 XObject。
    //
    // 图像画在单位方格 [0,1]² 上，再用当前的 CTM 变换——所以图像的框
    // 就是单位方格的四个角经 CTM 变换后的包围盒。
    void recordImage(int operatorIndex, QVector<ScannedImage>* images) {
        if (images == nullptr) return;
        const int nameIndex = previousSignificant(tokens_, operatorIndex - 1);
        if (nameIndex < 0 || tokens_.at(nameIndex).type != ContentTokenType::Name) return;
        const QString name = QString::fromLatin1(tokens_.at(nameIndex).raw.mid(1));
        const auto found = imageResources_.constFind(name);
        if (found == imageResources_.constEnd() || !found.value().isImage) return;

        const QPointF corners[4] = {transformPoint(ctm_, QPointF(0.0, 0.0)),
                                    transformPoint(ctm_, QPointF(1.0, 0.0)),
                                    transformPoint(ctm_, QPointF(0.0, 1.0)),
                                    transformPoint(ctm_, QPointF(1.0, 1.0))};
        double left = corners[0].x();
        double right = left;
        double bottom = corners[0].y();
        double top = bottom;
        for (const QPointF& corner : corners) {
            left = std::min(left, corner.x());
            right = std::max(right, corner.x());
            bottom = std::min(bottom, corner.y());
            top = std::max(top, corner.y());
        }
        ScannedImage image;
        image.bounds = QRectF(QPointF(left, bottom), QPointF(right, top));
        image.name = name;
        image.objectId = found.value().reference.objectId;
        image.generation = found.value().reference.generation;
        images->append(image);
    }

    const QVector<ContentToken>& tokens_;
    const QHash<QString, FontMetrics>& fonts_;
    const QHash<QString, FontDecoder>& decoders_;
    const QHash<QString, ImageResource>& imageResources_;
    QVector<Matrix> graphicsStack_;
    Matrix ctm_;
    Matrix textMatrix_;
    Matrix lineMatrix_;
    TextState text_;
    QString currentFont_;
};

#ifdef AIPDF_WITH_QPDF

// 资源字典里的键名（qpdf 的 getKeys() 返回的是**带前导斜杠**的名字）。
//
// 为什么要去掉斜杠：内容流里的资源名是「不带斜杠」的（/Im1 Do 里的 Im1），
// 两边对不上就等于这张表从没被查过——
// 字宽表查不到（推进量只能估）、字体解码查不到（粗体认不出）、
// 图像资源查不到（**图根本扫不出来**，结构树里就没有 /Figure）。
QString resourceKey(const std::string& key) {
    QString name = QString::fromStdString(key);
    if (name.startsWith(QLatin1Char('/'))) name.remove(0, 1);
    return name;
}

// 对象的字典：流对象本身**不是**字典（qpdf 里 isDictionary() 对流返回假），
// 要拿它的字典得走 getDict()。
//
// 为什么要专门写这一句：图像 XObject 是流。用 isDictionary() 判它，
// 等于把整张图从资源表里丢掉——结构树里永远不会有 /Figure，
// 而且没有任何报错，只是「图不见了」。
QPDFObjectHandle dictionaryOf(const QPDFObjectHandle& object) {
    return object.isStream() ? object.getDict() : object;
}

// 从页面的 /Resources /Font 里读字宽表。
QHash<QString, FontMetrics> loadFontMetrics(const QPDFObjectHandle& page) {
    QHash<QString, FontMetrics> result;
    const QPDFObjectHandle resources = page.getKey("/Resources");
    if (!resources.isDictionary()) return result;
    const QPDFObjectHandle fonts = resources.getKey("/Font");
    if (!fonts.isDictionary()) return result;

    for (const std::string& key : fonts.getKeys()) {
        const QPDFObjectHandle font = fonts.getKey(key);
        if (!font.isDictionary()) continue;
        FontMetrics metrics;
        const QPDFObjectHandle subtype = font.getKey("/Subtype");
        metrics.composite = subtype.isName() && subtype.getName() == "/Type0";
        const QPDFObjectHandle firstChar = font.getKey("/FirstChar");
        const QPDFObjectHandle widths = font.getKey("/Widths");
        if (firstChar.isInteger() && widths.isArray()) {
            metrics.firstChar = static_cast<int>(firstChar.getIntValue());
            for (const QPDFObjectHandle& width : widths.getArrayAsVector()) {
                metrics.widths.append(width.isNumber() ? width.getIntValueAsInt() / 1000.0 : 0.0);
            }
        }
        result.insert(resourceKey(key), metrics);
    }
    return result;
}

// 从页面的 /Resources /Font 里读解码信息（ToUnicode / Differences / 粗体）。
QHash<QString, FontDecoder> loadFontDecoders(const QPDFObjectHandle& page) {
    QHash<QString, FontDecoder> result;
    const QPDFObjectHandle resources = page.getKey("/Resources");
    if (!resources.isDictionary()) return result;
    const QPDFObjectHandle fonts = resources.getKey("/Font");
    if (!fonts.isDictionary()) return result;

    for (const std::string& key : fonts.getKeys()) {
        const QPDFObjectHandle font = fonts.getKey(key);
        if (!font.isDictionary()) continue;
        FontDecoder decoder;
        const QPDFObjectHandle subtype = font.getKey("/Subtype");
        decoder.composite = subtype.isName() && subtype.getName() == "/Type0";
        decoder.codeBytes = decoder.composite ? 2 : 1;

        const QPDFObjectHandle baseFont = font.getKey("/BaseFont");
        if (baseFont.isName()) {
            const QString name = QString::fromUtf8(baseFont.getName().c_str());
            decoder.bold = name.contains(QLatin1String("Bold")) || name.contains(QLatin1String("Black")) ||
                           name.contains(QLatin1String("Heavy")) ||
                           name.contains(QLatin1String("Semibold"));
        }

        const QPDFObjectHandle toUnicode = font.getKey("/ToUnicode");
        if (toUnicode.isStream()) {
            QPDFObjectHandle stream = toUnicode;
            std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
            if (data) {
                const QByteArray cmap(reinterpret_cast<const char*>(data->getBuffer()),
                                      static_cast<qsizetype>(data->getSize()));
                parseToUnicode(cmap, &decoder.toUnicode);
            }
        }

        const QPDFObjectHandle encoding = font.getKey("/Encoding");
        if (encoding.isDictionary()) {
            const QPDFObjectHandle differences = encoding.getKey("/Differences");
            if (differences.isArray()) {
                int code = 0;
                for (const QPDFObjectHandle& item : differences.getArrayAsVector()) {
                    if (item.isNumber()) {
                        code = item.getIntValueAsInt();
                        continue;
                    }
                    if (!item.isName()) continue;
                    QByteArray glyphName = item.getName().c_str();
                    if (glyphName.startsWith('/')) glyphName.remove(0, 1);
                    decoder.differences.insert(code, glyphName);
                    ++code;
                }
            }
        }
        result.insert(resourceKey(key), decoder);
    }
    return result;
}

// 页资源字典里哪些 XObject 是图像（以及它们的对象号）。
QHash<QString, ImageResource> loadImageResources(const QPDFObjectHandle& page) {
    QHash<QString, ImageResource> result;
    const QPDFObjectHandle resources = page.getKey("/Resources");
    if (!resources.isDictionary()) return result;
    const QPDFObjectHandle xobjects = resources.getKey("/XObject");
    if (!xobjects.isDictionary()) return result;
    for (const std::string& key : xobjects.getKeys()) {
        const QPDFObjectHandle object = xobjects.getKey(key);
        // 图像是**流**：用 isDictionary() 判会被丢掉，图就永远扫不出来。
        const QPDFObjectHandle dictionary = dictionaryOf(object);
        if (!dictionary.isDictionary()) continue;
        const QPDFObjectHandle subtype = dictionary.getKey("/Subtype");
        ImageResource resource;
        resource.isImage = subtype.isName() && subtype.getName() == "/Image";
        resource.reference.objectId = object.isIndirect() ? object.getObjectID() : -1;
        resource.reference.generation = object.isIndirect() ? object.getGeneration() : 0;
        result.insert(resourceKey(key), resource);
    }
    return result;
}

// 一页的打标记结果。
struct PageTagging {
    int mcidCount = 0;               // 这一页的 MCID 总数
    QVector<QVector<int>> spanMcids;  // 每个 Span 的 MCID 列表（已按调用方给的 order 排好）
};

// 把 BDC / EMC 插进内容流。
//
// 只做插入，不动任何一个原有 token：页面外观必然不变。
QByteArray insertMarkedContent(const QVector<ContentToken>& tokens, const QVector<TextRun>& runs,
                                const QVector<int>& mcids) {
    QHash<int, int> bdcAt;  // 操作数 token 下标 → MCID
    QHash<int, int> emcAt;  // 操作符 token 下标 → MCID
    for (int i = 0; i < runs.size() && i < mcids.size(); ++i) {
        bdcAt.insert(runs.at(i).operandToken, mcids.at(i));
        emcAt.insert(runs.at(i).operatorToken, mcids.at(i));
    }

    QByteArray output;
    for (int i = 0; i < tokens.size(); ++i) {
        const auto bdc = bdcAt.constFind(i);
        if (bdc != bdcAt.constEnd()) {
            output += kMarkedContentTag;
            output += " <</MCID ";
            output += QByteArray::number(bdc.value());
            output += ">> BDC ";
        }
        output += tokens.at(i).raw;
        const auto emc = emcAt.constFind(i);
        if (emc != emcAt.constEnd()) {
            output += " EMC";
        }
    }
    return output;
}

// 一页的内容流与扫描结果（共用件内部用）。
struct LoadedPage {
    QVector<QPDFObjectHandle> handles;
    QVector<QVector<ContentToken>> tokens;
    QVector<TextRun> runs;      // 全部文本运行（内容流次序）
    QVector<ScannedImage> images;
    QVector<int> visualOrder;   // 视觉次序下的运行下标
    QVector<int> mcidOf;         // 运行下标 → MCID
    int mcidCount = 0;
};

// 处理一页：读内容流 → 摘掉旧标记 → 扫文本运行 → 按视觉顺序发 MCID。
//
// 页面句柄用非 const 引用：qpdf 的修改接口（replaceKey / coalesceContentStreams /
// replaceStreamData）都不是 const 方法。
bool loadPage(QPDFObjectHandle& page, LoadedPage* out, QString* error) {
    page.coalesceContentStreams();
    // 注意：qpdf 的对象句柄只有非 const 版本能改（replaceStreamData / replaceKey）。
    std::vector<QPDFObjectHandle> contents = page.getPageContents();

    const QHash<QString, FontMetrics> fonts = loadFontMetrics(page);
    const QHash<QString, FontDecoder> decoders = loadFontDecoders(page);
    const QHash<QString, ImageResource> imageResources = loadImageResources(page);

    // 内容流句柄与 token 列表一一对应：contents 里可能有不是流的对象，
    // 跳过它们时两边必须同步跳过，否则写回时会写错流。
    for (size_t s = 0; s < contents.size(); ++s) {
        QPDFObjectHandle stream = contents.at(s);
        if (!stream.isStream()) continue;
        std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
        if (!data) continue;
        const QByteArray raw(reinterpret_cast<const char*>(data->getBuffer()),
                             static_cast<qsizetype>(data->getSize()));
        const ContentParseResult parsed = ContentStream::tokenize(raw);
        if (!parsed.isValid()) {
            if (error) {
                *error = i18n::tr("aipdf::pdf::TagTreeWriter", "内容流无法解析：%1").arg(parsed.error);
            }
            return false;
        }
        // 先摘掉上一轮打过的标记，再重新打：同一份文档反复「应用区域顺序」
        // 时不能把同一个显示操作符套两层 BDC（MCID 会重复）。
        const QVector<ContentToken> tokens = stripMarkedContent(parsed.tokens);
        RunScanner scanner(tokens, fonts, decoders, imageResources);
        const int before = out->runs.size();
        scanner.scan(&out->runs, &out->images);
        for (int i = before; i < out->runs.size(); ++i) out->runs[i].streamIndex = out->tokens.size();
        out->handles.append(stream);
        out->tokens.append(tokens);
    }

    if (out->runs.isEmpty()) return true;

    // 视觉顺序：先按基线从上到下聚成行，行内按 x 从左到右。
    //
    // 为什么按视觉顺序发 MCID：MCID 只是页内标签，真正决定读者顺序的是
    // 标签树里元素的排列。但按视觉顺序发号，能让「同一块内容」的 MCID
    // 连成一段，正好对上 PDF 规范里 /K 数组的常见形态。
    QVector<int> visualOrder(out->runs.size());
    std::iota(visualOrder.begin(), visualOrder.end(), 0);
    {
        // 先把基线聚成「行」：基线相差在半个字高以内的算同一行。
        // 聚类单独做，不用带容差的比较器——带容差的比较器不是严格弱序，
        // 排序结果不可预期。
        QVector<int> byBaseline(out->runs.size());
        std::iota(byBaseline.begin(), byBaseline.end(), 0);
        std::stable_sort(byBaseline.begin(), byBaseline.end(), [&out](int lhs, int rhs) {
            return out->runs.at(lhs).baseline > out->runs.at(rhs).baseline;
        });
        QVector<int> lineOf(out->runs.size(), 0);
        int line = 0;
        double lineBaseline = 0.0;
        for (int i = 0; i < byBaseline.size(); ++i) {
            const int index = byBaseline.at(i);
            const double tolerance = std::max(1.0, 0.5 * std::max(out->runs.at(index).fontSize, 1.0));
            if (i == 0 || out->runs.at(index).baseline < lineBaseline - tolerance) {
                ++line;
                lineBaseline = out->runs.at(index).baseline;
            }
            lineOf[index] = line;
        }
        std::stable_sort(visualOrder.begin(), visualOrder.end(), [&](int lhs, int rhs) {
            const int leftLine = lineOf.at(lhs);
            const int rightLine = lineOf.at(rhs);
            if (leftLine != rightLine) return leftLine < rightLine;
            const double leftX = out->runs.at(lhs).origin.x();
            const double rightX = out->runs.at(rhs).origin.x();
            if (std::abs(leftX - rightX) > 0.01) return leftX < rightX;
            return out->runs.at(lhs).streamOrder < out->runs.at(rhs).streamOrder;
        });
    }

    out->visualOrder = visualOrder;
    out->mcidOf.assign(out->runs.size(), -1);
    for (int position = 0; position < visualOrder.size(); ++position) {
        out->mcidOf[visualOrder.at(position)] = position;
    }
    out->mcidCount = static_cast<int>(out->runs.size());
    return true;
}

// 把运行按基线聚成行（扫描结果对外暴露的那部分）。
QVector<ScannedTextLine> groupRunsIntoLines(const QVector<TextRun>& runs) {
    QVector<ScannedTextLine> lines;
    if (runs.isEmpty()) return lines;

    QVector<int> order(runs.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&runs](int lhs, int rhs) {
        if (std::abs(runs.at(lhs).baseline - runs.at(rhs).baseline) > 0.01) {
            return runs.at(lhs).baseline > runs.at(rhs).baseline;
        }
        return runs.at(lhs).origin.x() < runs.at(rhs).origin.x();
    });

    int current = -1;
    double currentBaseline = 0.0;
    for (int index : order) {
        const TextRun& run = runs.at(index);
        const double tolerance = std::max(1.0, 0.5 * std::max(run.fontSize, 1.0));
        if (current < 0 || run.baseline < currentBaseline - tolerance) {
            lines.append(ScannedTextLine());
            current = lines.size() - 1;
            currentBaseline = run.baseline;
        }
        ScannedTextLine& line = lines[current];
        line.runIndices.append(index);
        line.fontSize = std::max(line.fontSize, run.fontSize);
        line.bold = line.bold || run.bold;
        line.text += run.text;
        const QPointF end = run.end;
        if (line.bounds.isNull() || line.bounds.isEmpty()) {
            line.bounds = QRectF(run.origin, end);
        } else {
            line.bounds = line.bounds.united(QRectF(run.origin, end));
        }
        // 行的框要按「基线 + 字高」算：字形画在基线**上方**，
        // 所以框的上边是基线 + 字高，下边是基线。
        const double top = run.baseline + std::max(run.fontSize, 1.0);
        const double bottom = run.baseline;
        line.bounds = line.bounds.united(QRectF(QPointF(run.origin.x(), bottom),
                                                QPointF(std::max(end.x(), run.origin.x()), top)));
    }
    return lines;
}

// 文本运行 → 对外的扫描结果。
ScannedTextRun toScannedRun(const TextRun& run) {
    ScannedTextRun result;
    result.origin = run.origin;
    result.end = run.end;
    result.baseline = run.baseline;
    result.fontSize = run.fontSize;
    result.text = run.text;
    result.bold = run.bold;
    result.streamIndex = run.streamIndex;
    result.streamOrder = run.streamOrder;
    return result;
}

// 把一份打开的 PDF 按计划写成带标签的 PDF（共用写手的核心）。
//
// 做的事：每页读内容流 → 摘旧标记 → 扫文本运行 → 按块发 MCID → 改写内容流；
// 然后按计划建结构元素、建 /ParentTree、写 catalog。
bool applyStructPlan(QPDF& pdf, std::vector<QPDFObjectHandle>& pages, const StructWritePlan& plan,
                     QString* error) {
    // ---- 1. 每页：读内容流、扫文本运行 ----
    QVector<LoadedPage> loaded(static_cast<int>(pages.size()));
    for (size_t i = 0; i < pages.size(); ++i) {
        QPDFObjectHandle page = pages.at(i);
        if (!loadPage(page, &loaded[static_cast<int>(i)], error)) return false;
    }

    // ---- 2. 块的 MCID 列表 ----
    // 页 → 块 → MCID 列表
    QVector<QVector<QVector<int>>> blockMcids(static_cast<int>(pages.size()));
    for (int pageIndex = 0; pageIndex < static_cast<int>(pages.size()); ++pageIndex) {
        const LoadedPage& page = loaded.at(pageIndex);
        const QVector<QVector<int>> blocks =
            pageIndex < plan.pageBlockRuns.size() ? plan.pageBlockRuns.at(pageIndex)
                                                  : QVector<QVector<int>>();
        QVector<QVector<int>> mcids;
        for (const QVector<int>& runIndices : blocks) {
            QVector<int> block;
            for (int runIndex : runIndices) {
                if (runIndex < 0 || runIndex >= page.mcidOf.size()) continue;
                const int mcid = page.mcidOf.at(runIndex);
                if (mcid < 0) continue;
                block.append(mcid);
            }
            mcids.append(block);
        }
        blockMcids[pageIndex] = mcids;
    }

    // ---- 3. 改写内容流：把 BDC / EMC 插进去，其余一个字节都不动 ----
    for (int pageIndex = 0; pageIndex < static_cast<int>(pages.size()); ++pageIndex) {
        LoadedPage& page = loaded[pageIndex];
        const QVector<QVector<int>>& blocks =
            pageIndex < plan.pageBlockRuns.size() ? plan.pageBlockRuns.at(pageIndex)
                                                  : QVector<QVector<int>>();
        if (page.tokens.isEmpty()) continue;
        for (int s = 0; s < page.tokens.size(); ++s) {
            QVector<TextRun> streamRuns;
            QVector<int> streamMcids;
            for (int i = 0; i < page.runs.size(); ++i) {
                if (page.runs.at(i).streamIndex != s) continue;
                streamRuns.append(page.runs.at(i));
                streamMcids.append(page.mcidOf.at(i));
            }
            if (streamRuns.isEmpty()) continue;
            const QByteArray rewritten = insertMarkedContent(page.tokens.at(s), streamRuns, streamMcids);
            // qpdf 的句柄只有非 const 版本能改，所以这里先取一份可改的副本。
            QPDFObjectHandle handle = page.handles.at(s);
            handle.replaceStreamData(rewritten.toStdString(), QPDFObjectHandle::newNull(),
                                     QPDFObjectHandle::newNull());
        }
        Q_UNUSED(blocks)
    }

    // ---- 4. 结构元素 ----
    QPDFObjectHandle catalog = pdf.getRoot();
    if (!catalog.isDictionary()) {
        if (error) *error = i18n::tr("aipdf::pdf::TagTreeWriter", "PDF 的根对象不是字典");
        return false;
    }

    // 页的 /StructParents 键：沿用计划给的，没有就按页序从 0 连续分配。
    QVector<int> pageKeys(static_cast<int>(pages.size()), -1);
    {
        QSet<int> used;
        for (int i = 0; i < static_cast<int>(plan.pageStructParentKeys.size()) && i < pageKeys.size(); ++i) {
            const int candidate = plan.pageStructParentKeys.at(i);
            if (candidate < 0 || used.contains(candidate)) continue;
            pageKeys[i] = candidate;
            used.insert(candidate);
        }
        int next = 0;
        for (int i = 0; i < pageKeys.size(); ++i) {
            if (pageKeys.at(i) >= 0) continue;
            while (used.contains(next)) ++next;
            pageKeys[i] = next;
            used.insert(next);
            ++next;
        }
    }

    // 每页的 /ParentTree 数组：先放 MCID 条目，表单域的 widget 接在后面。
    QVector<QVector<QPDFObjectHandle>> parentTreeArrays(static_cast<int>(pages.size()));
    for (int pageIndex = 0; pageIndex < static_cast<int>(pages.size()); ++pageIndex) {
        parentTreeArrays[pageIndex].resize(static_cast<size_t>(loaded.at(pageIndex).mcidCount));
    }

    // 先建元素（父元素要先建出来，子元素才能指 /P）。
    QVector<QPDFObjectHandle> elementRefs(plan.elements.size());
    for (int i = 0; i < plan.elements.size(); ++i) {
        const StructElementPlan& element = plan.elements.at(i);
        QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
        dict.replaceKey("/Type", QPDFObjectHandle::newName("/StructElem"));
        dict.replaceKey("/S", QPDFObjectHandle::newName("/" + element.type.toStdString()));
        if (!element.title.isEmpty()) {
            dict.replaceKey("/T", QPDFObjectHandle::newUnicodeString(element.title.toUtf8().toStdString()));
        }
        if (!element.alt.isEmpty()) {
            dict.replaceKey("/Alt", QPDFObjectHandle::newUnicodeString(element.alt.toUtf8().toStdString()));
        }
        if (!element.actualText.isEmpty()) {
            dict.replaceKey("/ActualText",
                            QPDFObjectHandle::newUnicodeString(element.actualText.toUtf8().toStdString()));
        }
        if (!element.language.isEmpty()) {
            dict.replaceKey("/Lang",
                            QPDFObjectHandle::newUnicodeString(element.language.toUtf8().toStdString()));
        }
        if (!element.scope.isEmpty()) {
            dict.replaceKey("/Scope", QPDFObjectHandle::newName("/" + element.scope.toStdString()));
        }
        if (element.pageIndex >= 0 && element.pageIndex < static_cast<int>(pages.size()) &&
            !element.inheritPage && pages.at(static_cast<size_t>(element.pageIndex)).isIndirect()) {
            dict.replaceKey("/Pg", pages.at(static_cast<size_t>(element.pageIndex)));
        }
        if (element.parentIndex >= 0 && element.parentIndex < elementRefs.size() &&
            elementRefs.at(element.parentIndex).isDictionary()) {
            dict.replaceKey("/P", elementRefs.at(element.parentIndex));
        }
        elementRefs[i] = pdf.makeIndirectObject(dict);
    }

    // 再填内容（/K）与 /ParentTree。
    for (int i = 0; i < plan.elements.size(); ++i) {
        const StructElementPlan& element = plan.elements.at(i);
        QPDFObjectHandle dict = elementRefs.at(i);

        // 表单域：/K 是 widget 注解，widget 拿 /StructParent，
        // /ParentTree 里对应下标指向这个结构元素。
        if (!element.annotations.isEmpty()) {
            std::vector<QPDFObjectHandle> widgets;
            for (const ObjectRef& reference : element.annotations) {
                if (reference.pageIndex < 0 || reference.pageIndex >= static_cast<int>(pages.size())) continue;
                QPDFObjectHandle widget = pdf.getObject(reference.objectId, reference.generation);
                if (!widget.isDictionary()) continue;
                widgets.push_back(widget);
                QVector<QPDFObjectHandle>& array = parentTreeArrays[reference.pageIndex];
                const int index = array.size();
                array.append(elementRefs.at(i));
                widget.replaceKey("/StructParent", QPDFObjectHandle::newInteger(index));
            }
            if (!widgets.empty()) {
                dict.replaceKey("/K", QPDFObjectHandle::newArray(widgets));
            }
            continue;
        }

        // 图像对象：/K 写成 /OBJR（引用页面上的图像 XObject）。
        //
        // 为什么用 /OBJR 而不是标记内容：图像是「画」出来的，
        // 它在内容流里不是一段文字；/OBJR 正是 PDF 为这种情况准备的
        // （PDF 32000-1 §14.8.4.3）：结构元素直接指向那个 XObject。
        if (!element.objectRefs.isEmpty()) {
            std::vector<QPDFObjectHandle> references;
            for (const ObjectRef& reference : element.objectRefs) {
                if (reference.objectId < 0) continue;
                QPDFObjectHandle object = pdf.getObject(reference.objectId, reference.generation);
                // 图像 XObject 是流，不是字典：这里也必须放行，否则 /K 是空的。
                if (!dictionaryOf(object).isDictionary()) continue;
                QPDFObjectHandle objectReference = QPDFObjectHandle::newDictionary();
                objectReference.replaceKey("/Type", QPDFObjectHandle::newName("/OBJR"));
                objectReference.replaceKey("/Obj", object);
                if (reference.pageIndex >= 0 &&
                    reference.pageIndex < static_cast<int>(pages.size())) {
                    objectReference.replaceKey("/Pg", pages.at(static_cast<size_t>(reference.pageIndex)));
                }
                references.push_back(objectReference);
            }
            if (!references.empty()) {
                dict.replaceKey("/K", QPDFObjectHandle::newArray(references));
            }
            continue;
        }

        // 文本块：/K 是 MCID 数组。
        if (element.blockIndices.isEmpty()) continue;
        const int pageIndex = element.pageIndex;
        if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) continue;
        const QVector<QVector<int>>& blocks = blockMcids.at(pageIndex);
        QVector<int> mcids;
        for (int blockIndex : element.blockIndices) {
            if (blockIndex < 0 || blockIndex >= blocks.size()) continue;
            for (int mcid : blocks.at(blockIndex)) {
                if (mcids.contains(mcid)) continue;
                mcids.append(mcid);
            }
        }
        if (mcids.isEmpty()) continue;
        std::vector<QPDFObjectHandle> mcidArray;
        mcidArray.reserve(static_cast<size_t>(mcids.size()));
        for (int mcid : mcids) mcidArray.push_back(QPDFObjectHandle::newInteger(mcid));
        dict.replaceKey("/K", QPDFObjectHandle::newArray(mcidArray));
        for (int mcid : mcids) {
            QVector<QPDFObjectHandle>& array = parentTreeArrays[pageIndex];
            if (mcid < 0 || mcid >= array.size()) continue;
            array[mcid] = elementRefs.at(i);
        }
    }

    // ---- 5. 树 ----
    // 根元素（elements[0]）的子节点按计划里的 childIndices 排。
    for (int i = 0; i < plan.elements.size(); ++i) {
        const StructElementPlan& element = plan.elements.at(i);
        if (element.childIndices.isEmpty()) continue;
        std::vector<QPDFObjectHandle> children;
        for (int child : element.childIndices) {
            if (child < 0 || child >= elementRefs.size()) continue;
            children.push_back(elementRefs.at(child));
        }
        if (children.empty()) continue;
        // 拷一份可改的：qpdf 的句柄只有非 const 版本能改，
        // 而句柄本身是共享的——副本改的是同一个对象。
        QPDFObjectHandle elementHandle = elementRefs.at(i);
        elementHandle.replaceKey("/K", QPDFObjectHandle::newArray(children));
    }

    // ---- 6. /ParentTree 与 catalog ----
    std::vector<QPDFObjectHandle> nums;
    for (int pageIndex = 0; pageIndex < parentTreeArrays.size(); ++pageIndex) {
        std::vector<QPDFObjectHandle> items;
        for (const QPDFObjectHandle& item : parentTreeArrays.at(pageIndex)) items.push_back(item);
        nums.push_back(QPDFObjectHandle::newInteger(pageKeys.at(pageIndex)));
        nums.push_back(QPDFObjectHandle::newArray(items));
    }
    QPDFObjectHandle parentTree = QPDFObjectHandle::newDictionary();
    parentTree.replaceKey("/Nums", QPDFObjectHandle::newArray(nums));
    const QPDFObjectHandle parentTreeRef = pdf.makeIndirectObject(parentTree);

    QPDFObjectHandle structTreeRoot = QPDFObjectHandle::newDictionary();
    structTreeRoot.replaceKey("/Type", QPDFObjectHandle::newName("/StructTreeRoot"));
    if (!plan.elements.isEmpty()) {
        const int root = qBound(0, plan.rootIndex, static_cast<int>(plan.elements.size()) - 1);
        structTreeRoot.replaceKey("/K", QPDFObjectHandle::newArray({elementRefs.at(root)}));
    }
    structTreeRoot.replaceKey("/ParentTree", parentTreeRef);
    int maxKey = -1;
    for (int key : pageKeys) maxKey = std::max(maxKey, key);
    structTreeRoot.replaceKey("/ParentTreeNextKey", QPDFObjectHandle::newInteger(maxKey + 1));
    const QPDFObjectHandle structTreeRootRef = pdf.makeIndirectObject(structTreeRoot);
    catalog.replaceKey("/StructTreeRoot", structTreeRootRef);

    QPDFObjectHandle markInfo = QPDFObjectHandle::newDictionary();
    markInfo.replaceKey("/Marked", QPDFObjectHandle::newBool(true));
    catalog.replaceKey("/MarkInfo", markInfo);

    // 页上写 /StructParents；按需要写 /Tabs /S。
    for (size_t i = 0; i < pages.size(); ++i) {
        QPDFObjectHandle page = pages.at(i);
        if (!page.isDictionary()) continue;
        page.replaceKey("/StructParents", QPDFObjectHandle::newInteger(pageKeys.at(static_cast<int>(i))));
        if (plan.writeTabsStructureOrder) {
            page.replaceKey("/Tabs", QPDFObjectHandle::newName("/S"));
        }
    }

    // 文档级：/Lang、/DisplayDocTitle、/Metadata、/Info /Title。
    if (!plan.documentLanguage.isEmpty()) {
        catalog.replaceKey("/Lang",
                            QPDFObjectHandle::newUnicodeString(plan.documentLanguage.toUtf8().toStdString()));
    }
    if (plan.displayDocTitle) {
        catalog.replaceKey("/DisplayDocTitle", QPDFObjectHandle::newBool(true));
    }
    if (!plan.xmpMetadata.isEmpty()) {
        QPDFObjectHandle metadata =
            QPDFObjectHandle::newStream(&pdf, plan.xmpMetadata.toStdString());
        // 流对象的字典要用 replaceDict 换：replaceKey 对流对象不生效
        // （qpdf 只会警告一句「ignoring key replacement request」，键就丢了）。
        QPDFObjectHandle metadataDict = QPDFObjectHandle::newDictionary();
        metadataDict.replaceKey("/Type", QPDFObjectHandle::newName("/Metadata"));
        metadataDict.replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));
        metadata.replaceDict(metadataDict);
        catalog.replaceKey("/Metadata", pdf.makeIndirectObject(metadata));
    }
    if (!plan.documentTitle.isEmpty()) {
        QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
        if (!info.isDictionary()) {
            info = QPDFObjectHandle::newDictionary();
            pdf.getTrailer().replaceKey("/Info", pdf.makeIndirectObject(info));
        }
        info.replaceKey("/Title",
                        QPDFObjectHandle::newUnicodeString(plan.documentTitle.toUtf8().toStdString()));
    }
    return true;
}

// 把一份打开的 PDF 写成字节（写完再重开校验一次）。
QByteArray writePdf(QPDF& pdf, QString* error) {
    QPDFWriter writer(pdf);
    // 关掉对象流：交叉引用表保持传统形态，出问题时人眼能直接看。
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.setOutputMemory();
    writer.write();
    std::unique_ptr<Buffer> output(writer.getBuffer());
    if (!output || output->getSize() == 0) {
        if (error) *error = i18n::tr("aipdf::pdf::TagTreeWriter", "qpdf 写出的 PDF 为空");
        return {};
    }
    const QByteArray written(reinterpret_cast<const char*>(output->getBuffer()),
                             static_cast<qsizetype>(output->getSize()));

    // 写完再重开校验一次：结构有问题就地失败，不把坏文件交出去。
    try {
        QPDF check;
        check.processMemoryFile("tag-tree-check.pdf", written.constData(),
                                static_cast<size_t>(written.size()));
        if (check.anyWarnings()) {
            const std::vector<QPDFExc> warnings = check.getWarnings();
            if (error) {
                *error = i18n::tr("aipdf::pdf::TagTreeWriter", "写出的 PDF 结构有问题：%1")
                             .arg(warnings.empty()
                                      ? i18n::tr("aipdf::pdf::TagTreeWriter", "qpdf 没有给出细节")
                                      : QString::fromStdString(warnings.front().what()));
            }
            return {};
        }
    } catch (const std::exception& exception) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::TagTreeWriter", "写出的 PDF 无法重新打开：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
        return {};
    }
    return written;
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

QByteArray writeStructPlan(const QByteArray& pdfBytes, const StructWritePlan& plan, QString* error) {
    if (error) error->clear();
    if (pdfBytes.isEmpty()) {
        if (error) *error = i18n::tr("aipdf::pdf::TagTreeWriter", "输入的 PDF 内容为空");
        return {};
    }
    if (plan.elements.isEmpty()) {
        if (error) *error = i18n::tr("aipdf::pdf::TagTreeWriter", "标签树计划里没有结构元素");
        return {};
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("tag-plan.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        if (pages.empty()) {
            if (error) *error = i18n::tr("aipdf::pdf::TagTreeWriter", "PDF 里没有页面");
            return {};
        }
        if (!applyStructPlan(pdf, pages, plan, error)) return {};
        return writePdf(pdf, error);
    } catch (const std::exception& exception) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::TagTreeWriter", "解析 PDF 失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
        return {};
    }
#else
    if (error) {
        *error = i18n::tr("aipdf::pdf::TagTreeWriter", "当前构建没有启用 qpdf，无法写入标签树");
    }
    return {};
#endif
}

bool scanDocumentContent(const QByteArray& pdfBytes, ScannedDocument* out, QString* error) {
    if (error) error->clear();
    if (out == nullptr) return false;
    out->pages.clear();
    if (pdfBytes.isEmpty()) {
        if (error) *error = i18n::tr("aipdf::pdf::TagTreeWriter", "输入的 PDF 内容为空");
        return false;
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("tag-scan.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        for (size_t i = 0; i < pages.size(); ++i) {
            LoadedPage page;
            QPDFObjectHandle handle = pages.at(i);
            if (!loadPage(handle, &page, error)) return false;
            ScannedPage scanned;
            for (const TextRun& run : page.runs) scanned.runs.append(toScannedRun(run));
            scanned.lines = groupRunsIntoLines(page.runs);
            scanned.images = page.images;
            out->pages.append(scanned);
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::TagTreeWriter", "解析 PDF 失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
        return false;
    }
#else
    if (error) {
        *error = i18n::tr("aipdf::pdf::TagTreeWriter", "当前构建没有启用 qpdf，无法扫描内容流");
    }
    return false;
#endif
}

QByteArray writeTaggedStructure(const QByteArray& pdfBytes,
                                 const QVector<QVector<TaggedRegion>>& pageRegions,
                                 QString* error) {
    if (error) error->clear();
    if (pdfBytes.isEmpty()) {
        if (error) *error = i18n::tr("aipdf::pdf::TagTreeWriter", "输入的 PDF 内容为空");
        return {};
    }

    // 没有区域就什么都不写：原样返回，调用方不必特判。
    bool anyRegion = false;
    for (const QVector<TaggedRegion>& regions : pageRegions) {
        if (!regions.isEmpty()) {
            anyRegion = true;
            break;
        }
    }
    if (!anyRegion) return pdfBytes;

#ifdef AIPDF_WITH_QPDF
    try {
        // 写之前先在内存里重开校验一次：输入本身有问题就别往下走。
        QPDF pdf;
        pdf.processMemoryFile("tag-tree.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        if (pages.empty()) {
            if (error) *error = i18n::tr("aipdf::pdf::TagTreeWriter", "PDF 里没有页面");
            return {};
        }

        // ---- 1. 逐页扫内容流，按「就近匹配」把文本运行归进区域 ----
        QVector<LoadedPage> loaded(static_cast<int>(pages.size()));
        for (size_t i = 0; i < pages.size(); ++i) {
            QPDFObjectHandle page = pages.at(i);
            if (!loadPage(page, &loaded[static_cast<int>(i)], error)) return {};
        }

        QVector<QVector<QVector<int>>> pageBlockRuns(static_cast<int>(pages.size()));
        for (size_t i = 0; i < pages.size(); ++i) {
            const int pageIndex = static_cast<int>(i);
            const LoadedPage& page = loaded.at(pageIndex);
            const QVector<TaggedRegion> regions =
                pageIndex < pageRegions.size() ? pageRegions.at(pageIndex) : QVector<TaggedRegion>();
            QVector<QVector<int>> blocks;
            if (regions.isEmpty() || page.runs.isEmpty()) {
                pageBlockRuns[pageIndex] = blocks;
                continue;
            }

            // 区域按 order 升序排；order 相同按传入次序排。
            QVector<int> regionOrder(regions.size());
            std::iota(regionOrder.begin(), regionOrder.end(), 0);
            std::stable_sort(regionOrder.begin(), regionOrder.end(),
                             [&regions](int lhs, int rhs) { return regions.at(lhs).order < regions.at(rhs).order; });

            // 归入区域：每个显示操作符归到离它最近的那个区域。
            QVector<int> owner(page.runs.size(), -1);
            bool anyInside = false;
            for (int i = 0; i < page.runs.size(); ++i) {
                double best = std::numeric_limits<double>::max();
                int bestRegion = -1;
                for (int position = 0; position < regionOrder.size(); ++position) {
                    const int regionIndex = regionOrder.at(position);
                    const double distance = distanceToRect(page.runs.at(i).origin, page.runs.at(i).end,
                                                          regions.at(regionIndex).rect);
                    if (distance < best - 1e-9) {
                        best = distance;
                        bestRegion = regionIndex;
                    }
                    if (distance <= 0.0) anyInside = true;
                }
                owner[i] = bestRegion;
            }

            // 兜底：矩形与文字完全对不上（一个都没落进矩形里）时，按视觉顺序把整页的
            // 显示操作符均分成若干段，一段对应一个区域。宁可粗一点，也不在内容流里
            // 做有风险的裁剪——写出来的 PDF 必须能打开、页面外观一点不变。
            if (!anyInside && !page.runs.isEmpty()) {
                const int chunkCount = regionOrder.size();
                const int total = static_cast<int>(page.visualOrder.size());
                for (int position = 0; position < total; ++position) {
                    const int chunk = std::min(chunkCount - 1, (position * chunkCount) / total);
                    owner[page.visualOrder.at(position)] = regionOrder.at(chunk);
                }
            }

            for (int position = 0; position < regionOrder.size(); ++position) {
                const int regionIndex = regionOrder.at(position);
                QVector<int> runIndices;
                for (int i = 0; i < page.runs.size(); ++i) {
                    if (owner.at(i) == regionIndex) runIndices.append(i);
                }
                blocks.append(runIndices);
            }
            pageBlockRuns[pageIndex] = blocks;
        }

        // ---- 2. 建标签树计划：Document → 每页一个 /P → 每行一个 /Span ----
        StructWritePlan plan;
        StructElementPlan document;
        document.type = QStringLiteral("Document");
        plan.elements.append(document);

        for (size_t i = 0; i < pages.size(); ++i) {
            const int pageIndex = static_cast<int>(i);
            StructElementPlan pageElement;
            pageElement.type = QStringLiteral("P");
            pageElement.pageIndex = pageIndex;
            pageElement.parentIndex = 0;
            const int pageElementIndex = plan.elements.size();
            plan.elements.append(pageElement);
            plan.elements[0].childIndices.append(pageElementIndex);

            const QVector<QVector<int>>& blocks = pageBlockRuns.at(pageIndex);
            for (int blockIndex = 0; blockIndex < blocks.size(); ++blockIndex) {
                if (blocks.at(blockIndex).isEmpty()) continue;
                StructElementPlan span;
                span.type = QStringLiteral("Span");
                span.pageIndex = pageIndex;
                span.inheritPage = true;
                span.parentIndex = pageElementIndex;
                span.blockIndices.append(blockIndex);
                const int spanIndex = plan.elements.size();
                plan.elements.append(span);
                plan.elements[pageElementIndex].childIndices.append(spanIndex);
            }
        }
        plan.pageBlockRuns = pageBlockRuns;

        // ---- 3. 写 ----
        if (!applyStructPlan(pdf, pages, plan, error)) return {};
        return writePdf(pdf, error);
    } catch (const std::exception& exception) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::TagTreeWriter", "解析 PDF 失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
        return {};
    }
#else
    if (error) {
        *error = i18n::tr("aipdf::pdf::TagTreeWriter", "当前构建没有启用 qpdf，无法写入标签树");
    }
    return {};
#endif
}

QVector<QVector<int>> readTaggedOrder(const QByteArray& pdfBytes, QString* error) {
    if (error) error->clear();
#ifdef AIPDF_WITH_QPDF
    if (pdfBytes.isEmpty()) {
        if (error) *error = i18n::tr("aipdf::pdf::TagTreeWriter", "输入的 PDF 内容为空");
        return {};
    }
    try {
        QPDF pdf;
        pdf.processMemoryFile("tag-tree-read.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const QPDFObjectHandle catalog = pdf.getRoot();
        const QPDFObjectHandle structTreeRoot = catalog.getKey("/StructTreeRoot");
        if (!structTreeRoot.isDictionary()) return {};

        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        QVector<QVector<int>> result(static_cast<int>(pages.size()));

        // StructTreeRoot 的 /K 是「Document 元素」的数组（规范也允许直接给一个
        // 字典），Document 的 /K 才是各页的 /P 元素。
        const QPDFObjectHandle rootChildren = structTreeRoot.getKey("/K");
        QVector<QPDFObjectHandle> documentElements;
        if (rootChildren.isDictionary()) {
            documentElements.append(rootChildren);
        } else if (rootChildren.isArray()) {
            for (const QPDFObjectHandle& child : rootChildren.getArrayAsVector()) {
                if (child.isDictionary()) documentElements.append(child);
            }
        }

        for (const QPDFObjectHandle& documentElement : documentElements) {
            const QPDFObjectHandle pageChildren = documentElement.getKey("/K");
            if (!pageChildren.isArray()) continue;
            for (const QPDFObjectHandle& child : pageChildren.getArrayAsVector()) {
                if (!child.isDictionary()) continue;
                const QPDFObjectHandle pageObject = child.getKey("/Pg");
                int pageIndex = -1;
                if (pageObject.isIndirect()) {
                    for (size_t i = 0; i < pages.size(); ++i) {
                        if (pages.at(i).getObjectID() == pageObject.getObjectID()) {
                            pageIndex = static_cast<int>(i);
                            break;
                        }
                    }
                }
                if (pageIndex < 0 || pageIndex >= result.size()) continue;

                // 这一页的 Span：先按最小 MCID 排出稳定身份，再按树顺序输出。
                const QPDFObjectHandle children = child.getKey("/K");
                if (!children.isArray()) continue;
                QVector<QPair<int, int>> spans;  // (最小 MCID, 树里的位置)
                const std::vector<QPDFObjectHandle> spanArray = children.getArrayAsVector();
                for (size_t i = 0; i < spanArray.size(); ++i) {
                    const QPDFObjectHandle span = spanArray.at(i);
                    const QPDFObjectHandle keys = span.getKey("/K");
                    if (!keys.isArray()) continue;
                    int minimum = -1;
                    for (const QPDFObjectHandle& key : keys.getArrayAsVector()) {
                        if (!key.isInteger()) continue;
                        const int value = static_cast<int>(key.getIntValue());
                        if (minimum < 0 || value < minimum) minimum = value;
                    }
                    if (minimum < 0) continue;
                    spans.append({minimum, static_cast<int>(i)});
                }
                std::stable_sort(spans.begin(), spans.end(),
                                 [](const QPair<int, int>& lhs, const QPair<int, int>& rhs) {
                                     return lhs.first < rhs.first;
                                 });
                QVector<int> identity(spanArray.size(), -1);
                for (int i = 0; i < spans.size(); ++i) identity[spans.at(i).second] = i;
                for (int i = 0; i < spanArray.size(); ++i) {
                    if (identity.at(i) >= 0) result[pageIndex].append(identity.at(i));
                }
            }
        }
        return result;
    } catch (const std::exception& exception) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::TagTreeWriter", "解析 PDF 失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
        return {};
    }
#else
    Q_UNUSED(pdfBytes)
    if (error) {
        *error = i18n::tr("aipdf::pdf::TagTreeWriter", "当前构建没有启用 qpdf，无法读取标签树");
    }
    return {};
#endif
}

}  // namespace aipdf::pdf
