#include "app/PageTransform.h"

namespace aipdf::app {

void PageTransform::setPage(double mediaLeft,
                            double mediaBottom,
                            double width,
                            double height) {
    mediaLeft_ = mediaLeft;
    mediaBottom_ = mediaBottom;
    width_ = width;
    height_ = height;
}

void PageTransform::setRotationDegrees(int degrees) {
    int normalized = ((degrees % 360) + 360) % 360;
    // 只支持 90 度的整数倍，其余按 90 度取整。
    rotation_ = (normalized / 90) * 90;
}

int PageTransform::rotationDegrees() const {
    return rotation_;
}

QSizeF PageTransform::displaySize() const {
    const bool swapped = (rotation_ == 90 || rotation_ == 270);
    return swapped ? QSizeF(height_, width_) : QSizeF(width_, height_);
}

QPointF PageTransform::toDisplay(const QPointF& userSpace) const {
    // 相对 MediaBox 左下角的位置；ry 向上为正。
    const double rx = userSpace.x() - mediaLeft_;
    const double ry = userSpace.y() - mediaBottom_;

    switch (rotation_) {
        case 90:
            // 顺时针 90°：显示尺寸变成 高 × 宽。
            return {ry, rx};
        case 180:
            return {width_ - rx, ry};
        case 270:
            return {height_ - ry, width_ - rx};
        default:
            // 未旋转：翻转到左上原点。
            return {rx, height_ - ry};
    }
}

QRectF PageTransform::toDisplay(const QRectF& userSpace) const {
    const QPointF a = toDisplay(QPointF(userSpace.left(), userSpace.bottom()));
    const QPointF b = toDisplay(QPointF(userSpace.right(), userSpace.top()));
    const double left = qMin(a.x(), b.x());
    const double top = qMin(a.y(), b.y());
    return QRectF(left, top, qMax(a.x(), b.x()) - left, qMax(a.y(), b.y()) - top);
}

QPointF PageTransform::toUserSpace(const QPointF& displaySpace) const {
    // toDisplay 的逆变换，逐个旋转角解方程：
    //   0°:  dx = rx,      dy = height - ry
    //   90°: dx = ry,      dy = rx
    //   180°:dx = width - rx, dy = ry
    //   270°:dx = height - ry, dy = width - rx
    const double dx = displaySpace.x();
    const double dy = displaySpace.y();

    switch (rotation_) {
        case 90:
            return {mediaLeft_ + dy, mediaBottom_ + dx};
        case 180:
            return {mediaLeft_ + width_ - dx, mediaBottom_ + dy};
        case 270:
            return {mediaLeft_ + width_ - dy, mediaBottom_ + height_ - dx};
        default:
            return {mediaLeft_ + dx, mediaBottom_ + height_ - dy};
    }
}

}  // namespace aipdf::app
