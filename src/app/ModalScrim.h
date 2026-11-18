#pragma once

#include <QWidget>

class QDialog;
class QEvent;
class QPaintEvent;

namespace aipdf::app {

// 盖在主窗口上的半透明蒙层。
//
// 为什么需要它：Qt 的模态对话框只是「挡住」父窗口的输入，
// 父窗口本身还是一片明亮——用户的注意力不会自己落到弹窗上。
// 蒙层把弹窗之外的内容压暗，焦点自然就过去了。
//
// 实现要点：
// - 蒙层是主窗口的子控件，铺满窗口的整个客户区（含菜单栏、Ribbon、侧栏）。
// - 弹窗本身是独立的原生窗口，永远盖在蒙层之上，视觉顺序天然正确。
// - 蒙层不吃鼠标事件：模态弹窗已经挡住了父窗口的输入，
//   再挡一层只会让「点哪里都没反应」显得像卡死。
class ModalScrim : public QWidget {
    Q_OBJECT

public:
    explicit ModalScrim(QWidget* parent);

protected:
    void paintEvent(QPaintEvent* event) override;

    // 盯住父窗口的尺寸与布局变化：变化时把蒙层重新铺满并置顶。
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // 铺满父窗口并置顶。
    //
    // 主窗口里有停靠面板（侧栏、AI 面板），它们和中央部件是同一层兄弟；
    // 蒙层必须比它们都高，否则会从面板缝里露出来。
    void syncToParent();
};

// 带蒙层显示模态对话框；返回值与 QDialog::exec() 一致。
int execWithScrim(QDialog& dialog, QWidget* parent);

}  // namespace aipdf::app
