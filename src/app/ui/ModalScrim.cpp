#include "app/ui/ModalScrim.h"

#include <QDialog>
#include <QEvent>
#include <QPaintEvent>
#include <QPainter>

namespace aipdf::app {

namespace {

// 蒙层的颜色：接近黑的冷色，透明度约 47%。
//
// 深色主题下同样的黑会更沉一些，这是对的：
// 暗色界面本来已经暗，再压一层反而更聚焦于弹窗。
constexpr int kScrimAlpha = 120;

}  // namespace

ModalScrim::ModalScrim(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("modalScrim"));
    // 蒙层只负责压暗，不负责接收输入：模态弹窗已经把父窗口的输入挡住了，
    // 蒙层再吃事件只会让界面看起来像卡住。
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // 自己的底色由 paintEvent 画，不让样式表再画一层。
    setAttribute(Qt::WA_NoSystemBackground);

    if (parent != nullptr) {
        // 主窗口会因为停靠面板的显隐、标签页切换而重新布局，
        // 每次这类变化都要把蒙层重新铺满、重新置顶。
        parent->installEventFilter(this);
    }
    syncToParent();
}

void ModalScrim::syncToParent() {
    QWidget* parent = parentWidget();
    if (parent == nullptr) {
        return;
    }
    setGeometry(parent->rect());
    raise();
}

void ModalScrim::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(event->rect(), QColor(8, 10, 14, kScrimAlpha));
}

bool ModalScrim::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event != nullptr) {
        switch (event->type()) {
            case QEvent::Resize:
            case QEvent::Move:
            case QEvent::Show:
            case QEvent::LayoutRequest:
            case QEvent::ParentChange:
                syncToParent();
                break;
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// 带蒙层显示模态对话框。
//
// 返回值与 QDialog::exec() 一致。
//
// 顺序有讲究，不能颠倒：
// 1. 蒙层先 show，再 exec 弹窗。模态弹窗会阻塞父窗口的事件循环，
//    之后再 show 的蒙层可能一帧都画不出来（父窗口不再处理绘制事件）。
// 2. exec 返回后再收起蒙层：这时弹窗已经关闭，父窗口恢复交互。
int execWithScrim(QDialog& dialog, QWidget* parent) {
    QWidget* window = parent != nullptr ? parent->window() : nullptr;
    if (window == nullptr) {
        return dialog.exec();
    }
    auto* scrim = new ModalScrim(window);
    scrim->show();
    const int result = dialog.exec();
    scrim->hide();
    scrim->deleteLater();
    return result;
}

}  // namespace aipdf::app
