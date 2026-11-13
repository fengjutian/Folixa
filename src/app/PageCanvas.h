#pragma once

#include <QAbstractScrollArea>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include "app/PageLayout.h"
#include "app/TileCache.h"

class QMouseEvent;
class QPainter;

namespace aipdf::app {

class RenderThread;
class TextController;
class NavigationController;
class AnnotationController;

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

    // 绑定文本控制器：鼠标选择与高亮绘制都靠它。
    // 没有文本层时（扫描件）鼠标拖选不会有任何反应。
    void setTextController(TextController* controller);
    TextController* textController() const;

    // 绑定导航控制器：链接热区判定与点击跳转都靠它。
    void setNavigationController(NavigationController* controller);
    NavigationController* navigationController() const;

    // 绑定批注控制器：批注绘制与画布上的批注工具都靠它。
    void setAnnotationController(AnnotationController* controller);
    AnnotationController* annotationController() const;

    // 高亮某页的一块（显示空间矩形）：索引检索结果定位用。
    //
    // 传 pageIndex = -1 或空列表就是取消高亮。
    void setIndexHighlight(int pageIndex, const QVector<QRectF>& displayRects);

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
    // 链接被点击：内部跳转给出目标页（从 0 开始），外部链接给出地址。
    void linkActivated(int pageIndex, int targetPage, const QString& uri);
    // 便签工具：点在页面上，要放一个便签（坐标是 PDF user space）。
    void noteAnnotationRequested(int pageIndex, const QPointF& userSpacePoint);
    // 矩形工具：拖出一块（坐标是 PDF user space）。
    void squareAnnotationRequested(int pageIndex, const QRectF& userSpaceRect);
    // 墨迹工具：拖出一笔（坐标是 PDF user space）。
    void inkAnnotationRequested(int pageIndex, const QVector<QPointF>& userSpaceStroke);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    PageLayout layout_;
    TileCache* cache_ = nullptr;
    RenderThread* renderThread_ = nullptr;
    TextController* textController_ = nullptr;
    NavigationController* navigationController_ = nullptr;
    AnnotationController* annotationController_ = nullptr;
    // 索引结果定位的高亮：页序号 + 显示空间矩形。
    int indexHighlightPage_ = -1;
    QVector<QRectF> indexHighlightRects_;
    int currentPage_ = 0;
    // 上一次设置滚动范围时的内容高度与视口高度，用来判断范围是否真的变了。
    int lastContentHeight_ = -1;
    int lastViewportHeight_ = -1;
    // 正在拖选：记录锚点页与锚点字符（阅读顺序下标）。
    bool selecting_ = false;
    // 指针当前是否停在链接上：用来在状态翻转时记一笔轨迹。
    bool overLink_ = false;
    int anchorPage_ = -1;
    int anchorOrder_ = -1;
    // 正在用批注工具拖拽（矩形 / 墨迹）：坐标都在显示空间。
    bool drawingSquare_ = false;
    bool drawingInk_ = false;
    int drawPage_ = -1;
    QPointF drawAnchor_;
    QPointF drawCurrent_;
    QVector<QPointF> inkPoints_;

    void updateScrollRange();
    void requestVisiblePages();
    double pageLeft(int index) const;
    // 视口坐标 → 页面序号 + 页面显示空间坐标（点）。
    bool pagePointAt(const QPoint& viewportPos, int* pageIndex, QPointF* displayPoint) const;
    // 页面显示空间矩形 → 视口矩形。
    QRectF toViewportRect(const QRectF& displayRect, const QRectF& pageRect, double zoom) const;
    // 画搜索高亮与选区高亮。
    void paintHighlights(QPainter& painter, int pageIndex, const QRectF& pageRect, double zoom) const;
    // 画批注（按模型绘制：PDFium 不会渲染没有外观流的批注）。
    void paintAnnotations(QPainter& painter,
                          int pageIndex,
                          const QRectF& pageRect,
                          double zoom) const;
    // 正在拖的批注工具的预览。
    void paintAnnotationPreview(QPainter& painter,
                                const QRectF& pageRect,
                                double zoom) const;
    // 按指针位置更新光标：悬在链接上时给手型。
    void updateLinkCursor(const QPoint& viewportPos);
    // 显示空间点 → 用户空间点（批注的坐标一律用用户空间）。
    QPointF toUserSpacePoint(int pageIndex, const QPointF& displayPoint) const;
    // 批注工具激活时把光标换成十字，退出时恢复。
    void updateToolCursor();
};

}  // namespace aipdf::app
