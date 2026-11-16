#pragma once

#include <QColor>
#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

enum class TextAlignment { Left, Center, Right, Justify };

struct TextStyle {
    QString family = QStringLiteral("Helvetica");
    double fontSize = 12.0;
    QColor color = Qt::black;
    double lineHeight = 1.2;
    double characterSpacing = 0.0;
    double horizontalScale = 1.0;
    double opacity = 1.0;
    double baselineShift = 0.0;
    bool vertical = false;
    TextAlignment alignment = TextAlignment::Left;
};

struct ReflowLine {
    QString text;
    QPointF baseline;
    double width = 0.0;
    // UTF-16 source range.  Keeping this range is important for linked frames:
    // visual-order text (BiDi), ligatures and surrogate pairs cannot safely be
    // mapped back by counting the glyphs that happened to be painted.
    qsizetype sourceStart = 0;
    qsizetype sourceLength = 0;
};
struct ReflowResult {
    QVector<ReflowLine> lines;
    QRectF bounds;
    bool overflow = false;
    qsizetype consumedLength = 0;
    QString remainingText;
};

class TextReflow final {
public:
    static ReflowResult layoutLine(const QString& text, const QRectF& box, const TextStyle& style);
    static ReflowResult layoutParagraph(const QString& text, const QRectF& box, const TextStyle& style);
    static ReflowResult layoutFrames(const QString& text, const QVector<QRectF>& frames,
                                     const TextStyle& style);
};

}  // namespace aipdf::pdf
