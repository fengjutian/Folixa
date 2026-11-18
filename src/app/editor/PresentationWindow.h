#pragma once

#include "i18n/Translate.h"

#include <QImage>
#include <QWidget>

#include "pdf/kernel/PdfiumAdapter.h"

namespace aipdf::app {

// 放映窗口：全屏、一页一张、方向键翻页。
//
// 为什么单独一个窗口而不是把主界面全屏：放映时要藏掉工具栏、侧栏、状态栏，
// 还要吃下 Esc/方向键而不让它们漏给主窗口。新开一个顶置全屏窗口比在主窗口里
// 逐个隐藏控件干净得多，退出时也不会留下"忘了恢复"的状态。
class PresentationWindow final : public QWidget {
    Q_OBJECT

public:
    explicit PresentationWindow(pdf::PdfDocument document, QWidget* parent = nullptr);

    // 进入全屏并显示第一页；调用方不需要再 show()。
    void start();

    int currentPage() const { return page_; }

    // 关闭时发出，带上最后停留的页码：验收脚本靠它确认「退出前真的翻过页」。
    // 参数是 1 基页码，写 trace 时不需要再换算。
    int lastShownPage() const { return page_ + (pageCount_ > 0 ? 1 : 0); }
    int pageCount() const { return pageCount_; }

    // 循环放映：翻过最后一页后回到第一页（往前翻过第一页则回到最后一页）。
    //
    // 默认关闭——关着时行为与从前逐字一致（到头就停住），
    // 验收脚本 scripts/verify-presentation.ps1 依赖这个行为。
    void setLoop(bool enabled);
    bool loop() const { return loop_; }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    // 翻页。越界时不动（放映时最烦的就是最后一页按右键跳回第一页）。
    void step(int delta);
    void renderCurrent();
    // 切换语言后重译窗口标题。
    void retranslateUi();
    // 窗口标题：循环打开时带「（循环）」，让人一眼看出放映不会停。
    //
    // 名字不叫 windowTitle：QWidget 已经有同名成员函数，
    // 换个名字免得把基类那个「当前窗口标题」盖住。
    QString presentationTitle() const;

    pdf::PdfDocument document_;
    int pageCount_ = 0;
    int page_ = 0;
    QImage image_;
    // 循环放映。默认关闭，保持「到头就停」的老行为。
    bool loop_ = false;
    // 一次设定、长期显示的窗口标题：切换语言时就地重译。
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
