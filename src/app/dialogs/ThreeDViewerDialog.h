#pragma once

#include <QDialog>
#include <QImage>
#include <QPoint>

#include "pdf/annot/RichMedia3d.h"
#include "pdf/annot/ThreeDModel.h"

class QLabel;
class QPushButton;

namespace aipdf::app {

// 渲染结果的画布：显示位图，并把拖动 / 滚轮翻译成轨道相机的变化。
//
// 为什么交互放在这一层、渲染放在 pdf 层：pdf::threeDRenderModel 是纯函数
// （给相机参数出图），拖动只是改了两个数字。这样「转了 90° 画面变了没有」
// 在领域层就能单测，界面只负责把鼠标事件变成数字。
class ThreeDViewerCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit ThreeDViewerCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    QSize sizeHint() const override;

signals:
    // 拖动带来的角度增量（度）。
    void orbitChanged(double dAzimuth, double dElevation);
    // 滚轮带来的缩放倍率。
    void zoomChanged(double factor);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QImage image_;
    QPoint lastPos_;
    bool dragging_ = false;
};

// 3D 模型查看器：把文档里的 3D 注解渲染出来，可以拖动旋转、滚轮缩放。
//
// 这个对话框存在的意义是**把话说明白**：画面到底是本程序解出来的 3D 网格，
// 还是退回来的海报 / SVG 表现层。标题那一行永远写清楚是哪一种——
// 把海报当模型转，是最容易发生的谎报。
class ThreeDViewerDialog final : public QDialog {
    Q_OBJECT

public:
    // info 是读回来的 3D 注解；poster 是它的降级海报（读不到就传空的）。
    ThreeDViewerDialog(const pdf::ThreeDAnnotationInfo& info, const pdf::ThreeDPosterImage& poster,
                       QWidget* parent = nullptr);

private:
    void buildUi();
    void rerender();

    pdf::ThreeDAnnotationInfo info_;
    pdf::ThreeDPosterImage poster_;
    pdf::ThreeDOrbitCamera camera_;
    pdf::ThreeDRenderOptions options_;

    ThreeDViewerCanvas* canvas_ = nullptr;
    QLabel* sourceLabel_ = nullptr;
    QLabel* detailLabel_ = nullptr;
    QPushButton* resetButton_ = nullptr;
};

}  // namespace aipdf::app
