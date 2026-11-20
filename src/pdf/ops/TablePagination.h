#pragma once

#include <QVector>

namespace aipdf::pdf {

// 表格跨页时的一行区间。
struct TableRowChunk {
    int firstRow = 0;
    int rowCount = 0;
    double height = 0.0;
    // 这一块要不要从新页开始（第一块总是 false）。
    bool newPage = false;
};

// 把一张 rows 行的表按页高切成若干块。
//
// cursor 是当前页已经用到的高度位置（页面坐标，从下往上排）；
// contentBottom/contentTop 是正文区上下边界；rowHeight 是每行高度；
// spacing 是块之间的段间距。
//
// 规则：优先在当前页放；放不下就换页；单页一行都放不下时返回空
// （调用方按「搬不过去」处理）。行不会被裁掉。
QVector<TableRowChunk> paginateTableRows(int rows, double rowHeight, double contentBottom,
                                         double contentTop, double cursor, double spacing);

}  // namespace aipdf::pdf
