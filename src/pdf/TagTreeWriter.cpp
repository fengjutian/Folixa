#include "pdf/TagTreeWriter.h"

#include "i18n/Translate.h"

#include "pdf/ContentStream.h"

#include <QHash>
#include <QPointF>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <numeric>

#ifdef AIPDF_WITH_QPDF

#include <qpdf/Buffer.hh>
#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
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
    const double dy = std::max({box.top() - top, bottom - box.bottom(), 0.0});
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
    result.e = left.e * right.a + left.f * right.c + right.e;
    result.f = left.e * right.b + left.f * right.d + right.f;
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


// --------------------------------------------------------------- 文本状态 ----

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
    RunScanner(const QVector<ContentToken>& tokens, const QHash<QString, FontMetrics>& fonts)
        : tokens_(tokens), fonts_(fonts) {}

    // 扫描出这一条内容流里的全部显示操作符，结果追加到 runs。
    void scan(QVector<TextRun>* runs) {
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
                const int nameIndex = previousSignificant(tokens_, i - 1);
                if (nameIndex >= 0 && tokens_.at(nameIndex).type == ContentTokenType::Name) {
                    currentFont_ = QString::fromLatin1(tokens_.at(nameIndex).raw.mid(1));
                    QVector<double> values;
                    if (readNumbersBefore(nameIndex, 1, &values)) text_.fontSize = values.at(0);
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
                       static_cast<unsigned char>(bytes.at(i * 2 + 1));
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

    const QVector<ContentToken>& tokens_;
    const QHash<QString, FontMetrics>& fonts_;
    QVector<Matrix> graphicsStack_;
    Matrix ctm_;
    Matrix textMatrix_;
    Matrix lineMatrix_;
    TextState text_;
    QString currentFont_;
};

#ifdef AIPDF_WITH_QPDF

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
        result.insert(QString::fromStdString(key), metrics);
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

// 处理一页：扫描 → 分配 MCID → 归入区域 → 改写内容流。
//
// 页面句柄用非 const 引用：qpdf 的修改接口（replaceKey / coalesceContentStreams /
// replaceStreamData）都不是 const 方法。
bool tagPage(QPDFObjectHandle& page, int pageIndex, const QVector<TaggedRegion>& regions,
             PageTagging* result, QString* error) {
    // /StructParents 是这一页在 ParentTree 里的键：从 0 开始按页序。
    // 即使这一页没有文字也要给，键得连续。
    page.replaceKey("/StructParents", QPDFObjectHandle::newInteger(pageIndex));

    if (regions.isEmpty()) return true;

    // 区域按 order 升序排；order 相同按传入次序排。
    QVector<int> regionOrder(regions.size());
    std::iota(regionOrder.begin(), regionOrder.end(), 0);
    std::stable_sort(regionOrder.begin(), regionOrder.end(),
                     [&regions](int lhs, int rhs) { return regions.at(lhs).order < regions.at(rhs).order; });

    page.coalesceContentStreams();
    // 注意：qpdf 的对象句柄只有非 const 版本能改（replaceStreamData / replaceKey）。
    std::vector<QPDFObjectHandle> contents = page.getPageContents();

    QVector<TextRun> runs;
    QVector<QVector<ContentToken>> streamTokens;
    // 内容流句柄与 token 列表一一对应：contents 里可能有不是流的对象，
    // 跳过它们时两边必须同步跳过，否则写回时会写错流。
    QVector<QPDFObjectHandle> streamHandles;
    const QHash<QString, FontMetrics> fonts = loadFontMetrics(page);
    for (size_t s = 0; s < contents.size(); ++s) {
        QPDFObjectHandle stream = contents.at(s);
        if (!stream.isStream()) continue;
        const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
        if (!data) continue;
        const QByteArray raw(reinterpret_cast<const char*>(data->getBuffer()),
                             static_cast<qsizetype>(data->getSize()));
        const ContentParseResult parsed = ContentStream::tokenize(raw);
        if (!parsed.isValid()) {
            if (error) {
                *error = i18n::tr("aipdf::pdf::TagTreeWriter", "第 %1 页的内容流无法解析：%2")
                             .arg(pageIndex + 1)
                             .arg(parsed.error);
            }
            return false;
        }
        // 先摘掉上一轮打过的标记，再重新打：同一份文档反复「应用区域顺序」
        // 时不能把同一个显示操作符套两层 BDC（MCID 会重复）。
        const QVector<ContentToken> tokens = stripMarkedContent(parsed.tokens);
        RunScanner scanner(tokens, fonts);
        const int before = runs.size();
        scanner.scan(&runs);
        for (int i = before; i < runs.size(); ++i) runs[i].streamIndex = streamTokens.size();
        streamHandles.append(stream);
        streamTokens.append(tokens);
    }

    if (runs.isEmpty()) return true;

    // 视觉顺序：先按基线从上到下聚成行，行内按 x 从左到右。
    //
    // 为什么按视觉顺序发 MCID：MCID 只是页内标签，真正决定读者顺序的是
    // 标签树里 Span 的排列。但按视觉顺序发号，能让「同一块内容」的 MCID
    // 连成一段，正好对上 PDF 规范里 /K 数组的常见形态。
    QVector<int> visualOrder(runs.size());
    std::iota(visualOrder.begin(), visualOrder.end(), 0);
    {
        // 先把基线聚成「行」：基线相差在半个字高以内的算同一行。
        // 聚类单独做，不用带容差的比较器——带容差的比较器不是严格弱序，
        // 排序结果不可预期。
        QVector<int> byBaseline(runs.size());
        std::iota(byBaseline.begin(), byBaseline.end(), 0);
        std::stable_sort(byBaseline.begin(), byBaseline.end(), [&runs](int lhs, int rhs) {
            return runs.at(lhs).baseline > runs.at(rhs).baseline;
        });
        QVector<int> lineOf(runs.size(), 0);
        int line = 0;
        double lineBaseline = 0.0;
        for (int i = 0; i < byBaseline.size(); ++i) {
            const int index = byBaseline.at(i);
            const double tolerance = std::max(1.0, 0.5 * std::max(runs.at(index).fontSize, 1.0));
            if (i == 0 || runs.at(index).baseline < lineBaseline - tolerance) {
                ++line;
                lineBaseline = runs.at(index).baseline;
            }
            lineOf[index] = line;
        }
        std::stable_sort(visualOrder.begin(), visualOrder.end(), [&](int lhs, int rhs) {
            const int leftLine = lineOf.at(lhs);
            const int rightLine = lineOf.at(rhs);
            if (leftLine != rightLine) return leftLine < rightLine;
            const double leftX = runs.at(lhs).origin.x();
            const double rightX = runs.at(rhs).origin.x();
            if (std::abs(leftX - rightX) > 0.01) return leftX < rightX;
            return runs.at(lhs).streamOrder < runs.at(rhs).streamOrder;
        });
    }

    // MCID 按视觉顺序发号。
    QVector<int> mcidOf(runs.size(), -1);
    for (int position = 0; position < visualOrder.size(); ++position) {
        mcidOf[visualOrder.at(position)] = position;
    }

    // 归入区域：每个显示操作符归到离它最近的那个区域。
    QVector<int> owner(runs.size(), -1);
    bool anyInside = false;
    for (int i = 0; i < runs.size(); ++i) {
        double best = std::numeric_limits<double>::max();
        int bestRegion = -1;
        for (int position = 0; position < regionOrder.size(); ++position) {
            const int regionIndex = regionOrder.at(position);
            const double distance = distanceToRect(runs.at(i).origin, runs.at(i).end,
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
    if (!anyInside && !runs.isEmpty()) {
        const int chunkCount = regionOrder.size();
        const int total = static_cast<int>(visualOrder.size());
        for (int position = 0; position < total; ++position) {
            const int chunk = std::min(chunkCount - 1, (position * chunkCount) / total);
            owner[visualOrder.at(position)] = regionOrder.at(chunk);
        }
    }

    // 改写内容流：把 BDC / EMC 插进去，其余一个字节都不动。
    for (int s = 0; s < streamTokens.size(); ++s) {
        QVector<TextRun> streamRuns;
        QVector<int> streamMcids;
        for (int i = 0; i < runs.size(); ++i) {
            if (runs.at(i).streamIndex != s) continue;
            streamRuns.append(runs.at(i));
            streamMcids.append(mcidOf.at(i));
        }
        if (streamRuns.isEmpty()) continue;
        const QByteArray rewritten = insertMarkedContent(streamTokens.at(s), streamRuns, streamMcids);
        // qpdf 的句柄只有非 const 版本能改，所以这里先取一份可改的副本。
        QPDFObjectHandle handle = streamHandles.at(s);
        handle.replaceStreamData(rewritten.toStdString(), QPDFObjectHandle::newNull(),
                                 QPDFObjectHandle::newNull());
    }

    // 每个区域一个 Span，顺序按调用方给的 order。
    result->mcidCount = runs.size();
    for (int position = 0; position < regionOrder.size(); ++position) {
        const int regionIndex = regionOrder.at(position);
        QVector<int> mcids;
        for (int i = 0; i < runs.size(); ++i) {
            if (owner.at(i) == regionIndex) mcids.append(mcidOf.at(i));
        }
        if (!mcids.isEmpty()) result->spanMcids.append(mcids);
    }
    return true;
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

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
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        if (pages.empty()) {
            if (error) *error = i18n::tr("aipdf::pdf::TagTreeWriter", "PDF 里没有页面");
            return {};
        }

        QVector<PageTagging> tagging(static_cast<int>(pages.size()));
        for (size_t i = 0; i < pages.size(); ++i) {
            const int pageIndex = static_cast<int>(i);
            const QVector<TaggedRegion> regions =
                pageIndex < pageRegions.size() ? pageRegions.at(pageIndex) : QVector<TaggedRegion>();
            QString pageError;
            QPDFObjectHandle page = pages.at(i);
            if (!tagPage(page, pageIndex, regions, &tagging[pageIndex], &pageError)) {
                if (error) *error = pageError;
                return {};
            }
        }

        // ---- 建标签树 ----
        QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            if (error) *error = i18n::tr("aipdf::pdf::TagTreeWriter", "PDF 的根对象不是字典");
            return {};
        }

        QPDFObjectHandle documentElement = QPDFObjectHandle::newDictionary();
        documentElement.replaceKey("/Type", QPDFObjectHandle::newName("/StructElem"));
        documentElement.replaceKey("/S", QPDFObjectHandle::newName("/Document"));
        QPDFObjectHandle documentRef = pdf.makeIndirectObject(documentElement);

        QVector<QPDFObjectHandle> pageElementRefs;
        QVector<QPDFObjectHandle> parentTreeValues;

        for (size_t i = 0; i < pages.size(); ++i) {
            const PageTagging& page = tagging.at(static_cast<int>(i));

            QPDFObjectHandle pageElement = QPDFObjectHandle::newDictionary();
            pageElement.replaceKey("/Type", QPDFObjectHandle::newName("/StructElem"));
            pageElement.replaceKey("/S", QPDFObjectHandle::newName("/P"));
            pageElement.replaceKey("/P", documentRef);
            // /Pg 指向页面对象本身（页面在文件里本来就是间接对象）。
            if (pages.at(i).isIndirect()) pageElement.replaceKey("/Pg", pages.at(i));
            QPDFObjectHandle pageElementRef = pdf.makeIndirectObject(pageElement);

            // ParentTree 的值是「MCID → 元素」的数组，下标就是 MCID。
            std::vector<QPDFObjectHandle> mcidToElement(static_cast<size_t>(page.mcidCount),
                                                        QPDFObjectHandle::newNull());
            QVector<QPDFObjectHandle> spanRefs;
            for (const QVector<int>& mcids : page.spanMcids) {
                std::vector<QPDFObjectHandle> mcidArray;
                mcidArray.reserve(static_cast<size_t>(mcids.size()));
                for (int mcid : mcids) mcidArray.push_back(QPDFObjectHandle::newInteger(mcid));

                QPDFObjectHandle span = QPDFObjectHandle::newDictionary();
                span.replaceKey("/Type", QPDFObjectHandle::newName("/StructElem"));
                span.replaceKey("/S", QPDFObjectHandle::newName("/Span"));
                span.replaceKey("/P", pageElementRef);
                span.replaceKey("/K", QPDFObjectHandle::newArray(mcidArray));
                const QPDFObjectHandle spanRef = pdf.makeIndirectObject(span);

                for (int mcid : mcids) {
                    if (mcid >= 0 && mcid < static_cast<int>(mcidToElement.size())) {
                        mcidToElement[static_cast<size_t>(mcid)] = spanRef;
                    }
                }
                spanRefs.append(spanRef);
            }

            // 这一页的 Span（顺序 = 调用方给的阅读顺序）。
            std::vector<QPDFObjectHandle> spanArray;
            spanArray.reserve(static_cast<size_t>(spanRefs.size()));
            for (const QPDFObjectHandle& spanRef : spanRefs) spanArray.push_back(spanRef);
            if (!spanArray.empty()) {
                pageElementRef.replaceKey("/K", QPDFObjectHandle::newArray(spanArray));
            }
            pageElementRefs.append(pageElementRef);
            parentTreeValues.append(QPDFObjectHandle::newArray(mcidToElement));
        }

        // Document 的子元素是各页的 /P。
        std::vector<QPDFObjectHandle> documentChildren;
        documentChildren.reserve(static_cast<size_t>(pageElementRefs.size()));
        for (const QPDFObjectHandle& pageElementRef : pageElementRefs) {
            documentChildren.push_back(pageElementRef);
        }
        documentRef.replaceKey("/K", QPDFObjectHandle::newArray(documentChildren));

        // ParentTree：数字树，/Nums 是 [键 值 键 值 …]，键就是页的 /StructParents。
        std::vector<QPDFObjectHandle> nums;
        nums.reserve(static_cast<size_t>(parentTreeValues.size()) * 2);
        for (int i = 0; i < parentTreeValues.size(); ++i) {
            nums.push_back(QPDFObjectHandle::newInteger(i));
            nums.push_back(parentTreeValues.at(i));
        }
        QPDFObjectHandle parentTree = QPDFObjectHandle::newDictionary();
        parentTree.replaceKey("/Nums", QPDFObjectHandle::newArray(nums));
        const QPDFObjectHandle parentTreeRef = pdf.makeIndirectObject(parentTree);

        QPDFObjectHandle structTreeRoot = QPDFObjectHandle::newDictionary();
        structTreeRoot.replaceKey("/Type", QPDFObjectHandle::newName("/StructTreeRoot"));
        structTreeRoot.replaceKey("/K",
                                  QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{documentRef}));
        structTreeRoot.replaceKey("/ParentTree", parentTreeRef);
        structTreeRoot.replaceKey("/ParentTreeNextKey", QPDFObjectHandle::newInteger(pages.size()));
        const QPDFObjectHandle structTreeRootRef = pdf.makeIndirectObject(structTreeRoot);

        catalog.replaceKey("/StructTreeRoot", structTreeRootRef);
        QPDFObjectHandle markInfo = QPDFObjectHandle::newDictionary();
        markInfo.replaceKey("/Marked", QPDFObjectHandle::newBool(true));
        catalog.replaceKey("/MarkInfo", markInfo);

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
