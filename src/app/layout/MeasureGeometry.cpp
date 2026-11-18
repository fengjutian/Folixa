#include "app/layout/MeasureGeometry.h"

#include <QLineF>
#include <QString>

#include <cmath>

namespace aipdf::app {

double measureDistance(const QPointF& from, const QPointF& to) {
    return QLineF(from, to).length();
}

double measurePerimeter(const QVector<QPointF>& points) {
    double total = 0.0;
    for (int i = 0; i + 1 < points.size(); ++i) {
        total += QLineF(points.at(i), points.at(i + 1)).length();
    }
    // 多边形周长要闭合：首尾也要算一段。
    if (points.size() > 2) {
        total += QLineF(points.last(), points.first()).length();
    }
    return total;
}

double measureArea(const QVector<QPointF>& points) {
    if (points.size() < 3) {
        return 0.0;
    }
    double twice = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF& a = points.at(i);
        const QPointF& b = points.at((i + 1) % points.size());
        twice += a.x() * b.y() - b.x() * a.y();
    }
    return std::fabs(twice) / 2.0;
}

QString formatMeasure(MeasureMode mode, const QVector<QPointF>& points, double scale) {
    const double factor = (scale > 0.0) ? scale : 1.0;
    switch (mode) {
        case MeasureMode::Distance:
            if (points.size() < 2) {
                return {};
            }
            return QStringLiteral("%1 pt").arg(measureDistance(points.at(0), points.at(1)) * factor,
                                               0, 'f', 1);
        case MeasureMode::Perimeter:
            if (points.size() < 2) {
                return {};
            }
            return QStringLiteral("%1 pt").arg(measurePerimeter(points) * factor, 0, 'f', 1);
        case MeasureMode::Area:
            if (points.size() < 3) {
                return {};
            }
            // 面积按比例平方缩放：长度放大两倍，面积就是四倍。
            return QStringLiteral("%1 pt²").arg(measureArea(points) * factor * factor, 0, 'f', 1);
    }
    return {};
}

}  // namespace aipdf::app
