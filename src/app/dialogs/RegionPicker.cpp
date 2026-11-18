#include "app/RegionPicker.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

#include "app/ScreenCapture.h"

namespace aipdf::app {

namespace {

// 选区外面压暗的程度：暗到能看清边界，又不至于看不清内容。
constexpr int kDimAlpha = 140;

}  // namespace

RegionPicker::RegionPicker(const QImage& screenShot, QWidget* parent)
    : QDialog(parent), shot_(screenShot) {
    setWindowTitle(tr("选择截图区域"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    // 选区层自己画提示，不依赖系统窗口标题栏。
    setAttribute(Qt::WA_DeleteOnClose, false);
}

QRect RegionPicker::selectedRegion() const {
    return region_;
}

bool RegionPicker::hasSelection() const {
    return isValidRegion(region_);
}

void RegionPicker::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QDialog::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    anchor_ = event->pos();
    current_ = anchor_;
    region_ = QRect();
    update();
}

void RegionPicker::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) {
        QDialog::mouseMoveEvent(event);
        return;
    }
    current_ = event->pos();
    region_ = dragRegion(anchor_, current_);
    update();
}

void RegionPicker::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !dragging_) {
        QDialog::mouseReleaseEvent(event);
        return;
    }
    dragging_ = false;
    current_ = event->pos();
    region_ = dragRegion(anchor_, current_);
    update();
    // 没框出东西就当取消：空选区没什么可截的。
    if (hasSelection()) {
        accept();
    } else {
        reject();
    }
}

void RegionPicker::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        region_ = QRect();
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}

void RegionPicker::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    if (!shot_.isNull()) {
        painter.drawImage(0, 0, shot_);
    }
    // 压暗整块屏幕，再把选区那块原样画回来。
    painter.fillRect(rect(), QColor(0, 0, 0, kDimAlpha));
    if (hasSelection()) {
        painter.drawImage(region_, shot_, region_);
        painter.setPen(QColor(60, 160, 255));
        painter.drawRect(region_.adjusted(0, 0, -1, -1));
    }
    // 提示文字：告诉用户怎么退出。
    painter.setPen(QColor(255, 255, 255));
    const QString hint = tr("拖动鼠标框选要截图的区域，松开鼠标确认，Esc 取消");
    painter.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignTop | Qt::AlignLeft, hint);
}

// 抓一整块屏幕并让用户框选。
bool pickScreenRegion(QWidget* parent, QRect* region, QImage* screenshot) {
    if (region == nullptr || screenshot == nullptr) {
        return false;
    }
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return false;
    }
    const QRect geometry = screen->geometry();
    // grabWindow 给的是 QPixmap，转成 QImage 才能按区域裁。
    const QImage shot = screen->grabWindow(0).toImage();
    if (shot.isNull()) {
        return false;
    }

    RegionPicker picker(shot, parent);
    picker.setGeometry(geometry);
    if (picker.exec() != QDialog::Accepted) {
        return false;
    }
    const QRect picked = picker.selectedRegion();
    if (!isValidRegion(picked)) {
        return false;
    }
    // 选区层铺在 primaryScreen 上：控件坐标要加上屏幕原点才是屏幕坐标。
    *region = picked.translated(geometry.topLeft());
    *screenshot = shot;
    return true;
}

}  // namespace aipdf::app
