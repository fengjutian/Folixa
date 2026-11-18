#include "app/controllers/ScreenCapture.h"

#include <QRect>

namespace aipdf::app {

QRect dragRegion(const QPoint& anchor, const QPoint& current) {
    const int left = qMin(anchor.x(), current.x());
    const int top = qMin(anchor.y(), current.y());
    // 宽高用差值算，不用 QRect(QPoint, QPoint)：那个构造把第二个点当成
    // **包含**的右下角，会把宽高各多算一像素（选区与鼠标差一格）。
    const int width = qAbs(current.x() - anchor.x());
    const int height = qAbs(current.y() - anchor.y());
    if (width <= 0 || height <= 0) {
        // 起点与终点重合：还没框出东西。
        return QRect();
    }
    return QRect(left, top, width, height);
}

bool isValidRegion(const QRect& region) {
    return region.width() > 0 && region.height() > 0;
}

QImage cropRegion(const QImage& source, const QRect& region) {
    if (source.isNull() || !isValidRegion(region)) {
        return QImage();
    }
    // 先与图片边界求交：用户可能把框拖到屏幕外。
    const QRect clipped = region.intersected(source.rect());
    if (!isValidRegion(clipped)) {
        return QImage();
    }
    return source.copy(clipped);
}

}  // namespace aipdf::app
