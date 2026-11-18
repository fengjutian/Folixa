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
    // 竖排度量（1/1000 em）：来自嵌入字体的 vhea / vmtx。
    //
    // 为什么要有：竖排里一个字占的是**字身框**（1 em 见方），
    // 拿横排宽度当步距，标点与全角字的竖列就会忽长忽短。
    //
    // hasVerticalMetrics 为假时，竖排按「竖排默认宽度」（1 em）步进，
    // 并在 ReflowResult::verticalMetricsDegraded 里把这次降级标出来。
    bool hasVerticalMetrics = false;
    // 竖排原点的高度（vhea 的 ascender）。
    double verticalAscent = 0.0;
    // 竖排原点的下沉量（vhea 的 descender，通常是负数）。
    double verticalDescent = 0.0;
    // 竖排默认宽度：字身框有多高（vmtx 的竖排 advance）。
    double verticalDefaultWidth = 0.0;
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
    // 竖排时是不是走了降级路径（字体没有竖排度量，按竖排默认宽度步进）。
    bool verticalMetricsDegraded = false;
};

class TextReflow final {
public:
    static ReflowResult layoutLine(const QString& text, const QRectF& box, const TextStyle& style);
    static ReflowResult layoutParagraph(const QString& text, const QRectF& box, const TextStyle& style);
    static ReflowResult layoutFrames(const QString& text, const QVector<QRectF>& frames,
                                     const TextStyle& style);
};

}  // namespace aipdf::pdf
