#include "app/PageCanvas.h"

#include "app/RenderThread.h"
#include "app/Language.h"
#include "app/TextController.h"
#include "app/TextLayout.h"
#include "app/TraceText.h"
#include "app/NavigationController.h"
#include "app/AnnotationController.h"
#include "app/InkGeometry.h"
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

    // 空画布的占位提示：常驻文案，登记后切换语言时就地重译并重画。
    texts_.bind([this] {
        emptyText_ = tr("未打开文档");
        viewport()->update();
    });

    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &PageCanvas::retranslateUi);
}

void PageCanvas::retranslateUi() {
    texts_.retranslate();
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
        painter.drawText(event->rect(), Qt::AlignCenter, emptyText_);
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
        // 带 /AP 的批注已经由 PDFium 画进页面位图了，这里再画一遍就是两层叠加
        // （高亮颜色深一倍）。外部软件写的批注同样会被 PDFium 绘制，所以这条对所有批注生效。
        if (annotation.hasAppearance) {
            continue;
        }
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
            case pdf::AnnotationType::Stamp: {
                const QRectF rect = toViewportRect(transform.toDisplay(annotation.rect), pageRect, zoom);
                painter.setPen(QPen(annotation.color, std::max(1.5, rect.height() * 0.06)));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(rect.adjusted(1, 1, -1, -1));
                QFont font = painter.font();
                font.setBold(true);
                font.setPointSizeF(std::max(5.0, rect.height() * 0.42));
                painter.setFont(font);
                painter.setPen(annotation.color);
                painter.drawText(rect, Qt::AlignCenter, annotation.contents.trimmed().isEmpty()
                                                             ? QStringLiteral("APPROVED")
                                                             : annotation.contents.trimmed());
                break;
            }
            case pdf::AnnotationType::FreeText: {
                const QRectF rect = toViewportRect(transform.toDisplay(annotation.rect), pageRect, zoom);
                const double size = (annotation.fontSize > 0.0 ? annotation.fontSize : 12.0) * zoom;
                QFont font = painter.font();
                font.setPointSizeF(std::max(1.0, size));
                painter.setFont(font);
                painter.setPen(annotation.color);
                // 中文靠画布自己排，不依赖外观流里的 WinAnsi 字体。
                painter.drawText(rect.adjusted(2, 2, -2, -2),
                                 Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                                 annotation.contents);
                break;
            }
            case pdf::AnnotationType::Sound: {
                const QRectF rect = toViewportRect(transform.toDisplay(annotation.rect), pageRect, zoom);
                painter.setPen(QPen(annotation.color, 1.4));
                painter.setBrush(QColor(255, 255, 255, 220));
                // 小喇叭图标：一个梯形加两道声波。
                QPolygonF speaker;
                speaker << QPointF(rect.left() + 3, rect.center().y() - 3)
                        << QPointF(rect.left() + 8, rect.center().y() - 3)
                        << QPointF(rect.left() + 14, rect.center().y() - 7)
                        << QPointF(rect.left() + 14, rect.center().y() + 7)
                        << QPointF(rect.left() + 8, rect.center().y() + 3)
                        << QPointF(rect.left() + 3, rect.center().y() + 3);
                painter.drawPolygon(speaker);
                break;
            }
            case pdf::AnnotationType::FileAttachment: {
                // 回形针：一个带斜线的小矩形，够认就行。
                const QRectF rect = toViewportRect(transform.toDisplay(annotation.rect), pageRect, zoom);
                painter.setPen(QPen(annotation.color, 1.4));
                painter.setBrush(QColor(255, 255, 255, 220));
                painter.drawRect(rect);
                painter.drawLine(rect.topLeft() + QPointF(4, 4), rect.bottomRight() - QPointF(4, 4));
                painter.drawLine(rect.topRight() + QPointF(-4, 4), rect.bottomLeft() + QPointF(4, -4));
                break;
            }
            case pdf::AnnotationType::Measure: {
                if (annotation.vertices.size() < 2) {
                    break;
                }
                const QPointF a = toViewportRect(QRectF(transform.toDisplay(annotation.vertices.at(0)),
                                                        QSizeF(0.0, 0.0)),
                                                 pageRect, zoom).topLeft();
                const QPointF b = toViewportRect(QRectF(transform.toDisplay(annotation.vertices.at(1)),
                                                        QSizeF(0.0, 0.0)),
                                                 pageRect, zoom).topLeft();
                painter.setPen(QPen(annotation.color, 1.5, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawLine(a, b);
                // 端点小刻度
                painter.drawLine(a + QPointF(-4, -4), a + QPointF(4, 4));
                painter.drawLine(b + QPointF(-4, -4), b + QPointF(4, 4));
                painter.setPen(annotation.color);
                const QPointF mid((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0);
                painter.drawText(QRectF(mid.x() - 60, mid.y() - 18, 120, 16),
                                 Qt::AlignCenter, annotation.contents);
                break;
            }
            case pdf::AnnotationType::Line:
            case pdf::AnnotationType::Arrow: {
                if (annotation.vertices.size() < 2) {
                    break;
                }
                const QPointF a = toViewportRect(QRectF(transform.toDisplay(annotation.vertices.at(0)),
                                                        QSizeF(0.0, 0.0)),
                                                 pageRect, zoom).topLeft();
                const QPointF b = toViewportRect(QRectF(transform.toDisplay(annotation.vertices.at(1)),
                                                        QSizeF(0.0, 0.0)),
                                                 pageRect, zoom).topLeft();
                painter.setPen(QPen(annotation.color, 1.5));
                painter.setBrush(Qt::NoBrush);
                painter.drawLine(a, b);
                if (annotation.type == pdf::AnnotationType::Arrow) {
                    const double dx = b.x() - a.x();
                    const double dy = b.y() - a.y();
                    const double length = std::hypot(dx, dy);
                    if (length > 0.01) {
                        const double head = 9.0;
                        const double ux = dx / length;
                        const double uy = dy / length;
                        const double angle = 0.4363;
                        const double ca = std::cos(angle);
                        const double sa = std::sin(angle);
                        painter.drawLine(b, QPointF(b.x() - head * (ux * ca - uy * sa),
                                                    b.y() - head * (uy * ca + ux * sa)));
                        painter.drawLine(b, QPointF(b.x() - head * (ux * ca + uy * sa),
                                                    b.y() - head * (uy * ca - ux * sa)));
                    }
                }
                break;
            }
            case pdf::AnnotationType::Polygon:
            case pdf::AnnotationType::Cloud: {
                if (annotation.vertices.size() < 3) {
                    break;
                }
                QPolygonF polygon;
                for (const QPointF& vertex : annotation.vertices) {
                    polygon << toViewportRect(QRectF(transform.toDisplay(vertex), QSizeF(0.0, 0.0)),
                                              pageRect, zoom).topLeft();
                }
                painter.setPen(QPen(annotation.color, 1.5));
                painter.setBrush(Qt::NoBrush);
                painter.drawPolygon(polygon);
                break;
            }
            case pdf::AnnotationType::Ellipse: {
                const QRectF rect = toViewportRect(transform.toDisplay(annotation.rect), pageRect, zoom);
                painter.setPen(QPen(annotation.color, 1.5));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(rect.adjusted(1, 1, -1, -1));
                break;
            }
            case pdf::AnnotationType::Squiggly: {
                // 波浪线：沿行的下沿画锯齿，和外观流里的算法保持一致的比例。
                painter.setPen(QPen(annotation.color, 1.6));
                painter.setBrush(Qt::NoBrush);
                for (const QRectF& quad : annotation.quads) {
                    const QRectF rect = toViewportRect(transform.toDisplay(quad), pageRect, zoom);
                    const double baseY = rect.bottom() - 1.0;
                    const double amplitude = 1.6;
                    const double period = 4.0;
                    QPolygonF zigzag;
                    zigzag << QPointF(rect.left(), baseY);
                    bool up = true;
                    for (double x = rect.left() + period; x <= rect.right(); x += period) {
                        zigzag << QPointF(x, baseY + (up ? -amplitude : amplitude));
                        up = !up;
                    }
                    zigzag << QPointF(rect.right(), baseY);
                    painter.drawPolyline(zigzag);
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
    // 双页模式下同一纵坐标会落在左右两页上，必须带上 x 才能判对。
    const int index =
        layout_.pageAtPoint(QPointF(viewportPos.x(), scrollTop + viewportPos.y()));
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
            if (tool == AnnotationController::Tool::Attachment) {
                const QPointF userPoint = toUserSpacePoint(pageIndex, displayPoint);
                trace(QStringLiteral("annotation tool attachment page=%1").arg(pageIndex));
                emit attachmentRequested(pageIndex, userPoint);
                return;
            }
            if (tool == AnnotationController::Tool::ImageStamp) {
                const QPointF userPoint = toUserSpacePoint(pageIndex, displayPoint);
                trace(QStringLiteral("annotation tool image stamp page=%1").arg(pageIndex));
                emit imageStampRequested(pageIndex, userPoint);
                return;
            }
            if (tool == AnnotationController::Tool::Sound) {
                const QPointF userPoint = toUserSpacePoint(pageIndex, displayPoint);
                trace(QStringLiteral("annotation tool sound page=%1").arg(pageIndex));
                emit soundRequested(pageIndex, userPoint);
                return;
            }
            if (tool == AnnotationController::Tool::Stamp) {
                const QPointF userPoint = toUserSpacePoint(pageIndex, displayPoint);
                trace(QStringLiteral("annotation tool stamp page=%1").arg(pageIndex));
                emit stampRequested(pageIndex, userPoint);
                return;
            }
            if (tool == AnnotationController::Tool::FreeText) {
                drawingSquare_ = true;
                drawingFreeText_ = true;
                drawPage_ = pageIndex;
                drawAnchor_ = displayPoint;
                drawCurrent_ = displayPoint;
                trace(QStringLiteral("annotation tool freetext page=%1").arg(pageIndex));
                viewport()->update();
                return;
            }
            if (tool == AnnotationController::Tool::Square ||
                tool == AnnotationController::Tool::Ellipse) {
                drawingSquare_ = true;
                drawingEllipse_ = tool == AnnotationController::Tool::Ellipse;
                drawPage_ = pageIndex;
                drawAnchor_ = displayPoint;
                drawCurrent_ = displayPoint;
                trace(QStringLiteral("annotation tool %1 page=%2")
                          .arg(drawingEllipse_ ? QStringLiteral("ellipse") : QStringLiteral("square"))
                          .arg(pageIndex));
                viewport()->update();
                return;
            }
            if (tool == AnnotationController::Tool::Line ||
                tool == AnnotationController::Tool::Arrow ||
                tool == AnnotationController::Tool::Measure) {
                drawingLine_ = true;
                lineType_ = tool == AnnotationController::Tool::Arrow
                                ? pdf::AnnotationType::Arrow
                                : (tool == AnnotationController::Tool::Measure
                                       ? pdf::AnnotationType::Measure
                                       : pdf::AnnotationType::Line);
                drawPage_ = pageIndex;
                drawAnchor_ = displayPoint;
                drawCurrent_ = displayPoint;
                trace(QStringLiteral("annotation tool line page=%1").arg(pageIndex));
                viewport()->update();
                return;
            }
            if (tool == AnnotationController::Tool::Polygon ||
                tool == AnnotationController::Tool::Cloud ||
                tool == AnnotationController::Tool::MeasurePerimeter ||
                tool == AnnotationController::Tool::MeasureArea) {
                measureMode_ = tool == AnnotationController::Tool::MeasureArea
                                   ? MeasureMode::Area
                                   : (tool == AnnotationController::Tool::MeasurePerimeter
                                          ? MeasureMode::Perimeter
                                          : MeasureMode::Distance);
                // 逐点输入：第一次点决定页号，之后只收同一页的点。
                if (polygonPage_ != pageIndex) {
                    polygonPoints_.clear();
                    polygonPage_ = pageIndex;
                    polygonType_ = tool == AnnotationController::Tool::Cloud
                                       ? pdf::AnnotationType::Cloud
                                       : pdf::AnnotationType::Polygon;
                }
                polygonPoints_.append(displayPoint);
                trace(QStringLiteral("annotation polygon point page=%1 count=%2")
                          .arg(pageIndex)
                          .arg(polygonPoints_.size()));
                viewport()->update();
                return;
            }
            if (tool == AnnotationController::Tool::Eraser) {
                erasing_ = true;
                drawPage_ = pageIndex;
                trace(QStringLiteral("annotation tool eraser page=%1").arg(pageIndex));
                eraseAt(pageIndex, displayPoint);
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
    if (drawingSquare_ || drawingInk_ || erasing_ || drawingLine_) {
        int pageIndex = -1;
        QPointF displayPoint;
        if (pagePointAt(event->pos(), &pageIndex, &displayPoint) && pageIndex == drawPage_) {
            drawCurrent_ = displayPoint;
            if (drawingInk_) {
                inkPoints_.append(displayPoint);
            }
            if (erasing_) {
                eraseAt(pageIndex, displayPoint);
            }
            if (!polygonPoints_.isEmpty() && polygonPage_ == pageIndex) {
                // 多边形预览跟着鼠标走：最后一个点是"虚线尾巴"。
                viewport()->update();
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

    if (drawingLine_) {
        const int pageIndex = drawPage_;
        const QPointF a = toUserSpacePoint(pageIndex, drawAnchor_);
        const QPointF b = toUserSpacePoint(pageIndex, drawCurrent_);
        drawingLine_ = false;
        drawPage_ = -1;
        viewport()->update();
        if (a != b) {
            emit lineAnnotationRequested(pageIndex, static_cast<int>(lineType_), a, b);
        }
        return;
    }

    if (erasing_) {
        erasing_ = false;
        drawPage_ = -1;
        trace(QStringLiteral("annotation tool eraser done"));
        viewport()->update();
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
                const QRectF rect = QRectF(a, b).normalized();
                if (drawingFreeText_) {
                    emit freeTextRequested(pageIndex, rect);
                } else if (drawingEllipse_) {
                    emit ellipseAnnotationRequested(pageIndex, rect);
                } else {
                    emit squareAnnotationRequested(pageIndex, rect);
                }
            }
            drawingEllipse_ = false;
            drawingFreeText_ = false;
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
                  .arg(traceText(text)));
    }
    selecting_ = false;
}

void PageCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    QAbstractScrollArea::mouseDoubleClickEvent(event);
    // 多边形/云线：双击就是"收笔"。这条必须先于文字选词判定，
    // 否则双击最后一个顶点时会顺手选中一个词。
    if (polygonPoints_.size() >= 3 && polygonPage_ >= 0) {
        QVector<QPointF> userPoints;
        userPoints.reserve(polygonPoints_.size());
        for (const QPointF& point : polygonPoints_) {
            userPoints.append(toUserSpacePoint(polygonPage_, point));
        }
        const int page = polygonPage_;
        const auto type = polygonType_;
        polygonPoints_.clear();
        polygonPage_ = -1;
        viewport()->update();
        if (measureMode_ != MeasureMode::Distance) {
            const auto mode = measureMode_;
            measureMode_ = MeasureMode::Distance;
            emit measureShapeRequested(page, static_cast<int>(mode), userPoints);
        } else {
            emit polygonAnnotationRequested(page, static_cast<int>(type), userPoints);
        }
        return;
    }
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

void PageCanvas::eraseAt(int pageIndex, const QPointF& displayPoint) {
    if (annotationController_ == nullptr) {
        return;
    }
    PageTransform transform;
    if (!annotationController_->transformForPage(pageIndex, &transform)) {
        return;
    }
    // 半径按屏幕像素给：缩放再大，橡皮的"手感"也不该变大变小。
    constexpr double kEraserRadius = 8.0;
    const auto annotations = annotationController_->annotationsForPage(pageIndex);
    for (const pdf::Annotation& annotation : annotations) {
        if (annotation.type != pdf::AnnotationType::Ink) {
            continue;
        }
        for (const QVector<QPointF>& stroke : annotation.strokes) {
            QVector<QPointF> display;
            display.reserve(stroke.size());
            for (const QPointF& point : stroke) {
                display.append(transform.toDisplay(point));
            }
            if (!strokeHitsCircle(display, displayPoint, kEraserRadius)) {
                continue;
            }
            annotationController_->removeAnnotation(annotation.id);
            trace(QStringLiteral("annotation erase id=%1").arg(annotation.id));
            break;
        }
    }
}

double PageCanvas::pageLeft(int index) const {
    // 整行居中，页在行内按 pageRect().x() 偏移：纵向长卷时一页一行（x 恒为 0），
    // 双页模式时左页 0、右页 一列宽 + 间距。
    const QRectF rect = layout_.pageRect(index);
    const double available = viewport()->width() - kPageMargin * 2.0;
    // 按"这一行"的宽度居中，而不是全篇最宽：页面尺寸不一致时，
    // 用最宽页居中会让窄页明显偏左。
    const double rowWidth = layout_.rowWidth(index);
    const double rowLeft = std::max(kPageMargin, (available - rowWidth) / 2.0 + kPageMargin);
    static int lastWidth = -1;
    if (index == 0 && viewport()->width() != lastWidth) {
        lastWidth = viewport()->width();
        trace(QStringLiteral("pageLeft diag viewport=%1 rowWidth=%2 rowLeft=%3")
                  .arg(viewport()->width())
                  .arg(rowWidth)
                  .arg(rowLeft));
    }
    return rowLeft + rect.x();
}

}  // namespace aipdf::app
