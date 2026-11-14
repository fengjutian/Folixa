#include "app/PdfView.h"

#include "app/PageCanvas.h"
#include "app/RenderThread.h"
#include "app/TextController.h"
#include "app/NavigationController.h"
#include "app/AnnotationController.h"
#include "app/IndexController.h"
#include "app/DocumentQaController.h"
#include "app/PageTransform.h"
#include "doc/DocumentChunk.h"
#include "doc/RecoveryLog.h"
#include "pdf/Annotation.h"
#include "pdf/DocumentWriter.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
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
// 工具栏图标的绘制尺寸：设计规范是 20px 画布，按钮上按 18px 排。
constexpr int kToolBarIconPixels = 18;

// 工具栏图标一律走资源里的 SVG（src/app/icons，20×20 Rounded Linear）。
QIcon toolbarIcon(const char* name) {
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(name)));
}

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
    toolBar->setIconSize(QSize(kToolBarIconPixels, kToolBarIconPixels));
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // 缩放与旋转是纯图标按钮：文字只留在 Tooltip 和可访问名称里，
    // 键盘与读屏用户靠这两处仍能知道按钮是干什么的。
    auto makeIconButton = [this](const char* iconName,
                                 const QString& tip,
                                 const QString& accessibleName,
                                 const QString& objectName) {
        auto* button = new QToolButton(this);
        button->setIcon(toolbarIcon(iconName));
        button->setToolTip(tip);
        button->setAccessibleName(accessibleName);
        button->setObjectName(objectName);
        button->setProperty("iconOnly", true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        return button;
    };

    auto* zoomOut = makeIconButton("zoom-out",
                                   QStringLiteral("缩小 (Ctrl+-)"),
                                   QStringLiteral("缩小"),
                                   QStringLiteral("document.zoomOut"));
    connect(zoomOut, &QToolButton::clicked, this, &PdfView::zoomOut);

    auto* zoomIn = makeIconButton("zoom-in",
                                  QStringLiteral("放大 (Ctrl++)"),
                                  QStringLiteral("放大"),
                                  QStringLiteral("document.zoomIn"));
    connect(zoomIn, &QToolButton::clicked, this, &PdfView::zoomIn);

    auto* fitWidthButton = new QToolButton(this);
    fitWidthButton->setText(QStringLiteral("适宽"));
    fitWidthButton->setIcon(toolbarIcon("fit-width"));
    fitWidthButton->setToolTip(QStringLiteral("缩放到适应窗口宽度"));
    fitWidthButton->setObjectName(QStringLiteral("document.fitWidth"));
    fitWidthButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(fitWidthButton, &QToolButton::clicked, this, &PdfView::zoomToFitWidth);

    auto* fitPageButton = new QToolButton(this);
    fitPageButton->setText(QStringLiteral("整页"));
    fitPageButton->setIcon(toolbarIcon("fit-page"));
    fitPageButton->setToolTip(QStringLiteral("缩放到整页可见"));
    fitPageButton->setObjectName(QStringLiteral("document.fitPage"));
    fitPageButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(fitPageButton, &QToolButton::clicked, this, &PdfView::zoomToFitPage);

    auto* rotateLeftButton = makeIconButton("rotate-left",
                                            QStringLiteral("向左旋转"),
                                            QStringLiteral("向左旋转"),
                                            QStringLiteral("document.rotateLeft"));
    connect(rotateLeftButton, &QToolButton::clicked, this, &PdfView::rotateLeft);

    auto* rotateRightButton = makeIconButton("rotate-right",
                                             QStringLiteral("向右旋转"),
                                             QStringLiteral("向右旋转"),
                                             QStringLiteral("document.rotateRight"));
    connect(rotateRightButton, &QToolButton::clicked, this, &PdfView::rotateRight);

    // 保存按钮（v0.8.0）：菜单栏是收起的，文档工具栏上留一个入口。
    auto* saveButton = new QToolButton(this);
    saveButton->setText(QStringLiteral("保存"));
    saveButton->setIcon(toolbarIcon("save"));
    saveButton->setToolTip(QStringLiteral("保存文档 (Ctrl+S)"));
    saveButton->setObjectName(QStringLiteral("document.save"));
    saveButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(saveButton, &QToolButton::clicked, this, &PdfView::saveRequested);

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
    toolBar->addWidget(saveButton);
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
    toolBar->setIconSize(QSize(kToolBarIconPixels, kToolBarIconPixels));
    // 批注工具是低频功能，按设计规范用「图标 + 文字」，不让人对着图标猜。
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto makeButton = [toolBar](const QString& text,
                                const QString& tip,
                                const QString& objectName,
                                const char* iconName) {
        auto* button = new QToolButton(toolBar);
        button->setText(text);
        button->setToolTip(tip);
        button->setObjectName(objectName);
        button->setIcon(toolbarIcon(iconName));
        // 工具栏的 toolButtonStyle 只作用在 QToolBar 自己按 QAction 建的按钮上，
        // addWidget 加进来的独立 QToolButton 默认还是纯图标，得逐个设置。
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        return button;
    };

    highlightButton_ = makeButton(QStringLiteral("高亮"),
                                   QStringLiteral("给选中的文字加高亮 (Ctrl+H)"),
                                   QStringLiteral("annotation.highlight"),
                                   "highlight");
    underlineButton_ = makeButton(QStringLiteral("下划线"),
                                   QStringLiteral("给选中的文字加下划线 (Ctrl+U)"),
                                   QStringLiteral("annotation.underline"),
                                   "underline");
    strikeOutButton_ = makeButton(QStringLiteral("删除线"),
                                  QStringLiteral("给选中的文字加删除线"),
                                  QStringLiteral("annotation.strikethrough"),
                                  "strikethrough");
    noteButton_ = makeButton(QStringLiteral("便签"),
                              QStringLiteral("在页面上放一个便签"),
                              QStringLiteral("annotation.note"),
                              "note");
    squareButton_ = makeButton(QStringLiteral("矩形"),
                                QStringLiteral("在页面上画一个矩形"),
                                QStringLiteral("annotation.square"),
                                "square");
    inkButton_ = makeButton(QStringLiteral("墨迹"),
                             QStringLiteral("在页面上画一笔"),
                             QStringLiteral("annotation.ink"),
                             "ink");
    undoButton_ = makeButton(QStringLiteral("撤销"),
                              QStringLiteral("撤销上一步批注操作 (Ctrl+Z)"),
                              QStringLiteral("annotation.undo"),
                              "undo");
    redoButton_ = makeButton(QStringLiteral("重做"),
                              QStringLiteral("重做上一步批注操作 (Ctrl+Shift+Z)"),
                              QStringLiteral("annotation.redo"),
                              "redo");

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
    modified_ = false;

    // 文本控制器与导航控制器：各自开一份独立句柄，在后台线程干活。
    setupControllers();

    // 批注控制器：持有批注模型、撤销栈，并把批注写进文档。
    annotationController_ = std::make_unique<AnnotationController>(document_);
    canvas_->setAnnotationController(annotationController_.get());
    connect(annotationController_.get(), &AnnotationController::annotationsChanged, this,
            [this](int) {
                canvas_->viewport()->update();
                emit annotationsChanged(annotationController_->annotationCount());
            });
    connect(annotationController_.get(), &AnnotationController::undoStateChanged, this,
            [this]() {
                updateAnnotationToolBar();
                // 有过编辑就记恢复日志：崩了还能把未保存的工作找回来。
                setModified(true);
                writeRecoveryLog();
            });
    connect(annotationController_.get(), &AnnotationController::toolChanged, this, [this]() {
        const auto tool = annotationController_->tool();
        noteButton_->setChecked(tool == AnnotationController::Tool::Note);
        squareButton_->setChecked(tool == AnnotationController::Tool::Square);
        inkButton_->setChecked(tool == AnnotationController::Tool::Ink);
    });
    // 页面结构变了：布局、渲染线程、后台控制器都要跟着换。
    connect(annotationController_.get(), &AnnotationController::documentStructureChanged,
            this, &PdfView::handlePageStructureChanged);

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
    startRenderThread();

    canvas_->setLayout(layout_);
    emit pageCountChanged(pageCount_);
    updateZoomLabel();
    updatePageInfo();
    // 文档换新了：批注工具栏的可用状态要跟着重算（工具可用、标记类按钮要等选区）。
    updateAnnotationToolBar();
    requestVisiblePages();
}

// 文本控制器与导航控制器：各自持有一份独立句柄，在后台线程取数据。
//
// 这两个控制器会被整体换掉（页面结构变化之后，旧副本里的页面已经过期），
// 所以建立过程单独抽出来，换文档与换副本都走这一条路。
void PdfView::setupControllers() {
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

    // 问答控制器持有索引控制器的指针：换索引控制器之前先把它放掉，
    // 不然它会握着一个已经销毁的指针。
    qaController_.reset();

    // 索引控制器：后台把文档切成块，带章节归属与坐标。
    // 它自己开一份独立句柄，在后台线程抽文本。
    indexController_ = std::make_unique<IndexController>(document_);
    indexController_->setDocumentPath(filePath_);
    connect(indexController_.get(), &IndexController::indexProgress, this,
            [this](int done, int total) {
                // 进度只在轨迹里留一句，界面上的状态由索引面板显示。
                Q_UNUSED(done);
                Q_UNUSED(total);
            });

    // 整篇问答控制器：跟着索引走（检索要用块，核验要用原文）。
    // 不设 QObject 父对象：它由 unique_ptr 负责释放，
    // 再挂到 QObject 父子树上会被删两次。
    qaController_ = std::make_unique<DocumentQaController>();
    qaController_->setIndexController(indexController_.get());
}

// 索引里的坐标是 PDF user space；显示时要用页面的变换换过去。
//
// 优先用批注控制器：它把「页面自带 /Rotate + 用户额外旋转」合在一起，
// 和画布用的显示空间是同一套。没有它就按文档里的页面盒子自己算一份
// （只有页面自带的旋转，没有用户旋转）。
bool PdfView::transformForIndexPage(int pageIndex, PageTransform* transform) const {
    if (transform == nullptr) {
        return false;
    }
    if (annotationController_ != nullptr &&
        annotationController_->transformForPage(pageIndex, transform)) {
        return true;
    }
    const auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return false;
    }
    const pdf::MediaBox& box = page->contentBox();
    const double left = qMin(box.left, box.right);
    const double right = qMax(box.left, box.right);
    const double bottom = qMin(box.bottom, box.top);
    const double top = qMax(box.bottom, box.top);
    transform->setPage(left, bottom, right - left, top - bottom);
    transform->setRotationDegrees(page->rotationDegrees());
    return true;
}

// 跳到某页的某个块，并把那块在页面上高亮出来。
void PdfView::showIndexedChunk(int pageIndex, const QString& chunkId) {
    if (indexController_ == nullptr) {
        return;
    }
    doc::DocumentChunk chunk;
    // 按页取块：两页内容完全一样时，按 id 全局取会拿到另一页的块。
    if (!indexController_->chunkByIdOnPage(pageIndex, chunkId, &chunk)) {
        trace(QStringLiteral("index chunk missing page=%1 id=%2").arg(pageIndex).arg(chunkId));
        return;
    }
    PageTransform transform;
    if (!transformForIndexPage(pageIndex, &transform)) {
        return;
    }
    QVector<QRectF> displayRects;
    displayRects.reserve(chunk.lineBoxes.size());
    for (const QRectF& box : chunk.lineBoxes) {
        displayRects.append(transform.toDisplay(box));
    }
    canvas_->setIndexHighlight(pageIndex, displayRects);
    goToPage(pageIndex);
    trace(QStringLiteral("index chunk page=%1 id=%2 rects=%3")
              .arg(pageIndex)
              .arg(chunkId)
              .arg(displayRects.size()));
}

// 渲染线程：自己开一份独立句柄。
void PdfView::startRenderThread() {
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
}

// 页面结构变了（插入、删除、旋转、移动页）：
// 布局要重排，渲染线程与后台控制器要换新副本，缓存要清。
void PdfView::handlePageStructureChanged() {
    if (!document_.isValid()) {
        return;
    }

    // 副本是从源字节重新解析出来的：内存里的改动它看不到。
    // 把最新的状态同步进源字节，换出来的副本才是新的。
    QString serializeError;
    const QByteArray bytes = document_.serialize(&serializeError);
    if (!bytes.isEmpty()) {
        document_.syncBytes(bytes);
    }

    // 布局：页数、顺序、页面尺寸（旋转后宽高会换）都可能变了。
    pageCount_ = document_.pageCount();
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
    cache_.clear();

    // 渲染线程与后台控制器换新副本。
    stopRenderThread();
    startRenderThread();
    setupControllers();
    // 控制器换过了：外面拿着旧指针的地方要重新指一遍。
    emit controllersRefreshed();

    canvas_->setLayout(layout_);
    emit pageCountChanged(pageCount_);
    updatePageInfo();
    requestVisiblePages();
    emit pageStructureChanged();
}

void PdfView::clear() {
    stopRenderThread();
    canvas_->setTextController(nullptr);
    canvas_->setNavigationController(nullptr);
    canvas_->setAnnotationController(nullptr);
    canvas_->setIndexHighlight(-1, {});
    textController_.reset();
    navigationController_.reset();
    annotationController_.reset();
    // 问答控制器持有索引控制器的指针：它要先于索引控制器释放。
    qaController_.reset();
    indexController_.reset();
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

QSizeF PdfView::pageSizePoints(int pageIndex) const {
    const auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return {};
    }
    return QSizeF(page->displayWidthPoints(), page->displayHeightPoints());
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

IndexController* PdfView::indexController() const {
    return indexController_.get();
}

DocumentQaController* PdfView::qaController() const {
    return qaController_.get();
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
    // 先叫醒线程让它收工，再等它退出。
    // 顺序反了会死等：线程正睡在条件变量上，只有 stop 叫得醒它。
    renderThread_->stop();
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

// --------------------------------------------------- 页面管理（v0.8.0）----

void PdfView::insertBlankPageAfterCurrent() {
    if (!annotationController_) {
        return;
    }
    const int current = currentPage();
    if (current < 0 || annotationController_->pageCount() <= 0) {
        return;
    }
    // 新页沿用当前页的尺寸。
    const double width = annotationController_->pageWidth(current);
    const double height = annotationController_->pageHeight(current);
    if (width <= 0.0 || height <= 0.0) {
        emit statusMessage(tr("当前页的尺寸取不到，插入失败"));
        return;
    }
    if (!annotationController_->insertBlankPage(current + 1, width, height)) {
        emit statusMessage(tr("插入空白页失败"));
        return;
    }
    emit statusMessage(tr("已在当前页后插入空白页"));
}

void PdfView::deleteCurrentPage() {
    if (!annotationController_) {
        return;
    }
    const int current = currentPage();
    const int count = annotationController_->pageCount();
    if (current < 0 || count <= 0) {
        return;
    }
    if (count <= 1) {
        emit statusMessage(tr("文档至少要保留一页"));
        return;
    }
    if (!annotationController_->deletePage(current)) {
        emit statusMessage(tr("删除页失败"));
        return;
    }
    emit statusMessage(tr("已删除第 %1 页").arg(current + 1));
}

void PdfView::rotateCurrentPageRight() {
    if (!annotationController_) {
        return;
    }
    const int current = currentPage();
    if (current < 0) {
        return;
    }
    const int turns = (annotationController_->pageRotation(current) + 1) % 4;
    if (!annotationController_->rotatePage(current, turns)) {
        emit statusMessage(tr("旋转页面失败"));
        return;
    }
    emit statusMessage(tr("第 %1 页已顺时针旋转 90 度").arg(current + 1));
}

void PdfView::rotateCurrentPageLeft() {
    if (!annotationController_) {
        return;
    }
    const int current = currentPage();
    if (current < 0) {
        return;
    }
    const int turns = (annotationController_->pageRotation(current) + 3) % 4;
    if (!annotationController_->rotatePage(current, turns)) {
        emit statusMessage(tr("旋转页面失败"));
        return;
    }
    emit statusMessage(tr("第 %1 页已逆时针旋转 90 度").arg(current + 1));
}

void PdfView::moveCurrentPageUp() {
    if (!annotationController_) {
        return;
    }
    const int current = currentPage();
    if (current <= 0) {
        emit statusMessage(tr("已经是第一页了"));
        return;
    }
    if (!annotationController_->movePage(current, current - 1)) {
        emit statusMessage(tr("移动页面失败"));
        return;
    }
    emit statusMessage(tr("第 %1 页已上移").arg(current + 1));
}

void PdfView::moveCurrentPageDown() {
    if (!annotationController_) {
        return;
    }
    const int current = currentPage();
    const int count = annotationController_->pageCount();
    if (current < 0) {
        return;
    }
    if (current >= count - 1) {
        emit statusMessage(tr("已经是最后一页了"));
        return;
    }
    if (!annotationController_->movePage(current, current + 1)) {
        emit statusMessage(tr("移动页面失败"));
        return;
    }
    emit statusMessage(tr("第 %1 页已下移").arg(current + 1));
}

// ---------------------------------------------------------- 安全保存 ----

bool PdfView::save(QString* errorMessage, QString* backupPath) {
    if (filePath_.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("这个文档还没有保存过，请用「另存为」");
        }
        return false;
    }
    return saveAs(filePath_, errorMessage, backupPath);
}

bool PdfView::saveAs(const QString& targetPath, QString* errorMessage, QString* backupPath) {
    if (!document_.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("没有打开文档");
        }
        return false;
    }
    if (targetPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("没有目标路径");
        }
        return false;
    }

    // 保存只有一个入口：序列化 + 补外观流 + 写临时文件 + 重开校验 + 备份 + 原子替换。
    // 分步调用会绕过后半段，"保存失败不破坏原文件"的保证就没了。
    const pdf::SaveOutcome saved = pdf::DocumentWriter::save(document_, targetPath, true);
    if (!saved.ok) {
        if (errorMessage != nullptr) {
            *errorMessage = saved.error;
        }
        trace(QStringLiteral("save failed path=%1").arg(targetPath));
        return false;
    }
    if (!saved.warning.isEmpty()) {
        trace(QStringLiteral("appearance not written: %1").arg(saved.warning));
    }
    if (backupPath != nullptr) {
        *backupPath = saved.backupPath;
    }

    // 另存为之后文档换了路径：旧的恢复日志用不上了。
    if (targetPath != filePath_) {
        doc::RecoveryLog::remove(filePath_);
        filePath_ = targetPath;
        emit filePathChanged(filePath_);
    }
    // 保存成功：这篇文档没有未保存的工作了。
    doc::RecoveryLog::remove(filePath_);
    setModified(false);
    trace(QStringLiteral("save ok path=%1 pages=%2").arg(targetPath).arg(document_.pageCount()));
    return true;
}

bool PdfView::isModified() const {
    return modified_;
}

// 记一次恢复日志：改过就要能恢复。
void PdfView::writeRecoveryLog() {
    if (!annotationController_ || filePath_.isEmpty()) {
        return;
    }
    doc::RecoveryLog::write(filePath_, annotationController_->serializedHistory());
}

void PdfView::setModified(bool modified) {
    if (modified_ == modified) {
        return;
    }
    modified_ = modified;
    emit modifiedChanged(modified_);
}

}  // namespace aipdf::app
