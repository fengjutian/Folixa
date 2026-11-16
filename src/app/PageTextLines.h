#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

#include "pdf/TextPage.h"

namespace aipdf::app {

// 一页的文字行（PDF user space），按从上到下、从左到右排。
//
// box 是 PDF user space（左下原点、y 向上）：y 取的是**下边**、高度向上为正，
// 与 pdf::TextChar::box 是同一套约定（pdf::TaggedRegion 也收这种矩形）。
// 注意这与屏幕坐标相反：屏幕上「top 小的是上面」，这里「top 小的才是下面」。
struct UserSpaceLine {
    QRectF box;
    QString text;
};

// 把一页的字符聚成文字行。
//
// 为什么自己聚类而不用 app::TextLayout：TextLayout 活在 app 层、按显示空间算，
// 而标签树要的是 user space 的行；而且整篇都要写标签树，
// 不能只覆盖「用户翻到过、文本层已经排好」的页。
//
// 聚类规则与 TextLayout 一致（同一份文档在选区与标签树里看到的行必须对得上）。
// 空字符（只有空白）与空框的字符会被丢掉。
QVector<UserSpaceLine> userSpaceTextLines(const pdf::TextPage& text);

}  // namespace aipdf::app
