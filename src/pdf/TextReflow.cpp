#include "pdf/TextReflow.h"

#include <QFont>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTextBoundaryFinder>

namespace aipdf::pdf {
namespace {
QFont fontFor(const TextStyle& style) { QFont f(style.family); f.setPointSizeF(style.fontSize); return f; }
QStringList units(const QString& text) {
    QStringList result;
    const QStringList words = text.split(QRegularExpression(QStringLiteral("(\\s+)")), Qt::SkipEmptyParts);
    for (const QString& word : words) result.append(word);
    return result;
}
}

ReflowResult TextReflow::layoutLine(const QString& text, const QRectF& box, const TextStyle& style) {
    ReflowResult result;
    if (box.isEmpty() || style.fontSize <= 0.0) return result;
    const QFontMetricsF metrics(fontFor(style));
    QString fitted = text;
    while (!fitted.isEmpty() && metrics.horizontalAdvance(fitted) > box.width()) fitted.chop(1);
    result.lines.append({fitted, QPointF(box.left(), box.top() + metrics.ascent()), metrics.horizontalAdvance(fitted)});
    result.overflow = fitted.size() < text.size();
    result.bounds = QRectF(box.left(), box.top(), result.lines.constFirst().width, metrics.height());
    return result;
}

ReflowResult TextReflow::layoutParagraph(const QString& text, const QRectF& box, const TextStyle& style) {
    ReflowResult result;
    if (box.isEmpty() || style.fontSize <= 0.0) return result;
    const QFontMetricsF metrics(fontFor(style));
    const double step = qMax(metrics.height(), style.fontSize * style.lineHeight);
    QString current;
    auto flush = [&] {
        if (current.isEmpty()) return;
        const double y = box.top() + metrics.ascent() + result.lines.size() * step;
        if (y + metrics.descent() > box.bottom()) { result.overflow = true; current.clear(); return; }
        result.lines.append({current, QPointF(box.left(), y), metrics.horizontalAdvance(current)});
        current.clear();
    };
    for (const QString& word : units(text)) {
        const QString candidate = current.isEmpty() ? word : current + QLatin1Char(' ') + word;
        if (metrics.horizontalAdvance(candidate) <= box.width()) { current = candidate; continue; }
        flush();
        if (result.overflow) break;
        if (metrics.horizontalAdvance(word) <= box.width()) current = word;
        else {
            for (QChar ch : word) {
                const QString candidateChar = current + ch;
                if (!current.isEmpty() && metrics.horizontalAdvance(candidateChar) > box.width()) flush();
                if (result.overflow) break;
                current += ch;
            }
        }
    }
    if (!result.overflow) flush();
    if (!result.lines.isEmpty()) result.bounds = QRectF(box.left(), box.top(), box.width(), result.lines.size() * step);
    return result;
}

}  // namespace aipdf::pdf
