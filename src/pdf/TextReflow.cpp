#include "pdf/TextReflow.h"

#include <QFont>
#include <QFontMetricsF>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QTextOption>

namespace aipdf::pdf {
namespace {
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
        double x = box.right() - metrics.ascent();
        double y = box.top() + metrics.ascent();
        qsizetype sourceOffset = 0;
        for (const QString& grapheme : graphemes(text)) {
            if (grapheme.trimmed().isEmpty()) continue;
            if (y + metrics.descent() > box.bottom()) {
                y = box.top() + metrics.ascent();
                x -= step;
            }
            if (x - metrics.descent() < box.left()) {
                result.overflow = true;
                break;
            }
            result.lines.append({grapheme, QPointF(x, y), metrics.horizontalAdvance(grapheme),
                                 sourceOffset, grapheme.size()});
            sourceOffset += grapheme.size();
            y += step;
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
