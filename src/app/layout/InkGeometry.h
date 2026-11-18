#pragma once

#include <QPointF>
#include <QVector>

namespace aipdf::app {

// 点到线段的最短距离。橡皮命中判定要用它，单独拿出来是为了能脱离画布测。
double distanceToSegment(const QPointF& point, const QPointF& a, const QPointF& b);

// 一笔（一串点）是否落在以 center 为心、radius 为半径的圆内。
bool strokeHitsCircle(const QVector<QPointF>& stroke, const QPointF& center, double radius);

}  // namespace aipdf::app
