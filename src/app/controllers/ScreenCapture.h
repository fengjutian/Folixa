#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>

namespace aipdf::app {

// ========================================================== 区域框选 ======
//
// 截图区域框选的纯计算部分：拖拽起止点 → 选区矩形 → 从整屏图里裁出来。
//
// 为什么单独拎出来：这几步是**算出来的**，不是画出来的，
// 而画出来的部分（选区层）没法在单元测试里驱动。
// 把算的部分留在纯函数里，测试就能钉住"往左上拖也是同一个矩形"这类规则。

// 拖拽起止点 → 选区矩形（屏幕坐标）。
//
// 方向任意：从右下往左上拖与从左上往右下拖得到同一个矩形。
// 起点与终点重合时返回空矩形（还没框出东西）。
QRect dragRegion(const QPoint& anchor, const QPoint& current);

// 选区是否可用（宽与高都大于 0）。
bool isValidRegion(const QRect& region);

// 从整屏截图里裁出选区。
//
// 选区超出图片边界时按边界裁剪（用户可能把框拖到屏幕外）。
// 选区无效或图片为空时返回空图，不返回半张图。
QImage cropRegion(const QImage& source, const QRect& region);

}  // namespace aipdf::app
