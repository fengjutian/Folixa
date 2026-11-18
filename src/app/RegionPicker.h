#pragma once

#include <QDialog>
#include <QImage>
#include <QPoint>
#include <QRect>

namespace aipdf::app {

// 区域框选层：铺满一块屏幕，显示这块屏幕的快照，让用户拖出一个矩形。
//
// 为什么是"先抓屏幕再显示快照"而不是边拖边实时抓：
//   * 实时抓会把选区自己拍进去（框到哪里哪里就有一块白的）；
//   * 每帧抓一次要几十毫秒，拖动时画面会卡。
//
// 为什么是 QDialog：Esc 取消、模态执行这些正好是 QDialog 自带的。
class RegionPicker final : public QDialog {
    Q_OBJECT
public:
    explicit RegionPicker(const QImage& screenShot, QWidget* parent = nullptr);

    // 用户框出来的区域（**屏幕坐标**）；取消时是空矩形。
    QRect selectedRegion() const;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    // 选区是否已经拖出来了（拖动中或已松开）。
    bool hasSelection() const;

    QImage shot_;
    QPoint anchor_;
    QPoint current_;
    bool dragging_ = false;
    QRect region_;
};

// 抓一整块屏幕并让用户框选。
//
// 返回 false 表示用户取消（Esc）：此时 region 与 screenshot 不作数。
// 选区是**屏幕坐标**，screenshot 是被抓的那块屏幕的整图。
bool pickScreenRegion(QWidget* parent, QRect* region, QImage* screenshot);

}  // namespace aipdf::app
