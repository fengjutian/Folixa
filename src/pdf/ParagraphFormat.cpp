#include "pdf/ParagraphFormat.h"

#include <QFont>
#include <QFontMetricsF>

namespace aipdf::pdf {
namespace {

// 与 pdf::TextReflow 内部一致的字体构造（family + 字号）。
//
// 为什么要自己再构造一次：TextReflow 的 fontFor 是文件私有的，
// 而本模块要自己量自然宽度（对齐与 justify 都要用）。
QFont fontFor(const ParagraphFormat& format) {
    QFont font(format.fontFamily);
    font.setPointSizeF(format.fontSize);
    return font;
}

// 行距（点）。口径与 TextReflow 内部的 step 一致：
// 行距不能小于字体自己的行高，否则行与行会叠在一起。
double lineStep(const ParagraphFormat& format, double fontHeight) {
    if (format.lineSpacingMode == LineSpacingMode::Fixed) {
        return qMax(fontHeight, format.lineSpacing);
    }
    return qMax(fontHeight, format.fontSize * format.lineSpacing);
}

// 行内的空格个数。justify 靠它把多出来的宽度分摊给空格。
int countSpaces(const QString& text) {
    int spaces = 0;
    for (const QChar& ch : text) {
        if (ch == QLatin1Char(' ')) ++spaces;
    }
    return spaces;
}

// 数字转成内容流里用的写法：去掉多余的 0，最多三位小数。
QByteArray formatNumber(double value) {
    QString text = QString::number(value, 'f', 3);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    if (text == QStringLiteral("-0")) text = QStringLiteral("0");
    return text.toLatin1();
}

// 文字转成 PDF 字面量字符串（含括号与转义）。
//
// 为什么超出 Latin-1 的字符写成 '?' 而不是悄悄丢掉：
// 丢掉的话排版结果会少字，而调用方很难发现；留一个占位符至少是可见的。
QByteArray pdfLiteralString(const QString& text) {
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
        case '(':
            out += "\\(";
            break;
        case ')':
            out += "\\)";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        default: {
            const unsigned char raw = static_cast<unsigned char>(byte);
            if (raw < 32 || raw > 126) {
                // 八进制转义必须是三位，少一位就会被下一个字符吃掉。
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

}  // namespace

ParagraphPlan layoutParagraph(const QString& text, const QRectF& box, const ParagraphFormat& format) {
    ParagraphPlan plan;
    if (text.isEmpty()) {
        plan.error = QStringLiteral("待排版的文字为空");
        return plan;
    }
    if (box.width() <= 0.0) {
        plan.error = QStringLiteral("排版框的宽度必须大于 0");
        return plan;
    }
    if (box.height() <= 0.0) {
        plan.error = QStringLiteral("排版框的高度必须大于 0");
        return plan;
    }
    if (format.fontSize <= 0.0) {
        plan.error = QStringLiteral("字号必须大于 0");
        return plan;
    }
    if (format.leftIndent < 0.0 || format.rightIndent < 0.0 || format.firstLineIndent < 0.0) {
        plan.error = QStringLiteral("缩进不能是负数");
        return plan;
    }

    const double contentLeft = box.left() + format.leftIndent;
    const double contentWidth = box.width() - format.leftIndent - format.rightIndent;
    if (contentWidth <= 0.0) {
        plan.error = QStringLiteral("左右缩进之和已经不小于排版框宽度，没有可用的排版宽度");
        return plan;
    }
    if (format.firstLineIndent >= contentWidth) {
        plan.error = QStringLiteral("首行缩进不小于可用的排版宽度，第一行放不下文字");
        return plan;
    }

    TextStyle style;
    style.family = format.fontFamily;
    style.fontSize = format.fontSize;
    // 对齐在下面自己算：TextReflow 的对齐是「整段一个设置」，
    // 而这里首行缩进会让第一行的可用宽度与其它行不同，
    // justify 的「最后一行不拉伸」也要按整段判断，不能交给单次排版去决定。
    style.alignment = TextAlignment::Left;
    if (format.lineSpacingMode == LineSpacingMode::Fixed) {
        // 固定磅值换算成倍数交给 TextReflow（它的 step 公式是
        // max(字体行高, 字号 × 倍数)，与本模块的口径一致）。
        style.lineHeight = format.lineSpacing / format.fontSize;
    } else {
        style.lineHeight = format.lineSpacing;
    }

    const QFontMetricsF metrics(fontFor(format));
    const double step = lineStep(format, metrics.height());

    QVector<ReflowLine> raw;
    QString remaining;

    if (format.firstLineIndent > 0.0) {
        // 第一行单独排一次：宽度按「内容宽度 - 首行缩进」。
        //
        // 为什么不能整段一起排完再把第一行平移一下：
        // 那样第一行的断行位置是按整段宽度算的，首行缩进之后第一行会顶出右边界。
        const QRectF firstBox(contentLeft + format.firstLineIndent, box.top(),
                              contentWidth - format.firstLineIndent, box.height());
        const ReflowResult head = TextReflow::layoutParagraph(text, firstBox, style);
        if (head.lines.isEmpty()) {
            plan.error = QStringLiteral("第一行放不下任何文字");
            return plan;
        }
        raw.append(head.lines.first());
        const qsizetype consumed = head.lines.first().sourceStart + head.lines.first().sourceLength;
        const QString tailText = text.mid(consumed);
        if (!tailText.isEmpty()) {
            // 后续各行按整段内容宽度排，起点落在第一行的下面。
            const double restTop = head.lines.first().baseline.y() + step - metrics.ascent();
            const double restHeight = (box.top() + box.height()) - restTop;
            if (restHeight <= 0.0) {
                remaining = tailText;
            } else {
                const QRectF restBox(contentLeft, restTop, contentWidth, restHeight);
                ReflowResult tail = TextReflow::layoutParagraph(tailText, restBox, style);
                for (ReflowLine& line : tail.lines) {
                    line.sourceStart += consumed;
                    raw.append(line);
                }
                remaining = tail.remainingText;
            }
        }
    } else {
        const QRectF contentBox(contentLeft, box.top(), contentWidth, box.height());
        const ReflowResult all = TextReflow::layoutParagraph(text, contentBox, style);
        raw = all.lines;
        remaining = all.remainingText;
    }

    if (raw.isEmpty()) {
        plan.error = QStringLiteral("排版框的高度放不下一行文字");
        return plan;
    }

    const int lineCount = raw.size();
    for (int i = 0; i < lineCount; ++i) {
        const ReflowLine& source = raw.at(i);
        ParagraphLine line;
        line.text = source.text;
        line.fontSize = format.fontSize;
        line.baseline = source.baseline;
        line.naturalWidth = source.width;
        line.lastLine = (i == lineCount - 1);

        const bool first = (i == 0);
        const double indent = first ? format.firstLineIndent : 0.0;
        const double left = contentLeft + indent;
        const double available = contentWidth - indent;

        switch (format.alignment) {
        case TextAlignment::Center:
            line.x = left + (available - line.naturalWidth) * 0.5;
            line.width = line.naturalWidth;
            break;
        case TextAlignment::Right:
            line.x = left + (available - line.naturalWidth);
            line.width = line.naturalWidth;
            break;
        case TextAlignment::Justify:
            line.x = left;
            line.width = line.naturalWidth;
            // 最后一行不拉伸：段落末尾留白是正常的，
            // 把最后一行也撑满反而看起来像排版坏了。
            if (!line.lastLine) {
                const int spaces = countSpaces(line.text);
                if (spaces > 0 && available > line.naturalWidth) {
                    line.spaceExtra = (available - line.naturalWidth) / spaces;
                    line.width = available;
                    line.justified = true;
                }
            }
            break;
        case TextAlignment::Left:
        default:
            line.x = left;
            line.width = line.naturalWidth;
            break;
        }
        plan.lines.append(line);
    }

    plan.overflow = !remaining.isEmpty();
    plan.remainingText = remaining;
    const double usedTop = box.top();
    const double usedBottom = plan.lines.last().baseline.y() + metrics.descent();
    plan.bounds = QRectF(contentLeft, usedTop, contentWidth, qMax(0.0, usedBottom - usedTop));
    return plan;
}

QByteArray buildParagraphContentStream(const ParagraphPlan& plan, const QString& fontResourceName) {
    if (!plan.ok() || plan.lines.isEmpty()) return {};

    // 资源名里不能有空白：PDF 的名字对象遇到空白就断了。
    QString resource = fontResourceName.trimmed();
    resource.remove(QLatin1Char(' '));
    if (resource.isEmpty()) resource = QStringLiteral("F1");

    QByteArray out("BT\n");
    double currentSize = plan.lines.first().fontSize;
    out += '/' + resource.toLatin1() + ' ' + formatNumber(currentSize) + " Tf\n";
    for (const ParagraphLine& line : plan.lines) {
        if (!qFuzzyCompare(currentSize + 1.0, line.fontSize + 1.0)) {
            currentSize = line.fontSize;
            out += '/' + resource.toLatin1() + ' ' + formatNumber(currentSize) + " Tf\n";
        }
        // 绝对定位：Tm 直接给基线位置，调用方不必再做坐标换算。
        out += "1 0 0 1 " + formatNumber(line.x) + ' ' + formatNumber(line.baseline.y()) + " Tm\n";
        if (line.spaceExtra > 0.0) {
            // Tw 是字间距：对每个空格字符额外加的宽度，正好就是「用空格拉伸」。
            out += formatNumber(line.spaceExtra) + " Tw\n";
        }
        out += pdfLiteralString(line.text) + " Tj\n";
        if (line.spaceExtra > 0.0) {
            out += "0 Tw\n";
        }
    }
    out += "ET\n";
    return out;
}

}  // namespace aipdf::pdf
