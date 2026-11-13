#include "app/PdfView.h"

#include "app/PageCanvas.h"
#include "app/RenderThread.h"
#include "app/TextController.h"
#include "app/NavigationController.h"
#include "app/AnnotationController.h"
#include "pdf/Annotation.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QShortcut>
#include <QSizePolicy>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace aipdf::app {

namespace {
constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 8.0;
constexpr double kZoomStep = 1.25;
// 缓存上限 128 MiB，按像素字节计。
constexpr qint64 kCacheBytes = 128LL * 1024 * 1024;

// 验收探针用的轨迹文件：复制动作要能被脚本看到。
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
}  // namespace

PdfView::PdfView(QWidget* parent) : QWidget(parent), cache_(kCacheBytes) {
    buildUi();
}

PdfView::~PdfView() {
    // 必须先停渲染线程再放掉文档：
    // PDFium 的句柄只能在创建它的线程里关闭。
    stopRenderThread();
}

void PdfView::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolBar = new QToolBar(this);
    toolBar->setObjectName(QStringLiteral("documentToolBar"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto* zoomOut = new QToolButton(this);
    zoomOut->setText(QStringLiteral("−"));
    zoomOut->setToolTip(QStringLiteral("缩小 (Ctrl+-)"));
    connect(zoomOut, &QToolButton::clicked, this, &PdfView::zoomOut);

    auto* zoomIn = new QToolButton(this);
    zoomIn->setText(QStringLiteral("+"));
    zoomIn->setToolTip(QStringLiteral("放大 (Ctrl++)"));
    connect(zoomIn, &QToolButton::clicked, this, &PdfView::zoomIn);

    auto* fitWidthButton = new QToolButton(this);
    fitWidthButton->setText(QStringLiteral("适宽"));
    fitWidthButton->setToolTip(QStringLiteral("缩放到适应窗口宽度"));
    connect(fitWidthButton, &QToolButton::clicked, this, &PdfView::zoomToFitWidth);

    auto* fitPageButton = new QToolButton(this);
    fitPageButton->setText(QStringLiteral("整页"));
    fitPageButton->setToolTip(QStringLiteral("缩放到整页可见"));
    connect(fitPageButton, &QToolButton::clicked, this, &PdfView::zoomToFitPage);

    auto* rotateLeftButton = new QToolButton(this);
    rotateLeftButton->setText(QStringLiteral("↺"));
    rotateLeftButton->setToolTip(QStringLiteral("向左旋转"));
    connect(rotateLeftButton, &QToolButton::clicked, this, &PdfView::rotateLeft);

    auto* rotateRightButton = new QToolButton(this);
    rotateRightButton->setText(QStringLiteral("↻"));
    rotateRightButton->setToolTip(QStringLiteral("向右旋转"));
    connect(rotateRightButton, &QToolButton::clicked, this, &PdfView::rotateRight);

    zoomLabel_ = new QLabel(QStringLiteral("100%"), this);
    zoomLabel_->setObjectName(QStringLiteral("secondaryText"));
    zoomLabel_->setMinimumWidth(56);
    zoomLabel_->setAlignment(Qt::AlignCenter);

    pageInfoLabel_ = new QLabel(QStringLiteral("未打开文档"), this);
    pageInfoLabel_->setObjectName(QStringLiteral("secondaryText"));
    pageInfoLabel_->setAlignment(Qt::AlignCenter);

    toolBar->addWidget(zoomOut);
    toolBar->addWidget(zoomIn);
    toolBar->addWidget(fitWidthButton);
    toolBar->addWidget(fitPageButton);
    toolBar->addSeparator();
    toolBar->addWidget(rotateLeftButton);
    toolBar->addWidget(rotateRightButton);
    toolBar->addSeparator();
    toolBar->addWidget(zoomLabel_);
    auto* spacer = new QWidget(toolBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(spacer);
    toolBar->addSeparator();
    toolBar->addWidget(pageInfoLabel_);
    layout->addWidget(toolBar);

    buildAnnotationToolBar();

    auto* zoomInShortcut = new QShortcut(QKeySequence::ZoomIn, this);
    connect(zoomInShortcut, &QShortcut::activated, this, &PdfView::zoomIn);
    auto* zoomOutShortcut = new QShortcut(QKeySequence::ZoomOut, this);
    connect(zoomOutShortcut, &QShortcut::activated, this, &PdfView::zoomOut);

    // 复制只在本视图内生效：焦点在搜索框时 Ctrl+C 仍然复制输入框内容。
    auto* copyShortcut = new QShortcut(QKeySequence::Copy, this);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated, this, &PdfView::copySelection);

    canvas_ = new PageCanvas(this);
    canvas_->setTileCache(&cache_);
    connect(canvas_, &PageCanvas::renderRequested, this, &PdfView::requestVisiblePages);
    connect(canvas_, &PageCanvas::currentPageChanged, this, [this](int index) {
        updatePageInfo();
        emit currentPageChanged(index);
    });
    layout->addWidget(canvas_, 1);

    // 指标定期刷新：渲染是异步的，缓存占用和队列长度都在变化。
    metricsTimer_ = new QTimer(this);
    metricsTimer_->setInterval(400);
    connect(metricsTimer_, &QTimer::timeout, this, &PdfView::updateMetrics);
    metricsTimer_->start();

    updateZoomLabel();
    updateMetrics();
}

// 批注工具栏：三种基于选区的标记 + 三种画布工具 + 撤销重做。
//
// 按钮给独立的对象名：验收脚本靠 AutomationId 定位，
// 靠按钮文字会随主题改版一起失效。
void PdfView::buildAnnotationToolBar() {
    auto* toolBar = new QToolBar(this);
    toolBar->setObjectName(QStringLiteral("annotationToolBar"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto makeButton = [toolBar](const QString& text,
                                const QString& tip,
                                const QString& objectName) {
        auto* button = new QToolButton(toolBar);
        button->setText(text);
        button->setToolTip(tip);
        button->setObjectName(objectName);
        return button;
    };

    highlightButton_ = makeButton(QStringLiteral("高亮"),
                                   QStringLiteral("给选中的文字加高亮 (Ctrl+H)"),
                                   QStringLiteral("annotation.highlight"));
    underlineButton_ = makeButton(QStringLiteral("下划线"),
                                   QStringLiteral("给选中的文字加下划线 (Ctrl+U)"),
                                   QStringLiteral("annotation.underline"));
    strikeOutButton_ = makeButton(QStringLiteral("删除线"),
                                  QStringLiteral("给选中的文字加删除线"),
                                  QStringLiteral("annotation.strikethrough"));
    noteButton_ = makeButton(QStringLiteral("便签"),
                              QStringLiteral("在页面上放一个便签"),
                              QStringLiteral("annotation.note"));
    squareButton_ = makeButton(QStringLiteral("矩形"),
                                QStringLiteral("在页面上画一个矩形"),
                                QStringLiteral("annotation.square"));
    inkButton_ = makeButton(QStringLiteral("墨迹"),
                             QStringLiteral("在页面上画一笔"),
                             QStringLiteral("annotation.ink"));
    undoButton_ = makeButton(QStringLiteral("撤销"),
                              QStringLiteral("撤销上一步批注操作 (Ctrl+Z)"),
                              QStringLiteral("annotation.undo"));
    redoButton_ = makeButton(QStringLiteral("重做"),
                              QStringLiteral("重做上一步批注操作 (Ctrl+Shift+Z)"),
                              QStringLiteral("annotation.redo"));

    toolBar->addWidget(highlightButton_);
    toolBar->addWidget(underlineButton_);
    toolBar->addWidget(strikeOutButton_);
    toolBar->addSeparator();
    toolBar->addWidget(noteButton_);
    toolBar->addWidget(squareButton_);
    toolBar->addWidget(inkButton_);
    toolBar->addSeparator();
    toolBar->addWidget(undoButton_);
    toolBar->addWidget(redoButton_);

    connect(highlightButton_, &QToolButton::clicked, this, [this]() {
        applyMarkAnnotation(pdf::AnnotationType::Highlight);
    });
    connect(underlineButton_, &QToolButton::clicked, this, [this]() {
        applyMarkAnnotation(pdf::AnnotationType::Underline);
    });
    connect(strikeOutButton_, &QToolButton::clicked, this, [this]() {
        applyMarkAnnotation(pdf::AnnotationType::StrikeOut);
    });

    // 画布工具是互斥的：再点一次同一个按钮就退出工具模式。
    noteButton_->setCheckable(true);
    squareButton_->setCheckable(true);
    inkButton_->setCheckable(true);
    noteButton_->setAutoExclusive(true);
    squareButton_->setAutoExclusive(true);
    inkButton_->setAutoExclusive(true);
    connect(noteButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Note
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(squareButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Square
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(inkButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Ink
                                                   : AnnotationController::Tool::None);
        }
    });

    connect(undoButton_, &QToolButton::clicked, this, &PdfView::undoAnnotation);
    connect(redoButton_, &QToolButton::clicked, this, &PdfView::redoAnnotation);

    // 撤销 / 重做快捷键：放在视图上，焦点在输入框时也能用。
    auto* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, &PdfView::undoAnnotation);
    auto* redoShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")), this);
    connect(redoShortcut, &QShortcut::activated, this, &PdfView::redoAnnotation);

    // 工具栏要挂在文档工具栏下面。
    auto* layout = qobject_cast<QVBoxLayout*>(this->layout());
    if (layout != nullptr) {
        layout->insertWidget(1, toolBar);
    }
    updateAnnotationToolBar();
}

bool PdfView::load(const QString& path, QString* errorMessage) {    const pdf::PdfOpenResult result = pdf::openDocument(path);
    if (!result.document.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorMessage;
        }
        return false;
    }

    setDocument(result.document, path);
    return true;
}

void PdfView::setDocument(pdf::PdfDocument document, const QString& path) {
    stopRenderThread();

    document_ = std::move(document);
    filePath_ = path;
    pageCount_ = document_.isValid() ? document_.pageCount() : 0;
    zoom_ = 1.0;
    userRotation_ = 0;
    fitToWidth_ = false;
    fitToPage_ = false;
    cache_.clear();

    // 文本控制器自己持有一份独立句柄，在后台线程做提取与搜索。
    textController_ = std::make_unique<TextController>(document_);
    canvas_->setTextController(textController_.get());
    // 选区、搜索命中与新到的文本层都要让画布重画。
    connect(textController_.get(), &TextController::selectionChanged, this, [this]() {
        canvas_->viewport()->update();
        // 选区变了，标记类批注按钮的可用状态也跟着变。
        updateAnnotationToolBar();
    });
    connect(textController_.get(), &TextController::searchHitsFound, this, [this](int) {
        canvas_->viewport()->update();
    });
    connect(textController_.get(), &TextController::searchFinished, this, [this](bool) {
        canvas_->viewport()->update();
    });
    connect(textController_.get(), &TextController::textPageReady, this, [this](int) {
        canvas_->viewport()->update();
    });

    // 导航控制器同样自己持有一份独立句柄，在后台线程取大纲与链接。
    navigationController_ = std::make_unique<NavigationController>(document_);
    canvas_->setNavigationController(navigationController_.get());
    connect(canvas_, &PageCanvas::linkActivated, this,
            [this](int pageIndex, int targetPage, const QString& uri) {
                if (targetPage >= 0) {
                    // 内部跳转：滚到目标页。
                    trace(QStringLiteral("link goto from=%1 to=%2").arg(pageIndex).arg(targetPage));
                    goToPage(targetPage);
                    emit linkFollowed(targetPage, QString());
                    return;
                }
                if (uri.isEmpty()) {
                    return;
                }
                // 外部链接交给系统浏览器：用户点了就是授权。
                // 验收探针可以要求不真的拉起浏览器（会打断脚本），
                // 但动作本身必须留在轨迹里。
                const bool suppressed = qEnvironmentVariableIsSet("AIPDF_SUPPRESS_EXTERNAL_LINKS");
                trace(QStringLiteral("link external uri=%1 suppressed=%2")
                          .arg(uri)
                          .arg(suppressed ? 1 : 0));
                if (!suppressed) {
                    QDesktopServices::openUrl(QUrl(uri));
                }
                emit linkFollowed(-1, uri);
            });

    // 批注控制器：持有批注模型、撤销栈，并把批注写进文档。
    annotationController_ = std::make_unique<AnnotationController>(document_);
    canvas_->setAnnotationController(annotationController_.get());
    connect(annotationController_.get(), &AnnotationController::annotationsChanged, this,
            [this](int) {
                canvas_->viewport()->update();
                emit annotationsChanged(annotationController_->annotationCount());
            });
    connect(annotationController_.get(), &AnnotationController::undoStateChanged, this,
            &PdfView::updateAnnotationToolBar);
    connect(annotationController_.get(), &AnnotationController::toolChanged, this, [this]() {
        const auto tool = annotationController_->tool();
        noteButton_->setChecked(tool == AnnotationController::Tool::Note);
        squareButton_->setChecked(tool == AnnotationController::Tool::Square);
        inkButton_->setChecked(tool == AnnotationController::Tool::Ink);
    });

    // 画布上的批注工具：便签要问一句内容，矩形与墨迹直接生成。
    connect(canvas_, &PageCanvas::noteAnnotationRequested, this,
            [this](int pageIndex, const QPointF& userPoint) {
                if (!annotationController_) {
                    return;
                }
                bool accepted = false;
                const QString contents = QInputDialog::getText(
                    this,
                    tr("新建便签"),
                    tr("备注内容"),
                    QLineEdit::Normal,
                    QString(),
                    &accepted);
                if (!accepted || contents.isEmpty()) {
                    trace(QStringLiteral("annotation note cancelled"));
                    return;
                }
                const QRectF rect(userPoint.x() - kNoteAnnotationSize / 2.0,
                                  userPoint.y() - kNoteAnnotationSize / 2.0,
                                  kNoteAnnotationSize,
                                  kNoteAnnotationSize);
                annotationController_->addNote(pageIndex, rect, contents);
            });
    connect(canvas_, &PageCanvas::squareAnnotationRequested, this,
            [this](int pageIndex, const QRectF& userRect) {
                if (annotationController_) {
                    annotationController_->addSquare(pageIndex, userRect);
                }
            });
    connect(canvas_, &PageCanvas::inkAnnotationRequested, this,
            [this](int pageIndex, const QVector<QPointF>& stroke) {
                if (annotationController_) {
                    annotationController_->addInk(pageIndex, stroke);
                }
            });

    // 采集每页的显示尺寸（页面自带 /Rotate 已经应用），布局服务据此排版。
    QVector<PageLayout::PageInfo> pages;
    pages.reserve(pageCount_);
    for (int index = 0; index < pageCount_; ++index) {
        PageLayout::PageInfo info;
        const auto page = document_.page(index);
        if (page.has_value()) {
            info.widthPoints = page->displayWidthPoints();
            info.heightPoints = page->displayHeightPoints();
        }
        pages.append(info);
    }
    layout_.setPages(pages);
    layout_.setUserRotation(userRotation_);
    layout_.setScale(zoom_);

    // 渲染线程自己开一份独立句柄（PDFium 句柄不能跨线程用）。
    renderThread_ = new RenderThread(document_, this);
    connect(renderThread_, &RenderThread::pageRendered, this,
            [this](int index, qint64 zoomKey, int rotation, const QImage& image) {
                // 只接受当前缩放与旋转的结果，旧的直接丢弃。
                const qint64 currentKey =
                    TileCache::zoomToKey(layout_.zoom() * canvas_->devicePixelRatioF());
                if (zoomKey != currentKey || rotation != layout_.userRotation() / 90) {
                    return;
                }
                cache_.insert(TileCache::Key{index, zoomKey, rotation}, image);
                canvas_->viewport()->update();
            });
    connect(renderThread_, &RenderThread::pageFailed, this, [](int, qint64, int) {});
    renderThread_->start();

    canvas_->setLayout(layout_);
    emit pageCountChanged(pageCount_);
    updateZoomLabel();
    updatePageInfo();
    // 文档换新了：批注工具栏的可用状态要跟着重算（工具可用、标记类按钮要等选区）。
    updateAnnotationToolBar();
    requestVisiblePages();
}

void PdfView::clear() {
    stopRenderThread();
    canvas_->setTextController(nullptr);
    canvas_->setNavigationController(nullptr);
    canvas_->setAnnotationController(nullptr);
    textController_.reset();
    navigationController_.reset();
    annotationController_.reset();
    document_ = pdf::PdfDocument();
    filePath_.clear();
    pageCount_ = 0;
    layout_.clear();
    cache_.clear();
    canvas_->setLayout(layout_);
    emit pageCountChanged(0);
    updateZoomLabel();
    updatePageInfo();
    // 没有文档了：批注工具一律不可用。
    updateAnnotationToolBar();
}

int PdfView::pageCount() const {
    return pageCount_;
}

int PdfView::currentPage() const {
    return canvas_ != nullptr ? canvas_->currentPage() : 0;
}

double PdfView::zoom() const {
    return zoom_;
}

QString PdfView::filePath() const {
    return filePath_;
}

QImage PdfView::renderThumbnail(int pageIndex, int targetWidth) const {
    const auto page = document_.page(pageIndex);
    if (!page.has_value() || targetWidth <= 0) {
        return {};
    }
    const double width = std::max(1.0, page->displayWidthPoints());
    return page->render(static_cast<double>(targetWidth) / width);
}

qint64 PdfView::cacheBytes() const {
    return cache_.bytes();
}

int PdfView::pendingRenders() const {
    return renderThread_ != nullptr ? renderThread_->pendingCount() : 0;
}

TextController* PdfView::textController() const {
    return textController_.get();
}

NavigationController* PdfView::navigationController() const {
    return navigationController_.get();
}

AnnotationController* PdfView::annotationController() const {
    return annotationController_.get();
}

void PdfView::setZoom(double factor) {
    const double clamped = std::clamp(factor, kMinZoom, kMaxZoom);
    if (qFuzzyCompare(clamped, zoom_)) {
        return;
    }
    zoom_ = clamped;
    fitToWidth_ = false;
    fitToPage_ = false;
    rebuildLayout();
    emit zoomChanged(zoom_);
}

void PdfView::zoomIn() {
    setZoom(zoom_ * kZoomStep);
}

void PdfView::zoomOut() {
    setZoom(zoom_ / kZoomStep);
}

void PdfView::zoomToFitWidth() {
    fitToWidth_ = true;
    fitToPage_ = false;
    zoom_ = fitZoomForWidth();
    rebuildLayout();
    emit zoomChanged(zoom_);
}

void PdfView::zoomToFitPage() {
    fitToPage_ = true;
    fitToWidth_ = false;
    zoom_ = fitZoomForPage();
    rebuildLayout();
    emit zoomChanged(zoom_);
}

void PdfView::rotateRight() {
    userRotation_ = (userRotation_ + 90) % 360;
    layout_.setUserRotation(userRotation_);
    if (textController_) {
        textController_->setUserRotation(userRotation_);
    }
    if (navigationController_) {
        navigationController_->setUserRotation(userRotation_);
    }
    if (annotationController_) {
        annotationController_->setUserRotation(userRotation_);
    }
    cache_.clear();
    canvas_->setLayout(layout_);
    updateZoomLabel();
    requestVisiblePages();
}

void PdfView::rotateLeft() {
    userRotation_ = (userRotation_ + 270) % 360;
    layout_.setUserRotation(userRotation_);
    if (textController_) {
        textController_->setUserRotation(userRotation_);
    }
    if (navigationController_) {
        navigationController_->setUserRotation(userRotation_);
    }
    if (annotationController_) {
        annotationController_->setUserRotation(userRotation_);
    }
    cache_.clear();
    canvas_->setLayout(layout_);
    updateZoomLabel();
    requestVisiblePages();
}

void PdfView::copySelection() {
    if (!textController_) {
        return;
    }
    const QString text = textController_->selectedText();
    if (text.isEmpty()) {
        return;
    }
    QGuiApplication::clipboard()->setText(text);
    trace(QStringLiteral("copy chars=%1 text=%2").arg(text.size()).arg(text));
    emit textCopied(text.size());
}

void PdfView::goToPage(int index) {
    if (canvas_ != nullptr) {
        canvas_->goToPage(index);
    }
}

void PdfView::applyMarkAnnotation(pdf::AnnotationType type) {
    if (!annotationController_ || !textController_) {
        return;
    }
    const int pageIndex = textController_->selectionPage();
    if (pageIndex < 0) {
        emit statusMessage(tr("请先选中要标记的文字"));
        return;
    }
    const QVector<QRectF> displayRects = textController_->selectionRects();
    if (displayRects.isEmpty()) {
        emit statusMessage(tr("请先选中要标记的文字"));
        return;
    }
    // 选区矩形在显示空间，批注坐标要的是用户空间。
    const QVector<QRectF> quads = annotationController_->toUserSpaceRects(pageIndex, displayRects);
    if (annotationController_->addMarkAnnotation(type, pageIndex, quads) == 0) {
        emit statusMessage(tr("批注没有添加成功"));
        return;
    }
    emit statusMessage(tr("已添加%1").arg(pdf::annotationTypeName(type)));
}

void PdfView::undoAnnotation() {
    if (!annotationController_) {
        return;
    }
    if (!annotationController_->undo()) {
        emit statusMessage(tr("没有可撤销的批注操作"));
    }
}

void PdfView::redoAnnotation() {
    if (!annotationController_) {
        return;
    }
    if (!annotationController_->redo()) {
        emit statusMessage(tr("没有可重做的批注操作"));
    }
}

void PdfView::updateAnnotationToolBar() {
    const bool hasDocument = annotationController_ != nullptr;
    const bool hasSelection = hasDocument && textController_ != nullptr &&
                              textController_->selectionPage() >= 0 &&
                              !textController_->selectionRects().isEmpty();

    highlightButton_->setEnabled(hasSelection);
    underlineButton_->setEnabled(hasSelection);
    strikeOutButton_->setEnabled(hasSelection);
    noteButton_->setEnabled(hasDocument);
    squareButton_->setEnabled(hasDocument);
    inkButton_->setEnabled(hasDocument);
    undoButton_->setEnabled(hasDocument && annotationController_->canUndo());
    redoButton_->setEnabled(hasDocument && annotationController_->canRedo());
    undoButton_->setToolTip(hasDocument && annotationController_->canUndo()
                                ? annotationController_->undoText()
                                : tr("撤销上一步批注操作 (Ctrl+Z)"));
    redoButton_->setToolTip(hasDocument && annotationController_->canRedo()
                                ? annotationController_->redoText()
                                : tr("重做上一步批注操作 (Ctrl+Shift+Z)"));
}

void PdfView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fitToWidth_ || fitToPage_) {
        const double target = fitToWidth_ ? fitZoomForWidth() : fitZoomForPage();
        if (!qFuzzyCompare(target, zoom_)) {
            zoom_ = target;
            rebuildLayout();
            emit zoomChanged(zoom_);
        }
    }
}

void PdfView::rebuildLayout() {
    layout_.setScale(zoom_);
    canvas_->setLayout(layout_);
    updateZoomLabel();
    requestVisiblePages();
}

void PdfView::updateZoomLabel() {
    zoomLabel_->setText(QStringLiteral("%1%").arg(qRound(zoom_ * 100.0)));
}

void PdfView::updatePageInfo() {
    if (pageCount_ <= 0) {
        pageInfoLabel_->setText(QStringLiteral("未打开文档"));
        return;
    }
    pageInfoLabel_->setText(QStringLiteral("第 %1 / %2 页")
                                .arg(currentPage() + 1)
                                .arg(pageCount_));
}

void PdfView::updateMetrics() {
    const qint64 bytes = cache_.bytes();
    const int pending = pendingRenders();
    if (bytes == lastCacheBytes_ && pending == lastPending_) {
        return;
    }
    lastCacheBytes_ = bytes;
    lastPending_ = pending;
    emit metricsChanged(QStringLiteral("缓存 %1 MiB · 待渲染 %2")
                            .arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1)
                            .arg(pending));
}

void PdfView::requestVisiblePages() {
    if (renderThread_ == nullptr || canvas_ == nullptr || layout_.isEmpty()) {
        return;
    }
    int first = 0;
    int last = -1;
    canvas_->visiblePageRange(&first, &last);
    if (last < first) {
        return;
    }
    // 可见页的链接注释要提前取回：鼠标停在页面上时才能立刻判定热区。
    if (navigationController_) {
        for (int index = first; index <= last; ++index) {
            navigationController_->requestPageLinks(index);
        }
    }
    const double devicePixelRatio = canvas_->devicePixelRatioF();
    renderThread_->requestRange(
        first, last, zoom_ * devicePixelRatio, layout_.userRotation() / 90);
}

void PdfView::stopRenderThread() {
    if (renderThread_ == nullptr) {
        return;
    }
    renderThread_->cancel();
    renderThread_->wait();
    delete renderThread_;
    renderThread_ = nullptr;
}

double PdfView::fitZoomForWidth() const {
    if (canvas_ == nullptr || layout_.pageCount() <= 0) {
        return zoom_;
    }
    const QSizeF size = layout_.pageSize(0);
    if (size.width() <= 0.0) {
        return zoom_;
    }
    // layout 里的尺寸是「点 × 当前缩放」，反推回去就是适应宽度需要的缩放。
    const double available = std::max(1.0, static_cast<double>(canvas_->viewport()->width()) - 32.0);
    return std::clamp(available / (size.width() / zoom_), kMinZoom, kMaxZoom);
}

double PdfView::fitZoomForPage() const {
    if (canvas_ == nullptr || layout_.pageCount() <= 0) {
        return zoom_;
    }
    const QSizeF size = layout_.pageSize(0);
    if (size.width() <= 0.0 || size.height() <= 0.0) {
        return zoom_;
    }
    const double availableWidth = std::max(1.0, static_cast<double>(canvas_->viewport()->width()) - 32.0);
    const double availableHeight = std::max(1.0, static_cast<double>(canvas_->viewport()->height()) - 32.0);
    const double byWidth = availableWidth / (size.width() / zoom_);
    const double byHeight = availableHeight / (size.height() / zoom_);
    return std::clamp(std::min(byWidth, byHeight), kMinZoom, kMaxZoom);
}

}  // namespace aipdf::app
