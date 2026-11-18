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

// 旋转后的页面坐标 → 原页面坐标。
//
// 扫描件横放时，引擎是在「转正后」的图上识别的，返回的框落在旋转后的页面坐标系里；
// 要贴回原页面必须先反变换回去。degrees 是**图像被顺时针旋转的角度**（90 / 180 / 270），
// pageSize 是**原页面**尺寸（不是旋转后的）。其他角度原样返回。
QRectF unrotatePageRect(const QRectF& rect, const QSizeF& pageSize, int degrees);

}  // namespace aipdf::ocr
