#pragma once

#include <QAbstractScrollArea>
#include <QVector>

#include "app/PageLayout.h"
#include "app/TileCache.h"

namespace aipdf::app {

class RenderThread;

// 连续页面画布：把所有页面画在一张虚拟的长文档上，按视口裁剪。
//
// 用 QAbstractScrollArea 而不是「一个大 QWidget 放进 QScrollArea」：
// 500 页文档的高度会到几十万像素，超大控件在不同平台上会被截断。
// 这里自己管滚动条范围，只画视口内的那几页。
class PageCanvas : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit PageCanvas(QWidget* parent = nullptr);

    void setLayout(const PageLayout& layout);
    const PageLayout& layout() const;

    void setTileCache(TileCache* cache);
    void setRenderThread(RenderThread* thread);

    // 当前视口顶页（从 1 开始，供界面显示）。
    int currentPage() const;

    // 视口内可见的页区间（含预取邻页）。
    void visiblePageRange(int* first, int* last) const;

public slots:
    // 滚到指定页（从 0 开始）。
    void goToPage(int index);

signals:
    void currentPageChanged(int index);
    void renderRequested(int firstPage, int lastPage);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    PageLayout layout_;
    TileCache* cache_ = nullptr;
    RenderThread* renderThread_ = nullptr;
    int currentPage_ = 0;

    void updateScrollRange();
    void requestVisiblePages();
    double pageLeft(int index) const;
};

}  // namespace aipdf::app
