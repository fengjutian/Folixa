#include "pdf/content/TextReflow.h"

#include <QFont>
#include <QFontMetricsF>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QTextOption>

#include <cmath>

namespace aipdf::pdf {
namespace {

// 竖排的默认字身框高度（1/1000 em）：字体没有竖排度量时的兜底。
//
// 为什么是 1 em：中文竖排里一个字占满一个 em 见方的字身框，
// 这是排版惯例，不是编出来的数——真正的竖排度量仍然来自字体的 vmtx。
constexpr double kVerticalDefaultWidth = 1000.0;

QFont fontFor(const TextStyle& style) {
    QFont f(style.family);
    f.setPointSizeF(style.fontSize);
    f.setLetterSpacing(QFont::AbsoluteSpacing, style.characterSpacing);
    f.setStretch(qRound(qBound(1.0, style.horizontalScale * 100.0, 4000.0)));
    return f;
}
double alignedX(double width, double boxWidth, TextAlignment alignment) {
    if (alignment == TextAlignment::Center) return (boxWidth - width) * 0.5;
    if (alignment == TextAlignment::Right) return boxWidth - width;
    return 0.0;
}
QString chopLastGrapheme(QString text) {
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    finder.toEnd();
    const qsizetype previous = finder.toPreviousBoundary();
    if (previous >= 0) text.truncate(previous);
    else text.clear();
    return text;
}
QStringList graphemes(const QString& text) {
    QStringList result;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    qsizetype start = 0;
    finder.toStart();
    for (qsizetype end = finder.toNextBoundary(); end >= 0; end = finder.toNextBoundary()) {
        result.append(text.mid(start, end - start));
        start = end;
    }
    return result;
}
}

ReflowResult TextReflow::layoutLine(const QString& text, const QRectF& box, const TextStyle& style) {
    if (style.vertical) return layoutParagraph(text, box, style);
    ReflowResult result;
    if (box.isEmpty() || style.fontSize <= 0.0) return result;
    const QFontMetricsF metrics(fontFor(style));
    QString fitted = text;
    while (!fitted.isEmpty() && metrics.horizontalAdvance(fitted) > box.width()) fitted = chopLastGrapheme(fitted);
    const double width = metrics.horizontalAdvance(fitted);
    result.lines.append({fitted, QPointF(box.left() + alignedX(width, box.width(), style.alignment),
                                         box.top() + metrics.ascent()), width, 0, fitted.size()});
    result.consumedLength = fitted.size();
    result.overflow = fitted.size() < text.size();
    result.remainingText = text.mid(result.consumedLength);
    result.bounds = QRectF(box.left(), box.top(), result.lines.constFirst().width, metrics.height());
    return result;
}

ReflowResult TextReflow::layoutParagraph(const QString& text, const QRectF& box, const TextStyle& style) {
    ReflowResult result;
    if (box.isEmpty() || style.fontSize <= 0.0) return result;
    const QFontMetricsF metrics(fontFor(style));
    const double step = qMax(metrics.height(), style.fontSize * style.lineHeight);
    if (style.vertical) {
        // 竖排：列自右向左、列内自上向下。
        //
        // 步进用**竖排度量**（字身框高度），不是横排宽度：
        // 横排宽度说的是「这个字有多宽」，竖排里占的是「这个字身框有多高」，
        // 拿横排宽度当步距，标点与全角字的竖列就会忽长忽短。
        //
        // 字体没有竖排度量时按竖排默认宽度（1 em）走，并如实标出降级 ——
        // 不拿横排宽度冒充竖排宽度。
        const bool hasVerticalMetrics = style.hasVerticalMetrics;
        const double verticalWidthEm =
            (hasVerticalMetrics && style.verticalDefaultWidth > 0.0)
                ? style.verticalDefaultWidth
                : kVerticalDefaultWidth;
        const double glyphStep = verticalWidthEm / 1000.0 * style.fontSize;
        const double ascent =
            (hasVerticalMetrics && style.verticalAscent > 0.0)
                ? style.verticalAscent / 1000.0 * style.fontSize
                : metrics.ascent();
        const double descent =
            (hasVerticalMetrics && style.verticalDescent != 0.0)
                ? std::abs(style.verticalDescent) / 1000.0 * style.fontSize
                : std::abs(metrics.descent());
        result.verticalMetricsDegraded = !hasVerticalMetrics;
        double x = box.right() - ascent;
        double y = box.top() + ascent;
        qsizetype sourceOffset = 0;
        for (const QString& grapheme : graphemes(text)) {
            if (grapheme.trimmed().isEmpty()) {
                // 空白不画，但**必须推进 sourceOffset**：
                // 不推进的话，后面每个字对应的源串位置都会错位，
                // consumedLength 也会少算这一段（linked frame 接着排就会接错地方）。
                sourceOffset += grapheme.size();
                continue;
            }
            if (y + descent > box.bottom()) {
                y = box.top() + ascent;
                x -= step;
            }
            if (x - descent < box.left()) {
                result.overflow = true;
                break;
            }
            result.lines.append({grapheme, QPointF(x, y), metrics.horizontalAdvance(grapheme),
                                 sourceOffset, grapheme.size()});
            sourceOffset += grapheme.size();
            y += glyphStep;
        }
        result.consumedLength = sourceOffset;
        result.remainingText = text.mid(sourceOffset);
        result.overflow = !result.remainingText.isEmpty();
        if (!result.lines.isEmpty()) result.bounds = box;
        return result;
    }
    // QTextLayout is the same shaping stack Qt uses for editors.  Unlike the
    // old word splitter it preserves whitespace/newlines and handles Unicode
    // BiDi, kerning, ligatures, combining marks and fallback fonts before line
    // breaking.  Source offsets let the next linked frame resume losslessly.
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    switch (style.alignment) {
    case TextAlignment::Center: option.setAlignment(Qt::AlignHCenter); break;
    case TextAlignment::Right: option.setAlignment(Qt::AlignRight); break;
    case TextAlignment::Justify: option.setAlignment(Qt::AlignJustify); break;
    default: option.setAlignment(Qt::AlignLeft); break;
    }
    QTextLayout layout(text, fontFor(style));
    layout.setTextOption(option);
    layout.beginLayout();
    double y = 0.0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        const double lineHeight = qMax(step, line.height());
        if (y + lineHeight > box.height() + 0.01) break;
        line.setLineWidth(box.width());
        line.setPosition(QPointF(0.0, y));
        const qsizetype start = line.textStart();
        const qsizetype length = line.textLength();
        const QString source = text.mid(start, length);
        const double width = qMin(box.width(), line.naturalTextWidth());
        const double x = box.left() + line.x();
        result.lines.append({source, QPointF(x, box.top() + y + line.ascent()),
                             width, start, length});
        result.consumedLength = qMax(result.consumedLength, start + length);
        y += lineHeight;
    }
    layout.endLayout();
    result.remainingText = text.mid(result.consumedLength);
    result.overflow = !result.remainingText.isEmpty();
    if (!result.lines.isEmpty())
        result.bounds = QRectF(box.left(), box.top(), box.width(), y);
    return result;
}

ReflowResult TextReflow::layoutFrames(const QString& text, const QVector<QRectF>& frames,
                                      const TextStyle& style) {
    ReflowResult result;
    QString remaining = text;
    qsizetype sourceBase = 0;
    for (const QRectF& frame : frames) {
        if (remaining.isEmpty()) break;
        ReflowResult laidOut = layoutParagraph(remaining, frame, style);
        if (laidOut.consumedLength <= 0) break;
        for (ReflowLine& line : laidOut.lines) line.sourceStart += sourceBase;
        result.lines += laidOut.lines;
        result.bounds = result.bounds.isNull() ? laidOut.bounds : result.bounds.united(laidOut.bounds);
        sourceBase += laidOut.consumedLength;
        remaining = laidOut.remainingText;
    }
    result.consumedLength = sourceBase;
    result.remainingText = remaining;
    result.overflow = !remaining.isEmpty();
    return result;
}

}  // namespace aipdf::pdf
