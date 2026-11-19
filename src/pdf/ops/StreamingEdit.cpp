#include "pdf/ops/StreamingEdit.h"

#include "pdf/compliance/StructTreeReader.h"
#include "pdf/content/ContentStream.h"
#include "pdf/content/ObjectInventory.h"
#include "pdf/content/TextReflow.h"
#include "pdf/kernel/PageObjectModel.h"

#include "i18n/Translate.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

#include <fpdf_text.h>
#include <fpdfview.h>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#endif

namespace aipdf::pdf {
namespace {

// ---------------------------------------------------------------------------
// 矩阵
// ---------------------------------------------------------------------------
//
// 口径与 PDF 一致：矩阵 [a b c d e f] 表示
//     | a b 0 |
//     | c d 0 |
//     | e f 1 |
// 即 x' = a*x + c*y + e，y' = b*x + d*y + f。
// 乘法 mul(m, n) 表示「先走 n，再走 m」（PDF 的 cm 就是左乘）。

struct Matrix {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;
};

Matrix multiply(const Matrix& m, const Matrix& n) {
    Matrix r;
    r.a = m.a * n.a + m.b * n.c;
    r.b = m.a * n.b + m.b * n.d;
    r.c = m.c * n.a + m.d * n.c;
    r.d = m.c * n.b + m.d * n.d;
    r.e = m.e * n.a + m.f * n.c + n.e;
    r.f = m.e * n.b + m.f * n.d + n.f;
    return r;
}

QPointF transformPoint(const Matrix& m, double x, double y) {
    return QPointF(m.a * x + m.c * y + m.e, m.b * x + m.d * y + m.f);
}

Matrix translation(double tx, double ty) {
    Matrix m;
    m.e = tx;
    m.f = ty;
    return m;
}

// ---------------------------------------------------------------------------
// 数字与字符串
// ---------------------------------------------------------------------------

// 内容流里的数字写法：去掉多余的 0，最多三位小数。
QByteArray formatNumber(double value) {
    QString text = QString::number(value, 'f', 3);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    if (text == QStringLiteral("-0")) text = QStringLiteral("0");
    return text.toLatin1();
}

// 解字面量字符串（raw 含首尾括号）。
QString decodeLiteralToken(const QByteArray& raw) {
    QByteArray out;
    for (int i = 1; i + 1 < raw.size(); ++i) {
        char c = raw[i];
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i + 1 >= raw.size()) break;
        c = raw[i];
        if (c == 'n') out += '\n';
        else if (c == 'r') out += '\r';
        else if (c == 't') out += '\t';
        else if (c == 'b') out += '\b';
        else if (c == 'f') out += '\f';
        else if (c == '\n') continue;
        else if (c == '\r') {
            if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
        } else if (c >= '0' && c <= '7') {
            int value = c - '0';
            for (int n = 0; n < 2 && i + 1 < raw.size() - 1 && raw[i + 1] >= '0' && raw[i + 1] <= '7'; ++n)
                value = value * 8 + (raw[++i] - '0');
            out += static_cast<char>(value);
        } else {
            out += c;
        }
    }
    return QString::fromLatin1(out);
}

// 解十六进制字符串（raw 含尖括号）。
QString decodeHexToken(const QByteArray& raw) {
    QByteArray digits;
    for (int i = 1; i + 1 < raw.size(); ++i) {
        const char c = raw[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) digits += c;
    }
    if (digits.size() % 2 != 0) digits += '0';
    QByteArray out;
    for (int i = 0; i + 1 < digits.size(); i += 2) {
        out += static_cast<char>(digits.mid(i, 2).toInt(nullptr, 16));
    }
    return QString::fromLatin1(out);
}

// 标准 14 号字体表示不了的字符写成 '?'。
//
// 为什么是 '?' 而不是悄悄丢掉：丢掉的话写回后的字数比原文少，
// 门禁会误报「丢字」，而调用方很难判断到底是丢字还是编码问题。
// 留一个可见的占位符，并把这件事**报告**出去，才是对的做法。
QString toLatin1Placeholder(const QString& text, bool* degraded) {
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text) {
        if (ch.unicode() > 0xFF) {
            result.append(QLatin1Char('?'));
            if (degraded) *degraded = true;
            continue;
        }
        result.append(ch);
    }
    return result;
}

// 文字写成 PDF 字面量字符串。
QByteArray literalBytes(const QString& text) {
    QByteArray out("(");
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            out += '?';
            ++i;
            continue;
        }
        const ushort unit = ch.unicode();
        const char byte = unit <= 0xFF ? static_cast<char>(unit) : '?';
        switch (byte) {
        case '(': out += "\\("; break;
        case ')': out += "\\)"; break;
        case '\\': out += "\\\\"; break;
        case '\r': out += "\\r"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default: {
            const unsigned char raw = static_cast<unsigned char>(byte);
            if (raw < 32 || raw > 126) {
                out += '\\';
                out += QByteArray::number(raw, 8).rightJustified(3, '0');
            } else {
                out += byte;
            }
            break;
        }
        }
    }
    out += ')';
    return out;
}

// ---------------------------------------------------------------------------
// 文字宽度估算
// ---------------------------------------------------------------------------

// 用 Qt 的字体度量量宽度。
//
// 为什么是估算：内容流里只有字体资源名，没有字体的字形宽度表；
// 仓库里其它地方（pdf::TextReflow / pdf::layoutParagraph）也是用同一套
// 字体度量做断行，口径一致比「绝对精确」更重要。
double estimateWidth(const QString& text, const TextRunStyle& style) {
    if (text.isEmpty()) return 0.0;
    QFont font(style.fontFamily);
    font.setPointSizeF(qMax(1.0, style.fontSize));
    const QFontMetricsF metrics(font);
    return metrics.horizontalAdvance(text);
}

double fontLineStep(const TextStyle& style) {
    QFont font(style.family);
    font.setPointSizeF(qMax(1.0, style.fontSize));
    const QFontMetricsF metrics(font);
    return qMax(metrics.height(), style.fontSize * style.lineHeight);
}

// ---------------------------------------------------------------------------
// 内容流走查
// ---------------------------------------------------------------------------

// 一次显示操作（一个 Tj / TJ / ' / "）。
struct ShowOp {
    // 写回用的 token 位置。
    int operandStart = -1;
    int operandEnd = -1;
    int operatorIndex = -1;
    QString text;
    TextRunStyle style;
    // 在页面上的起点（PDF user space）。
    double x = 0.0;
    double baseline = 0.0;
    // 标记内容编号。-1 表示没有。
    int mcid = -1;
    // BT…ET 区域编号。
    int regionIndex = -1;
    // 这一段之前的填充色（写回时用来还原颜色状态）。
    QColor ambientColor = Qt::black;
    bool ambientColorKnown = false;
    // 写回时用的文本矩阵与 CTM。
    double tmA = 1.0, tmB = 0.0, tmC = 0.0, tmD = 1.0;
    double ctmE = 0.0, ctmF = 0.0;
};

// 一个内容流的分析结果。
struct StreamAnalysis {
    int pageIndex = -1;
    QByteArray bytes;
    QHash<QString, QString> fonts;
    QRectF box;
    QVector<ShowOp> shows;
    // 图片 / 矢量算子的 token 下标（Do、路径绘制等）。
    QVector<int> graphicOperators;
    QVector<ContentTextBlock> blocks;
};

// 文字状态（跟着图形状态栈一起存）。
struct TextState {
    Matrix ctm;
    Matrix tm;
    Matrix tlm;
    QString fontResource = QStringLiteral("F1");
    double fontSize = 0.0;
    double charSpace = 0.0;
    double wordSpace = 0.0;
    double leading = 0.0;
    double hScale = 1.0;
    double rise = 0.0;
    QColor fillColor = Qt::black;
    bool fillColorKnown = false;
};

// 路径绘制算子：这些算子出现就说明页面上有图形（图片、表格线、矢量）。
bool isGraphicOperator(const QByteArray& raw) {
    static const QSet<QByteArray> kGraphicOps = {
        QByteArrayLiteral("Do"), QByteArrayLiteral("BI"),
        QByteArrayLiteral("S"), QByteArrayLiteral("s"), QByteArrayLiteral("f"),
        QByteArrayLiteral("F"), QByteArrayLiteral("f*"), QByteArrayLiteral("B"),
        QByteArrayLiteral("B*"), QByteArrayLiteral("b"), QByteArrayLiteral("b*"),
        QByteArrayLiteral("sh")
    };
    return kGraphicOps.contains(raw);
}

// 走查一页的内容流，把显示操作与图形算子挑出来。
bool analyzePageContent(const QByteArray& bytes, int pageIndex,
                        const QHash<QString, QString>& fonts, StreamAnalysis* out,
                        QString* error) {
    ContentParseResult parsed = ContentStream::tokenize(bytes);
    if (!parsed.isValid()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::StreamingEdit", "第 %1 页的内容流解析不了：%2")
                         .arg(pageIndex + 1)
                         .arg(parsed.error);
        }
        return false;
    }
    const QVector<ContentToken>& tokens = parsed.tokens;

    QVector<TextState> stack;  // q / Q 的保存栈
    TextState state;
    stack.append(state);

    QVector<int> pending;  // 当前算子的操作数 token 下标
    int mcid = -1;
    int regionIndex = -1;

    auto lastNumbers = [&tokens, &pending](int count) {
        QVector<double> values;
        if (pending.size() < count) return values;
        for (int k = pending.size() - count; k < pending.size(); ++k) {
            bool ok = false;
            const double value = tokens.at(pending.at(k)).raw.toDouble(&ok);
            if (!ok) return QVector<double>();
            values.append(value);
        }
        return values;
    };

    for (int index = 0; index < tokens.size(); ++index) {
        const ContentToken& token = tokens.at(index);
        if (token.type == ContentTokenType::Whitespace || token.type == ContentTokenType::Comment) {
            continue;
        }
        if (token.type != ContentTokenType::Operator) {
            pending.append(index);
            continue;
        }

        const QByteArray op = token.raw;
        if (isGraphicOperator(op)) out->graphicOperators.append(index);

        if (op == "q") {
            stack.append(state);
        } else if (op == "Q") {
            if (stack.size() > 1) {
                stack.removeLast();
                state = stack.last();
            }
        } else if (op == "cm") {
            const QVector<double> values = lastNumbers(6);
            if (values.size() == 6) {
                Matrix m;
                m.a = values.at(0); m.b = values.at(1); m.c = values.at(2);
                m.d = values.at(3); m.e = values.at(4); m.f = values.at(5);
                state.ctm = multiply(m, state.ctm);
            }
        } else if (op == "BT") {
            state.tm = Matrix();
            state.tlm = Matrix();
            ++regionIndex;
        } else if (op == "Tm") {
            const QVector<double> values = lastNumbers(6);
            if (values.size() == 6) {
                Matrix m;
                m.a = values.at(0); m.b = values.at(1); m.c = values.at(2);
                m.d = values.at(3); m.e = values.at(4); m.f = values.at(5);
                state.tm = m;
                state.tlm = m;
            }
        } else if (op == "Td" || op == "TD") {
            const QVector<double> values = lastNumbers(2);
            if (values.size() == 2) {
                if (op == "TD") state.leading = -values.at(1);
                state.tlm = multiply(translation(values.at(0), values.at(1)), state.tlm);
                state.tm = state.tlm;
            }
        } else if (op == "T*") {
            state.tlm = multiply(translation(0.0, -state.leading), state.tlm);
            state.tm = state.tlm;
        } else if (op == "TL") {
            const QVector<double> values = lastNumbers(1);
            if (values.size() == 1) state.leading = values.at(0);
        } else if (op == "Tc" || op == "Tw" || op == "Tz" || op == "Ts") {
            const QVector<double> values = lastNumbers(1);
            if (values.size() == 1) {
                if (op == "Tc") state.charSpace = values.at(0);
                else if (op == "Tw") state.wordSpace = values.at(0);
                else if (op == "Tz") state.hScale = values.at(0) / 100.0;
                else state.rise = values.at(0);
            }
        } else if (op == "Tf") {
            const QVector<double> values = lastNumbers(1);
            if (values.size() == 1 && pending.size() >= 2) {
                const ContentToken& name = tokens.at(pending.at(pending.size() - 2));
                if (name.type == ContentTokenType::Name) {
                    state.fontResource = QString::fromLatin1(name.raw.mid(1));
                }
                state.fontSize = values.at(0);
            }
        } else if (op == "rg" || op == "g" || op == "k") {
            const QVector<double> values = lastNumbers(op.size() == 1 ? 1 : (op.size() == 3 ? 3 : 4));
            if (!values.isEmpty()) {
                QColor color = Qt::black;
                if (values.size() == 1) {
                    color.setRgbF(values.at(0), values.at(0), values.at(0));
                } else if (values.size() == 3) {
                    color.setRgbF(values.at(0), values.at(1), values.at(2));
                } else {
                    const double c = values.at(0), mm = values.at(1), y = values.at(2), k = values.at(3);
                    color.setRgbF((1.0 - c) * (1.0 - k), (1.0 - mm) * (1.0 - k), (1.0 - y) * (1.0 - k));
                }
                state.fillColor = color;
                state.fillColorKnown = true;
            }
        } else if (op == "Tj" || op == "'" || op == "\"" || op == "TJ") {
            // 显示操作：记录操作数位置、文字与当前位置。
            int operandStart = -1;
            int operandEnd = -1;
            QString text;
            bool ok = false;
            if (op == "TJ") {
                if (!pending.isEmpty()) {
                    const int arrayEnd = pending.last();
                    if (tokens.at(arrayEnd).type == ContentTokenType::ArrayEnd) {
                        int depth = 0;
                        for (int k = pending.size() - 1; k >= 0; --k) {
                            const ContentTokenType type = tokens.at(pending.at(k)).type;
                            if (type == ContentTokenType::ArrayEnd) {
                                ++depth;
                            } else if (type == ContentTokenType::ArrayStart) {
                                if (--depth == 0) {
                                    operandStart = pending.at(k);
                                    operandEnd = arrayEnd;
                                    break;
                                }
                            }
                        }
                        if (operandStart >= 0) {
                            QStringList parts;
                            for (int k = operandStart + 1; k < operandEnd; ++k) {
                                const ContentToken& item = tokens.at(pending.at(k));
                                if (item.type == ContentTokenType::LiteralString) {
                                    parts.append(decodeLiteralToken(item.raw));
                                } else if (item.type == ContentTokenType::HexString) {
                                    parts.append(decodeHexToken(item.raw));
                                }
                            }
                            text = parts.join(QString());
                            ok = true;
                        }
                    }
                }
            } else {
                const int count = (op == "\"") ? 3 : 1;
                if (pending.size() >= count) {
                    const int stringIndex = pending.last();
                    const ContentToken& stringToken = tokens.at(stringIndex);
                    if (stringToken.type == ContentTokenType::LiteralString) {
                        text = decodeLiteralToken(stringToken.raw);
                        ok = true;
                    } else if (stringToken.type == ContentTokenType::HexString) {
                        text = decodeHexToken(stringToken.raw);
                        ok = true;
                    }
                    operandStart = pending.at(pending.size() - count);
                    operandEnd = stringIndex;
                }
            }

            if (ok) {
                if (op == "'" || op == "\"") {
                    state.tlm = multiply(translation(0.0, -state.leading), state.tlm);
                    state.tm = state.tlm;
                }
                ShowOp show;
                show.operandStart = operandStart;
                show.operandEnd = operandEnd;
                show.operatorIndex = index;
                show.text = text;
                show.mcid = mcid;
                show.regionIndex = regionIndex;
                show.ambientColor = state.fillColor;
                show.ambientColorKnown = state.fillColorKnown;
                show.style.fontResource = state.fontResource;
                show.style.fontSize = state.fontSize > 0.0 ? state.fontSize : 12.0;
                show.style.color = state.fillColor;
                show.style.colorKnown = state.fillColorKnown;
                show.style.fontFamily = fonts.value(state.fontResource, QStringLiteral("Helvetica"));
                show.style.lineHeight = 1.2;

                const QPointF origin = transformPoint(multiply(state.tm, state.ctm), 0.0, state.rise);
                show.x = origin.x();
                show.baseline = origin.y();
                show.tmA = state.tm.a; show.tmB = state.tm.b;
                show.tmC = state.tm.c; show.tmD = state.tm.d;
                show.ctmE = state.ctm.e; show.ctmF = state.ctm.f;
                out->shows.append(show);

                // 推进文本矩阵（宽度是估算的，见 estimateWidth 的说明）。
                double advance = 0.0;
                if (op == "TJ") {
                    for (int k = operandStart + 1; k < operandEnd; ++k) {
                        const ContentToken& item = tokens.at(pending.at(k));
                        if (item.type == ContentTokenType::LiteralString) {
                            advance += estimateWidth(decodeLiteralToken(item.raw), show.style);
                        } else if (item.type == ContentTokenType::HexString) {
                            advance += estimateWidth(decodeHexToken(item.raw), show.style);
                        } else if (item.type == ContentTokenType::Number) {
                            bool numeric = false;
                            const double value = item.raw.toDouble(&numeric);
                            if (numeric) advance -= value / 1000.0 * show.style.fontSize;
                        }
                    }
                } else {
                    advance = estimateWidth(text, show.style);
                }
                advance += state.charSpace * text.size();
                for (const QChar ch : text) {
                    if (ch == QLatin1Char(' ')) advance += state.wordSpace;
                }
                state.tm = multiply(translation(advance * state.hScale, 0.0), state.tm);
            }
        } else if (op == "BDC") {
            // 标记内容：/Tag << /MCID n >> BDC
            int found = -1;
            for (int k = 0; k + 2 < pending.size(); ++k) {
                const ContentToken& name = tokens.at(pending.at(k));
                if (name.type != ContentTokenType::Name) continue;
                if (name.raw != QByteArrayLiteral("/MCID")) continue;
                const ContentToken& value = tokens.at(pending.at(k + 1));
                if (value.type != ContentTokenType::Number) continue;
                bool numeric = false;
                const double number = value.raw.toDouble(&numeric);
                if (numeric) found = static_cast<int>(number);
                break;
            }
            mcid = found;
        } else if (op == "BMC" || op == "EMC") {
            mcid = -1;
        }
        pending.clear();
    }
    return true;
}

// 把显示操作归成文本块。
//
// 归并口径：
//   * 有 MCID：同一个 MCID 的**连续**显示操作算一段。
//     「连续」是必须的：中间夹着别的 MCID 的字时，把两段并成一段
//     会把别人的字一起改掉。
//   * 没有 MCID：按 BT 区域分，基线往上跳（另起一段）时断开。
void buildBlocks(int pageIndex, const QVector<ShowOp>& shows, int* nextBlockId,
                 QVector<ContentTextBlock>* blocks) {
    auto finish = [&](int start, int end) {
        if (start > end) return;
        ContentTextBlock block;
        block.pageIndex = pageIndex;
        block.blockId = (*nextBlockId)++;
        block.mcid = shows.at(start).mcid;
        block.style = shows.at(start).style;
        block.fontFamily = shows.at(start).style.fontFamily;
        block.firstBaseline = shows.at(start).baseline;
        block.firstX = shows.at(start).x;
        block.ambientColor = shows.at(start).ambientColor;
        block.ambientColorKnown = shows.at(start).ambientColorKnown;
        block.tmA = shows.at(start).tmA;
        block.tmB = shows.at(start).tmB;
        block.tmC = shows.at(start).tmC;
        block.tmD = shows.at(start).tmD;
        block.ctmE = shows.at(start).ctmE;
        block.ctmF = shows.at(start).ctmF;

        QString text;
        double minX = shows.at(start).x;
        double maxX = shows.at(start).x;
        double top = shows.at(start).baseline;
        double bottom = shows.at(start).baseline;
        for (int i = start; i <= end; ++i) {
            const ShowOp& show = shows.at(i);
            text += show.text;
            const double width = estimateWidth(show.text, show.style);
            minX = qMin(minX, show.x);
            maxX = qMax(maxX, show.x + width);
            top = qMax(top, show.baseline);
            bottom = qMin(bottom, show.baseline);
        }
        block.text = text;

        QFont font(block.fontFamily);
        font.setPointSizeF(qMax(1.0, block.style.fontSize));
        const QFontMetricsF metrics(font);
        block.bounds = QRectF(minX, bottom, qMax(0.0, maxX - minX),
                              qMax(0.0, top - bottom) + metrics.ascent() + metrics.descent());

        // 区域：按 BT 区域把这一段的显示操作分组，每组一个写回区间。
        int regionStart = start;
        for (int i = start + 1; i <= end + 1; ++i) {
            const bool boundary =
                (i > end) || (shows.at(i).regionIndex != shows.at(regionStart).regionIndex);
            if (!boundary) continue;
            ContentTextBlock::Region region;
            region.firstOperandToken = shows.at(regionStart).operandStart;
            region.lastOperatorToken = shows.at(i - 1).operatorIndex;
            block.regions.append(region);
            regionStart = i;
        }
        blocks->append(block);
    };

    int blockStart = -1;
    for (int i = 0; i < shows.size(); ++i) {
        const ShowOp& show = shows.at(i);
        bool sameBlock = false;
        if (blockStart >= 0 && i > 0) {
            const ShowOp& previous = shows.at(i - 1);
            if (show.mcid >= 0) {
                sameBlock = (show.mcid == previous.mcid);
            } else {
                sameBlock = (show.regionIndex == previous.regionIndex) &&
                            (show.baseline <= previous.baseline + 0.5 * show.style.fontSize) &&
                            qFuzzyCompare(show.style.fontSize + 1.0, previous.style.fontSize + 1.0);
            }
        }
        if (!sameBlock) {
            if (blockStart >= 0) finish(blockStart, i - 1);
            blockStart = i;
        }
    }
    if (blockStart >= 0) finish(blockStart, shows.size() - 1);
}

// ---------------------------------------------------------------------------
// qpdf：读页面内容流、页面盒子、字体表
// ---------------------------------------------------------------------------

#ifdef AIPDF_WITH_QPDF
QHash<QString, QString> readPageFonts(const QPDFObjectHandle& page) {
    QHash<QString, QString> fonts;
    // /Resources 可以挂在页上，也可以从 /Pages 继承下来。只读页自己的那份，
    // 继承来的字体就会全体退化成 Helvetica，断行按错误字宽算。
    QPDFObjectHandle resources;
    QPDFObjectHandle current = page;
    for (int depth = 0; depth < 64 && current.isDictionary(); ++depth) {
        if (current.hasKey("/Resources")) {
            resources = current.getKey("/Resources");
            break;
        }
        if (!current.hasKey("/Parent")) break;
        current = current.getKey("/Parent");
    }
    if (!resources.isDictionary()) return fonts;
    const QPDFObjectHandle fontDict = resources.getKey("/Font");
    if (!fontDict.isDictionary()) return fonts;
    for (const std::string& key : fontDict.getKeys()) {
        const QPDFObjectHandle font = fontDict.getKey(key);
        const QPDFObjectHandle base = font.getKey("/BaseFont");
        // /BaseFont 是名字对象（/ABCDEF+SimSun），不是字符串。
        // 以前只认 isString()，于是这个表一直是空的，所有字体都按 Helvetica 量宽。
        QString name;
        if (base.isName()) name = QString::fromStdString(base.getName());
        else if (base.isString()) name = QString::fromStdString(base.getStringValue());
        if (name.isEmpty()) continue;
        if (name.startsWith(QLatin1Char('/'))) name.remove(0, 1);
        // 子集前缀（"ABCDEF+"）去掉。
        if (name.size() > 7 && name.at(6) == QLatin1Char('+')) name = name.mid(7);
        // 样式后缀去掉：量宽度只用字族，Bold 之类交给 Qt 自己挑。
        static const QStringList kStyleSuffixes = {QStringLiteral("-BoldOblique"),
                                                   QStringLiteral("-BoldItalic"),
                                                   QStringLiteral("-Bold"), QStringLiteral("-Oblique"),
                                                   QStringLiteral("-Italic"), QStringLiteral("-Roman")};
        for (const QString& suffix : kStyleSuffixes) {
            if (name.endsWith(suffix)) {
                name = name.left(name.size() - suffix.size());
                break;
            }
        }
        fonts.insert(QString::fromStdString(key), name);
    }
    return fonts;
}

QRectF readPageBox(const QPDFObjectHandle& page) {
    QPDFObjectHandle box = page.getKey("/MediaBox");
    if (!box.isArray()) box = page.getKey("/CropBox");
    if (!box.isArray()) return QRectF();
    const std::vector<QPDFObjectHandle> values = box.getArrayAsVector();
    if (values.size() < 4) return QRectF();
    double coordinates[4] = {0.0, 0.0, 0.0, 0.0};
    for (int i = 0; i < 4; ++i) {
        coordinates[i] = values.at(static_cast<size_t>(i)).getNumericValue();
    }
    const double left = qMin(coordinates[0], coordinates[2]);
    const double bottom = qMin(coordinates[1], coordinates[3]);
    return QRectF(left, bottom, qAbs(coordinates[2] - coordinates[0]),
                  qAbs(coordinates[3] - coordinates[1]));
}

// 走一遍文档，按「页 → 内容流」的顺序给出分析结果。
//
// 块号在整篇里唯一：**读取**与**写回**两次走查必须给出同样的块号，
// 否则写回时会改错段落。两处都走这个函数，顺序一致，块号自然对齐。
bool analyzeDocument(const QByteArray& pdfBytes, QVector<StreamAnalysis>* streams, QString* error) {
    try {
        QPDF pdf;
        pdf.processMemoryFile("streaming-edit.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        int nextBlockId = 0;
        for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
            QPDFObjectHandle page = pages.at(pageIndex);
            page.coalesceContentStreams();
            const QHash<QString, QString> fonts = readPageFonts(page);
            const QRectF box = readPageBox(page);
            for (QPDFObjectHandle stream : page.getPageContents()) {
                if (!stream.isStream()) continue;
                const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
                if (!data) continue;
                StreamAnalysis analysis;
                analysis.pageIndex = static_cast<int>(pageIndex);
                analysis.bytes = QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                            static_cast<qsizetype>(data->getSize()));
                analysis.fonts = fonts;
                analysis.box = box;
                if (!analyzePageContent(analysis.bytes, static_cast<int>(pageIndex), fonts,
                                        &analysis, error)) {
                    return false;
                }
                buildBlocks(static_cast<int>(pageIndex), analysis.shows, &nextBlockId,
                            &analysis.blocks);
                streams->append(analysis);
            }
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = QString::fromUtf8(exception.what());
        return false;
    }
}
#endif

// ---------------------------------------------------------------------------
// 写回：把新的文字内容塞回内容流
// ---------------------------------------------------------------------------

// 生成一段新的文字内容（内容流片段）。
//
// 为什么自己拼而不是用 pdf::buildParagraphContentStream：
// 那份只处理「一整段一个样式」，而这里一行里可能有多个片段
// （合并选区时每一块有自己的样式），所以得自己拼。
QByteArray buildRewriteBlob(const ContentTextBlock& block, const BlockRewrite& rewrite) {
    QByteArray out("\n");
    for (int k = 0; k < rewrite.lines.size(); ++k) {
        const double baseline = rewrite.firstBaseline - k * rewrite.lineStep;
        double x = rewrite.firstX;
        if (!rewrite.leftAligned && k < rewrite.lineX.size()) x = rewrite.lineX.at(k);

        // Tm 的线性部分沿用原来的（保留旋转与缩放），只改平移部分。
        //
        // 位置换算：Tm × CTM 作用在 (0,0) 上要正好落在新的用户坐标上。
        const double e = x - (block.tmA * block.ctmE + block.tmC * block.ctmF);
        const double f = baseline - (block.tmB * block.ctmE + block.tmD * block.ctmF);
        out += formatNumber(block.tmA) + " " + formatNumber(block.tmB) + " " +
               formatNumber(block.tmC) + " " + formatNumber(block.tmD) + " " +
               formatNumber(e) + " " + formatNumber(f) + " Tm\n";

        for (const TextPiece& piece : rewrite.lines.at(k)) {
            if (piece.text.isEmpty()) continue;
            const QString latin1 = toLatin1Placeholder(piece.text, nullptr);
            out += "/" + piece.style.fontResource.toLatin1() + " " +
                   formatNumber(piece.style.fontSize) + " Tf\n";
            if (piece.style.colorKnown) {
                const QColor& color = piece.style.color;
                out += formatNumber(color.redF()) + " " + formatNumber(color.greenF()) + " " +
                       formatNumber(color.blueF()) + " rg\n";
            }
            out += literalBytes(latin1) + " Tj\n";
        }
    }
    // 把填充色状态还原回去：写回前的颜色是这一段之前的颜色，
    // 不还原的话后面接着画的东西会带上这一段的颜色。
    if (block.ambientColorKnown) {
        const QColor& color = block.ambientColor;
        out += formatNumber(color.redF()) + " " + formatNumber(color.greenF()) + " " +
               formatNumber(color.blueF()) + " rg\n";
    }
    out += "\n";
    return out;
}

// 把 token 序列按替换区间拼成新的内容流。
QByteArray spliceTokens(const QByteArray& bytes, const QHash<int, QPair<int, QByteArray>>& ranges,
                         QString* error) {
    ContentParseResult parsed = ContentStream::tokenize(bytes);
    if (!parsed.isValid()) {
        if (error) *error = parsed.error;
        return {};
    }
    QByteArray out;
    int index = 0;
    while (index < parsed.tokens.size()) {
        const auto it = ranges.constFind(index);
        if (it != ranges.constEnd()) {
            out += it->second;
            index = it->first + 1;
            continue;
        }
        out += parsed.tokens.at(index).raw;
        ++index;
    }
    // 用仓库自己的词法再解析一遍：写回后的内容流必须还是合法的。
    //
    // 这一步挡住的是「相邻 token 粘在一起」这类事故：
    // 内容流里 24 后面紧跟 Tf 时中间必须有分隔符，否则会变成 24Tf。
    const ContentParseResult check = ContentStream::tokenize(out);
    if (!check.isValid()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::StreamingEdit", "写回后的内容流解析不了：%1").arg(check.error);
        }
        return {};
    }
    return out;
}

// ---------------------------------------------------------------------------
// 带样式的换行
// ---------------------------------------------------------------------------

// 一段带样式的文字（合并多个块时用）。
struct StyledSegment {
    QString text;
    TextRunStyle style;
};

// 把带样式的文字按栏宽断行，每行再按样式切成片段。
QVector<QVector<TextPiece>> wrapStyledText(const QVector<StyledSegment>& segments, double width,
                                            const TextStyle& measureStyle, QString* error) {
    QVector<QVector<TextPiece>> lines;
    QString full;
    QVector<int> owner;
    for (int index = 0; index < segments.size(); ++index) {
        full += segments.at(index).text;
        for (int i = 0; i < segments.at(index).text.size(); ++i) owner.append(index);
    }
    if (full.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("待换行的文字是空的");
        return lines;
    }
    // 框高给一个很大的值：断行只看宽度，放不放得下由调用方按页面高度判断。
    const QRectF box(0.0, 0.0, qMax(1.0, width), 1.0e6);
    const ReflowResult laid = TextReflow::layoutParagraph(full, box, measureStyle);
    if (laid.lines.isEmpty()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::StreamingEdit", "这一段按新宽度一行都排不出来");
        }
        return lines;
    }
    if (laid.overflow) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::StreamingEdit", "这一段按新宽度排不完，有文字剩下");
        }
        return lines;
    }
    for (const ReflowLine& line : laid.lines) {
        QVector<TextPiece> pieces;
        const int start = static_cast<int>(line.sourceStart);
        const int end = qMin(static_cast<int>(owner.size()),
                             start + static_cast<int>(line.sourceLength));
        int cursor = start;
        while (cursor < end) {
            const int segment = owner.at(cursor);
            int run = cursor + 1;
            while (run < end && owner.at(run) == segment) ++run;
            TextPiece piece;
            piece.text = full.mid(cursor, run - cursor);
            piece.style = segments.at(segment).style;
            pieces.append(piece);
            cursor = run;
        }
        if (!pieces.isEmpty()) lines.append(pieces);
    }
    return lines;
}

// 去掉空白后的字数（比较「有没有丢字」时用：换行会吃掉行末的空格，
// 那不是丢字；真的少一个字才是）。
int significantCharCount(const QString& text) {
    int count = 0;
    for (const QChar ch : text) {
        if (!ch.isSpace()) ++count;
    }
    return count;
}

QString normalizedText(const QString& text) {
    QString result;
    for (const QChar ch : text) {
        if (!ch.isSpace()) result.append(ch);
    }
    return result;
}

// 块级标签：这些结构元素整体算一段。
bool isBlockTag(const QString& tag) {
    static const QSet<QString> kTags = {
        QStringLiteral("P"),       QStringLiteral("H1"),    QStringLiteral("H2"),
        QStringLiteral("H3"),      QStringLiteral("H4"),    QStringLiteral("H5"),
        QStringLiteral("H6"),      QStringLiteral("LI"),    QStringLiteral("Lbl"),
        QStringLiteral("LBody"),   QStringLiteral("TD"),    QStringLiteral("TH"),
        QStringLiteral("BlockQuote"), QStringLiteral("Caption"), QStringLiteral("Title"),
        QStringLiteral("Subtitle"), QStringLiteral("Pre"),  QStringLiteral("Quote"),
        QStringLiteral("Note")
    };
    return kTags.contains(tag);
}

// MCID → 段落（块级结构元素）的归属索引。
//
// 口径与 pdf::reflowTaggedDocument 一致：一个 MCID 属于**最近**的块级祖先。
// 按最细的元素算的话，一段正文会被拆成好几个块，与重排结果对不上。
QHash<QString, int> buildOwnerIndex(const StructTreeReport& tree) {
    QHash<QString, int> owners;
    const int count = tree.elements.size();
    if (count == 0) return owners;
    QVector<QVector<int>> children(count);
    for (int i = 0; i < count; ++i) {
        const int parent = tree.elements.at(i).parentIndex;
        if (parent >= 0 && parent < count) children[parent].append(i);
    }
    QVector<int> owner(count, -1);
    for (int i = 0; i < count; ++i) {
        if (isBlockTag(tree.elements.at(i).tag)) {
            owner[i] = i;
            continue;
        }
        int parent = tree.elements.at(i).parentIndex;
        while (parent >= 0 && parent < count) {
            if (isBlockTag(tree.elements.at(parent).tag)) {
                owner[i] = parent;
                break;
            }
            parent = tree.elements.at(parent).parentIndex;
        }
    }
    std::function<void(int, int)> gather = [&](int elementIndex, int ownerIndex) {
        const StructElement& element = tree.elements.at(elementIndex);
        for (const int mcid : element.mcids) {
            owners.insert(PageTextBlockIndex::mcidKey(element.pageIndex, mcid), ownerIndex);
        }
        for (const int child : children.at(elementIndex)) {
            const int childOwner = owner.at(child) >= 0 ? owner.at(child) : ownerIndex;
            gather(child, childOwner);
        }
    };
    for (int i = 0; i < count; ++i) {
        if (owner.at(i) < 0) continue;
        // 传 owner.at(i) 而不是 i：Span 的归属是它的块级祖先，
        // 传 i 会把 Span 自己的 MCID 记到 Span 名下，同一段就散成好几段。
        gather(i, owner.at(i));
    }
    return owners;
}

// 一个块里是不是夹着别人的字（夹着就不能整段换掉）。
bool isInterleaved(const ContentTextBlock& block, const QVector<ContentTextBlock>& all) {
    for (const ContentTextBlock::Region& region : block.regions) {
        for (const ContentTextBlock& other : all) {
            if (other.pageIndex != block.pageIndex) continue;
            if (other.blockId == block.blockId) continue;
            for (const ContentTextBlock::Region& otherRegion : other.regions) {
                const bool overlaps = otherRegion.firstOperandToken <= region.lastOperatorToken &&
                                      otherRegion.lastOperatorToken >= region.firstOperandToken;
                if (overlaps) return true;
            }
        }
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// 公开接口
// ---------------------------------------------------------------------------

QStringList extractPageTexts(const QByteArray& pdfBytes, int* pageCount, QString* error) {
    QStringList result;
    if (error) error->clear();
    detail::ensurePageObjectModelPdfiumReady();
    std::lock_guard<std::recursive_mutex> lock(detail::pageObjectModelPdfiumMutex());
    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(pdfBytes.constData(), static_cast<int>(pdfBytes.size()), nullptr);
    if (document == nullptr) {
        if (error) *error = i18n::tr("aipdf::pdf::StreamingEdit", "PDFium 打不开这份 PDF");
        return result;
    }
    const int count = FPDF_GetPageCount(document);
    if (pageCount) *pageCount = count;
    for (int index = 0; index < count; ++index) {
        FPDF_PAGE page = FPDF_LoadPage(document, index);
        QString text;
        if (page != nullptr) {
            FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
            if (textPage != nullptr) {
                const int chars = FPDFText_CountChars(textPage);
                QByteArray raw;
                raw.resize(qMax(0, chars) * 2);
                if (chars > 0) {
                    FPDFText_GetText(textPage, 0, chars,
                                     reinterpret_cast<unsigned short*>(raw.data()));
                }
                text = QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()), chars);
                FPDFText_ClosePage(textPage);
            }
            FPDF_ClosePage(page);
        }
        result.append(text);
    }
    FPDF_CloseDocument(document);
    return result;
}

bool readContentTextBlocks(const QByteArray& pdfBytes, QVector<ContentTextBlock>* blocks,
                            QString* error) {
    if (blocks) blocks->clear();
    if (error) error->clear();
#ifdef AIPDF_WITH_QPDF
    QVector<StreamAnalysis> streams;
    if (!analyzeDocument(pdfBytes, &streams, error)) return false;
    if (blocks) {
        for (const StreamAnalysis& stream : streams) {
            for (const ContentTextBlock& block : stream.blocks) blocks->append(block);
        }
    }
    return true;
#else
    if (error) {
        *error = i18n::tr("aipdf::pdf::StreamingEdit", "内容流通道不可用：这一版没有编进 qpdf");
    }
    return false;
#endif
}

QVector<TextParagraph> groupBlocksIntoParagraphs(const QByteArray& pdfBytes,
                                                 const QVector<ContentTextBlock>& blocks,
                                                 QString* error) {
    QVector<TextParagraph> paragraphs;
    if (error) error->clear();
    const StructTreeReport tree = readStructTree(pdfBytes);
    const QHash<QString, int> owners = buildOwnerIndex(tree);

    // 归组：同一个块级结构元素、同一页、且在页面上挨着的块算一段。
    QVector<QVector<int>> groups;
    QVector<int> groupOwner;
    for (int i = 0; i < blocks.size(); ++i) {
        const ContentTextBlock& block = blocks.at(i);
        int owner = -1;
        if (block.mcid >= 0) {
            owner = owners.value(PageTextBlockIndex::mcidKey(block.pageIndex, block.mcid), -1);
        }
        const bool same = !groups.isEmpty() && owner >= 0 && groupOwner.last() == owner &&
                          blocks.at(groups.last().first()).pageIndex == block.pageIndex;
        if (same) {
            groups.last().append(i);
        } else {
            groups.append(QVector<int>{i});
            groupOwner.append(owner);
        }
    }
    for (const QVector<int>& group : groups) {
        TextParagraph paragraph;
        const ContentTextBlock& first = blocks.at(group.first());
        paragraph.pageIndex = first.pageIndex;
        paragraph.style = first.style;
        paragraph.fontFamily = first.fontFamily;
        paragraph.firstBaseline = first.firstBaseline;
        paragraph.firstX = first.firstX;
        paragraph.hasStructTags = tree.hasStructTree;
        QRectF bounds = first.bounds;
        for (int index : group) {
            const ContentTextBlock& block = blocks.at(index);
            paragraph.blockIds.append(block.blockId);
            // 块与块之间补一个空格。
            //
            // 为什么：同一段里的两个块在页面上是分开的几行，
            // 拼成一段交给排版器（或者交给翻译 Provider）时，
            // 直接首尾相接会变成「...textand...」这种读不通的句子。
            if (!paragraph.text.isEmpty()) paragraph.text.append(QLatin1Char(' '));
            paragraph.text += block.text;
            bounds = bounds.united(block.bounds);
        }
        paragraph.bounds = bounds;
        paragraphs.append(paragraph);
    }
    return paragraphs;
}

double paragraphBottomLimit(const QVector<TextParagraph>& paragraphs, int index,
                            const QVector<QRectF>& pageBoxes) {
    if (index < 0 || index >= paragraphs.size()) return 0.0;
    const TextParagraph& current = paragraphs.at(index);
    // 页底下沿（PDF user space 里 y 向上，Qt 的 top() 才是下那条边）。
    double limit = 0.0;
    if (current.pageIndex >= 0 && current.pageIndex < pageBoxes.size()) {
        limit = pageBoxes.at(current.pageIndex).top() + 18.0;
    }
    // 同一页里、排在下面最近的那一段，它的上沿就是这段的下边界。
    for (int i = 0; i < paragraphs.size(); ++i) {
        if (i == index) continue;
        const TextParagraph& other = paragraphs.at(i);
        if (other.pageIndex != current.pageIndex) continue;
        if (other.firstBaseline >= current.firstBaseline) continue;
        limit = qMax(limit, other.bounds.top());
    }
    return limit;
}

QVector<QRectF> readPageMediaBoxes(const QByteArray& pdfBytes, QString* error) {
    QVector<QRectF> boxes;
    if (error) error->clear();
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("streaming-edit.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        for (const QPDFObjectHandle& page : pages) boxes.append(readPageBox(page));
    } catch (const std::exception& exception) {
        if (error) *error = QString::fromUtf8(exception.what());
    }
#else
    if (error) {
        *error = i18n::tr("aipdf::pdf::StreamingEdit", "内容流通道不可用：这一版没有编进 qpdf");
    }
#endif
    return boxes;
}

QByteArray applyBlockRewrites(const QByteArray& pdfBytes, const QVector<BlockRewrite>& rewrites,
                              QString* error) {
    if (error) error->clear();
    if (pdfBytes.isEmpty()) {
        if (error) *error = i18n::tr("aipdf::pdf::StreamingEdit", "输入的 PDF 是空的");
        return {};
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("streaming-edit.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();

        QHash<int, QVector<int>> byPage;
        for (int index = 0; index < rewrites.size(); ++index) {
            byPage[rewrites.at(index).pageIndex].append(index);
        }

        bool anyChange = false;
        int nextBlockId = 0;
        for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
            QPDFObjectHandle page = pages.at(pageIndex);
            page.coalesceContentStreams();
            const QHash<QString, QString> fonts = readPageFonts(page);
            for (QPDFObjectHandle stream : page.getPageContents()) {
                if (!stream.isStream()) continue;
                const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
                if (!data) continue;
                const QByteArray original(reinterpret_cast<const char*>(data->getBuffer()),
                                          static_cast<qsizetype>(data->getSize()));
                // 重新走一遍内容流，拿到与读取时完全一致的 token 位置。
                StreamAnalysis analysis;
                analysis.pageIndex = static_cast<int>(pageIndex);
                analysis.bytes = original;
                analysis.fonts = fonts;
                if (!analyzePageContent(original, static_cast<int>(pageIndex), fonts, &analysis,
                                        error)) {
                    return {};
                }
                buildBlocks(static_cast<int>(pageIndex), analysis.shows, &nextBlockId,
                            &analysis.blocks);

                QHash<int, QPair<int, QByteArray>> ranges;
                if (byPage.contains(static_cast<int>(pageIndex))) {
                    for (const int rewriteIndex :
                         byPage.value(static_cast<int>(pageIndex))) {
                        const BlockRewrite& rewrite = rewrites.at(rewriteIndex);
                        for (int b = 0; b < rewrite.blockIds.size(); ++b) {
                            const ContentTextBlock* block = nullptr;
                            for (const ContentTextBlock& candidate : analysis.blocks) {
                                if (candidate.blockId == rewrite.blockIds.at(b)) {
                                    block = &candidate;
                                    break;
                                }
                            }
                            if (block == nullptr) {
                                if (error) {
                                    *error = i18n::tr("aipdf::pdf::StreamingEdit",
                                                     "第 %1 页上找不到要改写的文字块")
                                                 .arg(static_cast<int>(pageIndex) + 1);
                                }
                                return {};
                            }
                            // 第一个块放新内容，其余的块清空。
                            const QByteArray blob =
                                (b == 0) ? buildRewriteBlob(*block, rewrite) : QByteArray();
                            for (const ContentTextBlock::Region& region : block->regions) {
                                ranges.insert(region.firstOperandToken,
                                              qMakePair(region.lastOperatorToken, blob));
                            }
                        }
                    }
                }
                if (ranges.isEmpty()) continue;
                const QByteArray updated = spliceTokens(original, ranges, error);
                if (!error->isEmpty()) return {};
                if (updated.isEmpty()) continue;
                if (updated != original) {
                    stream.replaceStreamData(updated.toStdString(), QPDFObjectHandle::newNull(),
                                             QPDFObjectHandle::newNull());
                    anyChange = true;
                }
            }
        }
        if (!anyChange) {
            if (error) {
                *error = i18n::tr("aipdf::pdf::StreamingEdit", "没有需要写回的内容");
            }
            return {};
        }
        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.write();
        std::unique_ptr<Buffer> output(writer.getBuffer());
        if (!output || output->getSize() == 0) {
            if (error) *error = i18n::tr("aipdf::pdf::StreamingEdit", "写回的 PDF 是空的");
            return {};
        }
        return QByteArray(reinterpret_cast<const char*>(output->getBuffer()),
                          static_cast<qsizetype>(output->getSize()));
    } catch (const std::exception& exception) {
        if (error) *error = QString::fromUtf8(exception.what());
        return {};
    }
#else
    if (error) {
        *error = i18n::tr("aipdf::pdf::StreamingEdit", "内容流通道不可用：这一版没有编进 qpdf");
    }
    return {};
#endif
}

// ---------------------------------------------------------------------------
// 全文流式改写
// ---------------------------------------------------------------------------

StreamingReflowResult reflowDocumentStream(const QByteArray& pdfBytes,
                                            const StreamingReflowOptions& options) {
    StreamingReflowResult result;
    if (pdfBytes.isEmpty()) {
        result.report.error = i18n::tr("aipdf::pdf::StreamingEdit", "输入的 PDF 是空的");
        return result;
    }
    if (options.targetPageWidth <= 0.0) {
        result.report.error = i18n::tr("aipdf::pdf::StreamingEdit", "目标页宽必须大于 0");
        return result;
    }
    const double contentWidth = options.targetPageWidth - options.marginLeft - options.marginRight;
    if (contentWidth <= 1.0) {
        result.report.error =
            i18n::tr("aipdf::pdf::StreamingEdit", "左右边距把可用宽度吃光了，没有可排版的栏宽");
        return result;
    }

    const StructTreeReport tree = readStructTree(pdfBytes);
    result.report.hasStructTags = tree.hasStructTree;
    if (!tree.hasStructTree) {
        if (options.requireStructTags) {
            result.report.error = i18n::tr(
                "aipdf::pdf::StreamingEdit",
                "这份文档没有结构标签，没有可靠的段落边界，整篇流式重排已拒绝；只能逐段处理");
            return result;
        }
        result.report.notes.append(i18n::tr(
            "aipdf::pdf::StreamingEdit",
            "这份文档没有结构标签，本次只按内容流里的段落逐段处理，不算整篇流式重排"));
    }

    QVector<ContentTextBlock> blocks;
    QString error;
    if (!readContentTextBlocks(pdfBytes, &blocks, &error)) {
        result.report.error = error;
        return result;
    }
    const QVector<QRectF> pageBoxes = readPageMediaBoxes(pdfBytes, &error);
    result.report.pageCount = pageBoxes.size();
    result.report.blockCount = blocks.size();
    for (const ContentTextBlock& block : blocks) result.report.sourceCharCount += block.text.size();

    // 按标签树把块归成段落；没有标签时每个块自成一段。
    const QVector<TextParagraph> paragraphs = groupBlocksIntoParagraphs(pdfBytes, blocks, &error);

    QVector<BlockRewrite> rewrites;
    QVector<QPair<int, QString>> expectedText;  // 页号 → 写回后应当出现的文字
    int degraded = 0;
    for (int paragraphIndex = 0; paragraphIndex < paragraphs.size(); ++paragraphIndex) {
        const TextParagraph& paragraph = paragraphs.at(paragraphIndex);
        ReflowedBlock record;
        record.blockId = paragraph.blockIds.value(0, -1);
        record.pageIndex = paragraph.pageIndex;

        QVector<StyledSegment> segments;
        for (int i = 0; i < paragraph.blockIds.size(); ++i) {
            const int blockId = paragraph.blockIds.at(i);
            for (const ContentTextBlock& block : blocks) {
                if (block.blockId != blockId) continue;
                // 块与块之间补空格（与 groupBlocksIntoParagraphs 的口径一致）。
                if (i > 0) segments.append({QStringLiteral(" "), block.style});
                segments.append({block.text, block.style});
            }
        }
        const QString text = paragraph.text;
        record.originalText = text;
        record.originalLineCount = paragraph.blockIds.size();

        // 交错检查：这一段里夹着别的文字块时，不能整段换掉。
        bool interleaved = false;
        for (const int blockId : paragraph.blockIds) {
            for (const ContentTextBlock& block : blocks) {
                if (block.blockId != blockId) continue;
                if (isInterleaved(block, blocks)) {
                    interleaved = true;
                    break;
                }
            }
            if (interleaved) break;
        }
        if (interleaved) {
            record.reason = i18n::tr("aipdf::pdf::StreamingEdit",
                                     "这一段的文字与别的内容交错，没法整段换行，保留原文");
            result.report.blocks.append(record);
            continue;
        }

        TextStyle measureStyle;
        measureStyle.family = paragraph.fontFamily;
        measureStyle.fontSize =
            paragraph.style.fontSize > 0.0 ? paragraph.style.fontSize : options.fontSize;
        measureStyle.lineHeight = options.lineHeight;

        QString wrapError;
        const QVector<QVector<TextPiece>> lines =
            wrapStyledText(segments, contentWidth, measureStyle, &wrapError);
        if (lines.isEmpty()) {
            record.reason = wrapError.isEmpty()
                               ? i18n::tr("aipdf::pdf::StreamingEdit", "这一段按新宽度排不出来，保留原文")
                               : wrapError;
            result.report.blocks.append(record);
            continue;
        }
        record.reflowedLineCount = lines.size();

        const double lineStep = fontLineStep(measureStyle);

        // 放不放得下：既不能超出页面，也不能压到下一段。
        const int pageIndex = paragraph.pageIndex;
        const double bottomLimit = paragraphBottomLimit(paragraphs, paragraphIndex, pageBoxes);
        QFont font(measureStyle.family);
        font.setPointSizeF(qMax(1.0, measureStyle.fontSize));
        const QFontMetricsF metrics(font);
        const double lowest =
            paragraph.firstBaseline - (lines.size() - 1) * lineStep - metrics.descent();
        if (lowest < bottomLimit) {
            record.reason = i18n::tr("aipdf::pdf::StreamingEdit",
                                     "这一段按新宽度排下来放不进原来的位置（会压到下一段或超出页面），保留原文");
            result.report.blocks.append(record);
            continue;
        }

        // 文字里有没有标准 14 号字体表示不了的字符。
        bool blockDegraded = false;
        const QString latin1 = toLatin1Placeholder(text, &blockDegraded);
        if (blockDegraded) ++degraded;

        BlockRewrite rewrite;
        rewrite.pageIndex = pageIndex;
        // 段落里的**所有**块都要写：第一个放新内容，其余的清空，
        // 否则同一段的文字会在页面上出现两遍。
        rewrite.blockIds = paragraph.blockIds;
        rewrite.lines = lines;
        rewrite.firstBaseline = paragraph.firstBaseline;
        rewrite.firstX = options.marginLeft;
        rewrite.lineStep = lineStep;
        rewrite.leftAligned = true;
        rewrites.append(rewrite);

        expectedText.append(qMakePair(pageIndex, normalizedText(latin1)));
        record.written = true;
        record.reflowedText = text;
        result.report.blocks.append(record);
    }
    result.report.degradedBlockCount = degraded;
    if (degraded > 0) {
        result.report.notes.append(
            i18n::tr("aipdf::pdf::StreamingEdit",
                     "有 %1 段文字里有标准 14 号字体表示不了的字符，写回时写成了 '?' 占位符")
                .arg(degraded));
    }
    if (rewrites.isEmpty()) {
        result.report.error =
            i18n::tr("aipdf::pdf::StreamingEdit", "没有一段能按新宽度重排，文档没有改动");
        return result;
    }

    // 写回前的对象清单：重排只该动文字，图片与矢量一条都不能少。
    QString inventoryError;
    const QVector<PageObjectInventory> beforeInventory =
        pageObjectInventory(pdfBytes, &inventoryError);
    if (!inventoryError.isEmpty()) {
        result.report.error =
            i18n::tr("aipdf::pdf::StreamingEdit",
                     "写回前取不出页面对象清单，无法证明不会丢对象：%1")
                .arg(inventoryError);
        return result;
    }

    const QByteArray written = applyBlockRewrites(pdfBytes, rewrites, &error);
    if (written.isEmpty()) {
        result.report.error = error;
        return result;
    }

    const QVector<PageObjectInventory> afterInventory =
        pageObjectInventory(written, &inventoryError);
    if (!inventoryError.isEmpty()) {
        result.report.error =
            i18n::tr("aipdf::pdf::StreamingEdit", "写回后取不出页面对象清单：%1")
                .arg(inventoryError);
        return result;
    }
    const QString lostObjects = objectInventoryLoss(beforeInventory, afterInventory);
    if (!lostObjects.isEmpty()) {
        result.report.error = lostObjects;
        return result;
    }

    // ---- 门禁：重新打开、重新统计 ----
    int pageCount = 0;
    const QStringList pageTexts = extractPageTexts(written, &pageCount, &error);
    if (!error.isEmpty()) {
        result.report.error = i18n::tr("aipdf::pdf::StreamingEdit", "写回后打不开：%1").arg(error);
        return result;
    }
    if (pageCount != result.report.pageCount) {
        result.report.error =
            i18n::tr("aipdf::pdf::StreamingEdit", "写回后页数变了（原来 %1 页，现在 %2 页）")
                .arg(result.report.pageCount)
                .arg(pageCount);
        return result;
    }
    QVector<ContentTextBlock> outputBlocks;
    if (!readContentTextBlocks(written, &outputBlocks, &error)) {
        result.report.error = error;
        return result;
    }
    for (const ContentTextBlock& block : outputBlocks) {
        result.report.outputCharCount += block.text.size();
    }
    // 逐段核对：重排之后这一段的字必须还在页面上，一个都不能少。
    for (const auto& expected : expectedText) {
        const QString pageText = normalizedText(pageTexts.value(expected.first));
        if (!pageText.contains(expected.second)) {
            result.report.error = i18n::tr("aipdf::pdf::StreamingEdit",
                                           "写回后第 %1 页上有一段文字少了字，已放弃写回")
                                       .arg(expected.first + 1);
            return result;
        }
    }
    result.report.reflowedBlockCount = rewrites.size();
    result.bytes = written;
    return result;
}

// ---------------------------------------------------------------------------
// 跨文本块合并
// ---------------------------------------------------------------------------

MergeBlocksResult mergeTextBlocksAcrossSelection(const QByteArray& pdfBytes,
                                                 const QVector<int>& blockIds,
                                                 const MergeBlocksOptions& options) {
    MergeBlocksResult result;
    if (pdfBytes.isEmpty()) {
        result.report.error = i18n::tr("aipdf::pdf::StreamingEdit", "输入的 PDF 是空的");
        return result;
    }
    if (blockIds.size() < 2) {
        result.report.error =
            i18n::tr("aipdf::pdf::StreamingEdit", "至少要两个文本块才谈得上合并，现在只有 %1 个")
                .arg(blockIds.size());
        return result;
    }
#ifdef AIPDF_WITH_QPDF
    QVector<StreamAnalysis> streams;
    QString error;
    if (!analyzeDocument(pdfBytes, &streams, &error)) {
        result.report.error = error;
        return result;
    }
    QVector<ContentTextBlock> blocks;
    for (const StreamAnalysis& stream : streams) {
        for (const ContentTextBlock& block : stream.blocks) blocks.append(block);
    }
    {
        QSet<int> pages;
        for (const StreamAnalysis& stream : streams) pages.insert(stream.pageIndex);
        result.report.pageCount = pages.size();
    }

    QVector<const ContentTextBlock*> selected;
    for (const int id : blockIds) {
        const ContentTextBlock* found = nullptr;
        for (const ContentTextBlock& block : blocks) {
            if (block.blockId == id) {
                found = &block;
                break;
            }
        }
        if (found == nullptr) {
            result.report.error =
                i18n::tr("aipdf::pdf::StreamingEdit", "找不到编号为 %1 的文本块").arg(id);
            return result;
        }
        selected.append(found);
    }
    // 按在页面上的先后排序（合并的顺序就是阅读顺序）。
    std::sort(selected.begin(), selected.end(),
              [](const ContentTextBlock* a, const ContentTextBlock* b) {
                  if (a->pageIndex != b->pageIndex) return a->pageIndex < b->pageIndex;
                  return a->firstBaseline > b->firstBaseline;
              });
    for (int i = 1; i < selected.size(); ++i) {
        if (selected.at(i)->pageIndex != selected.at(0)->pageIndex) {
            result.report.error = i18n::tr("aipdf::pdf::StreamingEdit",
                                           "选区跨了页，合并之后没法放在同一页上，已拒绝");
            return result;
        }
    }

    // 块与块之间是不是夹着图片 / 图形 / 表格。
    bool imageBetween = false;
    bool tableBetween = false;
    for (int i = 1; i < selected.size(); ++i) {
        const ContentTextBlock& previous = *selected.at(i - 1);
        const ContentTextBlock& next = *selected.at(i);
        if (previous.regions.isEmpty() || next.regions.isEmpty()) continue;
        const int from = previous.regions.last().lastOperatorToken;
        const int to = next.regions.first().firstOperandToken;
        if (to <= from) continue;
        for (const StreamAnalysis& stream : streams) {
            if (stream.pageIndex != previous.pageIndex) continue;
            for (const int token : stream.graphicOperators) {
                if (token > from && token < to) {
                    imageBetween = true;
                    break;
                }
            }
        }
    }
    // 表格与图片：PDFium 的页面对象模型能把它们从版面上认出来。
    {
        detail::ensurePageObjectModelPdfiumReady();
        const DocumentObjectModel model = readPageObjectModel(pdfBytes);
        if (model.ok()) {
            const double bandTop = selected.last()->bounds.top();
            const double bandBottom = selected.first()->bounds.bottom();
            for (const PageObject& object : model.objects) {
                if (object.pageIndex != selected.first()->pageIndex) continue;
                const bool inBand =
                    object.bounds.top() <= bandBottom && object.bounds.bottom() >= bandTop;
                if (!inBand) continue;
                if (object.kind == PageObjectKind::Table) tableBetween = true;
                else if (object.kind == PageObjectKind::Image) imageBetween = true;
            }
        }
    }
    result.report.imageBetween = imageBetween;
    result.report.tableBetween = tableBetween;
    if (imageBetween || tableBetween) {
        result.report.error = i18n::tr(
            "aipdf::pdf::StreamingEdit",
            "选中的块之间夹着图片或表格，合并会把中间的内容挤掉，已拒绝");
        return result;
    }

    // 拼成一段：块与块之间插入连接符，每一块的样式跟着自己那段文字。
    QVector<StyledSegment> segments;
    QString mergedText;
    for (int i = 0; i < selected.size(); ++i) {
        const ContentTextBlock& block = *selected.at(i);
        if (i > 0 && !options.separator.isEmpty()) {
            segments.append({options.separator, block.style});
            mergedText += options.separator;
        }
        segments.append({block.text, block.style});
        mergedText += block.text;
        result.report.sourceCharCount += block.text.size();
    }
    result.report.blockCount = selected.size();

    const ContentTextBlock& first = *selected.first();
    double width = options.targetWidth;
    if (width <= 0.0) width = qMax(20.0, first.bounds.width());
    TextStyle measureStyle;
    measureStyle.family = first.fontFamily;
    measureStyle.fontSize = first.style.fontSize > 0.0 ? first.style.fontSize : 12.0;
    measureStyle.lineHeight = options.lineHeight;

    QString wrapError;
    const QVector<QVector<TextPiece>> lines =
        wrapStyledText(segments, width, measureStyle, &wrapError);
    if (lines.isEmpty()) {
        result.report.error = wrapError.isEmpty()
                                  ? i18n::tr("aipdf::pdf::StreamingEdit", "合并之后一段都排不出来")
                                  : wrapError;
        return result;
    }
    result.report.mergedLineCount = lines.size();
    for (const QVector<TextPiece>& line : lines) {
        for (const TextPiece& piece : line) result.report.outputCharCount += piece.text.size();
    }

    const double lineStep = fontLineStep(measureStyle);

    // 合并之后会不会超出页面下边界。超了要**说**，不能静默把字印到页面外面。
    {
        double bottomLimit = 0.0;
        for (const StreamAnalysis& stream : streams) {
            if (stream.pageIndex != first.pageIndex) continue;
            // PDF user space 里 y 向上，Qt 的 top() 才是下那条边。
            bottomLimit = stream.box.top() + 18.0;
            break;
        }
        QFont font(measureStyle.family);
        font.setPointSizeF(qMax(1.0, measureStyle.fontSize));
        const QFontMetricsF metrics(font);
        const double lowest =
            first.firstBaseline - (lines.size() - 1) * lineStep - metrics.descent();
        if (lowest < bottomLimit) {
            result.report.notes.append(i18n::tr(
                "aipdf::pdf::StreamingEdit",
                "合并之后超出了页面下边界，文字仍然写进 PDF，但会印到页面外面"));
        }
    }

    BlockRewrite rewrite;
    rewrite.pageIndex = first.pageIndex;
    rewrite.blockIds.append(first.blockId);
    for (int i = 1; i < selected.size(); ++i) {
        rewrite.blockIds.append(selected.at(i)->blockId);
    }
    rewrite.lines = lines;
    rewrite.firstBaseline = first.firstBaseline;
    rewrite.firstX = first.firstX;
    rewrite.lineStep = lineStep;
    rewrite.leftAligned = true;

    const QByteArray written = applyBlockRewrites(pdfBytes, QVector<BlockRewrite>{rewrite}, &error);
    if (written.isEmpty()) {
        result.report.error = error;
        return result;
    }

    // ---- 门禁：重新打开、重新统计 ----
    int pageCount = 0;
    const QStringList pageTexts = extractPageTexts(written, &pageCount, &error);
    if (!error.isEmpty()) {
        result.report.error = i18n::tr("aipdf::pdf::StreamingEdit", "写回后打不开：%1").arg(error);
        return result;
    }
    if (pageCount != result.report.pageCount) {
        result.report.error = i18n::tr("aipdf::pdf::StreamingEdit", "写回后页数变了");
        return result;
    }
    QVector<ContentTextBlock> outputBlocks;
    if (!readContentTextBlocks(written, &outputBlocks, &error)) {
        result.report.error = error;
        return result;
    }
    QString outputText;
    for (const ContentTextBlock& block : outputBlocks) outputText += block.text;
    if (significantCharCount(outputText) < significantCharCount(mergedText)) {
        result.report.error =
            i18n::tr("aipdf::pdf::StreamingEdit", "写回后字数少了（合并前 %1 个字，写回后 %2 个字），已放弃写回")
                .arg(significantCharCount(mergedText))
                .arg(significantCharCount(outputText));
        return result;
    }
    // 合并后的文字必须真的在页面上。
    const QString flattened = normalizedText(mergedText);
    const QString pageText = normalizedText(pageTexts.value(first.pageIndex));
    if (!pageText.contains(flattened)) {
        result.report.error = i18n::tr("aipdf::pdf::StreamingEdit",
                                       "合并后的文字没有完整落到页面上，已放弃写回");
        return result;
    }
    result.report.mergedBlockIds = rewrite.blockIds;
    result.bytes = written;
    return result;
#else
    result.report.error =
        i18n::tr("aipdf::pdf::StreamingEdit", "内容流通道不可用：这一版没有编进 qpdf");
    return result;
#endif
}

}  // namespace aipdf::pdf
