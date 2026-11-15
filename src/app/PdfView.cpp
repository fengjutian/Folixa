#include "app/PdfView.h"

#include "app/AudioRecorder.h"
#include "app/DocumentOutput.h"
#include "app/EncryptionDialog.h"
#include "app/PdfPageOperations.h"
#include "app/Language.h"
#include "pdf/AnnotationSummary.h"
#include "pdf/FormData.h"
#include "pdf/PageDecorations.h"
#include "pdf/SoundFormat.h"
#include "pdf/Xfdf.h"
#include "app/PresentationWindow.h"
#include "app/ZoomMemory.h"
#include "app/PageCanvas.h"
#include "app/RenderThread.h"
#include "app/TextController.h"
#include "app/NavigationController.h"
#include "app/AnnotationController.h"
#include "app/IndexController.h"
#include "app/DocumentQaController.h"
#include "app/PageTransform.h"
#include "app/TraceText.h"
#include "doc/DocumentChunk.h"
#include "doc/RecoveryLog.h"
#include "pdf/Annotation.h"
#include "pdf/DocumentWriter.h"

#include <QColorDialog>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QPrinter>
#include <QPrintDialog>
#include <QMessageBox>
#include <QFileDialog>
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

    // 切换语言后所有登记过的文案就地重译：不重建视图，文档与滚动位置都留着。
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &PdfView::retranslateUi);
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
                                 const char* tipSource,
                                 const char* accessibleNameSource,
                                 const QString& objectName) {
        auto* button = new QToolButton(this);
        button->setIcon(toolbarIcon(iconName));
        button->setObjectName(objectName);
        button->setProperty("iconOnly", true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        texts_.bind([this, button, tipSource, accessibleNameSource] {
            button->setToolTip(tr(tipSource));
            button->setAccessibleName(tr(accessibleNameSource));
        });
        return button;
    };

    auto* zoomOut = makeIconButton("zoom-out",
                                   QT_TRANSLATE_NOOP("aipdf::app::PdfView", "缩小 (Ctrl+-)"),
                                   QT_TRANSLATE_NOOP("aipdf::app::PdfView", "缩小"),
                                   QStringLiteral("document.zoomOut"));
    connect(zoomOut, &QToolButton::clicked, this, &PdfView::zoomOut);

    auto* zoomIn = makeIconButton("zoom-in",
                                  QT_TRANSLATE_NOOP("aipdf::app::PdfView", "放大 (Ctrl++)"),
                                  QT_TRANSLATE_NOOP("aipdf::app::PdfView", "放大"),
                                  QStringLiteral("document.zoomIn"));
    connect(zoomIn, &QToolButton::clicked, this, &PdfView::zoomIn);

    auto* fitWidthButton = new QToolButton(this);
    fitWidthButton->setIcon(toolbarIcon("fit-width"));
    fitWidthButton->setObjectName(QStringLiteral("document.fitWidth"));
    fitWidthButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    texts_.bind([this, fitWidthButton] {
        fitWidthButton->setText(tr("适宽"));
        fitWidthButton->setToolTip(tr("缩放到适应窗口宽度"));
    });
    connect(fitWidthButton, &QToolButton::clicked, this, &PdfView::zoomToFitWidth);

    auto* fitPageButton = new QToolButton(this);
    fitPageButton->setIcon(toolbarIcon("fit-page"));
    fitPageButton->setObjectName(QStringLiteral("document.fitPage"));
    fitPageButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    texts_.bind([this, fitPageButton] {
        fitPageButton->setText(tr("整页"));
        fitPageButton->setToolTip(tr("缩放到整页可见"));
    });
    connect(fitPageButton, &QToolButton::clicked, this, &PdfView::zoomToFitPage);

    auto* rotateLeftButton = makeIconButton("rotate-left",
                                            QT_TRANSLATE_NOOP("aipdf::app::PdfView", "向左旋转"),
                                            QT_TRANSLATE_NOOP("aipdf::app::PdfView", "向左旋转"),
                                            QStringLiteral("document.rotateLeft"));
    connect(rotateLeftButton, &QToolButton::clicked, this, &PdfView::rotateLeft);

    auto* rotateRightButton = makeIconButton("rotate-right",
                                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "向右旋转"),
                                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "向右旋转"),
                                             QStringLiteral("document.rotateRight"));
    connect(rotateRightButton, &QToolButton::clicked, this, &PdfView::rotateRight);

    // 保存按钮（v0.8.0）：菜单栏是收起的，文档工具栏上留一个入口。
    auto* saveButton = new QToolButton(this);
    saveButton->setIcon(toolbarIcon("save"));
    saveButton->setObjectName(QStringLiteral("document.save"));
    saveButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    texts_.bind([this, saveButton] {
        saveButton->setText(tr("保存"));
        saveButton->setToolTip(tr("保存文档 (Ctrl+S)"));
    });
    connect(saveButton, &QToolButton::clicked, this, &PdfView::saveRequested);

    zoomLabel_ = new QLabel(QStringLiteral("100%"), this);
    zoomLabel_->setObjectName(QStringLiteral("secondaryText"));
    zoomLabel_->setMinimumWidth(56);
    zoomLabel_->setAlignment(Qt::AlignCenter);

    pageInfoLabel_ = new QLabel(QString(), this);
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

    // 前进/后退用浏览器与阅读器通用的 Alt+←/→。
    // 排布方式快捷键：Ctrl+1/2/3。验收脚本靠它切模式（菜单栏是隐藏的，UIA 点不到）。
    // 编辑类动作的快捷键：菜单栏是隐藏的，QAction 快捷键不生效，
    // 这里挂独立 QShortcut——用户能用，验收脚本也能触发。
    auto* watermarkShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")), this);
    connect(watermarkShortcut, &QShortcut::activated, this, &PdfView::applyWatermarkDialog);
    auto* headerFooterShortcut =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+H")), this);
    connect(headerFooterShortcut, &QShortcut::activated, this,
            &PdfView::applyHeaderFooterDialog);
    auto* pageNumberShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")), this);
    connect(pageNumberShortcut, &QShortcut::activated, this, &PdfView::applyPageNumbersDialog);
    auto* mergeShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+M")), this);
    connect(mergeShortcut, &QShortcut::activated, this, &PdfView::mergeDocumentDialog);
    auto* extractShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")), this);
    connect(extractShortcut, &QShortcut::activated, this, &PdfView::extractCurrentPageDialog);

    // F5 放映：菜单栏是隐藏的，QAction 的快捷键在隐藏菜单里不生效，
    // 这里挂一个独立 QShortcut（验收脚本与用户按键都走这条）。
    auto* presentationShortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(presentationShortcut, &QShortcut::activated, this, &PdfView::startPresentation);

    auto* singleShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+1")), this);
    connect(singleShortcut, &QShortcut::activated, this,
            [this]() { setLayoutMode(PageLayout::LayoutMode::Continuous); });
    auto* facingShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+2")), this);
    connect(facingShortcut, &QShortcut::activated, this,
            [this]() { setLayoutMode(PageLayout::LayoutMode::Facing); });
    auto* coverShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+3")), this);
    connect(coverShortcut, &QShortcut::activated, this,
            [this]() { setLayoutMode(PageLayout::LayoutMode::FacingCover); });

    auto* backShortcut = new QShortcut(QKeySequence(QStringLiteral("Alt+Left")), this);
    connect(backShortcut, &QShortcut::activated, this, &PdfView::navigateBack);
    auto* forwardShortcut = new QShortcut(QKeySequence(QStringLiteral("Alt+Right")), this);
    connect(forwardShortcut, &QShortcut::activated, this, &PdfView::navigateForward);

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
    updatePageInfo();
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

    // 第二行：图形、测量、附件这些低频工具。挤在一行会让 QToolBar 溢出，
    // 后面的按钮被收进溢出菜单——界面上看不见，UIA 也找不到。
    auto* shapesBar = new QToolBar(this);
    shapesBar->setObjectName(QStringLiteral("shapeToolBar"));
    shapesBar->setMovable(false);
    shapesBar->setIconSize(QSize(kToolBarIconPixels, kToolBarIconPixels));
    shapesBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto makeButton = [this](QToolBar* bar, const char* textSource, const char* tipSource,
                             const QString& objectName, const char* iconName) {
        auto* button = new QToolButton(bar);
        button->setObjectName(objectName);
        button->setIcon(toolbarIcon(iconName));
        // 工具栏的 toolButtonStyle 只作用在 QToolBar 自己按 QAction 建的按钮上，
        // addWidget 加进来的独立 QToolButton 默认还是纯图标，得逐个设置。
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        texts_.bind([this, button, textSource, tipSource] {
            button->setText(tr(textSource));
            button->setToolTip(tr(tipSource));
        });
        return button;
    };

    highlightButton_ = makeButton(toolBar,
                                  QT_TRANSLATE_NOOP("aipdf::app::PdfView", "高亮"),
                                  QT_TRANSLATE_NOOP("aipdf::app::PdfView", "给选中的文字加高亮 (Ctrl+H)"),
                                  QStringLiteral("annotation.highlight"),
                                  "highlight");
    underlineButton_ = makeButton(toolBar,
                                  QT_TRANSLATE_NOOP("aipdf::app::PdfView", "下划线"),
                                  QT_TRANSLATE_NOOP("aipdf::app::PdfView", "给选中的文字加下划线 (Ctrl+U)"),
                                  QStringLiteral("annotation.underline"),
                                  "underline");
    squigglyButton_ = makeButton(toolBar,
                                 QT_TRANSLATE_NOOP("aipdf::app::PdfView", "波浪线"),
                                 QT_TRANSLATE_NOOP("aipdf::app::PdfView", "给选中的文字加波浪线"),
                                 QStringLiteral("annotation.squiggly"),
                                 "squiggly");
    strikeOutButton_ = makeButton(toolBar,
                                  QT_TRANSLATE_NOOP("aipdf::app::PdfView", "删除线"),
                                  QT_TRANSLATE_NOOP("aipdf::app::PdfView", "给选中的文字加删除线"),
                                  QStringLiteral("annotation.strikethrough"),
                                  "strikethrough");
    ellipseButton_ = makeButton(shapesBar,
                                QT_TRANSLATE_NOOP("aipdf::app::PdfView", "椭圆"),
                                QT_TRANSLATE_NOOP("aipdf::app::PdfView", "拖出一个椭圆"),
                                QStringLiteral("annotation.ellipse"),
                                "ellipse");
    perimeterButton_ = makeButton(shapesBar,
                                  QT_TRANSLATE_NOOP("aipdf::app::PdfView", "周长"),
                                  QT_TRANSLATE_NOOP("aipdf::app::PdfView", "逐点点击，双击结束，量周长"),
                                  QStringLiteral("annotation.perimeter"),
                                  "perimeter");
    areaButton_ = makeButton(shapesBar,
                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "面积"),
                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "逐点点击，双击结束，量面积"),
                             QStringLiteral("annotation.area"),
                             "area");
    imageStampButton_ = makeButton(shapesBar,
                                   QT_TRANSLATE_NOOP("aipdf::app::PdfView", "图片图章"),
                                   QT_TRANSLATE_NOOP("aipdf::app::PdfView", "用一张图片做图章"),
                                   QStringLiteral("annotation.imagestamp"),
                                   "stamp");
    soundButton_ = makeButton(shapesBar,
                              QT_TRANSLATE_NOOP("aipdf::app::PdfView", "声音"),
                              QT_TRANSLATE_NOOP("aipdf::app::PdfView", "挂一段 WAV 音频"),
                              QStringLiteral("annotation.sound"),
                              "sound");
    attachmentButton_ = makeButton(shapesBar,
                                   QT_TRANSLATE_NOOP("aipdf::app::PdfView", "附件"),
                                   QT_TRANSLATE_NOOP("aipdf::app::PdfView", "在页面上挂一个文件"),
                                   QStringLiteral("annotation.attachment"),
                                   "attachment");
    measureButton_ = makeButton(shapesBar,
                                QT_TRANSLATE_NOOP("aipdf::app::PdfView", "测量"),
                                QT_TRANSLATE_NOOP("aipdf::app::PdfView", "拖出两点，显示距离"),
                                QStringLiteral("annotation.measure"),
                                "measure");
    stampButton_ = makeButton(shapesBar,
                              QT_TRANSLATE_NOOP("aipdf::app::PdfView", "图章"),
                              QT_TRANSLATE_NOOP("aipdf::app::PdfView", "在页面上放一个文字图章"),
                              QStringLiteral("annotation.stamp"),
                              "stamp");
    freeTextButton_ = makeButton(shapesBar,
                                 QT_TRANSLATE_NOOP("aipdf::app::PdfView", "文字"),
                                 QT_TRANSLATE_NOOP("aipdf::app::PdfView", "拖出一块并输入文字"),
                                 QStringLiteral("annotation.freetext"),
                                 "freetext");
    lineButton_ = makeButton(shapesBar,
                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "直线"),
                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "拖出一条直线"),
                             QStringLiteral("annotation.line"),
                             "line");
    arrowButton_ = makeButton(shapesBar,
                              QT_TRANSLATE_NOOP("aipdf::app::PdfView", "箭头"),
                              QT_TRANSLATE_NOOP("aipdf::app::PdfView", "拖出一个箭头"),
                              QStringLiteral("annotation.arrow"),
                              "arrow");
    polygonButton_ = makeButton(shapesBar,
                                QT_TRANSLATE_NOOP("aipdf::app::PdfView", "多边形"),
                                QT_TRANSLATE_NOOP("aipdf::app::PdfView", "逐点点击，双击结束"),
                                QStringLiteral("annotation.polygon"),
                                "polygon");
    cloudButton_ = makeButton(shapesBar,
                              QT_TRANSLATE_NOOP("aipdf::app::PdfView", "云线"),
                              QT_TRANSLATE_NOOP("aipdf::app::PdfView", "逐点点击，双击结束"),
                              QStringLiteral("annotation.cloud"),
                              "cloud");
    noteButton_ = makeButton(toolBar,
                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "便签"),
                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "在页面上放一个便签"),
                             QStringLiteral("annotation.note"),
                             "note");
    squareButton_ = makeButton(toolBar,
                               QT_TRANSLATE_NOOP("aipdf::app::PdfView", "矩形"),
                               QT_TRANSLATE_NOOP("aipdf::app::PdfView", "在页面上画一个矩形"),
                               QStringLiteral("annotation.square"),
                               "square");
    eraserButton_ = makeButton(toolBar,
                               QT_TRANSLATE_NOOP("aipdf::app::PdfView", "橡皮"),
                               QT_TRANSLATE_NOOP("aipdf::app::PdfView", "擦掉墨迹（整笔删除）"),
                               QStringLiteral("annotation.eraser"),
                               "eraser");
    inkButton_ = makeButton(toolBar,
                            QT_TRANSLATE_NOOP("aipdf::app::PdfView", "墨迹"),
                            QT_TRANSLATE_NOOP("aipdf::app::PdfView", "在页面上画一笔"),
                            QStringLiteral("annotation.ink"),
                            "ink");
    undoButton_ = makeButton(toolBar,
                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "撤销"),
                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "撤销上一步批注操作 (Ctrl+Z)"),
                             QStringLiteral("annotation.undo"),
                             "undo");
    redoButton_ = makeButton(toolBar,
                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "重做"),
                             QT_TRANSLATE_NOOP("aipdf::app::PdfView", "重做上一步批注操作 (Ctrl+Shift+Z)"),
                             QStringLiteral("annotation.redo"),
                             "redo");

    toolBar->addWidget(highlightButton_);
    toolBar->addWidget(underlineButton_);
    toolBar->addWidget(strikeOutButton_);
    toolBar->addSeparator();
    toolBar->addWidget(noteButton_);
    toolBar->addWidget(squareButton_);
    shapesBar->addWidget(ellipseButton_);
    shapesBar->addWidget(lineButton_);
    shapesBar->addWidget(arrowButton_);
    shapesBar->addWidget(polygonButton_);
    shapesBar->addWidget(cloudButton_);
    shapesBar->addWidget(freeTextButton_);
    shapesBar->addWidget(stampButton_);
    shapesBar->addWidget(measureButton_);
    shapesBar->addWidget(imageStampButton_);
    shapesBar->addWidget(soundButton_);
    shapesBar->addWidget(attachmentButton_);
    shapesBar->addWidget(perimeterButton_);
    shapesBar->addWidget(areaButton_);
    toolBar->addWidget(inkButton_);
    toolBar->addWidget(eraserButton_);
    toolBar->addSeparator();
    toolBar->addWidget(undoButton_);
    toolBar->addWidget(redoButton_);

    connect(highlightButton_, &QToolButton::clicked, this, [this]() {
        applyMarkAnnotation(pdf::AnnotationType::Highlight);
    });
    connect(underlineButton_, &QToolButton::clicked, this, [this]() {
        applyMarkAnnotation(pdf::AnnotationType::Underline);
    });
    connect(squigglyButton_, &QToolButton::clicked, this, [this]() {
        applyMarkAnnotation(pdf::AnnotationType::Squiggly);
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
    ellipseButton_->setAutoExclusive(true);
    lineButton_->setAutoExclusive(true);
    arrowButton_->setAutoExclusive(true);
    polygonButton_->setAutoExclusive(true);
    cloudButton_->setAutoExclusive(true);
    freeTextButton_->setAutoExclusive(true);
    stampButton_->setAutoExclusive(true);
    measureButton_->setAutoExclusive(true);
    attachmentButton_->setAutoExclusive(true);
    imageStampButton_->setAutoExclusive(true);
    soundButton_->setAutoExclusive(true);
    perimeterButton_->setAutoExclusive(true);
    areaButton_->setAutoExclusive(true);
    inkButton_->setAutoExclusive(true);
    eraserButton_->setAutoExclusive(true);
    connect(ellipseButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Ellipse
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(perimeterButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::MeasurePerimeter
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(areaButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::MeasureArea
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(imageStampButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::ImageStamp
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(soundButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Sound
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(attachmentButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Attachment
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(measureButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Measure
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(stampButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Stamp
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(freeTextButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::FreeText
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(lineButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Line
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(arrowButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Arrow
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(polygonButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Polygon
                                                   : AnnotationController::Tool::None);
        }
    });
    connect(cloudButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Cloud
                                                   : AnnotationController::Tool::None);
        }
    });
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
    connect(eraserButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (annotationController_) {
            annotationController_->setTool(checked ? AnnotationController::Tool::Eraser
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
        layout->insertWidget(2, shapesBar);
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
    // 换文档就把历史清掉：旧页码对新文档没有意义，留着只会在后退时跳到无关位置。
    history_.reset(ViewState{});
    // 同一份文档上次看的时候缩到多少，这次还按那个比例打开（没记过就是 100%）。
    zoom_ = 1.0;
    const double remembered = rememberedZoomForFile(path);
    if (remembered > 0.0) {
        zoom_ = remembered;
        trace(QStringLiteral("zoom restored value=%1").arg(remembered));
    }
    userRotation_ = 0;
    fitToWidth_ = false;
    fitToPage_ = false;
    cache_.clear();
    modified_ = false;

    // XFA 表单：PDFium 没有 XFA 引擎，页面会是空白的。必须当面说清楚，
    // 不能让用户以为文件坏了（F6）。
    if (document_.hasXfaForm()) {
        trace(QStringLiteral("document xfa form detected"));
        emit statusMessage(tr("这是 XFA 动态表单：本程序不支持 XFA，页面内容可能显示为空白。"));
        QMessageBox::information(
            this, tr("XFA 表单"),
            tr("这是 XFA 动态表单：PDFium 没有 XFA 引擎，页面可能显示为空白。"));
    }

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
    connect(canvas_, &PageCanvas::measureShapeRequested, this,
            [this](int pageIndex, int mode, const QVector<QPointF>& vertices) {
                if (annotationController_) {
                    annotationController_->addMeasureShape(
                        static_cast<MeasureMode>(mode), pageIndex, vertices, 1.0);
                }
            });
    connect(canvas_, &PageCanvas::imageStampRequested, this,
            [this](int pageIndex, const QPointF& userPoint) {
                const QString path = QFileDialog::getOpenFileName(
                    this, tr("选择图章图片"), QString(), tr("图片 (*.png *.jpg *.jpeg *.bmp)"));
                if (path.isEmpty() || !annotationController_) {
                    return;
                }
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    return;
                }
                const QByteArray bytes = file.readAll();
                file.close();
                QImage image;
                if (!image.loadFromData(bytes)) {
                    QMessageBox::warning(this, tr("图片图章"), tr("这看起来不是一张图片。"));
                    return;
                }
                // 保持原图宽高比，宽度固定 160pt。
                constexpr double kWidth = 160.0;
                const double height = kWidth * image.height() / std::max(1, image.width());
                const QRectF rect(userPoint.x() - kWidth / 2.0, userPoint.y() - height / 2.0,
                                  kWidth, height);
                annotationController_->addImageStamp(pageIndex, rect, bytes,
                                                     QFileInfo(path).baseName());
            });
    connect(canvas_, &PageCanvas::soundRequested, this,
            [this](int pageIndex, const QPointF& userPoint) {
                const QString path =
                    QFileDialog::getOpenFileName(this, tr("选择音频"), QString(), tr("WAV (*.wav)"));
                if (path.isEmpty() || !annotationController_) {
                    return;
                }
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    return;
                }
                const QByteArray bytes = file.readAll();
                file.close();
                const pdf::SoundPayload payload = pdf::parseWav(bytes);
                if (!payload.isValid()) {
                    QMessageBox::warning(this, tr("声音批注"), payload.error);
                    return;
                }
                constexpr double kSize = 24.0;
                const QRectF rect(userPoint.x() - kSize / 2.0, userPoint.y() - kSize / 2.0, kSize,
                                  kSize);
                // 走原生 /Sound 通路：保存时注入页面 /Annots，别的阅读器认得出。
                annotationController_->addSoundFromWav(pageIndex, rect, QFileInfo(path).fileName(),
                                                       bytes);
            });
    connect(canvas_, &PageCanvas::attachmentRequested, this,
            [this](int pageIndex, const QPointF& userPoint) {
                const QString path = QFileDialog::getOpenFileName(this, tr("添加附件"));
                if (path.isEmpty() || !annotationController_) {
                    return;
                }
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    QMessageBox::warning(this, tr("添加附件"), tr("读不了这个文件: %1").arg(path));
                    return;
                }
                const QByteArray data = file.readAll();
                file.close();
                annotationController_->addAttachment(pageIndex, userPoint,
                                                    QFileInfo(path).fileName(), data);
            });
    connect(canvas_, &PageCanvas::stampRequested, this,
            [this](int pageIndex, const QPointF& userPoint) {
                const QStringList labels{ QStringLiteral("APPROVED"), QStringLiteral("DRAFT"),
                                          QStringLiteral("CONFIDENTIAL"),
                                          QStringLiteral("REVIEWED") };
                bool accepted = false;
                const QString label = QInputDialog::getItem(
                    this, tr("图章"), tr("选择图章文字:"), labels, 0, false, &accepted);
                if (!accepted || label.isEmpty() || !annotationController_) {
                    return;
                }
                constexpr double kWidth = 140.0;
                constexpr double kHeight = 48.0;
                const QRectF rect(userPoint.x() - kWidth / 2.0, userPoint.y() - kHeight / 2.0,
                                  kWidth, kHeight);
                annotationController_->addStamp(pageIndex, rect, label);
            });
    connect(canvas_, &PageCanvas::freeTextRequested, this,
            [this](int pageIndex, const QRectF& userRect) {
                bool accepted = false;
                const QString text = QInputDialog::getMultiLineText(
                    this, tr("文本框"), tr("输入文字（中文会由本程序绘制，其他阅读器按字体回退显示）:"),
                    QString(), &accepted);
                if (accepted && !text.trimmed().isEmpty() && annotationController_) {
                    annotationController_->addFreeText(pageIndex, userRect, text);
                }
            });
    connect(canvas_, &PageCanvas::lineAnnotationRequested, this,
            [this](int pageIndex, int type, const QPointF& from, const QPointF& to) {
                if (!annotationController_) {
                    return;
                }
                if (static_cast<pdf::AnnotationType>(type) == pdf::AnnotationType::Measure) {
                    annotationController_->addMeasure(pageIndex, from, to, 1.0);
                } else {
                    annotationController_->addLine(static_cast<pdf::AnnotationType>(type), pageIndex,
                                                   from, to);
                }
            });
    connect(canvas_, &PageCanvas::polygonAnnotationRequested, this,
            [this](int pageIndex, int type, const QVector<QPointF>& vertices) {
                if (annotationController_) {
                    annotationController_->addPolygon(static_cast<pdf::AnnotationType>(type),
                                                      pageIndex, vertices);
                }
            });
    connect(canvas_, &PageCanvas::ellipseAnnotationRequested, this,
            [this](int pageIndex, const QRectF& userRect) {
                if (annotationController_) {
                    annotationController_->addShape(pdf::AnnotationType::Ellipse, pageIndex,
                                                    userRect);
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
    history_.reset(ViewState{});
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
    if (!filePath_.isEmpty()) {
        rememberZoomForFile(filePath_, zoom_);
        trace(QStringLiteral("zoom set value=%1").arg(clamped));
    }
    rebuildLayout();
    emit zoomChanged(zoom_);
    if (!restoringHistory_) {
        history_.record(captureViewState());
        emit historyChanged(history_.canGoBack(), history_.canGoForward());
    }
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
    trace(QStringLiteral("copy chars=%1 text=%2").arg(text.size()).arg(traceText(text)));
    emit textCopied(text.size());
}

void PdfView::setLayoutMode(PageLayout::LayoutMode mode) {
    if (layout_.layoutMode() == mode) {
        return;
    }
    layout_.setLayoutMode(mode);
    // 适配模式要按新的列宽重算，否则切到双页会有一半内容露在视口外。
    if (fitToWidth_) {
        zoom_ = fitZoomForWidth();
    } else if (fitToPage_) {
        zoom_ = fitZoomForPage();
    }
    layout_.setScale(zoom_);
    rebuildLayout();
    trace(QStringLiteral("layout mode=%1")
              .arg(mode == PageLayout::LayoutMode::Facing
                       ? QStringLiteral("facing")
                       : (mode == PageLayout::LayoutMode::FacingCover ? QStringLiteral("cover")
                                                                     : QStringLiteral("single"))));
    emit zoomChanged(zoom_);
}

PageLayout::LayoutMode PdfView::layoutMode() const {
    return layout_.layoutMode();
}

void PdfView::goToPage(int index) {
    if (canvas_ != nullptr) {
        canvas_->goToPage(index);
    }
    // 跳页是"用户主动换位置"的最强信号，记进历史。缩放同理（见 setZoom）。
    history_.record(captureViewState());
}

app::ViewState PdfView::captureViewState() const {
    ViewState state;
    state.pageIndex = currentPage();
    state.zoom = zoom_;
    state.anchor = QPointF(0.0, 0.0);  // 页内偏移暂不参与：滚轮会高频触发，需要单独去抖
    return state;
}

void PdfView::applyViewState(const ViewState& state) {
    // 恢复时不要再记历史，否则"后退"会把自己记成新动作。
    restoringHistory_ = true;
    if (state.pageIndex >= 0 && state.pageIndex < document_.pageCount()) {
        if (canvas_ != nullptr) {
            canvas_->goToPage(state.pageIndex);
        }
    }
    if (!qFuzzyCompare(state.zoom, zoom_)) {
        setZoom(state.zoom);
    }
    restoringHistory_ = false;
    emit historyChanged(history_.canGoBack(), history_.canGoForward());
}

void PdfView::exportAnnotationsXfdf() {
    if (!annotationController_) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("导出批注"), QString(),
                                                      tr("XFDF (*.xfdf)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("导出批注"), tr("写不了这个文件: %1").arg(path));
        return;
    }
    const QString xml = pdf::annotationsToXfdf(annotationController_->allAnnotations(),
                                               document_.pageCount());
    file.write(xml.toUtf8());
    file.close();
    emit statusMessage(tr("已导出批注到 %1").arg(QFileInfo(path).fileName()));
}

void PdfView::importAnnotationsXfdf() {
    if (!annotationController_) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("导入批注"), QString(),
                                                      tr("XFDF (*.xfdf)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("导入批注"), tr("读不了这个文件: %1").arg(path));
        return;
    }
    const auto annotations =
        pdf::annotationsFromXfdf(QString::fromUtf8(file.readAll()), document_.pageCount());
    file.close();
    int imported = 0;
    for (const pdf::Annotation& annotation : annotations) {
        if (annotationController_->importAnnotation(annotation) != 0) {
            ++imported;
        }
    }
    emit statusMessage(tr("导入了 %1 条批注").arg(imported));
}

void PdfView::exportAnnotationSummary() {
    if (!annotationController_) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("导出批注摘要"), QString(),
                                                      tr("HTML (*.html)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("导出批注摘要"), tr("写不了这个文件: %1").arg(path));
        return;
    }
    const QString title = filePath_.isEmpty() ? tr("批注摘要") : QFileInfo(filePath_).fileName();
    file.write(pdf::annotationsToHtmlSummary(annotationController_->allAnnotations(),
                                             document_.pageCount(), title)
                   .toUtf8());
    file.close();
    emit statusMessage(tr("已导出批注摘要"));
}

void PdfView::printAnnotationSummary() {
    if (!annotationController_) {
        return;
    }
    QPrinter printer;
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("打印批注摘要"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString title = filePath_.isEmpty() ? tr("批注摘要") : QFileInfo(filePath_).fileName();
    QTextDocument document;
    document.setHtml(pdf::annotationsToHtmlSummary(annotationController_->allAnnotations(),
                                                   document_.pageCount(), title));
    document.print(&printer);
    emit statusMessage(tr("批注摘要已发送到打印机"));
}

void PdfView::applyWatermarkDialog() {
    if (!document_.isValid()) {
        return;
    }
    bool accepted = false;
    const QString text = QInputDialog::getText(this, tr("添加水印"), tr("水印文字（仅支持西文）:"),
                                               QLineEdit::Normal, QStringLiteral("DRAFT"),
                                               &accepted);
    if (!accepted || text.trimmed().isEmpty()) {
        return;
    }
    pdf::WatermarkOptions options;
    options.text = text.trimmed();
    if (!pdf::canRenderWithStandardFont(options.text)) {
        QMessageBox::warning(this, tr("添加水印"),
                             tr("水印目前只支持西文：中文需要嵌入字体，暂不支持。"));
        return;
    }
    if (!pdf::applyWatermark(document_, options)) {
        QMessageBox::warning(this, tr("添加水印"), tr("水印没有写进去。"));
        return;
    }
    modified_ = true;
    refreshAfterDecoration();
    trace(QStringLiteral("decor watermark applied"));
    emit statusMessage(tr("已给每页添加水印"));
}

void PdfView::applyHeaderFooterDialog() {
    if (!document_.isValid()) {
        return;
    }
    bool accepted = false;
    const QString header = QInputDialog::getText(this, tr("页眉页脚"), tr("页眉文字（留空不加）:"),
                                                 QLineEdit::Normal, QString(), &accepted);
    if (!accepted) {
        return;
    }
    const QString footer = QInputDialog::getText(this, tr("页眉页脚"), tr("页脚文字（留空不加）:"),
                                                 QLineEdit::Normal, QString(), &accepted);
    if (!accepted) {
        return;
    }
    pdf::HeaderFooterOptions options;
    options.header = header.trimmed();
    options.footer = footer.trimmed();
    if (!pdf::canRenderWithStandardFont(options.header) ||
        !pdf::canRenderWithStandardFont(options.footer)) {
        QMessageBox::warning(this, tr("页眉页脚"),
                             tr("页眉页脚目前只支持西文：中文需要嵌入字体，暂不支持。"));
        return;
    }
    if (!pdf::applyHeaderFooter(document_, options)) {
        QMessageBox::warning(this, tr("页眉页脚"), tr("没有写入内容。"));
        return;
    }
    modified_ = true;
    refreshAfterDecoration();
    trace(QStringLiteral("decor header footer applied"));
    emit statusMessage(tr("已添加页眉页脚"));
}

void PdfView::applyPageNumbersDialog() {
    if (!document_.isValid()) {
        return;
    }
    pdf::PageNumberOptions options;
    if (!pdf::applyPageNumbers(document_, options)) {
        QMessageBox::warning(this, tr("页码"), tr("页码没有写进去。"));
        return;
    }
    modified_ = true;
    refreshAfterDecoration();
    trace(QStringLiteral("decor page numbers applied"));
    emit statusMessage(tr("已添加页码"));
}

// ------------------------------------------------------ 安全（v1.6.0）----

bool PdfView::encryptionEnabled() const {
    return encryptionEnabled_;
}

void PdfView::configureEncryptionDialog() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("加密与权限"), tr("先打开一份 PDF。"));
        return;
    }
    if (!pdf::encryptionAvailable()) {
        QMessageBox::warning(
            this, tr("加密与权限"),
            tr("本次构建没有启用 qpdf，写不了加密文档。请用带 qpdf 的安装包。"));
        return;
    }
    EncryptionDialog dialog(encryptionOptions_, encryptionEnabled_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const bool wasEnabled = encryptionEnabled_;
    encryptionOptions_ = dialog.options();
    encryptionEnabled_ = dialog.encryptionEnabled();
    if (encryptionEnabled_ || wasEnabled) {
        // 加密是保存时的动作：标脏，避免用户改完设置却没保存就关掉。
        setModified(true);
    }
    if (encryptionEnabled_) {
        const bool needsPassword = !encryptionOptions_.userPassword.isEmpty();
        emit statusMessage(
            needsPassword
                ? tr("保存时将使用 AES-256 加密；重新打开需要密码")
                : tr("保存时将使用 AES-256 加密并写入权限位（打开不需要密码）"));
    } else {
        emit statusMessage(tr("已关闭加密：下次保存写明文 PDF"));
    }
    trace(QStringLiteral("encryption configured enabled=%1 password=%2")
              .arg(encryptionEnabled_ ? 1 : 0)
              .arg(encryptionOptions_.userPassword.isEmpty() ? 0 : 1));
}

// 装饰写进页面内容后，渲染线程手里的文档副本已经过期，必须换一份。

void PdfView::cropCurrentPageDialog() {
    if (!document_.isValid()) {
        return;
    }
    const int pageIndex = currentPage();
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return;
    }
    const pdf::MediaBox box = page->contentBox();
    const double minSide = std::min(box.right - box.left, box.top - box.bottom);
    bool accepted = false;
    const double margin = QInputDialog::getDouble(
        this, tr("裁剪页面"), tr("四边各裁掉多少点（72 点 = 1 英寸）:"), 18.0,
        0.0, std::max(0.0, minSide / 2.0 - 1.0), 1, &accepted);
    if (!accepted || margin <= 0.0) {
        return;
    }
    pdf::MediaBox cropped = box;
    cropped.left += margin;
    cropped.bottom += margin;
    cropped.right -= margin;
    cropped.top -= margin;
    if (!page->setCropBox(cropped)) {
        QMessageBox::warning(this, tr("裁剪页面"), tr("裁剪范围无效。"));
        return;
    }
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (!bytes.isEmpty()) {
        document_.syncBytes(bytes);
    }
    cache_.clear();
    rebuildLayout();
    if (canvas_ != nullptr) {
        canvas_->viewport()->update();
    }
    modified_ = true;
    emit statusMessage(tr("已裁剪第 %1 页").arg(pageIndex + 1));
}

void PdfView::applyBatesNumberingDialog() {
    if (!document_.isValid()) {
        return;
    }
    bool accepted = false;
    const QString prefix = QInputDialog::getText(
        this, tr("贝茨编号"), tr("前缀（仅支持西文）:"), QLineEdit::Normal,
        QStringLiteral("BATES"), &accepted);
    if (!accepted) {
        return;
    }
    const int start = QInputDialog::getInt(this, tr("贝茨编号"), tr("起始编号:"), 1, 0,
                                           999999, 1, &accepted);
    if (!accepted) {
        return;
    }
    pdf::BatesOptions options;
    options.prefix = prefix.trimmed();
    options.start = start;
    if (!pdf::canRenderWithStandardFont(options.prefix)) {
        QMessageBox::warning(this, tr("贝茨编号"),
                             tr("贝茨编号目前只支持西文：中文需要嵌入字体，暂不支持。"));
        return;
    }
    if (!pdf::applyBatesNumbering(document_, options)) {
        QMessageBox::warning(this, tr("贝茨编号"), tr("编号没有写进去。"));
        return;
    }
    modified_ = true;
    refreshAfterDecoration();
    emit statusMessage(tr("已添加贝茨编号"));
}


// ------------------------------------------------ 页面组装（对齐福昕）----

void PdfView::duplicateCurrentPage() {
    if (!document_.isValid()) {
        return;
    }
    const int index = currentPage();
    const pdf::PdfDocument backup = document_.backupPage(index);
    if (!backup.isValid() || !document_.restorePageFrom(backup, index + 1)) {
        emit statusMessage(tr("复制页面失败"));
        return;
    }
    handlePageStructureChanged();
    modified_ = true;
    goToPage(index + 1);
    trace(QStringLiteral("page duplicate index=%1 pages=%2").arg(index).arg(pageCount()));
    emit statusMessage(tr("已复制第 %1 页").arg(index + 1));
}

void PdfView::swapCurrentWithNext() {
    if (!document_.isValid() || annotationController_ == nullptr) {
        return;
    }
    const int index = currentPage();
    if (index < 0 || index + 1 >= document_.pageCount()) {
        emit statusMessage(tr("已经是最后一页了"));
        return;
    }
    if (!annotationController_->movePage(index, index + 1)) {
        emit statusMessage(tr("交换页面失败"));
        return;
    }
    goToPage(index + 1);
    emit statusMessage(tr("已交换第 %1、%2 页").arg(index + 1).arg(index + 2));
}

void PdfView::reversePages() {
    if (!document_.isValid() || annotationController_ == nullptr) {
        return;
    }
    const int count = document_.pageCount();
    if (count < 2) {
        emit statusMessage(tr("只有一页，不需要逆序"));
        return;
    }
    // 按目标位置逐页搬运：每次把「原本在最后的那一页」放到当前位置。
    // 一次操作一条命令，整篇逆序可以用 Ctrl+Z 逐步退回。
    QVector<int> order;
    order.reserve(count);
    for (int i = 0; i < count; ++i) {
        order.append(i);
    }
    for (int target = 0; target < count; ++target) {
        const int desired = count - 1 - target;
        const int position = order.indexOf(desired);
        if (position != target && !annotationController_->movePage(position, target)) {
            emit statusMessage(tr("逆序失败，已停止在第 %1 页").arg(target + 1));
            return;
        }
        order.move(position, target);
    }
    goToPage(0);
    emit statusMessage(tr("已把 %1 页逆序").arg(count));
}

void PdfView::splitDocumentDialog() {
    if (!document_.isValid()) {
        return;
    }
    bool accepted = false;
    const int perFile = QInputDialog::getInt(this, tr("拆分文档"), tr("每份文件包含多少页:"),
                                              1, 1, document_.pageCount(), 1, &accepted);
    if (!accepted) {
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(this, tr("选择输出目录"));
    if (directory.isEmpty()) {
        return;
    }
    const int count = document_.pageCount();
    int written = 0;
    for (int start = 0; start < count; start += perFile) {
        const int end = std::min(count, start + perFile);
        pdf::PdfDocument part = document_.backupPage(start);
        if (!part.isValid()) {
            continue;
        }
        for (int page = start + 1; page < end; ++page) {
            if (!part.restorePageFrom(document_.backupPage(page), part.pageCount())) {
                part = pdf::PdfDocument();
                break;
            }
        }
        if (!part.isValid()) {
            continue;
        }
        QString error;
        const QByteArray bytes = part.serialize(&error);
        if (bytes.isEmpty()) {
            continue;
        }
        const QString base = QFileInfo(filePath_).completeBaseName();
        const QString name = QStringLiteral("%1_%2-%3.pdf")
                                 .arg(base.isEmpty() ? QStringLiteral("part") : base)
                                 .arg(start + 1)
                                 .arg(end);
        QFile file(QDir(directory).filePath(name));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(bytes);
            file.close();
            ++written;
        }
    }
    trace(QStringLiteral("split pages=%1 perFile=%2 written=%3")
              .arg(count).arg(perFile).arg(written));
    emit statusMessage(tr("已拆分出 %1 个文件").arg(written));
}


void PdfView::resizeCurrentPageDialog() {
    if (!document_.isValid()) {
        return;
    }
    const QStringList presets = {tr("A4（595 × 842 点）"), tr("Letter（612 × 792 点）"),
                                 tr("自定义…")};
    bool accepted = false;
    const QString choice = QInputDialog::getItem(this, tr("调整页面大小"), tr("目标尺寸:"),
                                                 presets, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    double width = 595.0;
    double height = 842.0;
    if (choice == presets.at(1)) {
        width = 612.0;
        height = 792.0;
    } else if (choice == presets.at(2)) {
        width = QInputDialog::getDouble(this, tr("调整页面大小"), tr("宽度（点）:"), 595.0,
                                        36.0, 14400.0, 0, &accepted);
        if (!accepted) {
            return;
        }
        height = QInputDialog::getDouble(this, tr("调整页面大小"), tr("高度（点）:"), 842.0,
                                         36.0, 14400.0, 0, &accepted);
        if (!accepted) {
            return;
        }
    }
    const int pageIndex = currentPage();
    auto page = document_.page(pageIndex);
    if (!page.has_value() || !page->resizePage(width, height, true)) {
        QMessageBox::warning(this, tr("调整页面大小"), tr("调整失败。"));
        return;
    }
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (!bytes.isEmpty()) {
        document_.syncBytes(bytes);
    }
    cache_.clear();
    rebuildLayout();
    if (canvas_ != nullptr) {
        canvas_->viewport()->update();
    }
    modified_ = true;
    emit statusMessage(tr("已把第 %1 页调整为 %2 × %3 点")
                           .arg(pageIndex + 1)
                           .arg(qRound(width))
                           .arg(qRound(height)));
}

void PdfView::setPageBackgroundDialog() {
    if (!document_.isValid()) {
        return;
    }
    const QColor color = QColorDialog::getColor(QColor(255, 249, 224), this, tr("页面背景"));
    if (!color.isValid()) {
        return;
    }
    const int pageIndex = currentPage();
    auto page = document_.page(pageIndex);
    if (!page.has_value() || !page->setPageBackground(color)) {
        QMessageBox::warning(this, tr("页面背景"), tr("背景没有写进去。"));
        return;
    }
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (!bytes.isEmpty()) {
        document_.syncBytes(bytes);
    }
    cache_.clear();
    if (canvas_ != nullptr) {
        canvas_->viewport()->update();
    }
    modified_ = true;
    emit statusMessage(tr("已给第 %1 页加背景").arg(pageIndex + 1));
}


namespace {

// 把 source 的第 index 页追加到 target 末尾。target 必须已经有一页。
bool appendPageFrom(pdf::PdfDocument& target, const pdf::PdfDocument& source, int index) {
    const pdf::PdfDocument backup = source.backupPage(index);
    return backup.isValid() && target.restorePageFrom(backup, target.pageCount());
}

}  // namespace

void PdfView::replaceCurrentPageDialog() {
    if (!document_.isValid() || annotationController_ == nullptr) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("替换页面"), QString(),
                                                      tr("PDF 文件 (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    const auto opened = pdf::openDocument(path);
    if (!opened.document.isValid()) {
        QMessageBox::warning(this, tr("替换页面"), tr("打不开这份 PDF：%1").arg(opened.errorMessage));
        return;
    }
    bool accepted = false;
    const int sourcePage = QInputDialog::getInt(this, tr("替换页面"),
                                                tr("用源文件的第几页替换当前页:"), 1, 1,
                                                opened.document.pageCount(), 1, &accepted);
    if (!accepted) {
        return;
    }
    // 备份源页：命令层只认 PageBackup，不依赖源文档还活着。
    PdfPageOperations sourceOps(opened.document);
    std::shared_ptr<doc::PageBackup> replacement;
    if (!sourceOps.backupPage(sourcePage - 1, &replacement) || replacement == nullptr) {
        QMessageBox::warning(this, tr("替换页面"), tr("替换失败。"));
        return;
    }
    const int targetIndex = currentPage();
    if (!annotationController_->replacePageWithBackup(targetIndex, replacement)) {
        QMessageBox::warning(this, tr("替换页面"), tr("替换失败。"));
        return;
    }
    emit statusMessage(tr("已用 %1 的第 %2 页替换第 %3 页（可撤销）")
                           .arg(QFileInfo(path).fileName())
                           .arg(sourcePage)
                           .arg(targetIndex + 1));
}
void PdfView::crossMergeDialog() {
    if (!document_.isValid()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("交叉合并"), QString(),
                                                      tr("PDF 文件 (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    const auto opened = pdf::openDocument(path);
    if (!opened.document.isValid()) {
        QMessageBox::warning(this, tr("交叉合并"), tr("打不开这份 PDF：%1").arg(opened.errorMessage));
        return;
    }
    const int mine = document_.pageCount();
    const int theirs = opened.document.pageCount();
    pdf::PdfDocument result;
    bool seeded = false;
    auto appendAny = [&](const pdf::PdfDocument& source, int pageIndex) {
        if (!seeded) {
            result = source.backupPage(pageIndex);
            seeded = result.isValid();
            return seeded;
        }
        return appendPageFrom(result, source, pageIndex);
    };
    const int total = std::max(mine, theirs);
    for (int index = 0; index < total; ++index) {
        if (index < mine && !appendAny(document_, index)) {
            QMessageBox::warning(this, tr("交叉合并"), tr("合并失败。"));
            return;
        }
        if (index < theirs && !appendAny(opened.document, index)) {
            QMessageBox::warning(this, tr("交叉合并"), tr("合并失败。"));
            return;
        }
    }
    setDocument(std::move(result), filePath_);
    setModified(true);
    emit statusMessage(tr("已与 %1 交叉合并：共 %2 页")
                           .arg(QFileInfo(path).fileName())
                           .arg(pageCount()));
}

void PdfView::extractPagesDialog() {
    if (!document_.isValid()) {
        return;
    }
    const int count = document_.pageCount();
    bool accepted = false;
    const int first = QInputDialog::getInt(this, tr("提取页面"), tr("起始页:"), 1, 1,
                                           count, 1, &accepted);
    if (!accepted) {
        return;
    }
    const int last = QInputDialog::getInt(this, tr("提取页面"), tr("结束页:"), first, first,
                                          count, 1, &accepted);
    if (!accepted) {
        return;
    }
    pdf::PdfDocument part = document_.backupPage(first - 1);
    if (!part.isValid()) {
        return;
    }
    for (int page = first; page < last; ++page) {
        if (!part.restorePageFrom(document_.backupPage(page), part.pageCount())) {
            QMessageBox::warning(this, tr("提取页面"), tr("提取失败。"));
            return;
        }
    }
    const QString base = QFileInfo(filePath_).completeBaseName();
    const QString suggested = QDir(QFileInfo(filePath_).absolutePath())
                                  .filePath(QStringLiteral("%1_%2-%3.pdf")
                                                .arg(base.isEmpty() ? QStringLiteral("extract") : base)
                                                .arg(first)
                                                .arg(last));
    const QString path = QFileDialog::getSaveFileName(this, tr("提取页面"), suggested,
                                                      tr("PDF 文件 (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const QByteArray bytes = part.serialize(&error);
    QFile file(path);
    if (bytes.isEmpty() || !file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(bytes) != bytes.size()) {
        QMessageBox::warning(this, tr("提取页面"), tr("写不了这个文件。"));
        return;
    }
    file.close();
    emit statusMessage(tr("已提取第 %1–%2 页").arg(first).arg(last));
}

void PdfView::insertPagesFromFileDialog() {
    if (!document_.isValid()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("插入页面"), QString(),
                                                      tr("PDF 文件 (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    const auto opened = pdf::openDocument(path);
    if (!opened.document.isValid()) {
        QMessageBox::warning(this, tr("插入页面"), tr("打不开这份 PDF：%1").arg(opened.errorMessage));
        return;
    }
    const int insertAt = currentPage() + 1;
    const int sourceCount = opened.document.pageCount();
    for (int index = 0; index < sourceCount; ++index) {
        const pdf::PdfDocument backup = opened.document.backupPage(index);
        if (!backup.isValid() ||
            !document_.restorePageFrom(backup, insertAt + index)) {
            QMessageBox::warning(this, tr("插入页面"), tr("插入失败。"));
            return;
        }
    }
    handlePageStructureChanged();
    modified_ = true;
    goToPage(insertAt);
    emit statusMessage(tr("已插入 %1 页").arg(sourceCount));
}

void PdfView::refreshAfterDecoration() {
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (!bytes.isEmpty()) {
        document_.syncBytes(bytes);
    }
    cache_.clear();
    if (layout_.pageCount() != document_.pageCount()) {
        rebuildLayout();
    }
    if (canvas_ != nullptr) {
        canvas_->viewport()->update();
    }
}

QVector<pdf::FormField> PdfView::formFields() const {
    return document_.formFields();
}

bool PdfView::setFormFieldValue(int pageIndex, const QString& name, const QString& value) {
    if (!document_.setFormFieldValue(pageIndex, name, value)) {
        return false;
    }
    // 写进去的是数据，画出来靠 PDFium 重绘：清缓存再让可见页重渲染。
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (!bytes.isEmpty()) {
        document_.syncBytes(bytes);
    }
    cache_.clear();
    if (canvas_ != nullptr) {
        canvas_->viewport()->update();
    }
    modified_ = true;
    emit formFieldsChanged();
    return true;
}

bool PdfView::flattenFormsOnPage(int pageIndex) {
    if (!document_.flattenPage(pageIndex)) {
        return false;
    }
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (!bytes.isEmpty()) {
        document_.syncBytes(bytes);
    }
    cache_.clear();
    if (canvas_ != nullptr) {
        canvas_->viewport()->update();
    }
    modified_ = true;
    emit formFieldsChanged();
    return true;
}

void PdfView::mergeDocumentDialog() {
    if (!document_.isValid()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("合并 PDF"), QString(),
                                                      tr("PDF (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    const auto opened = pdf::openDocument(path);
    if (!opened.document.isValid()) {
        QMessageBox::warning(this, tr("合并 PDF"), tr("打不开这份 PDF：%1").arg(opened.errorMessage));
        return;
    }
    if (!document_.appendDocumentPages(opened.document)) {
        QMessageBox::warning(this, tr("合并 PDF"), tr("合并失败。"));
        return;
    }
    refreshAfterDecoration();
    rebuildLayout();
    modified_ = true;
    trace(QStringLiteral("document merged pages=%1").arg(document_.pageCount()));
    emit statusMessage(tr("已合并 %1").arg(QFileInfo(path).fileName()));
}

void PdfView::extractCurrentPageDialog() {
    if (!document_.isValid()) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("抽取当前页"), QString(),
                                                      tr("PDF (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    const QByteArray bytes = document_.extractPagesToBytes({ currentPage() });
    if (bytes.isEmpty()) {
        QMessageBox::warning(this, tr("抽取当前页"), tr("抽取失败。"));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("抽取当前页"), tr("写不了这个文件。"));
        return;
    }
    file.write(bytes);
    file.close();
    trace(QStringLiteral("page extracted index=%1").arg(currentPage()));
    emit statusMessage(tr("已抽取第 %1 页").arg(currentPage() + 1));
}

void PdfView::replaceSelectionWithImageDialog() {
    if (!document_.isValid() || textController_ == nullptr) {
        return;
    }
    const int pageIndex = textController_->selectionPage();
    const QVector<QRectF> displayRects = textController_->selectionRects();
    if (pageIndex < 0 || displayRects.isEmpty()) {
        QMessageBox::information(this, tr("图片替换"), tr("先在页面上选中要替换的区域。"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("图片替换"), QString(),
                                                      tr("图片 (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty()) {
        return;
    }
    QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("图片替换"), tr("这不是一张能读的图片。"));
        return;
    }
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return;
    }
    QRectF userRect;
    for (const QRectF& rect : displayRects) {
        const QPointF a = displayToUser(pageIndex, rect.topLeft());
        const QPointF b = displayToUser(pageIndex, rect.bottomRight());
        userRect = userRect.united(QRectF(a, b).normalized());
    }
    if (userRect.isEmpty()) {
        return;
    }
    if (!page->addImageLayer(userRect, image)) {
        QMessageBox::warning(this, tr("图片替换"), tr("图片没有写进页面。"));
        return;
    }
    refreshAfterDecoration();
    modified_ = true;
    emit statusMessage(tr("已用图片替换选区"));
}

void PdfView::deleteSelectionImagesDialog() {
    if (!document_.isValid() || textController_ == nullptr) {
        return;
    }
    const int pageIndex = textController_->selectionPage();
    const QVector<QRectF> displayRects = textController_->selectionRects();
    if (pageIndex < 0 || displayRects.isEmpty()) {
        QMessageBox::information(this, tr("删除图片"), tr("先框选要删除的图片。"));
        return;
    }
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return;
    }
    QRectF userRect;
    for (const QRectF& rect : displayRects) {
        userRect = userRect.united(
            QRectF(displayToUser(pageIndex, rect.topLeft()),
                   displayToUser(pageIndex, rect.bottomRight())).normalized());
    }
    if (!page->removeImagesInRect(userRect)) {
        QMessageBox::information(this, tr("删除图片"), tr("选区里没有可以删除的图片。"));
        return;
    }
    refreshAfterDecoration();
    modified_ = true;
    emit statusMessage(tr("已删除选区内的图片"));
}

void PdfView::replaceSelectionTextDialog() {
    if (!document_.isValid() || textController_ == nullptr) {
        return;
    }
    const int pageIndex = textController_->selectionPage();
    const QVector<QRectF> displayRects = textController_->selectionRects();
    if (pageIndex < 0 || displayRects.isEmpty()) {
        QMessageBox::information(this, tr("替换文字"), tr("先选中要替换的文字。"));
        return;
    }
    bool accepted = false;
    const QString text = QInputDialog::getText(this, tr("替换文字"),
                                               tr("新文字（仅支持西文）:"), QLineEdit::Normal,
                                               QString(), &accepted);
    if (!accepted || text.isEmpty()) {
        return;
    }
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return;
    }
    QRectF userRect;
    for (const QRectF& rect : displayRects) {
        const QPointF a = displayToUser(pageIndex, rect.topLeft());
        const QPointF b = displayToUser(pageIndex, rect.bottomRight());
        userRect = userRect.united(QRectF(a, b).normalized());
    }
    const double fontSize = std::max(8.0, userRect.height() * 0.8);
    if (!page->replaceTextInRect(userRect, text, fontSize, QColor(20, 24, 32))) {
        QMessageBox::warning(this, tr("替换文字"), tr("替换失败。"));
        return;
    }
    refreshAfterDecoration();
    modified_ = true;
    emit statusMessage(tr("已替换选中文字（行级）"));
}

// 显示空间（页面左上原点、y 向下）→ PDF 用户空间（左下原点、y 向上）。
QPointF PdfView::displayToUser(int pageIndex, const QPointF& displayPoint) const {
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return displayPoint;
    }
    const double height = page->heightPoints();
    return QPointF(displayPoint.x(), height - displayPoint.y());
}

void PdfView::importFormFdfDialog() {
    if (!document_.isValid()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("导入 FDF 数据"), QString(),
                                                      tr("FDF (*.fdf)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("导入 FDF 数据"), tr("读不了这个文件。"));
        return;
    }
    const auto values = pdf::formValuesFromFdf(file.readAll());
    file.close();
    if (values.isEmpty()) {
        QMessageBox::warning(this, tr("导入 FDF 数据"), tr("这份 FDF 里没有可用的字段值。"));
        return;
    }
    // 按名字匹配：只写存在的字段，只读字段跳过。
    const QVector<pdf::FormField> fields = document_.formFields();
    int applied = 0;
    for (const pdf::FormValue& value : values) {
        for (const pdf::FormField& field : fields) {
            if (field.name == value.name && !field.readOnly) {
                if (document_.setFormFieldValue(field.pageIndex, field.name, value.value)) {
                    ++applied;
                }
                break;
            }
        }
    }
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (!bytes.isEmpty()) {
        document_.syncBytes(bytes);
    }
    cache_.clear();
    modified_ = true;
    emit formFieldsChanged();
    emit statusMessage(tr("从 FDF 导入了 %1 个字段").arg(applied));
}

void PdfView::exportFormFdfDialog() {
    if (!document_.isValid()) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("导出 FDF 数据"), QString(),
                                                      tr("FDF (*.fdf)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("导出 FDF 数据"), tr("写不了这个文件。"));
        return;
    }
    file.write(pdf::formValuesToFdf(document_.formFields()));
    file.close();
    emit statusMessage(tr("已导出 FDF 数据"));
}

void PdfView::recordSoundAnnotationDialog() {
    if (!document_.isValid() || !annotationController_) {
        return;
    }
    aipdf::app::AudioRecorder recorder;
    QString error;
    if (!recorder.start(&error)) {
        QMessageBox::warning(this, tr("录制声音批注"), error);
        return;
    }
    // 极简交互：弹一个"正在录音"的框，按确定就是停止。
    QMessageBox box(QMessageBox::Information, tr("录制声音批注"),
                    tr("正在录音…按「确定」停止并插入批注。"), QMessageBox::Ok, this);
    box.exec();
    const QByteArray wav = recorder.stop();
    if (wav.isEmpty()) {
        QMessageBox::warning(this, tr("录制声音批注"), tr("没有录到音频。"));
        return;
    }
    auto page = document_.page(currentPage());
    if (!page.has_value()) {
        return;
    }
    const double width = page->widthPoints();
    const double height = page->heightPoints();
    constexpr double kSize = 28.0;
    const QRectF rect(width / 2.0 - kSize / 2.0, height / 2.0 - kSize / 2.0, kSize, kSize);
    const QString name = QStringLiteral("recording-%1.wav")
                             .arg(QDateTime::currentDateTime().toString("HHmmss"));
    if (annotationController_->addSoundFromWav(currentPage(), rect, name, wav) == 0) {
        QMessageBox::warning(this, tr("录制声音批注"), tr("批注没有创建成功。"));
        return;
    }
    modified_ = true;
    emit statusMessage(tr("已插入录音批注"));
}

void PdfView::startPresentation() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("放映"), tr("先打开一份 PDF。"));
        return;
    }
    // 窗口自己持有一份文档句柄；关掉就随 WA_DeleteOnClose 一起释放。
    auto* presentation = new PresentationWindow(document_, window());
    presentation->start();
}

void PdfView::navigateBack() {
    if (!history_.canGoBack()) {
        return;
    }
    applyViewState(history_.goBack());
}

void PdfView::navigateForward() {
    if (!history_.canGoForward()) {
        return;
    }
    applyViewState(history_.goForward());
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
        pageInfoLabel_->setText(tr("未打开文档"));
        return;
    }
    pageInfoLabel_->setText(tr("第 %1 / %2 页")
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
    emit metricsChanged(tr("缓存 %1 MiB · 待渲染 %2")
                            .arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1)
                            .arg(pending));
}

void PdfView::refreshMetrics() {
    // 忘掉上一次的值，逼 updateMetrics() 重新算一遍并发出新语言的文案。
    lastCacheBytes_ = -1;
    lastPending_ = -1;
    updateMetrics();
}

void PdfView::retranslateUi() {
    texts_.retranslate();
    // 随状态变化的文案由各自的 update 函数按当前语言重算。
    updatePageInfo();
    updateAnnotationToolBar();
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
    // 双页模式下"整行"才是要适配的宽度：totalWidth 是当前缩放下的整行宽（逻辑像素）。
    const double rowWidthPoints = layout_.totalWidth() / zoom_;
    return std::clamp(available / rowWidthPoints, kMinZoom, kMaxZoom);
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
    const double byWidth = availableWidth / (layout_.totalWidth() / zoom_);
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
    // 模型是权威版本：附件内容、作者、审阅状态、回复关系都只存在这里。
    QVector<pdf::Annotation> modelAnnotations;
    if (annotationController_ != nullptr) {
        modelAnnotations = annotationController_->allAnnotations();
    }
    const pdf::SaveOutcome saved =
        pdf::DocumentWriter::save(document_, targetPath, true, modelAnnotations,
                                  encryptionEnabled_ ? &encryptionOptions_ : nullptr);
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

// ------------------------------------------------------------ 打印与导出 ----

void PdfView::printDocument() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("打印"), tr("先打开一份 PDF。"));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("打印"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // 每张纸排几页：系统打印对话框不提供这个选项，问一次就够了。
    const QStringList layouts{tr("1 页"), tr("2 页"), tr("4 页"), tr("6 页"), tr("9 页")};
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(this, tr("打印版式"), tr("每张纸排几页："),
                                                 layouts, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    const int index = layouts.indexOf(chosen);

    PrintOptions options;
    options.pagesPerSheet = QList<int>{ 1, 2, 4, 6, 9 }.value(index < 0 ? 0 : index, 1);
    QString error;
    if (!app::printDocument(document_, &printer, options, &error)) {
        QMessageBox::warning(this, tr("打印失败"), error);
        return;
    }
    trace(QStringLiteral("printed pages=%1 perSheet=%2")
              .arg(document_.pageCount())
              .arg(options.pagesPerSheet));
}

void PdfView::exportPageImages() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("导出图片"), tr("先打开一份 PDF。"));
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(this, tr("导出图片到目录"));
    if (directory.isEmpty()) {
        return;
    }

    const QString baseName = filePath_.isEmpty()
                                 ? QStringLiteral("document")
                                 : QFileInfo(filePath_).completeBaseName();
    ImageExportOptions options;
    const auto result = app::exportPageImages(document_, directory, baseName, options);
    if (!result.ok) {
        QMessageBox::warning(this, tr("导出图片失败"), result.error);
        return;
    }
    trace(QStringLiteral("exported images=%1").arg(result.files.size()));
    QMessageBox::information(this, tr("导出图片"),
                             tr("已导出 %1 张到 %2").arg(result.files.size()).arg(directory));
}

void PdfView::exportPlainText() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("导出文本"), tr("先打开一份 PDF。"));
        return;
    }
    const QString suggested =
        (filePath_.isEmpty() ? QStringLiteral("document") : QFileInfo(filePath_).completeBaseName()) +
        QStringLiteral(".txt");
    const QString path = QFileDialog::getSaveFileName(this, tr("导出文本"), suggested,
                                                      tr("文本文件 (*.txt)"));
    if (path.isEmpty()) {
        return;
    }

    TextExportOptions options;
    const auto result = app::exportPlainText(document_, path, options);
    if (!result.ok) {
        QMessageBox::warning(this, tr("导出文本失败"), result.error);
        return;
    }
    trace(QStringLiteral("exported text path=%1").arg(path));
}

// 记一次恢复日志：改过就要能恢复。
void PdfView::writeRecoveryLog() {    if (!annotationController_ || filePath_.isEmpty()) {
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
