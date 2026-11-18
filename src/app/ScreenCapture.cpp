#include "app/ScreenCapture.h"

#include <QRect>

namespace aipdf::app {

QRect dragRegion(const QPoint& anchor, const QPoint& current) {
    const QPoint topLeft(qMin(anchor.x(), current.x()), qMin(anchor.y(), current.y()));
    const QPoint bottomRight(qMax(anchor.x(), current.x()), qMax(anchor.y(), current.y()));
    const QRect raw(topLeft, bottomRight);
    // 起点与终点重合：QRect 的宽高会是 0（不是负的），这里显式返回空矩形，
    // 免得调用方拿到一个"有一个点"的矩形当成有效选区。
    if (raw.width() <= 0 || raw.height() <= 0) {
        return QRect();
    }
    return raw;
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
