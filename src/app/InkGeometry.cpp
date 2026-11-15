#include "app/InkGeometry.h"

#include <algorithm>
#include <cmath>

namespace aipdf::app {

double distanceToSegment(const QPointF& point, const QPointF& a, const QPointF& b) {
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0) {
        // 退化成一个点（单击留下的"点笔"）。
        return std::hypot(point.x() - a.x(), point.y() - a.y());
    }
    // 把 point 投影到 ab 上，t 夹在 [0,1]：线段外只能取端点。
    const double t = std::clamp(((point.x() - a.x()) * dx + (point.y() - a.y()) * dy) /
                                    lengthSquared,
                                0.0, 1.0);
    const double px = a.x() + t * dx;
    const double py = a.y() + t * dy;
    return std::hypot(point.x() - px, point.y() - py);
}

bool strokeHitsCircle(const QVector<QPointF>& stroke, const QPointF& center, double radius) {
    if (stroke.isEmpty() || radius <= 0.0) {
        return false;
    }
    if (stroke.size() == 1) {
        return distanceToSegment(center, stroke.first(), stroke.first()) <= radius;
    }
    for (int i = 0; i + 1 < stroke.size(); ++i) {
        if (distanceToSegment(center, stroke.at(i), stroke.at(i + 1)) <= radius) {
            return true;
        }
    }
    return false;
}

}  // namespace aipdf::app
