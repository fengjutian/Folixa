#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace aipdf::app {

// PDF 用户空间（左下原点）↔ 显示空间（左上原点）的变换。
//
// 显示空间是「页面渲染出来之后」使用的坐标系：左上角为原点，x 向右、y 向下，
// 单位是点（1/72 英寸），缩放由外部乘上去。
//
// 渲染时 PDFium 收到的总旋转角是「页面自带 /Rotate + 用户额外旋转」，
// 这个类用同一个角度做变换，保证文本层与渲染图像严格对齐——
// 否则旋转页上的选区会整体错位。
class PageTransform {
public:
    // 未旋转的页面：MediaBox 左下角在用户空间里的位置，以及宽高（点）。
    void setPage(double mediaLeft, double mediaBottom, double width, double height);

    // 总旋转角（0/90/180/270），其它值按 90 度取整。
    void setRotationDegrees(int degrees);

    int rotationDegrees() const;

    // 显示尺寸（点）：旋转 90°/270° 时宽高互换。
    QSizeF displaySize() const;

    // 用户空间点 → 显示空间点。
    QPointF toDisplay(const QPointF& userSpace) const;

    // 显示空间点 → 用户空间点（toDisplay 的逆变换）。
    // 链接命中判定需要：链接矩形在用户空间，而鼠标位置在显示空间。
    QPointF toUserSpace(const QPointF& displaySpace) const;

    // 用户空间矩形 → 显示空间矩形（变换后取包围盒）。
    QRectF toDisplay(const QRectF& userSpace) const;

private:
    double mediaLeft_ = 0.0;
    double mediaBottom_ = 0.0;
    double width_ = 0.0;
    double height_ = 0.0;
    int rotation_ = 0;
};

}  // namespace aipdf::app
