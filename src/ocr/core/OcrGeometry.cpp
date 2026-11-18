#include "ocr/core/OcrGeometry.h"

#include <algorithm>

namespace aipdf::ocr {

QRectF imageRectToPage(const QRectF& pixelRect, const QSize& imageSize, const QSizeF& pageSize) {
    if (imageSize.isEmpty() || pageSize.width() <= 0.0 || pageSize.height() <= 0.0) {
        return {};
    }

    const double scaleX = pageSize.width() / static_cast<double>(imageSize.width());
    const double scaleY = pageSize.height() / static_cast<double>(imageSize.height());

    const double left = pixelRect.left() * scaleX;
    const double right = pixelRect.right() * scaleX;
    // 图像坐标从上往下，页面坐标从下往上：用页面高度减回来。
    const double top = pageSize.height() - pixelRect.top() * scaleY;
    const double bottom = pageSize.height() - pixelRect.bottom() * scaleY;

    // 引擎可能给出越界框（图像边缘的字符），统一裁到页面里。
    const QRectF pageRect(QPointF(0.0, 0.0), pageSize);
    const QRectF mapped(left, bottom, right - left, top - bottom);
    return mapped.normalized().intersected(pageRect);
}

QRectF unrotatePageRect(const QRectF& rect, const QSizeF& pageSize, int degrees) {
    if (pageSize.width() <= 0.0 || pageSize.height() <= 0.0) {
        return rect;
    }
    const double w = pageSize.width();
    const double h = pageSize.height();
    const double x1 = rect.left();
    const double x2 = rect.right();
    // PDF 坐标原点在左下：top 是上边（y 大），bottom 是下边（y 小）。
    const double y1 = rect.bottom();
    const double y2 = rect.top();

    switch (degrees) {
    case 90:
        // 图顺时针转 90° 后：x = W - y'，y = x'
        return QRectF(w - y2, x1, y2 - y1, x2 - x1).normalized();
    case 180:
        // x = W - x'，y = H - y'
        return QRectF(w - x2, h - y2, x2 - x1, y2 - y1).normalized();
    case 270:
        // 图顺时针转 270°（等价逆时针 90°）：x = y'，y = H - x'
        return QRectF(y1, h - x2, y2 - y1, x2 - x1).normalized();
    default:
        return rect;
    }
}

}  // namespace aipdf::ocr
