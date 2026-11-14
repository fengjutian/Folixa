#include "ocr/OcrGeometry.h"

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

}  // namespace aipdf::ocr
