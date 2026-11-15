#pragma once

#include <QSize>
#include <QSizeF>

namespace aipdf::app {

// 把一页放进一块屏幕里：返回渲染倍率（1.0 = 72 DPI）与渲染像素上限之间的较小值。
//
// 为什么需要单独算：放映窗口是全屏的，4K 屏上"铺满"意味着把 612×792 点的页面
// 放大到 3840×2160 附近——那是 830 万像素；再乘上页面本身的宽高比，一张位图
// 可能上百 MB。适配层对单页渲染有硬上限（单边 8192、总量 64M 像素），超了直接
// 返回空图，表现是"放映一片黑"。
//
// 所以这里显式给一个像素预算：宁可稍糊一点，也不能黑屏。
struct FitResult {
    double zoom = 1.0;
    QSize pixelSize;
    // 是否被像素预算截断（截断后画面上会有一圈黑边，属预期）。
    bool clampedByPixels = false;
};

FitResult fitPageToViewport(const QSizeF& pagePoints,
                            const QSize& viewportPixels,
                            qint64 maxPixels = 16LL * 1024 * 1024);

}  // namespace aipdf::app
