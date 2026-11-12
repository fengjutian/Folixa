#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::app {

// 显示空间里的一个字符。
struct LayoutChar {
    QString text;
    QRectF box;  // 显示空间（左上原点）
    // 对应原始文本页里的字符下标，便于回查。
    int sourceIndex = 0;
};

// 一页文本的排版结果：重新分行 + 重建阅读顺序。
//
// PDFium 给出的字符顺序是**内容流顺序**，不等于视觉阅读顺序：
// 多栏、表格、图注混排时直接复制会得到乱序文本。
// 这里按显示空间里的位置重新聚成行，行内按横向位置排序，
// 得到「行优先、从上到下」的阅读顺序。
//
// 另外，竖排（CJK 竖写）时按列聚类、列内从上到下、列间从右到左。
class TextLayout {
public:
    // 一行：阅读顺序下的一段连续区间 [start, end)。
    struct Line {
        int start = 0;
        int end = 0;
        QRectF box;  // 该行所有字符的包围盒（显示空间）
    };

    void setChars(const QVector<LayoutChar>& chars);
    void clear();

    bool isEmpty() const;
    int charCount() const;

    // 阅读顺序 → 原始字符下标。
    const QVector<int>& readingOrder() const;

    // 重新分行后的行列表（按阅读顺序排列）。
    const QVector<Line>& lines() const;

    // 命中测试：显示空间坐标 → 阅读顺序下标；没命中返回 -1。
    int hitTest(const QPointF& point) const;

    // 双击选词：阅读顺序下的 [start, end)；没命中返回 false。
    bool wordRangeAt(int orderIndex, int* start, int* end) const;

    // 选区文本：阅读顺序区间 [start, end) 拼出的文本。
    QString textForRange(int start, int end) const;

    // 选区高亮矩形（按行拆开，便于逐行绘制）。
    QVector<QRectF> highlightRects(int start, int end) const;

    // 文本是否按竖排处理（用于诊断与测试）。
    bool isVertical() const;

private:
    QVector<LayoutChar> chars_;
    QVector<int> order_;
    QVector<Line> lines_;
    // 每个字符在阅读顺序中的位置（与 chars_ 平行）。
    QVector<int> orderPositions_;
    bool vertical_ = false;

    void rebuild();
    // 阅读顺序下标 → 所属行的下标；没有对应行时返回 -1。
    int lineOfPosition(int orderPosition) const;
};

}  // namespace aipdf::app
