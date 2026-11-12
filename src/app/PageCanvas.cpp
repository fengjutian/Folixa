#include "app/PageCanvas.h"

#include "app/RenderThread.h"

#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>

#include <algorithm>

namespace aipdf::app {

namespace {
// 页与页之间留一点空隙，页面本身画一圈浅色边框。
constexpr double kPageSpacing = 12.0;
constexpr double kPageMargin = 16.0;
constexpr int kPrefetchPages = 1;
}  // namespace

PageCanvas::PageCanvas(QWidget* parent) : QAbstractScrollArea(parent) {
    setBackgroundRole(QPalette::Mid);
    viewport()->setAutoFillBackground(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
}

void PageCanvas::setLayout(const PageLayout& layout) {
    layout_ = layout;
    updateScrollRange();
    viewport()->update();
}

const PageLayout& PageCanvas::layout() const {
    return layout_;
}

void PageCanvas::setTileCache(TileCache* cache) {
    cache_ = cache;
}

void PageCanvas::setRenderThread(RenderThread* thread) {
    renderThread_ = thread;
}

int PageCanvas::currentPage() const {
    return currentPage_;
}

void PageCanvas::visiblePageRange(int* first, int* last) const {
    if (layout_.isEmpty()) {
        if (first != nullptr) {
            *first = 0;
        }
        if (last != nullptr) {
            *last = -1;
        }
        return;
    }

    const double top = verticalScrollBar()->value();
    const double bottom = top + viewport()->height();

    int firstIndex = layout_.pageAt(top);
    if (firstIndex < 0) {
        firstIndex = 0;
    }
    int lastIndex = firstIndex;
    while (lastIndex + 1 < layout_.pageCount() &&
           layout_.pageTop(lastIndex + 1) < bottom) {
        ++lastIndex;
    }

    // 预取邻页，滚动时内容已经在缓存里。
    firstIndex = std::max(0, firstIndex - kPrefetchPages);
    lastIndex = std::min(layout_.pageCount() - 1, lastIndex + kPrefetchPages);

    if (first != nullptr) {
        *first = firstIndex;
    }
    if (last != nullptr) {
        *last = lastIndex;
    }
}

void PageCanvas::goToPage(int index) {
    if (layout_.isEmpty()) {
        return;
    }
    const int clamped = std::clamp(index, 0, layout_.pageCount() - 1);
    verticalScrollBar()->setValue(static_cast<int>(layout_.pageTop(clamped)));
}

void PageCanvas::paintEvent(QPaintEvent* event) {
    QPainter painter(viewport());
    painter.fillRect(event->rect(), palette().color(QPalette::Mid));

    if (layout_.isEmpty()) {
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(event->rect(), Qt::AlignCenter, QStringLiteral("未打开文档"));
        return;
    }

    const double devicePixelRatio = this->devicePixelRatioF();
    const double scrollTop = verticalScrollBar()->value();
    const int firstPage = std::max(0, layout_.pageAt(scrollTop));
    const int lastPage = std::min(layout_.pageCount() - 1, layout_.pageAt(scrollTop + viewport()->height()));

    for (int index = firstPage; index <= lastPage; ++index) {
        const QSizeF size = layout_.pageSize(index);
        const double top = layout_.pageTop(index) - scrollTop;
        const double left = pageLeft(index);
        const QRectF pageRect(left, top, size.width(), size.height());

        // 页面底板。
        painter.fillRect(pageRect, Qt::white);
        painter.setPen(QPen(palette().color(QPalette::Mid).darker(120), 1.0));
        painter.drawRect(pageRect.adjusted(0.0, 0.0, -1.0, -1.0));

        if (cache_ == nullptr) {
            continue;
        }
        // 缓存键用的是渲染倍率（缩放 × 设备像素比）和用户旋转，
        // 和渲染线程保持一致。
        const qint64 zoomKey =
            TileCache::zoomToKey(layout_.zoom() * devicePixelRatio);
        const TileCache::Key key{index, zoomKey, layout_.userRotation() / 90};
        QImage tile = cache_->lookup(key);
        if (tile.isNull()) {
            // 还没渲染出来：画页码占位，别让用户以为是空白页。
            painter.setPen(palette().color(QPalette::Mid));
            painter.drawText(pageRect, Qt::AlignCenter, QStringLiteral("%1").arg(index + 1));
            continue;
        }
        tile.setDevicePixelRatio(devicePixelRatio);
        painter.drawImage(QPointF(left, top), tile);
    }
}

void PageCanvas::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollRange();
    requestVisiblePages();
}

void PageCanvas::scrollContentsBy(int dx, int dy) {
    QAbstractScrollArea::scrollContentsBy(dx, dy);
    updateScrollRange();
    requestVisiblePages();
}

void PageCanvas::updateScrollRange() {
    QScrollBar* bar = verticalScrollBar();
    const int viewportHeight = std::max(1, viewport()->height());
    const int contentHeight = static_cast<int>(std::ceil(layout_.totalHeight())) + kPageMargin;

    // 只有内容高度或视口高度真的变了才重设范围：
    // 每次滚动都重设会把当前滚动位置钳回去（范围一变，Qt 就会夹紧取值）。
    if (contentHeight != lastContentHeight_ || viewportHeight != lastViewportHeight_) {
        lastContentHeight_ = contentHeight;
        lastViewportHeight_ = viewportHeight;
        bar->setRange(0, std::max(0, contentHeight - viewportHeight));
        bar->setPageStep(viewportHeight);
        bar->setSingleStep(std::max(1, viewportHeight / 20));
    }

    const int page = layout_.isEmpty() ? 0 : std::max(0, layout_.pageAt(bar->value()));
    if (page != currentPage_) {
        currentPage_ = page;
        emit currentPageChanged(currentPage_);
    }
}

void PageCanvas::requestVisiblePages() {
    int first = 0;
    int last = -1;
    visiblePageRange(&first, &last);
    if (last < first) {
        return;
    }
    emit renderRequested(first, last);
}

double PageCanvas::pageLeft(int index) const {
    Q_UNUSED(index);
    const QSizeF size = layout_.pageSize(index);
    const double available = viewport()->width() - kPageMargin * 2.0;
    return std::max(kPageMargin, (available - size.width()) / 2.0 + kPageMargin);
}

}  // namespace aipdf::app
