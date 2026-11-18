#include "app/PageCanvas.h"

#include "app/ReadingScheme.h"
#include "app/Theme.h"

#include "app/RenderThread.h"
#include "app/Language.h"
#include "app/TextController.h"
#include "app/TextLayout.h"
#include "app/TraceText.h"
#include "app/NavigationController.h"
#include "app/AnnotationController.h"
#include "app/InkGeometry.h"
#include "app/MouseShortcuts.h"
#include "pdf/Annotation.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QLineF>
#include <QPlainTextEdit>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>

namespace aipdf::app {

namespace {
// 页与页之间留一点空隙，页面本身画一圈浅色边框。
constexpr double kPageSpacing = 20.0;
constexpr double kPageMargin = 24.0;
constexpr int kPrefetchPages = 1;

// 阅读区底色。
//
// 显式选了夜间 / 护眼 / 高对比的，用它自己那套底色；
// 「浅色」跟随界面主题——深色界面下还用浅灰底的话，
// 窗口中间会横着一道亮带，跟整个界面拧着。
QColor canvasBackdrop(theme::ReadingScheme scheme) {
    if (scheme == theme::ReadingScheme::Light && theme::isDarkInterface()) {
        return QColor(0x2A, 0x2E, 0x38);
    }
    return theme::backdropColor(scheme);
}

// 画在阅读区底色上的文字色：底色深就用浅字，底色浅就用界面的正文色。
QColor canvasForeground(const QColor& backdrop) {
    if (backdrop.lightness() < 128) {
        return QColor(0xE6, 0xE8, 0xEC);
    }
    return theme::interfaceTextColor();
}

// 翻页过渡的总时长（毫秒）。够短不打断阅读，够长能看出变化。
constexpr int kTransitionDurationMs = 220;

// 标尺的尺寸（像素）：横杆高、竖杆宽。
constexpr double kRulerThickness = 20.0;
constexpr double kRulerLabelWidth = 46.0;

// 自动滚动的参数：离边缘多近算贴边、贴边时的最大速度（像素/秒）。
constexpr double kAutoScrollEdgeMargin = 36.0;
constexpr double kAutoScrollMaxVelocity = 900.0;

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
    // 视口底色与文字色跟随界面主题：写死颜色的话，暗色主题下画布周围会亮一块。
    canvasPalette.setColor(QPalette::Mid, theme::surfaceColor());
    canvasPalette.setColor(QPalette::Text, theme::interfaceTextColor());
    setPalette(canvasPalette);
    setBackgroundRole(QPalette::Mid);
    setFrameShape(QFrame::NoFrame);
    // QAbstractScrollArea 会创建多个名为 qt_scrollarea_viewport 的控件；给 PDF
    // 画布视口唯一标识，避免验收脚本误拿到左侧导航面板的 viewport。
    viewport()->setObjectName(QStringLiteral("pdfCanvas.viewport"));
    viewport()->setAutoFillBackground(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    // 画布要能拿到焦点：点一下之后 Ctrl+C 才能复制选中的文字。
    viewport()->setFocusPolicy(Qt::StrongFocus);
    // 鼠标跟踪：没有它就收不到「只是移过去」的移动事件，
    // 悬停在链接上时手型光标就不会出现。
    viewport()->setMouseTracking(true);
    // 右键菜单挂在视口上：QAbstractScrollArea 的 contextMenuEvent 由视口转发，
    // 直接依赖 QWidget 的冒泡在不同平台上位置坐标系不一致，统一在过滤器里取。
    viewport()->installEventFilter(this);

    // 空画布的占位提示：常驻文案，登记后切换语言时就地重译并重画。
    texts_.bind([this] {
        emptyText_ = tr("未打开文档");
        viewport()->update();
    });

    // 自动滚动：拖动时指针贴到视口边缘就跟着滚。
    // 定时器常驻但默认不跑 —— 速度为 0 时自己停，省得空转。
    autoScrollTimer_ = new QTimer(this);
    autoScrollTimer_->setInterval(16);
    connect(autoScrollTimer_, &QTimer::timeout, this, &PageCanvas::onAutoScrollTick);

    // 翻页过渡：旧页截图叠在视口上淡出 / 推走。
    transitionTimer_ = new QTimer(this);
    transitionTimer_->setInterval(16);
    connect(transitionTimer_, &QTimer::timeout, this, &PageCanvas::onTransitionTick);

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

void PageCanvas::setReadingScheme(theme::ReadingScheme scheme) {
    if (readingScheme_ == scheme) {
        return;
    }
    readingScheme_ = scheme;
    viewport()->update();
}

theme::ReadingScheme PageCanvas::readingScheme() const {
    return readingScheme_;
}

void PageCanvas::setRulerVisible(bool visible) {
    if (rulerVisible_ == visible) {
        return;
    }
    rulerVisible_ = visible;
    viewport()->update();
}

bool PageCanvas::rulerVisible() const {
    return rulerVisible_;
}

void PageCanvas::setRulerUnit(viewmodes::RulerUnit unit) {
    if (rulerUnit_ == unit) {
        return;
    }
    rulerUnit_ = unit;
    viewport()->update();
}

viewmodes::RulerUnit PageCanvas::rulerUnit() const {
    return rulerUnit_;
}

void PageCanvas::setAutoScrollEnabled(bool enabled) {
    if (autoScrollEnabled_ == enabled) {
        return;
    }
    autoScrollEnabled_ = enabled;
    if (!enabled) {
        stopAutoScroll();
    }
}

bool PageCanvas::autoScrollEnabled() const {
    return autoScrollEnabled_;
}

void PageCanvas::setPageTransition(viewmodes::PageTransition transition) {
    pageTransition_ = transition;
    if (transition == viewmodes::PageTransition::None) {
        transitionTimer_->stop();
        transitionSnapshot_ = QPixmap();
    }
}

viewmodes::PageTransition PageCanvas::pageTransition() const {
    return pageTransition_;
}

void PageCanvas::setMirrorEnabled(bool enabled) {
    if (mirrorEnabled_ == enabled) {
        return;
    }
    mirrorEnabled_ = enabled;
    viewport()->update();
}

bool PageCanvas::mirrorEnabled() const {
    return mirrorEnabled_;
}

void PageCanvas::setPanModeEnabled(bool enabled) {
    if (panMode_ == enabled) {
        return;
    }
    panMode_ = enabled;
    if (!panMode_) {
        // 关掉手型时正在拖的那一下也要收尾，否则滚动位置会僵在中间。
        panDrag_ = viewmodes::PanDragState();
    }
    updateToolCursor();
}

bool PageCanvas::panModeEnabled() const {
    return panMode_;
}

void PageCanvas::goToPage(int index) {
    if (layout_.isEmpty()) {
        return;
    }
    const int clamped = std::clamp(index, 0, layout_.pageCount() - 1);
    // 过渡：先抓下旧画面，再滚到新页 —— 抓在滚动之前，拿到的是旧页。
    startPageTransition();
    verticalScrollBar()->setValue(static_cast<int>(layout_.pageTop(clamped)));
}

void PageCanvas::paintEvent(QPaintEvent* event) {
    QPainter painter(viewport());
    // 阅读区底色按配色方案走：夜间是深色、护眼是米绿、高对比是纯黑。
    const QColor backdrop = canvasBackdrop(readingScheme_);
    painter.fillRect(event->rect(), backdrop);

    if (layout_.isEmpty()) {
        painter.setPen(canvasForeground(backdrop));
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

        // 逆序镜像：把整页内容（页面位图 + 高亮 + 批注）左右翻过来。
        // 用 painter 变换而不是逐个改坐标：页面位图是位图，没法靠改坐标翻，
        // 而变换一次就保证「图与图上的标注」一起翻，不会出现图翻了标注没翻。
        // 变换是绕页面自身的中线翻的，所以页面在视口里的位置不变。
        painter.save();
        if (mirrorEnabled_) {
            painter.translate(2.0 * pageRect.left() + pageRect.width(), 0.0);
            painter.scale(-1.0, 1.0);
        }

        // 页面底板和克制的投影，帮助纸张从工作区背景中分离。
        painter.fillRect(pageRect.translated(0.0, 3.0), QColor(24, 24, 27, 20));
        painter.fillRect(pageRect, Qt::white);
        painter.setPen(QPen(theme::pageOutlineColor(readingScheme_), 1.0));
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
        if (index == objectSelectionPage_ && !objectSelectionRect_.isEmpty()) {
            const QRectF selection = toViewportRect(objectSelectionRect_, pageRect, layout_.zoom());
            painter.setPen(QPen(QColor(QStringLiteral("#4169C1")), 1.5, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(selection);
            painter.setPen(QPen(Qt::white, 1.0));
            painter.setBrush(QColor(QStringLiteral("#4169C1")));
            const QPointF points[] = {selection.topLeft(), QPointF(selection.center().x(), selection.top()),
                selection.topRight(), QPointF(selection.right(), selection.center().y()),
                selection.bottomRight(), QPointF(selection.center().x(), selection.bottom()),
                selection.bottomLeft(), QPointF(selection.left(), selection.center().y())};
            for (const QPointF& point : points) painter.drawRect(QRectF(point - QPointF(4, 4), QSizeF(8, 8)));
            const QPointF rotationHandle(selection.center().x(), selection.top() - 24.0);
            painter.drawLine(QPointF(selection.center().x(), selection.top()), rotationHandle);
            painter.drawEllipse(rotationHandle, 5.0, 5.0);
            painter.setPen(QPen(QColor(QStringLiteral("#6F8FD0")), 1.0, Qt::DashLine));
            if (std::isfinite(objectGuideX_)) {
                const double x = pageRect.left() + objectGuideX_ * layout_.zoom();
                painter.drawLine(QPointF(x, pageRect.top()), QPointF(x, pageRect.bottom()));
            }
            if (std::isfinite(objectGuideY_)) {
                const double y = pageRect.top() + objectGuideY_ * layout_.zoom();
                painter.drawLine(QPointF(pageRect.left(), y), QPointF(pageRect.right(), y));
            }
        }
        // 正在拖的批注工具的预览。
        if (index == drawPage_) {
            paintAnnotationPreview(painter, pageRect, layout_.zoom());
        }
        // callout 等箭头尖时的预览：注释框 + 一条指向鼠标的虚线。
        if (awaitingCalloutTip_ && index == calloutPage_) {
            const QRectF box = toViewportRect(calloutBoxDisplay_, pageRect, layout_.zoom());
            painter.setPen(QPen(QColor(64, 124, 235), 1.4, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box);
            const QPointF tip(pageRect.left() + calloutTipPreviewDisplay_.x() * layout_.zoom(),
                              pageRect.top() + calloutTipPreviewDisplay_.y() * layout_.zoom());
            painter.drawLine(box.center(), tip);
        }
        painter.restore();
    }

    // 标尺画在最上面：它是量尺寸的工具，不该被页面内容盖住。
    if (rulerVisible_) {
        paintRulers(painter, currentPage_);
    }

    // 翻页过渡：旧页的截图叠在最上层，按进度淡出 / 推走。
    if (transitionTimer_->isActive() && !transitionSnapshot_.isNull()) {
        const double progress = static_cast<double>(transitionElapsedMs_) /
                                static_cast<double>(kTransitionDurationMs);
        const viewmodes::PageTransitionFrame frame =
            viewmodes::pageTransitionFrame(pageTransition_, progress);
        const double pageWidthPixels = layout_.pageCount() > 0
                                          ? layout_.pageSize(currentPage_).width()
                                          : viewport()->width();
        painter.save();
        painter.setOpacity(qBound(0.0, frame.opacity, 1.0));
        painter.drawPixmap(QPointF(frame.offsetX * pageWidthPixels, 0.0), transitionSnapshot_);
        painter.restore();
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
                // 区域高亮带自定义外观（半透明填充），普通多边形只描边。
                // 靠 customAppearance 非空来区分：屏幕上的颜色必须与写进文件的外观一致，
                // 否则用户看到的和别的软件打开的不一样。
                if (!annotation.customAppearance.isEmpty()) {
                    QColor fill = annotation.color;
                    const double opacity = (annotation.opacity > 0.0 && annotation.opacity <= 1.0)
                                              ? annotation.opacity
                                              : 0.35;
                    fill.setAlpha(qBound(0, static_cast<int>(opacity * 255.0 + 0.5), 255));
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(fill);
                    painter.drawPolygon(polygon);
                    break;
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
                // callout 用 Square + 自定义 /AP：屏幕要画成「注释框 + 箭头」，
                // 与文件里的外观一致，不能画成一个半透明大方块。
                if (!annotation.customAppearance.isEmpty()) {
                    paintCallout(painter, annotation, transform, pageRect, zoom);
                    break;
                }
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

// callout 的屏幕画法：注释框描边 + 箭头折线 + 箭头尖。
//
// 顶点约定（前 4 个是注释框的四角，后面是箭头的折线点，都在页面 user space）：
// 这是界面层与画布之间约定好的，与写进 PDF 的外观流画的是同一套几何。
void PageCanvas::paintCallout(QPainter& painter,
                              const pdf::Annotation& annotation,
                              const PageTransform& transform,
                              const QRectF& pageRect,
                              double zoom) const {
    const QVector<QPointF>& vertices = annotation.vertices;
    if (vertices.size() < 6) {
        return;
    }
    const double strokeWidth = (annotation.lineWidth > 0.0 ? annotation.lineWidth : 1.0);
    const auto toViewportPoint = [&](const QPointF& userPoint) {
        return toViewportRect(QRectF(transform.toDisplay(userPoint), QSizeF(0.0, 0.0)),
                              pageRect, zoom).topLeft();
    };

    QPolygonF box;
    for (int i = 0; i < 4; ++i) {
        box << toViewportPoint(vertices.at(i));
    }
    painter.setPen(QPen(annotation.color, strokeWidth * zoom));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(box);

    QPolygonF arrow;
    for (int i = 4; i < vertices.size(); ++i) {
        arrow << toViewportPoint(vertices.at(i));
    }
    if (arrow.size() < 2) {
        return;
    }
    painter.drawPolyline(arrow);

    // 箭头尖：两段短线。角度与长度与 pdf::AnnotationShapes 里生成外观流时一致，
    // 免得屏幕上一个大头小尾、文件里是另一个样子。
    const QPointF tip = arrow.last();
    const QPointF from = arrow.at(arrow.size() - 2);
    const double dx = tip.x() - from.x();
    const double dy = tip.y() - from.y();
    const double length = std::hypot(dx, dy);
    if (length <= 0.01) {
        return;
    }
    const double head = std::min(std::max(6.0, strokeWidth * 4.0) * zoom, length * 0.45);
    const double ux = dx / length;
    const double uy = dy / length;
    const double angle = 0.4363;  // 25°
    const double ca = std::cos(angle);
    const double sa = std::sin(angle);
    painter.drawLine(tip, QPointF(tip.x() - head * (ux * ca - uy * sa),
                                 tip.y() - head * (uy * ca + ux * sa)));
    painter.drawLine(tip, QPointF(tip.x() - head * (ux * ca + uy * sa),
                                 tip.y() - head * (uy * ca - ux * sa)));
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
    QPointF point = displayPoint;
    if (mirrorEnabled_) {
        // 画面是镜像的：先把 x 换算回「未镜像」的位置，再进页面坐标，
        // 否则点在镜像后的正文上会命中反方向的字。
        const double zoom = qMax(0.01, layout_.zoom());
        const double pageWidthPoints = layout_.pageSize(pageIndex).width() / zoom;
        const double mirrored = viewmodes::mirrorX(displayPoint.x(), pageWidthPoints);
        point = QPointF(mirrored, displayPoint.y());
    }
    return transform.toUserSpace(point);
}

void PageCanvas::setIndexHighlight(int pageIndex, const QVector<QRectF>& displayRects) {
    indexHighlightPage_ = pageIndex;
    indexHighlightRects_ = displayRects;
    viewport()->update();
}

void PageCanvas::setSpeechHighlight(int pageIndex, const QVector<QRectF>& displayRects) {
    speechHighlightPage_ = pageIndex;
    speechHighlightRects_ = displayRects;
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

    // 朗读高亮画在最后：它是「此刻正在读」的位置，
    // 应该是页面上最显眼的一块。
    if (speechHighlightPage_ == pageIndex && !speechHighlightRects_.isEmpty()) {
        painter.setPen(QPen(QColor(214, 90, 49), 1.2));
        painter.setBrush(QColor(255, 153, 0, 110));
        for (const QRectF& rect : speechHighlightRects_) {
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
    repositionInlineEditor();
}

void PageCanvas::scrollContentsBy(int dx, int dy) {
    QAbstractScrollArea::scrollContentsBy(dx, dy);
    updateScrollRange();
    requestVisiblePages();
    repositionInlineEditor();
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

    // 横向：页面比视口宽时（放大、双页、宽幅文档）必须能滚到两侧。
    // 以前只设纵向范围，放大后页面中间之外的部分永远看不到，
    // 中键平移与 Shift+滚轮也就没有效果。
    QScrollBar* horizontal = horizontalScrollBar();
    const int viewportWidth = std::max(1, viewport()->width());
    const int contentWidth =
        static_cast<int>(std::ceil(layout_.totalWidth() + kPageMargin * 2.0));
    if (contentWidth != lastContentWidth_ || viewportWidth != lastViewportWidth_) {
        lastContentWidth_ = contentWidth;
        lastViewportWidth_ = viewportWidth;
        horizontal->setRange(0, std::max(0, contentWidth - viewportWidth));
        horizontal->setPageStep(viewportWidth);
        horizontal->setSingleStep(std::max(1, viewportWidth / 20));
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
    const double contentY = scrollTop + viewportPos.y();
    // 页面在视口里是整行居中的：pageRect() 用的是行内坐标（x 从 0 起），
    // 而鼠标给的是视口坐标。不先减掉行左边距，页面右半边会整片命中失败
    // （窗口越宽、右边距越大，失效范围越大）。
    const int rowPage = layout_.pageAt(contentY);
    if (rowPage < 0 || rowPage >= layout_.pageCount()) {
        return false;
    }
    const double rowLeft = pageLeft(rowPage) - layout_.pageRect(rowPage).left();
    // 双页模式下同一纵坐标会落在左右两页上，必须带上 x 才能判对。
    const int index = layout_.pageAtPoint(QPointF(viewportPos.x() - rowLeft, contentY));
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
    // 中键拖动平移：不管当前是哪个工具都能用，松开就恢复。
    // 这是「临时手型」，不改 panMode_ 的开关状态。
    if (event->button() == Qt::MiddleButton) {
        viewport()->setFocus();
        panDrag_ = viewmodes::beginPanDrag(event->pos(), horizontalScrollBar()->value(),
                                            verticalScrollBar()->value());
        middlePan_ = true;
        viewport()->setCursor(Qt::ClosedHandCursor);
        trace(QStringLiteral("middle pan begin"));
        event->accept();
        return;
    }
    // 鼠标侧键：后退 / 前进，与 Alt+← / Alt+→ 同一套历史。
    if (event->button() == Qt::BackButton) {
        emit backRequested();
        event->accept();
        return;
    }
    if (event->button() == Qt::ForwardButton) {
        emit forwardRequested();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    viewport()->setFocus();
    // 连击计数要在工具分发之前记：三击 / 四击要选中整行 / 整段。
    const int clickCount = registerClick(event->pos());
    // 手型工具：按住左键就是平移，页面外的空白区也一样能拖。
    // 必须在页面命中判定之前 —— 平移不关心指针压在哪一页上。
    if (panMode_) {
        panDrag_ = viewmodes::beginPanDrag(event->pos(), horizontalScrollBar()->value(),
                                            verticalScrollBar()->value());
        viewport()->setCursor(Qt::ClosedHandCursor);
        trace(QStringLiteral("pan drag begin"));
        return;
    }
    // 拖动期间指针贴到视口边缘就自动滚：定时器在这里开，
    // 松手时停。速度由 mouseMoveEvent 里的 autoScrollState 决定。
    startAutoScroll();

    int pageIndex = -1;
    QPointF displayPoint;
    if (!pagePointAt(event->pos(), &pageIndex, &displayPoint)) {
        trace(QStringLiteral("press ignored pos=%1,%2").arg(event->pos().x()).arg(event->pos().y()));
        return;
    }

    // callout 的第二段：注释框已经拖好了，这一次点击是选箭头指向的位置。
    // 必须在工具分发之前处理，否则这次点击会被当成「再拖一个框」。
    if (awaitingCalloutTip_) {
        if (pageIndex != calloutPage_) {
            emit hintMessage(tr("请在同一个页面上选择箭头指向的位置"));
            return;
        }
        const int boxPage = calloutPage_;
        const QRectF box = calloutBoxDisplay_.normalized();
        // 显示坐标 → 用户坐标：复用画布现成的那一套换算，不另写一份。
        const QRectF userBox =
            QRectF(toUserSpacePoint(boxPage, box.topLeft()),
                   toUserSpacePoint(boxPage, box.bottomRight())).normalized();
        const QPointF userTip = toUserSpacePoint(pageIndex, displayPoint);
        awaitingCalloutTip_ = false;
        calloutBoxDisplay_ = QRectF();
        calloutPage_ = -1;
        viewport()->update();
        emit calloutRequested(boxPage, userBox, userTip);
        return;
    }

    if (!drawingObjectSelection_ && pageIndex == objectSelectionPage_ &&
        !objectSelectionRect_.isEmpty()) {
        const double tolerance = 8.0 / qMax(0.01, layout_.zoom());
        const QPointF handles[] = {objectSelectionRect_.topLeft(),
            QPointF(objectSelectionRect_.center().x(), objectSelectionRect_.top()),
            objectSelectionRect_.topRight(),
            QPointF(objectSelectionRect_.right(), objectSelectionRect_.center().y()),
            objectSelectionRect_.bottomRight(),
            QPointF(objectSelectionRect_.center().x(), objectSelectionRect_.bottom()),
            objectSelectionRect_.bottomLeft(),
            QPointF(objectSelectionRect_.left(), objectSelectionRect_.center().y()),
            QPointF(objectSelectionRect_.center().x(),
                    objectSelectionRect_.top() - 24.0 / qMax(0.01, layout_.zoom()))};
        objectDragHandle_ = -1;
        for (int handle = 0; handle < 9; ++handle) {
            if (QLineF(displayPoint, handles[handle]).length() <= tolerance) {
                objectDragHandle_ = handle;
                break;
            }
        }
        if (objectDragHandle_ >= 0 || objectSelectionRect_.contains(displayPoint)) {
            draggingObjectSelection_ = true;
            objectDragStart_ = displayPoint;
            objectDragStartRect_ = objectSelectionRect_;
            if (objectDragHandle_ == 8) {
                const QPointF offset = displayPoint - objectSelectionRect_.center();
                objectRotationStartAngle_ = qRadiansToDegrees(std::atan2(offset.y(), offset.x()));
                objectRotationDegrees_ = 0.0;
            }
            return;
        }
    }

    if (drawingObjectSelection_ || objectEditMode_) {
        selecting_ = false;
        drawingSquare_ = true;
        drawingObjectSelection_ = true;
        objectSelectionAdditive_ = event->modifiers().testFlag(Qt::ShiftModifier);
        drawPage_ = pageIndex;
        drawAnchor_ = displayPoint;
        drawCurrent_ = displayPoint;
        viewport()->update();
        return;
    }

    // 表单建域工具优先：拖框完成前不触发批注、文字选择或链接。
    if (!formFieldKind_.isEmpty()) {
        selecting_ = false;
        drawingSquare_ = true;
        drawingFormField_ = true;
        drawPage_ = pageIndex;
        drawAnchor_ = displayPoint;
        drawCurrent_ = displayPoint;
        viewport()->update();
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
            if (tool == AnnotationController::Tool::NumberStamp) {
                const QPointF userPoint = toUserSpacePoint(pageIndex, displayPoint);
                trace(QStringLiteral("annotation tool number stamp page=%1").arg(pageIndex));
                emit numberStampRequested(pageIndex, userPoint);
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
            if (tool == AnnotationController::Tool::Callout) {
                // 第一段：拖出注释框。松手之后再点一下选箭头尖（见上面那段）。
                drawingSquare_ = true;
                drawingCallout_ = true;
                drawPage_ = pageIndex;
                drawAnchor_ = displayPoint;
                drawCurrent_ = displayPoint;
                trace(QStringLiteral("annotation tool callout page=%1").arg(pageIndex));
                viewport()->update();
                return;
            }
            if (tool == AnnotationController::Tool::Polygon ||
                tool == AnnotationController::Tool::Cloud ||
                tool == AnnotationController::Tool::RegionHighlight ||
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
                    polygonIsRegionHighlight_ =
                        tool == AnnotationController::Tool::RegionHighlight;
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

    // 三击选行、四击选段：在普通拖选的锚点建立之前处理。
    if (clickCount >= 3 && selectTextUnitAt(clickCount, pageIndex, order)) {
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
    // 手型拖拽中：光标判定交给平移，别让链接判定把光标改回去。
    if (panDrag_.active) {
        const int nextX = viewmodes::panScrollValue(panDrag_.startScrollX, panDrag_.anchor.x(),
                                                     event->pos().x(), horizontalScrollBar()->maximum());
        const int nextY = viewmodes::panScrollValue(panDrag_.startScrollY, panDrag_.anchor.y(),
                                                     event->pos().y(), verticalScrollBar()->maximum());
        horizontalScrollBar()->setValue(nextX);
        verticalScrollBar()->setValue(nextY);
        return;
    }
    updateLinkCursor(event->pos());
    // 等箭头尖时记下鼠标位置，用来画「注释框 → 箭头尖」的预览线。
    if (awaitingCalloutTip_) {
        int pageIndex = -1;
        QPointF displayPoint;
        if (pagePointAt(event->pos(), &pageIndex, &displayPoint) && pageIndex == calloutPage_) {
            calloutTipPreviewDisplay_ = displayPoint;
            viewport()->update();
        }
        return;
    }
    if (draggingObjectSelection_) {
        updateAutoScroll(event->pos().y());
        int pageIndex = -1;
        QPointF point;
        if (!pagePointAt(event->pos(), &pageIndex, &point) || pageIndex != objectSelectionPage_) return;
        const QPointF delta = point - objectDragStart_;
        QRectF next = objectDragStartRect_;
        objectGuideX_ = std::numeric_limits<double>::quiet_NaN();
        objectGuideY_ = std::numeric_limits<double>::quiet_NaN();
        if (objectDragHandle_ == 8) {
            const QPointF offset = point - objectDragStartRect_.center();
            const double angle = qRadiansToDegrees(std::atan2(offset.y(), offset.x()));
            objectRotationDegrees_ = angle - objectRotationStartAngle_;
            if (event->modifiers().testFlag(Qt::ShiftModifier))
                objectRotationDegrees_ = qRound(objectRotationDegrees_ / 15.0) * 15.0;
            viewport()->update();
            return;
        } else if (objectDragHandle_ < 0) next.translate(delta);
        else {
            if (objectDragHandle_ == 0 || objectDragHandle_ == 6 || objectDragHandle_ == 7) next.setLeft(next.left() + delta.x());
            if (objectDragHandle_ == 2 || objectDragHandle_ == 3 || objectDragHandle_ == 4) next.setRight(next.right() + delta.x());
            if (objectDragHandle_ == 0 || objectDragHandle_ == 1 || objectDragHandle_ == 2) next.setTop(next.top() + delta.y());
            if (objectDragHandle_ == 4 || objectDragHandle_ == 5 || objectDragHandle_ == 6) next.setBottom(next.bottom() + delta.y());
            if (event->modifiers().testFlag(Qt::ShiftModifier) &&
                (objectDragHandle_ == 0 || objectDragHandle_ == 2 ||
                 objectDragHandle_ == 4 || objectDragHandle_ == 6)) {
                const double ratio = objectDragStartRect_.width() /
                                     qMax(0.01, objectDragStartRect_.height());
                const double width = next.width();
                const double height = next.height();
                if (width / qMax(0.01, height) > ratio) {
                    const double targetHeight = width / ratio;
                    if (objectDragHandle_ < 3) next.setTop(next.bottom() - targetHeight);
                    else next.setBottom(next.top() + targetHeight);
                } else {
                    const double targetWidth = height * ratio;
                    if (objectDragHandle_ == 0 || objectDragHandle_ == 6)
                        next.setLeft(next.right() - targetWidth);
                    else
                        next.setRight(next.left() + targetWidth);
                }
            }
        }
        if (!event->modifiers().testFlag(Qt::AltModifier)) {
            const QRectF pageLayoutRect = layout_.pageRect(pageIndex);
            const double pageWidth = pageLayoutRect.width() / qMax(0.01, layout_.zoom());
            const double pageHeight = pageLayoutRect.height() / qMax(0.01, layout_.zoom());
            const double tolerance = 6.0 / qMax(0.01, layout_.zoom());
            QVector<double> targetsX{0.0, pageWidth / 2.0, pageWidth};
            QVector<double> targetsY{0.0, pageHeight / 2.0, pageHeight};
            for (const QRectF& rect : objectSnapRects_) {
                targetsX.append({rect.left(), rect.center().x(), rect.right()});
                targetsY.append({rect.top(), rect.center().y(), rect.bottom()});
            }
            const double pointsX[] = {next.left(), next.center().x(), next.right()};
            const double pointsY[] = {next.top(), next.center().y(), next.bottom()};
            double bestX = tolerance;
            double bestDx = 0.0;
            double bestY = tolerance;
            double bestDy = 0.0;
            for (double target : targetsX) for (double edge : pointsX) {
                const double distance = qAbs(target - edge);
                if (distance < bestX) { bestX = distance; bestDx = target - edge; objectGuideX_ = target; }
            }
            for (double target : targetsY) for (double edge : pointsY) {
                const double distance = qAbs(target - edge);
                if (distance < bestY) { bestY = distance; bestDy = target - edge; objectGuideY_ = target; }
            }
            if (std::isfinite(objectGuideX_)) next.translate(bestDx, 0.0);
            if (std::isfinite(objectGuideY_)) next.translate(0.0, bestDy);
        }
        if (next.width() >= 2.0 && next.height() >= 2.0) objectSelectionRect_ = next.normalized();
        viewport()->update();
        return;
    }

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
        if (drawingSquare_ || drawingInk_ || drawingLine_) {
            updateAutoScroll(event->pos().y());
        }
        return;
    }

    if (!selecting_ || textController_ == nullptr) {
        return;
    }
    updateAutoScroll(event->pos().y());
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
    if (drawingObjectSelection_ || !formFieldKind_.isEmpty() || (annotationController_ != nullptr &&
        annotationController_->tool() != AnnotationController::Tool::None)) {
        viewport()->setCursor(Qt::CrossCursor);
        return;
    }
    // 手型工具开着：光标是「抓手」，链接判定不参与。
    if (panMode_) {
        viewport()->setCursor(Qt::OpenHandCursor);
        return;
    }
    if (navigationController_ == nullptr) {
        viewport()->setCursor(Qt::ArrowCursor);
        return;
    }
    int pageIndex = -1;
    QPointF displayPoint;
    if (!pagePointAt(viewportPos, &pageIndex, &displayPoint)) {
        // 移到页面外的空白区也算"离开链接"：状态要翻转，轨迹也要记，
        // 否则验收里"移出链接后指针恢复默认"永远观察不到。
        if (overLink_) {
            overLink_ = false;
            trace(QStringLiteral("cursor default"));
        }
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
    // 松手就停：边缘自动滚动只发生在拖动期间。
    // 这一句必须放在最前面 —— 下面每条分支都会 return，
    // 放后面的话拖完直线/墨迹/方框后定时器还在跑，视口会一直自己滚。
    stopAutoScroll();
    // 中键平移结束：只清临时状态，光标按当前工具 / 手型恢复。
    if (middlePan_) {
        middlePan_ = false;
        panDrag_ = viewmodes::PanDragState();
        viewport()->setCursor(panMode_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
        trace(QStringLiteral("middle pan end"));
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    // 手型拖拽结束：收手，光标回到「抓手」。
    if (panDrag_.active) {
        panDrag_ = viewmodes::PanDragState();
        viewport()->setCursor(Qt::OpenHandCursor);
        trace(QStringLiteral("pan drag end"));
        return;
    }
    if (draggingObjectSelection_) {
        draggingObjectSelection_ = false;
        objectGuideX_ = std::numeric_limits<double>::quiet_NaN();
        objectGuideY_ = std::numeric_limits<double>::quiet_NaN();
        const int releasedHandle = objectDragHandle_;
        const QRectF before = objectDragStartRect_;
        const QRectF after = objectSelectionRect_;
        objectDragHandle_ = -1;
        if (releasedHandle == 8) {
            const double degrees = objectRotationDegrees_;
            objectRotationDegrees_ = 0.0;
            if (!qFuzzyIsNull(degrees)) emit objectSelectionRotated(objectSelectionPage_, degrees);
            return;
        }
        if (before != after) {
            PageTransform transform;
            if (annotationController_ && annotationController_->transformForPage(objectSelectionPage_, &transform)) {
                const auto toUserRect = [&transform](const QRectF& rect) {
                    return QRectF(transform.toUserSpace(rect.topLeft()),
                                  transform.toUserSpace(rect.bottomRight())).normalized();
                };
                emit objectSelectionTransformed(objectSelectionPage_, toUserRect(before),
                                                toUserRect(after));
            }
        }
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
            if (drawingCallout_) {
                // callout 的第一段结束：记下注释框，等下一次点击选箭头尖。
                drawingCallout_ = false;
                const QRectF box = QRectF(drawAnchor_, drawCurrent_).normalized();
                if (box.width() > 4.0 && box.height() > 4.0) {
                    calloutPage_ = pageIndex;
                    calloutBoxDisplay_ = box;
                    awaitingCalloutTip_ = true;
                    emit hintMessage(tr("点击页面选择箭头指向的位置"));
                }
                return;
            }
            if (drawingObjectSelection_ && QLineF(drawAnchor_, drawCurrent_).length() <
                                               4.0 / qMax(0.01, layout_.zoom())) {
                drawingObjectSelection_ = false;
                objectSelectionPage_ = pageIndex;
                emit objectPointSelected(pageIndex, a, objectSelectionAdditive_);
                updateToolCursor();
                return;
            }
            if (a != b) {
                const QRectF rect = QRectF(a, b).normalized();
                if (drawingObjectSelection_) {
                    drawingObjectSelection_ = false;
                    objectSelectionPage_ = pageIndex;
                    objectSelectionRect_ = QRectF(drawAnchor_, drawCurrent_).normalized();
                    emit objectRectSelected(pageIndex, rect);
                    updateToolCursor();
                    return;
                }
                if (drawingFormField_) {
                    const QString kind = formFieldKind_;
                    drawingFormField_ = false;
                    formFieldKind_.clear();
                    updateToolCursor();
                    emit formFieldRectRequested(kind, pageIndex, rect);
                } else if (drawingFreeText_) {
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

void PageCanvas::beginObjectSelection() {
    if (annotationController_ && annotationController_->tool() != AnnotationController::Tool::None)
        annotationController_->setTool(AnnotationController::Tool::None);
    objectEditMode_ = true;
    drawingObjectSelection_ = true;
    drawingSquare_ = false;
    drawingFormField_ = false;
    formFieldKind_.clear();
    viewport()->setCursor(Qt::CrossCursor);
}

void PageCanvas::setObjectSelection(int pageIndex, const QRectF& userSpaceRect) {
    PageTransform transform;
    if (!annotationController_ || !annotationController_->transformForPage(pageIndex, &transform)) return;
    objectSelectionPage_ = pageIndex;
    objectSelectionRect_ = transform.toDisplay(userSpaceRect).normalized();
    viewport()->update();
}

void PageCanvas::setObjectSnapGuides(const QVector<QRectF>& displayRects) {
    objectSnapRects_ = displayRects;
}

void PageCanvas::clearObjectSelection() {
    objectSelectionPage_ = -1;
    objectSelectionRect_ = {};
    objectSnapRects_.clear();
    draggingObjectSelection_ = false;
    if (inlineEditor_) {
        inlineEditor_->blockSignals(true);
        inlineEditor_->deleteLater();
        inlineEditor_ = nullptr;
        inlineEditPage_ = -1;
        inlineEditObject_ = -1;
    }
    viewport()->update();
}

void PageCanvas::setFormFieldPlacementKind(const QString& kind) {
    if (!kind.isEmpty()) {
        objectEditMode_ = false;
        drawingObjectSelection_ = false;
        clearObjectSelection();
    }
    formFieldKind_ = kind;
    drawingFormField_ = false;
    updateToolCursor();
}

void PageCanvas::wheelEvent(QWheelEvent* event) {
    const QPoint delta = event->angleDelta();
    switch (mouse::wheelGesture(event->modifiers(), delta)) {
        case mouse::WheelGesture::Zoom: {
            const int step = delta.y() != 0 ? delta.y() : delta.x();
            const double factor = mouse::wheelZoomFactor(step);
            if (qFuzzyCompare(factor, 1.0)) {
                event->accept();
                return;
            }
            // 指针下的页面坐标先记下来；缩放完由 PdfView 调 restoreZoomAnchor
            // 把同一个点放回指针下面，视觉上就是「以指针为中心缩放」。
            int pageIndex = -1;
            QPointF pagePoint;
            pagePointAt(event->position().toPoint(), &pageIndex, &pagePoint);
            emit zoomAtRequested(factor, pageIndex, pagePoint, event->position().toPoint());
            trace(QStringLiteral("wheel zoom factor=%1 page=%2").arg(factor).arg(pageIndex));
            event->accept();
            return;
        }
        case mouse::WheelGesture::HorizontalScroll: {
            const int step = delta.y() != 0 ? delta.y() : delta.x();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - step);
            event->accept();
            return;
        }
        case mouse::WheelGesture::VerticalScroll:
            break;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void PageCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    QAbstractScrollArea::mouseDoubleClickEvent(event);
    // Qt 的双击事件取代了第二次 press：连击计数在这里补成 2，
    // 下一次 press 才能数到 3（选行）与 4（选段）。
    if (event->button() == Qt::LeftButton) {
        clickCount_ = 2;
        lastClickMs_ = QDateTime::currentMSecsSinceEpoch();
        lastClickPos_ = event->pos();
    }
    // 多边形/云线/区域高亮：双击就是"收笔"。这条必须先于文字选词判定，
    // 否则双击最后一个顶点时会顺手选中一个词。
    if (polygonPoints_.size() >= 3 && polygonPage_ >= 0) {
        finishPolygonInput();
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
    if (objectEditMode_) {
        emit objectDoubleClicked(pageIndex, toUserSpacePoint(pageIndex, displayPoint));
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

int PageCanvas::registerClick(const QPoint& viewportPos) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int distance = (viewportPos - lastClickPos_).manhattanLength();
    clickCount_ = mouse::nextClickCount(clickCount_, now - lastClickMs_, distance,
                                        QApplication::doubleClickInterval(),
                                        QApplication::startDragDistance());
    lastClickMs_ = now;
    lastClickPos_ = viewportPos;
    return clickCount_;
}

bool PageCanvas::selectTextUnitAt(int clickCount, int pageIndex, int order) {
    if (textController_ == nullptr || clickCount < 3) {
        return false;
    }
    const TextLayout* textLayout = textController_->layout(pageIndex);
    if (textLayout == nullptr) {
        return false;
    }
    int start = 0;
    int end = 0;
    // 三击选行，四击选段。四击的段落是启发式：行距正常就连在一起。
    const bool selected = clickCount >= 4
                              ? textLayout->paragraphRangeAt(order, &start, &end)
                              : textLayout->lineRangeAt(order, &start, &end);
    if (!selected || end <= start) {
        return false;
    }
    selecting_ = false;
    textController_->setSelection(pageIndex, start, end);
    trace(QStringLiteral("click select page=%1 count=%2 range=%3..%4")
              .arg(pageIndex)
              .arg(clickCount)
              .arg(start)
              .arg(end));
    viewport()->update();
    return true;
}

void PageCanvas::restoreZoomAnchor(int pageIndex, const QPointF& pagePoint,
                                   const QPoint& viewportPoint) {
    if (pageIndex < 0 || pageIndex >= layout_.pageCount()) {
        return;
    }
    const double zoom = layout_.zoom();
    if (zoom <= 0.0) {
        return;
    }
    // 缩放后页面在内容坐标里的位置：页面左边 + 页内坐标 × 新缩放。
    const double contentX = pageContentLeft(pageIndex) + pagePoint.x() * zoom;
    const double contentY = layout_.pageTop(pageIndex) + pagePoint.y() * zoom;
    horizontalScrollBar()->setValue(mouse::anchoredScrollValue(
        contentX, viewportPoint.x(), horizontalScrollBar()->maximum()));
    verticalScrollBar()->setValue(mouse::anchoredScrollValue(
        contentY, viewportPoint.y(), verticalScrollBar()->maximum()));
    viewport()->update();
}

void PageCanvas::keyPressEvent(QKeyEvent* event) {
    // 回车收笔：与双击等价。逐点输入的工具（多边形 / 区域高亮 / 面积周长）
    // 都能用它结束，免得用户必须去够鼠标双击。
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        polygonPoints_.size() >= 3 && polygonPage_ >= 0) {
        finishPolygonInput();
        event->accept();
        return;
    }
    // ESC 取消箭头框的第二段：注释框已经拖好了，但用户改主意了。
    if (event->key() == Qt::Key_Escape && (awaitingCalloutTip_ || drawingCallout_)) {
        awaitingCalloutTip_ = false;
        drawingCallout_ = false;
        calloutBoxDisplay_ = QRectF();
        calloutPage_ = -1;
        viewport()->update();
        emit hintMessage(tr("已取消箭头框"));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && objectEditMode_) {
        objectEditMode_ = false;
        drawingObjectSelection_ = false;
        drawingSquare_ = false;
        clearObjectSelection();
        updateToolCursor();
        event->accept();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void PageCanvas::beginInlineTextEdit(int pageIndex, int objectIndex, const QRectF& displayRect,
                                     const QString& text, double fontSize) {
    if (inlineEditor_) {
        inlineEditor_->deleteLater();
        inlineEditor_ = nullptr;
    }
    inlineEditPage_ = pageIndex;
    inlineEditObject_ = objectIndex;
    inlineEditDisplayRect_ = displayRect;
    inlineEditor_ = new QPlainTextEdit(viewport());
    inlineEditor_->setPlainText(text);
    inlineEditor_->setTabChangesFocus(false);
    inlineEditor_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    QFont font = inlineEditor_->font();
    font.setPointSizeF(qMax(6.0, fontSize * layout_.zoom()));
    inlineEditor_->setFont(font);
    inlineEditor_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { border: 2px solid #4169C1; background: rgba(255,255,255,235); padding: 2px; }"));
    inlineEditor_->installEventFilter(this);
    repositionInlineEditor();
    inlineEditor_->show();
    inlineEditor_->setFocus();
    QTextCursor cursor = inlineEditor_->textCursor();
    cursor.movePosition(QTextCursor::End);
    inlineEditor_->setTextCursor(cursor);
}

bool PageCanvas::eventFilter(QObject* watched, QEvent* event) {
    // 右键菜单：视口上的 QContextMenuEvent，位置用视口坐标交给界面。
    if (watched == viewport() && event->type() == QEvent::ContextMenu) {
        auto* menuEvent = static_cast<QContextMenuEvent*>(event);
        emit contextMenuRequested(menuEvent->pos());
        menuEvent->accept();
        return true;
    }
    if (watched == inlineEditor_ && inlineEditor_) {
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                finishInlineTextEdit(false);
                return true;
            }
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
                key->modifiers().testFlag(Qt::ControlModifier)) {
                finishInlineTextEdit(true);
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            finishInlineTextEdit(true);
        }
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}

void PageCanvas::finishInlineTextEdit(bool commit) {
    if (!inlineEditor_) return;
    const QString text = inlineEditor_->toPlainText();
    const int page = inlineEditPage_;
    const int object = inlineEditObject_;
    inlineEditor_->removeEventFilter(this);
    inlineEditor_->deleteLater();
    inlineEditor_ = nullptr;
    inlineEditPage_ = -1;
    inlineEditObject_ = -1;
    if (commit) emit inlineTextEditCommitted(page, object, text);
}

void PageCanvas::repositionInlineEditor() {
    if (!inlineEditor_ || inlineEditPage_ < 0 || inlineEditPage_ >= layout_.pageCount()) return;
    const double zoom = layout_.zoom();
    const double scrollTop = verticalScrollBar()->value();
    const QRectF pageRect(pageLeft(inlineEditPage_), layout_.pageTop(inlineEditPage_) - scrollTop,
                          layout_.pageRect(inlineEditPage_).width(),
                          layout_.pageRect(inlineEditPage_).height());
    QRectF rect = toViewportRect(inlineEditDisplayRect_, pageRect, zoom).adjusted(-3, -3, 3, 3);
    rect.setWidth(qMax(80.0, rect.width()));
    rect.setHeight(qMax(56.0, rect.height()));
    inlineEditor_->setGeometry(rect.toAlignedRect());
}

void PageCanvas::updateToolCursor() {
    const bool annotationTool = annotationController_ != nullptr &&
                                annotationController_->tool() != AnnotationController::Tool::None;
    if (annotationTool && objectEditMode_) {
        objectEditMode_ = false;
        drawingObjectSelection_ = false;
        clearObjectSelection();
    }
    const bool drawing = drawingObjectSelection_ || !formFieldKind_.isEmpty() ||
                         annotationTool;
    if (drawing) {
        viewport()->setCursor(Qt::CrossCursor);
        return;
    }
    // 手型工具开着：光标是「抓手」。
    if (panMode_) {
        viewport()->setCursor(Qt::OpenHandCursor);
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

double PageCanvas::pageContentLeft(int index) const {
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

double PageCanvas::pageLeft(int index) const {
    // 内容坐标减去横向滚动偏移，就是页面在视口里的左边缘。
    return pageContentLeft(index) - horizontalScrollBar()->value();
}

QRectF PageCanvas::pageRectInViewport(int index) const {
    if (index < 0 || index >= layout_.pageCount()) {
        return QRectF();
    }
    const QSizeF size = layout_.pageSize(index);
    const double top = layout_.pageTop(index) - verticalScrollBar()->value();
    return QRectF(pageLeft(index), top, size.width(), size.height());
}

// 标尺：一横一竖，贴在视口的上面与左边。
//
// 刻度网格锚在**当前页的绝对坐标**上：视口左边缘落在页面上的位置是
// -page.left()/zoom（可能是负数），把它当作标尺的起点传给 rulerTicks，
// 领域层算出的刻度就自动对齐到绝对网格上 —— 页面上 10mm 的位置永远显示 10mm。
void PageCanvas::paintRulers(QPainter& painter, int pageIndex) {
    if (layout_.isEmpty() || pageIndex < 0 || pageIndex >= layout_.pageCount()) {
        return;
    }
    const double zoom = layout_.zoom();
    const QRectF page = pageRectInViewport(pageIndex);
    const double viewportWidth = viewport()->width();
    const double viewportHeight = viewport()->height();

    painter.save();
    QFont font = painter.font();
    font.setPointSizeF(7.0);
    painter.setFont(font);
    painter.setBrush(QColor(246, 246, 248, 235));
    painter.setPen(QPen(QColor(150, 152, 158), 1.0));
    painter.drawRect(QRectF(0.0, 0.0, viewportWidth, kRulerThickness));
    painter.drawRect(QRectF(0.0, 0.0, kRulerLabelWidth, viewportHeight));

    // 横杆。
    const double horizontalOrigin = -page.left() / zoom;
    const QVector<viewmodes::RulerTick> horizontalTicks =
        viewmodes::rulerTicks(horizontalOrigin, viewportWidth / zoom, rulerUnit_);
    double lastLabelRight = kRulerLabelWidth;
    for (const viewmodes::RulerTick& tick : horizontalTicks) {
        const double absolute = horizontalOrigin + tick.position;
        const double x = page.left() + absolute * zoom;
        if (x < kRulerLabelWidth || x > viewportWidth) {
            continue;
        }
        painter.setPen(QPen(QColor(150, 152, 158), 1.0));
        painter.drawLine(QPointF(x, tick.major ? 1.0 : kRulerThickness - 6.0),
                         QPointF(x, kRulerThickness));
        if (tick.major && !tick.label.isEmpty()) {
            const QRectF box(x + 2.0, 1.0, 120.0, kRulerThickness - 2.0);
            // 缩得太小时刻度会挤在一起，标签只画第一个不重叠的。
            if (box.left() >= lastLabelRight) {
                painter.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, tick.label);
                lastLabelRight = box.left() + 46.0;
            }
        }
    }

    // 竖杆。
    const double verticalOrigin = -page.top() / zoom;
    const QVector<viewmodes::RulerTick> verticalTicks =
        viewmodes::rulerTicks(verticalOrigin, viewportHeight / zoom, rulerUnit_);
    double lastLabelBottom = -1000.0;
    for (const viewmodes::RulerTick& tick : verticalTicks) {
        const double absolute = verticalOrigin + tick.position;
        const double y = page.top() + absolute * zoom;
        if (y < kRulerThickness || y > viewportHeight) {
            continue;
        }
        painter.setPen(QPen(QColor(150, 152, 158), 1.0));
        painter.drawLine(QPointF(tick.major ? 1.0 : kRulerLabelWidth - 6.0, y),
                         QPointF(kRulerLabelWidth, y));
        if (tick.major && !tick.label.isEmpty()) {
            const QRectF box(2.0, y + 1.0, kRulerLabelWidth - 4.0, 12.0);
            if (box.top() >= lastLabelBottom) {
                painter.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, tick.label);
                lastLabelBottom = box.bottom() + 2.0;
            }
        }
    }
    painter.restore();
}

void PageCanvas::startAutoScroll() {
    if (!autoScrollEnabled_ || autoScrollTimer_ == nullptr) {
        return;
    }
    if (!autoScrollTimer_->isActive()) {
        autoScrollTimer_->start();
    }
}

void PageCanvas::stopAutoScroll() {
    autoScrollVelocity_ = 0.0;
    if (autoScrollTimer_ != nullptr) {
        autoScrollTimer_->stop();
    }
}

void PageCanvas::updateAutoScroll(int pointerViewportY) {
    if (!autoScrollEnabled_) {
        autoScrollVelocity_ = 0.0;
        return;
    }
    autoScrollPointerY_ = pointerViewportY;
    // 这里传的是「视口像素」：领域层只负责「贴边越近越快」的换算，
    // 具体的像素/秒由界面决定。
    const viewmodes::AutoScrollState state =
        viewmodes::autoScrollState(pointerViewportY, viewport()->height(),
                                   kAutoScrollEdgeMargin, kAutoScrollMaxVelocity);
    // 参数非法时领域层返回「不滚动」，这里照着不滚，不硬滚。
    autoScrollVelocity_ = state.active ? state.velocity : 0.0;
    // 指针刚挪到边缘时速度还是 0（那时定时器已经停了），这里要把它重新叫醒，
    // 否则「拖到边缘自动滚」在指针先停在中间、再移向边缘时不会生效。
    if (autoScrollVelocity_ != 0.0 && autoScrollTimer_ != nullptr &&
        !autoScrollTimer_->isActive()) {
        autoScrollTimer_->start();
    }
}

void PageCanvas::onAutoScrollTick() {
    if (autoScrollVelocity_ == 0.0) {
        autoScrollTimer_->stop();
        return;
    }
    const double seconds = static_cast<double>(autoScrollTimer_->interval()) / 1000.0;
    const int dy = static_cast<int>(autoScrollVelocity_ * seconds);
    if (dy != 0) {
        scrollContentsBy(0, dy);
    }
}

void PageCanvas::startPageTransition() {
    if (pageTransition_ == viewmodes::PageTransition::None || transitionTimer_ == nullptr) {
        return;
    }
    if (viewport()->width() <= 0 || viewport()->height() <= 0) {
        return;
    }
    // 先把旧画面抓下来：翻页之后画在上面的就是这一张，
    // 底下露出来的是新页 —— 淡出与滑动都是它在做。
    transitionSnapshot_ = viewport()->grab();
    if (transitionSnapshot_.isNull()) {
        return;
    }
    transitionElapsedMs_ = 0;
    transitionTimer_->start();
}

void PageCanvas::onTransitionTick() {
    transitionElapsedMs_ += transitionTimer_->interval();
    if (transitionElapsedMs_ >= kTransitionDurationMs) {
        transitionTimer_->stop();
        transitionSnapshot_ = QPixmap();
    }
    viewport()->update();
}

// 逐点输入的收尾：多边形 / 云线 / 区域高亮 / 面积周长共用。
void PageCanvas::finishPolygonInput() {
    if (polygonPoints_.size() < 3 || polygonPage_ < 0) {
        polygonPoints_.clear();
        polygonPage_ = -1;
        polygonIsRegionHighlight_ = false;
        viewport()->update();
        return;
    }
    QVector<QPointF> userPoints;
    userPoints.reserve(polygonPoints_.size());
    for (const QPointF& point : polygonPoints_) {
        userPoints.append(toUserSpacePoint(polygonPage_, point));
    }
    const int page = polygonPage_;
    const auto type = polygonType_;
    const bool region = polygonIsRegionHighlight_;
    polygonPoints_.clear();
    polygonPage_ = -1;
    polygonIsRegionHighlight_ = false;
    viewport()->update();
    if (region) {
        emit regionHighlightRequested(page, userPoints);
        return;
    }
    if (measureMode_ != MeasureMode::Distance) {
        const auto mode = measureMode_;
        measureMode_ = MeasureMode::Distance;
        emit measureShapeRequested(page, static_cast<int>(mode), userPoints);
        return;
    }
    emit polygonAnnotationRequested(page, static_cast<int>(type), userPoints);
}

}  // namespace aipdf::app
