#pragma once

#include <QPointF>
#include <QVector>

namespace aipdf::app {

// 测量的三种口径。放进纯函数是为了能单测：面积算错在界面上看不出来，
// 只有数字对不上时才会被用户发现。
enum class MeasureMode {
    Distance,   // 两点距离
    Perimeter,  // 折线总长
    Area,       // 多边形面积（鞋带公式）
};

// 距离（两点）。
double measureDistance(const QPointF& from, const QPointF& to);

// 折线总长（相邻点距离之和）。
double measurePerimeter(const QVector<QPointF>& points);

// 多边形面积（绝对值，单位是平方点）。
double measureArea(const QVector<QPointF>& points);

// 按模式计算并格式化，单位后缀由调用方给（默认 pt）。
QString formatMeasure(MeasureMode mode, const QVector<QPointF>& points, double scale = 1.0);

}  // namespace aipdf::app
