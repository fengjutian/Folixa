#include "app/PageCanvas.h"

#include "app/RenderThread.h"
#include "app/TextController.h"
#include "app/TextLayout.h"
#include "app/NavigationController.h"
#include "app/AnnotationController.h"
#include "pdf/Annotation.h"

#include <QDateTime>
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QScrollBar>

#include <algorithm>

namespace aipdf::app {

namespace {
// 页与页之间留一点空隙，页面本身画一圈浅色边框。
constexpr double kPageSpacing = 20.0;
constexpr double kPageMargin = 24.0;
constexpr int kPrefetchPages = 1;

// 验收探针用的轨迹文件：选择与复制的过程要能被脚本看到。
void trace(const QString& message) {
    const QString path = qEnvironmentVariable("AIPDF_TRACE_FILE");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " + message + "\n").toUtf8());
    }
}

// 便签图标：一块带折角的便签纸。
void paintNoteIcon(QPainter& painter, const QRectF& rect, const QColor& color) {
    painter.setPen(QPen(color.darker(150), 1.0));
    painter.setBrush(color);
    painter.drawRect(rect);

    // 右下角的折角。
    QPolygonF corner;
    corner << rect.bottomRight()
           << QPointF(rect.right() - rect.width() * 0.45, rect.bottom())
           << QPointF(rect.right(), rect.bottom() - rect.height() * 0.45);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color.darker(170));
    painter.drawPolygon(corner);
}
}  // namespace

PageCanvas::PageCanvas(QWidget* parent) : QAbstractScrollArea(parent) {
    // objectName 会进 UI Automation 的 AutomationId，
    // 验收脚本靠它定位画布视口、换算屏幕坐标做拖选。
    setObjectName(QStringLiteral("pdfCanvas"));
    QPalette canvasPalette = palette();
    canvasPalette.setColor(QPalette::Mid, QColor(QStringLiteral("#D9DEE7")));
    canvasPalette.setColor(QPalette::Text, QColor(QStringLiteral("#465267")));
    setPalette(canvasPalette);
    setBackgroundRole(QPalette::Mid);
    setFrameShape(QFrame::NoFrame);
    viewport()->setAutoFillBackground(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    // 画布要能拿到焦点：点一下之后 Ctrl+C 才能复制选中的文字。
    viewport()->setFocusPolicy(Qt::StrongFocus);
    // 鼠标跟踪：没有它就收不到「只是移过去」的移动事件，
    // 悬停在链接上时手型光标就不会出现。
    viewport()->setMouseTracking(true);
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

void PageCanvas::setTextController(TextController* controller) {
    textController_ = controller;
}

TextController* PageCanvas::textController() const {
    return textController_;
}

void PageCanvas::setNavigationController(NavigationController* controller) {
    navigationController_ = controller;
    viewport()->setCursor(Qt::ArrowCursor);
}

NavigationController* PageCanvas::navigationController() const {
    return navigationController_;
}

void PageCanvas::setAnnotationController(AnnotationController* controller) {
    annotationController_ = controller;
    if (controller != nullptr) {
        connect(controller, &AnnotationController::toolChanged,
                this, &PageCanvas::updateToolCursor);
    }
    updateToolCursor();
}

AnnotationController* PageCanvas::annotationController() const {
    return annotationController_;
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

        // 页面底板和克制的投影，帮助纸张从工作区背景中分离。
        painter.fillRect(pageRect.translated(0.0, 3.0), QColor(32, 42, 58, 28));
        painter.fillRect(pageRect, Qt::white);
        painter.setPen(QPen(QColor(QStringLiteral("#CBD2DE")), 1.0));
        painter.drawRect(pageRect.adjusted(0.0, 0.0, -1.0, -1.0));

        if (cache_ != nullptr) {
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
            } else {
                tile.setDevicePixelRatio(devicePixelRatio);
                painter.drawImage(QPointF(left, top), tile);
            }
        }

        // 文本高亮画在页面图像之上。
        paintHighlights(painter, index, pageRect, layout_.zoom());
        // 批注画在最上层。
        paintAnnotations(painter, index, pageRect, layout_.zoom());
        // 正在拖的批注工具的预览。
        if (index == drawPage_) {
            paintAnnotationPreview(painter, pageRect, layout_.zoom());
        }
    }
}

void PageCanvas::paintAnnotations(QPainter& painter,
                                  int pageIndex,
                                  const QRectF& pageRect,
                                  double zoom) const {
    if (annotationController_ == nullptr) {
        return;
    }
    const QVector<pdf::Annotation> annotations =
        annotationController_->annotationsForPage(pageIndex);
    if (annotations.isEmpty()) {
        return;
    }

    PageTransform transform;
    if (!annotationController_->transformForPage(pageIndex, &transform)) {
        return;
    }

    for (const pdf::Annotation& annotation : annotations) {
        switch (annotation.type) {
            case pdf::AnnotationType::Highlight: {
                // 高亮：逐行铺一层半透明底色。
                QColor fill = annotation.color;
                fill.setAlpha(105);
                painter.setPen(Qt::NoPen);
                painter.setBrush(fill);
                for (const QRectF& quad : annotation.quads) {
                    painter.drawRect(toViewportRect(transform.toDisplay(quad), pageRect, zoom));
                }
                break;
            }
            case pdf::AnnotationType::Underline: {
                // 下划线：贴着每行的下沿画一条线。
                painter.setPen(QPen(annotation.color, 1.6));
                painter.setBrush(Qt::NoBrush);
                for (const QRectF& quad : annotation.quads) {
                    const QRectF rect = toViewportRect(transform.toDisplay(quad), pageRect, zoom);
                    painter.drawLine(QPointF(rect.left(), rect.bottom()),
                                     QPointF(rect.right(), rect.bottom()));
                }
                break;
            }
            case pdf::AnnotationType::StrikeOut: {
                // 删除线：画在每行的中间。
                painter.setPen(QPen(annotation.color, 1.6));
                painter.setBrush(Qt::NoBrush);
                for (const QRectF& quad : annotation.quads) {
                    const QRectF rect = toViewportRect(transform.toDisplay(quad), pageRect, zoom);
                    const double y = rect.center().y();
                    painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
                }
                break;
            }
            case pdf::AnnotationType::Note: {
                const QRectF rect = toViewportRect(transform.toDisplay(annotation.rect), pageRect, zoom);
                paintNoteIcon(painter, rect, annotation.color);
                break;
            }
            case pdf::AnnotationType::Square: {
                const QRectF rect = toViewportRect(transform.toDisplay(annotation.rect), pageRect, zoom);
                QColor fill = annotation.color;
                fill.setAlpha(38);
                painter.setPen(QPen(annotation.color, 1.6));
                painter.setBrush(fill);
                painter.drawRect(rect);
                break;
            }
            case pdf::AnnotationType::Ink: {
                painter.setPen(QPen(annotation.color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                for (const QVector<QPointF>& stroke : annotation.strokes) {
                    if (stroke.size() < 2) {
                        continue;
                    }
                    QPolygonF polyline;
                    for (const QPointF& point : stroke) {
                        const QPointF display = transform.toDisplay(point);
                        polyline << QPointF(pageRect.left() + display.x() * zoom,
                                            pageRect.top() + display.y() * zoom);
                    }
                    painter.drawPolyline(polyline);
                }
                break;
            }
        }
    }
}

void PageCanvas::paintAnnotationPreview(QPainter& painter,
                                        const QRectF& pageRect,
                                        double zoom) const {
    if (annotationController_ == nullptr || drawPage_ < 0) {
        return;
    }
    PageTransform transform;
    if (!annotationController_->transformForPage(drawPage_, &transform)) {
        return;
    }
    painter.setPen(QPen(QColor(64, 124, 235), 1.4, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);

    if (drawingSquare_) {
        const QPointF a = transform.toUserSpace(drawAnchor_);
        const QPointF b = transform.toUserSpace(drawCurrent_);
        const QRectF rect = toViewportRect(transform.toDisplay(QRectF(a, b).normalized()),
                                          pageRect,
                                          zoom);
        painter.drawRect(rect);
        return;
    }
    if (drawingInk_ && inkPoints_.size() >= 2) {
        QPolygonF polyline;
        for (const QPointF& point : inkPoints_) {
            const QPointF display = transform.toDisplay(transform.toUserSpace(point));
            polyline << QPointF(pageRect.left() + display.x() * zoom,
                                pageRect.top() + display.y() * zoom);
        }
        painter.drawPolyline(polyline);
    }
}

QPointF PageCanvas::toUserSpacePoint(int pageIndex, const QPointF& displayPoint) const {
    if (annotationController_ == nullptr) {
        return displayPoint;
    }
    PageTransform transform;
    if (!annotationController_->transformForPage(pageIndex, &transform)) {
        return displayPoint;
    }
    return transform.toUserSpace(displayPoint);
}

void PageCanvas::setIndexHighlight(int pageIndex, const QVector<QRectF>& displayRects) {
    indexHighlightPage_ = pageIndex;
    indexHighlightRects_ = displayRects;
    viewport()->update();
}

void PageCanvas::paintHighlights(QPainter& painter,
                                 int pageIndex,
                                 const QRectF& pageRect,
                                 double zoom) const {
    // 索引高亮与文本层无关：扫描件没有文本层，但索引里照样可能有块
    // （比如从别处拿来的坐标），所以先画它。
    if (indexHighlightPage_ == pageIndex && !indexHighlightRects_.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(46, 160, 67, 110));
        for (const QRectF& rect : indexHighlightRects_) {
            painter.drawRect(toViewportRect(rect, pageRect, zoom));
        }
    }

    if (textController_ == nullptr) {
        return;
    }

    const QVector<QRectF> searchRects = textController_->searchHighlightRects(pageIndex);
    if (!searchRects.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 198, 51, 120));
        for (const QRectF& rect : searchRects) {
            painter.drawRect(toViewportRect(rect, pageRect, zoom));
        }
    }

    if (textController_->selectionPage() == pageIndex) {
        const QVector<QRectF> selection = textController_->selectionRects();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(64, 124, 235, 90));
        for (const QRectF& rect : selection) {
            painter.drawRect(toViewportRect(rect, pageRect, zoom));
        }
    }
}

QRectF PageCanvas::toViewportRect(const QRectF& displayRect,
                                   const QRectF& pageRect,
                                   double zoom) const {
    return QRectF(pageRect.left() + displayRect.left() * zoom,
                  pageRect.top() + displayRect.top() * zoom,
                  displayRect.width() * zoom,
                  displayRect.height() * zoom);
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

    const int page = layout_.isEmpty() ? 0 : std::max(0, layout_.dominantPage(bar->value(), viewportHeight));
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
    // 可见页要文本层：选中文字与搜索高亮都靠它。
    if (textController_ != nullptr) {
        for (int index = first; index <= last; ++index) {
            textController_->requestTextPage(index);
        }
    }
    // 可见页也要链接注释：悬停判定与点击跳转都靠它。
    if (navigationController_ != nullptr) {
        for (int index = first; index <= last; ++index) {
            navigationController_->requestPageLinks(index);
        }
    }
    emit renderRequested(first, last);
}

bool PageCanvas::pagePointAt(const QPoint& viewportPos,
                             int* pageIndex,
                             QPointF* displayPoint) const {
    if (layout_.isEmpty()) {
        return false;
    }
    const double zoom = layout_.zoom();
    if (zoom <= 0.0) {
        return false;
    }
    const double scrollTop = verticalScrollBar()->value();
    const int index = layout_.pageAt(scrollTop + viewportPos.y());
    if (index < 0 || index >= layout_.pageCount()) {
        return false;
    }
    if (pageIndex != nullptr) {
        *pageIndex = index;
    }
    if (displayPoint != nullptr) {
        const double left = pageLeft(index);
        const double top = layout_.pageTop(index) - scrollTop;
        *displayPoint = QPointF((viewportPos.x() - left) / zoom,
                                (viewportPos.y() - top) / zoom);
    }
    return true;
}

void PageCanvas::mousePressEvent(QMouseEvent* event) {
    QAbstractScrollArea::mousePressEvent(event);
    if (event->button() != Qt::LeftButton) {
        return;
    }
    viewport()->setFocus();

    int pageIndex = -1;
    QPointF displayPoint;
    if (!pagePointAt(event->pos(), &pageIndex, &displayPoint)) {
        trace(QStringLiteral("press ignored pos=%1,%2").arg(event->pos().x()).arg(event->pos().y()));
        return;
    }

    // 批注工具优先：工具激活时不进文字拖选，也不触发链接。
    if (annotationController_ != nullptr) {
        const auto tool = annotationController_->tool();
        if (tool != AnnotationController::Tool::None) {
            selecting_ = false;
            if (tool == AnnotationController::Tool::Note) {
                const QPointF userPoint = toUserSpacePoint(pageIndex, displayPoint);
                trace(QStringLiteral("annotation tool note page=%1").arg(pageIndex));
                emit noteAnnotationRequested(pageIndex, userPoint);
                viewport()->update();
                return;
            }
            if (tool == AnnotationController::Tool::Square) {
                drawingSquare_ = true;
                drawPage_ = pageIndex;
                drawAnchor_ = displayPoint;
                drawCurrent_ = displayPoint;
                trace(QStringLiteral("annotation tool square page=%1").arg(pageIndex));
                viewport()->update();
                return;
            }
            if (tool == AnnotationController::Tool::Ink) {
                drawingInk_ = true;
                drawPage_ = pageIndex;
                inkPoints_.clear();
                inkPoints_.append(displayPoint);
                trace(QStringLiteral("annotation tool ink page=%1").arg(pageIndex));
                viewport()->update();
                return;
            }
        }
    }

    // 链接优先：点在链接热区上就激活链接，不进入文字拖选。
    if (navigationController_ != nullptr) {
        const int hit = navigationController_->hitLink(pageIndex, displayPoint);
        if (hit >= 0) {
            const QVector<pdf::PageLink>& links = navigationController_->linksForPage(pageIndex);
            const pdf::PageLink& link = links.at(hit);
            trace(QStringLiteral("link click page=%1 target=%2 uri=%3")
                      .arg(pageIndex)
                      .arg(link.pageIndex)
                      .arg(link.uri));
            selecting_ = false;
            emit linkActivated(pageIndex, link.pageIndex, link.uri);
            viewport()->update();
            return;
        }
    }

    if (textController_ == nullptr) {
        return;
    }
    const int order = textController_->hitAt(pageIndex, displayPoint);
    trace(QStringLiteral("press page=%1 point=%2,%3 order=%4")
              .arg(pageIndex)
              .arg(displayPoint.x())
              .arg(displayPoint.y())
              .arg(order));
    if (order < 0) {
        // 点的不是文字：清掉旧选区，不开始拖选。
        selecting_ = false;
        textController_->clearSelection();
        viewport()->update();
        return;
    }

    selecting_ = true;
    anchorPage_ = pageIndex;
    anchorOrder_ = order;
    textController_->setSelection(pageIndex, order, order + 1);
    trace(QStringLiteral("select start page=%1 order=%2").arg(pageIndex).arg(order));
    viewport()->update();
}

void PageCanvas::mouseMoveEvent(QMouseEvent* event) {
    QAbstractScrollArea::mouseMoveEvent(event);
    updateLinkCursor(event->pos());

    // 批注工具的拖拽中：只收本页的点。
    if (drawingSquare_ || drawingInk_) {
        int pageIndex = -1;
        QPointF displayPoint;
        if (pagePointAt(event->pos(), &pageIndex, &displayPoint) && pageIndex == drawPage_) {
            drawCurrent_ = displayPoint;
            if (drawingInk_) {
                inkPoints_.append(displayPoint);
            }
            viewport()->update();
        }
        return;
    }

    if (!selecting_ || textController_ == nullptr) {
        return;
    }
    int pageIndex = -1;
    QPointF displayPoint;
    if (!pagePointAt(event->pos(), &pageIndex, &displayPoint)) {
        return;
    }
    if (pageIndex != anchorPage_) {
        // 拖到别的页上：选区不跨页，到此为止。
        selecting_ = false;
        return;
    }
    const int order = textController_->hitAt(pageIndex, displayPoint);
    if (order < 0) {
        return;
    }
    const int start = std::min(anchorOrder_, order);
    const int end = std::max(anchorOrder_, order) + 1;
    textController_->setSelection(pageIndex, start, end);
    viewport()->update();
}

void PageCanvas::updateLinkCursor(const QPoint& viewportPos) {
    // 批注工具激活时，光标归工具管：链接判定不能把它改成手型或箭头。
    if (annotationController_ != nullptr &&
        annotationController_->tool() != AnnotationController::Tool::None) {
        viewport()->setCursor(Qt::CrossCursor);
        return;
    }
    if (navigationController_ == nullptr) {
        viewport()->setCursor(Qt::ArrowCursor);
        return;
    }
    int pageIndex = -1;
    QPointF displayPoint;
    if (!pagePointAt(viewportPos, &pageIndex, &displayPoint)) {
        viewport()->setCursor(Qt::ArrowCursor);
        return;
    }
    const int hit = navigationController_->hitLink(pageIndex, displayPoint);
    const bool overLink = hit >= 0;
    // 只在状态翻转时记一笔：鼠标移动事件很密集，每次都记会把轨迹刷爆。
    if (overLink != overLink_) {
        overLink_ = overLink;
        trace(overLink ? QStringLiteral("cursor link") : QStringLiteral("cursor default"));
    }
    viewport()->setCursor(overLink ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void PageCanvas::mouseReleaseEvent(QMouseEvent* event) {
    QAbstractScrollArea::mouseReleaseEvent(event);
    if (event->button() != Qt::LeftButton) {
        return;
    }

    // 批注工具：拖拽结束，生成批注。
    if (drawingSquare_ || drawingInk_) {
        const int pageIndex = drawPage_;
        if (drawingSquare_) {
            const QPointF a = toUserSpacePoint(pageIndex, drawAnchor_);
            const QPointF b = toUserSpacePoint(pageIndex, drawCurrent_);
            drawingSquare_ = false;
            drawPage_ = -1;
            viewport()->update();
            if (a != b) {
                emit squareAnnotationRequested(pageIndex, QRectF(a, b).normalized());
            }
            return;
        }
        QVector<QPointF> stroke;
        stroke.reserve(inkPoints_.size());
        for (const QPointF& point : inkPoints_) {
            stroke.append(toUserSpacePoint(pageIndex, point));
        }
        drawingInk_ = false;
        drawPage_ = -1;
        inkPoints_.clear();
        viewport()->update();
        if (stroke.size() >= 2) {
            emit inkAnnotationRequested(pageIndex, stroke);
        }
        return;
    }

    if (selecting_ && textController_ != nullptr) {
        const QString text = textController_->selectedText();
        trace(QStringLiteral("selection page=%1 chars=%2 text=%3")
                  .arg(textController_->selectionPage())
                  .arg(text.size())
                  .arg(text));
    }
    selecting_ = false;
}

void PageCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    QAbstractScrollArea::mouseDoubleClickEvent(event);
    if (event->button() != Qt::LeftButton || textController_ == nullptr) {
        return;
    }
    int pageIndex = -1;
    QPointF displayPoint;
    if (!pagePointAt(event->pos(), &pageIndex, &displayPoint)) {
        return;
    }
    const int order = textController_->hitAt(pageIndex, displayPoint);
    if (order < 0) {
        return;
    }
    const TextLayout* textLayout = textController_->layout(pageIndex);
    if (textLayout == nullptr) {
        return;
    }
    int start = 0;
    int end = 0;
    if (!textLayout->wordRangeAt(order, &start, &end)) {
        return;
    }
    textController_->setSelection(pageIndex, start, end);
    viewport()->update();
}

void PageCanvas::updateToolCursor() {
    const bool drawing = annotationController_ != nullptr &&
                         annotationController_->tool() != AnnotationController::Tool::None;
    if (drawing) {
        viewport()->setCursor(Qt::CrossCursor);
        return;
    }
    // 退出工具模式后把光标交回链接判定。
    overLink_ = false;
    viewport()->setCursor(Qt::ArrowCursor);
}

double PageCanvas::pageLeft(int index) const {
    Q_UNUSED(index);
    const QSizeF size = layout_.pageSize(index);
    const double available = viewport()->width() - kPageMargin * 2.0;
    return std::max(kPageMargin, (available - size.width()) / 2.0 + kPageMargin);
}

}  // namespace aipdf::app
