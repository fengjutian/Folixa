#include "pdf/TextReflow.h"

#include <QFont>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTextBoundaryFinder>

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
QStringList units(const QString& text) {
    QStringList result;
    const QStringList words = text.split(QRegularExpression(QStringLiteral("(\\s+)")), Qt::SkipEmptyParts);
    for (const QString& word : words) result.append(word);
    return result;
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
                                         box.top() + metrics.ascent()), width});
    result.overflow = fitted.size() < text.size();
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
            result.lines.append({grapheme, QPointF(x, y), metrics.horizontalAdvance(grapheme)});
            y += step;
        }
        if (!result.lines.isEmpty()) result.bounds = box;
        return result;
    }
    QString current;
    auto flush = [&] {
        if (current.isEmpty()) return;
        const double y = box.top() + metrics.ascent() + result.lines.size() * step;
        if (y + metrics.descent() > box.bottom()) { result.overflow = true; current.clear(); return; }
        QString output = current;
        double width = metrics.horizontalAdvance(output);
        if (style.alignment == TextAlignment::Justify && output.contains(QLatin1Char(' ')) &&
            width < box.width() * 0.92) {
            const QStringList words = output.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            const int gaps = words.size() - 1;
            const double spaceWidth = qMax(0.1, metrics.horizontalAdvance(QLatin1Char(' ')));
            int extras = qMax(0, qFloor((box.width() - width) / spaceWidth));
            output.clear();
            for (int word = 0; word < words.size(); ++word) {
                if (word > 0) {
                    const int gap = word - 1;
                    const int count = 1 + extras / gaps + (gap < extras % gaps ? 1 : 0);
                    output += QString(count, QLatin1Char(' '));
                }
                output += words[word];
            }
            width = metrics.horizontalAdvance(output);
        }
        result.lines.append({output, QPointF(box.left() + alignedX(width, box.width(), style.alignment), y), width});
        current.clear();
    };
    for (const QString& word : units(text)) {
        const QString candidate = current.isEmpty() ? word : current + QLatin1Char(' ') + word;
        if (metrics.horizontalAdvance(candidate) <= box.width()) { current = candidate; continue; }
        flush();
        if (result.overflow) break;
        if (metrics.horizontalAdvance(word) <= box.width()) current = word;
        else {
            for (const QString& grapheme : graphemes(word)) {
                const QString candidateChar = current + grapheme;
                if (!current.isEmpty() && metrics.horizontalAdvance(candidateChar) > box.width()) flush();
                if (result.overflow) break;
                current += grapheme;
            }
        }
    }
    if (!result.overflow) flush();
    if (!result.lines.isEmpty()) result.bounds = QRectF(box.left(), box.top(), box.width(), result.lines.size() * step);
    return result;
}

ReflowResult TextReflow::layoutFrames(const QString& text, const QVector<QRectF>& frames,
                                      const TextStyle& style) {
    ReflowResult result;
    QString remaining = text.simplified();
    for (const QRectF& frame : frames) {
        if (remaining.isEmpty()) break;
        int low = 1;
        int high = remaining.size();
        int best = 0;
        while (low <= high) {
            const int middle = (low + high) / 2;
            const ReflowResult candidate = layoutParagraph(remaining.left(middle), frame, style);
            if (!candidate.overflow) { best = middle; low = middle + 1; }
            else high = middle - 1;
        }
        if (best <= 0) break;
        if (best < remaining.size() && !remaining.at(best).isSpace()) {
            const int boundary = remaining.lastIndexOf(QLatin1Char(' '), best - 1);
            if (boundary > 0) best = boundary;
        }
        const QString portion = remaining.left(best).trimmed();
        const ReflowResult laidOut = layoutParagraph(portion, frame, style);
        result.lines += laidOut.lines;
        result.bounds = result.bounds.isNull() ? laidOut.bounds : result.bounds.united(laidOut.bounds);
        remaining = remaining.mid(best).trimmed();
    }
    result.overflow = !remaining.isEmpty();
    return result;
}

}  // namespace aipdf::pdf
