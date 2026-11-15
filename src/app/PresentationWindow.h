#pragma once

#include <QImage>
#include <QWidget>

#include "pdf/PdfiumAdapter.h"

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

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    // 翻页。越界时不动（放映时最烦的就是最后一页按右键跳回第一页）。
    void step(int delta);
    void renderCurrent();

    pdf::PdfDocument document_;
    int pageCount_ = 0;
    int page_ = 0;
    QImage image_;
};

}  // namespace aipdf::app
