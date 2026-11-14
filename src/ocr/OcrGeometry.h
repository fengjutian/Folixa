#pragma once

#include <QRectF>
#include <QSize>
#include <QSizeF>

namespace aipdf::ocr {

// 图像像素矩形 → 页面用户空间矩形（PDF 点，原点在左下）。
//
// PDF 用户空间原点在左下，图像原点在左上：不翻 Y 轴的话，高亮框会上下颠倒，
// 而且只在非对称页面上才看得出来——所以单独成函数并单测。
// 返回值裁剪到页面范围内；尺寸非法时返回空矩形。
QRectF imageRectToPage(const QRectF& pixelRect, const QSize& imageSize, const QSizeF& pageSize);

}  // namespace aipdf::ocr
