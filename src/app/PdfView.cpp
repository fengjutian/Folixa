#include "app/PdfView.h"

#include "app/AudioRecorder.h"
#include "app/DocumentOutput.h"
#include "app/EncryptionDialog.h"
#include "app/PdfPageOperations.h"
#include "app/Language.h"
#include "pdf/AnnotationSummary.h"
#include "pdf/FormData.h"
#include "pdf/FormFieldWriter.h"
#include "pdf/FormToolSettings.h"
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
#include "pdf/PageAnnotsInjector.h"
#include "pdf/OutlineWriter.h"
#include "pdf/TextReflow.h"
#include "pdf/ContentStream.h"

#include <QColorDialog>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QPrinter>
#include <QPrintDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QMessageBox>
#include <QFileDialog>
#include <QFormLayout>
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
#include <QTextEdit>
#include <QTextBrowser>
#include <QTextBoundaryFinder>
#include <QTableWidget>
#include <QFontMetricsF>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace aipdf::app {

namespace {
constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 8.0;
constexpr double kZoomStep = 1.25;

struct AccessibilityAudit {
    int pages = 0;
    int emptyTextPages = 0;
    int unicodeErrorPages = 0;
    int unnamedFields = 0;
    int fieldsWithoutDescription = 0;
    QStringList details;

    int warningCount() const {
        return emptyTextPages + unicodeErrorPages + unnamedFields + fieldsWithoutDescription;
    }
};

AccessibilityAudit auditDocument(const pdf::PdfDocument& document) {
    AccessibilityAudit audit;
    audit.pages = document.pageCount();
    for (int index = 0; index < document.pageCount(); ++index) {
        const std::optional<pdf::PdfPage> page = document.page(index);
        const std::optional<pdf::TextPage> text = page ? page->extractText() : std::nullopt;
        if (!text || text->isEmpty()) {
            ++audit.emptyTextPages;
            audit.details.append(QObject::tr("第 %1 页没有可提取文字，可能需要 OCR 或替代文本。")
                                     .arg(index + 1));
        } else if (text->unicodeMapError) {
            ++audit.unicodeErrorPages;
            audit.details.append(QObject::tr("第 %1 页有字符缺少 Unicode 映射。")
                                     .arg(index + 1));
        }
    }
    const QVector<pdf::FormField> fields = document.formFields();
    for (const pdf::FormField& field : fields) {
        if (field.name.trimmed().isEmpty()) {
            ++audit.unnamedFields;
            audit.details.append(QObject::tr("第 %1 页存在未命名的表单域。")
                                     .arg(field.pageIndex + 1));
        }
        if (field.toolTip.trimmed().isEmpty()) {
            ++audit.fieldsWithoutDescription;
            audit.details.append(QObject::tr("第 %1 页的表单域“%2”缺少说明。")
                                     .arg(field.pageIndex + 1)
                                     .arg(field.name.isEmpty() ? QObject::tr("未命名") : field.name));
        }
    }
    return audit;
}

QString accessibilityReportHtml(const pdf::PdfDocument& document, const QString& filePath) {
    const AccessibilityAudit audit = auditDocument(document);
    QString html = QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"><style>"
                                  "body{font-family:sans-serif;margin:28px;color:#202a3a}"
                                  "h1{font-size:22px}table{border-collapse:collapse}"
                                  "td,th{border:1px solid #d9dee7;padding:7px 12px;text-align:left}"
                                  ".ok{color:#18794e}.warn{color:#a15c00}</style></head><body>");
    html += QStringLiteral("<h1>%1</h1><p>%2</p>")
                .arg(QObject::tr("辅助功能检查报告").toHtmlEscaped(),
                     QFileInfo(filePath).fileName().toHtmlEscaped());
    html += QStringLiteral("<table><tr><th>%1</th><th>%2</th></tr>")
                .arg(QObject::tr("检查项").toHtmlEscaped(), QObject::tr("结果").toHtmlEscaped());
    const auto row = [&html](const QString& label, int count) {
        html += QStringLiteral("<tr><td>%1</td><td class=\"%2\">%3</td></tr>")
                    .arg(label.toHtmlEscaped(), count == 0 ? QStringLiteral("ok") : QStringLiteral("warn"),
                         count == 0 ? QObject::tr("通过") : QObject::tr("%1 项警告").arg(count));
    };
    row(QObject::tr("页面文本层"), audit.emptyTextPages);
    row(QObject::tr("Unicode 映射"), audit.unicodeErrorPages);
    row(QObject::tr("表单域名称"), audit.unnamedFields);
    row(QObject::tr("表单域说明"), audit.fieldsWithoutDescription);
    html += QStringLiteral("</table><h2>%1</h2>").arg(QObject::tr("详细信息").toHtmlEscaped());
    if (audit.details.isEmpty()) {
        html += QStringLiteral("<p class=\"ok\">%1</p>").arg(QObject::tr("未发现上述基础无障碍问题。").toHtmlEscaped());
    } else {
        html += QStringLiteral("<ul>");
        for (const QString& detail : audit.details) html += QStringLiteral("<li>%1</li>").arg(detail.toHtmlEscaped());
        html += QStringLiteral("</ul>");
    }
    html += QStringLiteral("<p><small>%1</small></p></body></html>")
                .arg(QObject::tr("说明：本检查不等同于 PDF/UA 合规认证，标签树、图片替代文本和语义结构仍需专业工具复核。").toHtmlEscaped());
    return html;
}

struct ThesisCheckResult {
    int textCharacters = 0;
    int repeatedCharacters = 0;
    QVector<QPair<QString, int>> repeatedSegments;

    double rate() const {
        return textCharacters > 0
                   ? qMin(100.0, 100.0 * repeatedCharacters / textCharacters)
                   : 0.0;
    }
};

ThesisCheckResult checkInternalRepetition(const pdf::PdfDocument& document) {
    ThesisCheckResult result;
    QHash<QString, QPair<QString, int>> segments;
    const QRegularExpression separator(QStringLiteral("[。！？!?；;\\n]+"));
    const QRegularExpression ignored(QStringLiteral("[\\s，,、：:（）()《》〈〉“”‘’\"'—…·.]+"));
    for (int pageIndex = 0; pageIndex < document.pageCount(); ++pageIndex) {
        const std::optional<pdf::PdfPage> page = document.page(pageIndex);
        const std::optional<pdf::TextPage> text = page ? page->extractText() : std::nullopt;
        if (!text) continue;
        const QStringList sentences = text->plainText().split(separator, Qt::SkipEmptyParts);
        for (const QString& sentence : sentences) {
            QString normalized = sentence.toCaseFolded();
            normalized.remove(ignored);
            if (normalized.size() < 12) continue;
            result.textCharacters += normalized.size();
            auto it = segments.find(normalized);
            if (it == segments.end()) {
                segments.insert(normalized, qMakePair(sentence.trimmed(), 1));
            } else {
                ++it.value().second;
            }
        }
    }
    for (auto it = segments.cbegin(); it != segments.cend(); ++it) {
        if (it.value().second < 2) continue;
        result.repeatedCharacters += it.key().size() * (it.value().second - 1);
        result.repeatedSegments.append(it.value());
    }
    std::sort(result.repeatedSegments.begin(), result.repeatedSegments.end(),
              [](const auto& left, const auto& right) {
                  if (left.second != right.second) return left.second > right.second;
                  return left.first.size() > right.first.size();
              });
    return result;
}

QString thesisResultHtml(const ThesisCheckResult& result, const QString& filePath) {
    QString html = QStringLiteral("<h2>%1</h2><p><b>%2</b></p>")
                       .arg(QObject::tr("本地重复段落自查").toHtmlEscaped(),
                            QFileInfo(filePath).fileName().toHtmlEscaped());
    html += QStringLiteral("<p>%1：%2<br>%3：%4<br>%5：%6%</p>")
                .arg(QObject::tr("参与分析的字符")).arg(result.textCharacters)
                .arg(QObject::tr("重复字符")).arg(result.repeatedCharacters)
                .arg(QObject::tr("文内重复比例")).arg(result.rate(), 0, 'f', 1);
    html += QStringLiteral("<p><small>%1</small></p>")
                .arg(QObject::tr("此结果只比较当前文档内部的完全重复句段，不连接论文数据库，也不能作为正式查重报告。")
                         .toHtmlEscaped());
    if (result.repeatedSegments.isEmpty()) {
        html += QStringLiteral("<p>%1</p>").arg(QObject::tr("未发现长度不少于 12 字符的重复句段。"));
    } else {
        html += QStringLiteral("<h3>%1</h3><ol>").arg(QObject::tr("重复句段"));
        const int limit = qMin(50, result.repeatedSegments.size());
        for (int index = 0; index < limit; ++index) {
            const auto& segment = result.repeatedSegments.at(index);
            html += QStringLiteral("<li><b>%1 %2</b> — %3</li>")
                        .arg(QObject::tr("出现"))
                        .arg(segment.second)
                        .arg(segment.first.left(240).toHtmlEscaped());
        }
        html += QStringLiteral("</ol>");
    }
    return html;
}
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
    auto* extractShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+X")), this);
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
    // 画布允许压缩到 200 高：小屏上把垂直空间让给 Ribbon 与状态栏。
    canvas_->setMinimumHeight(150);
    canvas_->setTileCache(&cache_);
    connect(canvas_, &PageCanvas::renderRequested, this, &PdfView::requestVisiblePages);
    connect(canvas_, &PageCanvas::currentPageChanged, this, [this](int index) {
        updatePageInfo();
        emit currentPageChanged(index);
    });
    connect(canvas_, &PageCanvas::formFieldRectRequested, this,
            [this](const QString& kind, int pageIndex, const QRectF& rect) {
        createFormFieldAt(kind, pageIndex, rect);
    });
    connect(canvas_, &PageCanvas::objectRectSelected, this,
            [this](int pageIndex, const QRectF& rect) { editObjectsInRect(pageIndex, rect); });
    connect(canvas_, &PageCanvas::objectPointSelected,
            this, &PdfView::selectObjectAtPoint);
    connect(canvas_, &PageCanvas::objectSelectionTransformed,
            this, &PdfView::transformSelectedObjects);
    connect(canvas_, &PageCanvas::objectSelectionRotated,
            this, &PdfView::rotateSelectedObjects);
    connect(canvas_, &PageCanvas::objectDoubleClicked,
            this, &PdfView::beginInlineTextEdit);
    connect(canvas_, &PageCanvas::inlineTextEditCommitted,
            this, &PdfView::commitInlineTextEdit);
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
    ownerPermissionsUnlocked_ = false;
    contentUndo_.clear();
    contentRedo_.clear();
    editUndoTimeline_.clear();
    editRedoTimeline_.clear();
    knownAnnotationUndoCount_ = 0;
    selectedObjectPage_ = -1;
    selectedObjectIndexes_.clear();
    objectCyclePage_ = -1;
    objectCyclePosition_ = -1;
    canvas_->clearObjectSelection();
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
    emit permissionsChanged();

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
                const int currentCount = annotationController_->undoCount();
                if (!replayingEditHistory_ && currentCount > knownAnnotationUndoCount_) {
                    for (int i = knownAnnotationUndoCount_; i < currentCount; ++i)
                        editUndoTimeline_.append(EditHistoryKind::Annotation);
                    editRedoTimeline_.clear();
                    contentRedo_.clear();
                }
                knownAnnotationUndoCount_ = currentCount;
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
                if (!annotationController_) return;
                QByteArray bytes = pendingSignatureImage_;
                QString label = pendingSignatureName_;
                if (bytes.isEmpty()) {
                    const QString path = QFileDialog::getOpenFileName(
                        this, tr("选择图章图片"), QString(), tr("图片 (*.png *.jpg *.jpeg *.bmp)"));
                    if (path.isEmpty()) return;
                    QFile file(path);
                    if (!file.open(QIODevice::ReadOnly)) return;
                    bytes = file.readAll();
                    label = QFileInfo(path).baseName();
                }
                QImage image;
                if (!image.loadFromData(bytes)) {
                    QMessageBox::warning(this, tr("图片图章"), tr("这看起来不是一张图片。"));
                    return;
                }
                // 签名通常比普通图片图章窄；仍保持原图宽高比。
                const double kWidth = pendingSignatureImage_.isEmpty() ? 160.0 : 120.0;
                const double height = kWidth * image.height() / std::max(1, image.width());
                const QRectF rect(userPoint.x() - kWidth / 2.0, userPoint.y() - height / 2.0,
                                  kWidth, height);
                annotationController_->addImageStamp(pageIndex, rect, bytes, label);
                if (!pendingSignatureImage_.isEmpty()) {
                    pendingSignatureImage_.clear();
                    pendingSignatureName_.clear();
                    annotationController_->setTool(AnnotationController::Tool::None);
                    emit statusMessage(tr("签章已放置；保存文档后写入 PDF。"));
                }
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
    trace(QStringLiteral("layout viewport=%1 canvas=%2 content=%3 zoom=%4")
              .arg(canvas_->viewport()->width())
              .arg(canvas_->width())
              .arg(layout_.totalWidth())
              .arg(zoom_));
    emit pageCountChanged(pageCount_);
    updateZoomLabel();
    updatePageInfo();
    // 文档换新了：批注工具栏的可用状态要跟着重算（工具可用、标记类按钮要等选区）。
    updateAnnotationToolBar();
    requestVisiblePages();
    emit controllersRefreshed();
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
    trace(QStringLiteral("layout viewport=%1 canvas=%2 content=%3 zoom=%4")
              .arg(canvas_->viewport()->width())
              .arg(canvas_->width())
              .arg(layout_.totalWidth())
              .arg(zoom_));
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
    contentUndo_.clear();
    contentRedo_.clear();
    editUndoTimeline_.clear();
    editRedoTimeline_.clear();
    knownAnnotationUndoCount_ = 0;
    selectedObjectPage_ = -1;
    selectedObjectIndexes_.clear();
    canvas_->clearObjectSelection();
    history_.reset(ViewState{});
    filePath_.clear();
    pageCount_ = 0;
    layout_.clear();
    cache_.clear();
    canvas_->setLayout(layout_);
    trace(QStringLiteral("layout viewport=%1 canvas=%2 content=%3 zoom=%4")
              .arg(canvas_->viewport()->width())
              .arg(canvas_->width())
              .arg(layout_.totalWidth())
              .arg(zoom_));
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

std::vector<pdf::OutlineItem> PdfView::outline() const {
    return document_.isValid() ? document_.outline() : std::vector<pdf::OutlineItem>{};
}

bool PdfView::replaceOutline(const std::vector<pdf::OutlineItem>& outline, QString* error) {
    if (!document_.isValid()) {
        if (error != nullptr) *error = tr("没有打开的文档。");
        return false;
    }
    QString serializeError;
    const QByteArray bytes = document_.serialize(&serializeError);
    if (bytes.isEmpty()) {
        if (error != nullptr) *error = serializeError;
        return false;
    }
    const pdf::OutlineWriteResult written = pdf::rewriteOutline(bytes, outline);
    if (!written.ok) {
        if (error != nullptr) *error = written.error;
        return false;
    }
    pdf::PdfOpenResult reopened = pdf::openDocumentFromBytes(written.bytes, filePath_);
    if (!reopened.document.isValid()) {
        if (error != nullptr) *error = reopened.errorMessage;
        return false;
    }
    const int pageBefore = currentPage();
    const double zoomBefore = zoom();
    setDocument(std::move(reopened.document), filePath_);
    goToPage(pageBefore);
    setZoom(zoomBefore);
    setModified(true);
    return true;
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
    trace(QStringLiteral("layout viewport=%1 canvas=%2 content=%3 zoom=%4")
              .arg(canvas_->viewport()->width())
              .arg(canvas_->width())
              .arg(layout_.totalWidth())
              .arg(zoom_));
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
    trace(QStringLiteral("layout viewport=%1 canvas=%2 content=%3 zoom=%4")
              .arg(canvas_->viewport()->width())
              .arg(canvas_->width())
              .arg(layout_.totalWidth())
              .arg(zoom_));
    updateZoomLabel();
    requestVisiblePages();
}

bool PdfView::requirePermission(bool allowed, const QString& action) {
    if (allowed) return true;
    const auto choice = QMessageBox::question(
        this, tr("文档权限限制"),
        tr("文档所有者已禁止“%1”。是否输入所有者密码，为本次会话解除限制？").arg(action),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice == QMessageBox::Yes && unlockPermissionsWithOwnerPassword()) return true;
    emit statusMessage(tr("操作被文档权限阻止：%1").arg(action));
    trace(QStringLiteral("permission denied action=%1").arg(action));
    return false;
}

void PdfView::copySelection() {
    if (!requirePermission(effectivePermissions().allowCopying, tr("复制内容"))) return;
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
    // 换布局会重排所有行，滚动像素值不变就会落到别的页上。
    // 记下当前页，重排后回到这一页，切布局不该改变用户正在看的位置。
    const int anchorPage = canvas_ != nullptr ? canvas_->currentPage() : currentPage();
    layout_.setLayoutMode(mode);
    // 适配模式要按新的列宽重算，否则切到双页会有一半内容露在视口外。
    if (fitToWidth_) {
        zoom_ = fitZoomForWidth();
    } else if (fitToPage_) {
        zoom_ = fitZoomForPage();
    }
    layout_.setScale(zoom_);
    rebuildLayout();
    if (canvas_ != nullptr && anchorPage >= 0 && anchorPage < document_.pageCount()) {
        canvas_->goToPage(anchorPage);
    }
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
    trace(QStringLiteral("decor watermark dialog"));
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
    setModified(true);
    refreshAfterDecoration();
    trace(QStringLiteral("decor watermark applied"));
    emit statusMessage(tr("已给每页添加水印"));
}

void PdfView::applyHeaderFooterDialog() {
    trace(QStringLiteral("decor header footer dialog"));
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
    setModified(true);
    refreshAfterDecoration();
    trace(QStringLiteral("decor header footer applied"));
    emit statusMessage(tr("已添加页眉页脚"));
}

void PdfView::applyPageNumbersDialog() {
    if (!document_.isValid()) {
        return;
    }
    pdf::PageNumberOptions options;
    // 页码格式此前是写死的 "%1 / %2"，用户要"第 3 页 / 共 12 页"就改不了。
    // 这里给一个输入框：%1 是当前页码，%2 是总页数。
    bool accepted = false;
    options.format = QInputDialog::getText(
        this, tr("页码"), tr("页码格式（%1 是当前页，%2 是总页数）:"),
        QLineEdit::Normal, options.format, &accepted);
    if (!accepted) {
        return;
    }
    if (!pdf::applyPageNumbers(document_, options)) {
        QMessageBox::warning(this, tr("页码"), tr("页码没有写进去。"));
        return;
    }
    setModified(true);
    refreshAfterDecoration();
    trace(QStringLiteral("decor page numbers applied"));
    emit statusMessage(tr("已添加页码"));
}

// ------------------------------------------------------ 安全（v1.6.0）----

bool PdfView::encryptionEnabled() const {
    return encryptionEnabled_;
}

pdf::DocumentPermissions PdfView::effectivePermissions() const {
    pdf::DocumentPermissions permissions = document_.permissions();
    if (ownerPermissionsUnlocked_) {
        permissions.allowPrinting = true;
        permissions.allowHighQualityPrinting = true;
        permissions.allowCopying = true;
        permissions.allowAccessibility = true;
        permissions.allowAnnotations = true;
        permissions.allowFormFilling = true;
        permissions.allowAssembly = true;
        permissions.allowModifying = true;
    }
    return permissions;
}

bool PdfView::unlockPermissionsWithOwnerPassword() {
    if (!document_.isValid() || !document_.permissions().encrypted || ownerPermissionsUnlocked_)
        return true;
    if (modified_) {
        QMessageBox::information(this, tr("解除权限限制"),
                                 tr("请先保存当前修改，再输入所有者密码解除权限限制。"));
        return false;
    }
    bool accepted = false;
    const QString password = QInputDialog::getText(
        this, tr("所有者密码"), tr("输入 PDF 所有者密码："), QLineEdit::Password,
        QString(), &accepted);
    if (!accepted) return false;
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("解除权限限制"), tr("无法读取原始 PDF。"));
        return false;
    }
    const QByteArray bytes = file.readAll();
    QString error;
    if (!pdf::verifyOwnerPassword(bytes, password, &error)) {
        QMessageBox::warning(this, tr("所有者密码无效"), error);
        return false;
    }
    pdf::PdfOpenResult reopened = pdf::openDocumentFromBytes(bytes, filePath_, password);
    if (!reopened.document.isValid()) {
        QMessageBox::warning(this, tr("解除权限限制"), reopened.errorMessage);
        return false;
    }
    setDocument(std::move(reopened.document), filePath_);
    ownerPermissionsUnlocked_ = true;
    emit permissionsChanged();
    emit statusMessage(tr("已使用所有者密码解除本次会话的权限限制"));
    trace(QStringLiteral("permissions unlocked by owner password"));
    return true;
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

void PdfView::sanitizeHiddenData() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("清理隐藏信息"), tr("先打开一份 PDF。"));
        return;
    }
    if (!pdf::encryptionAvailable()) {
        QMessageBox::warning(this, tr("清理隐藏信息"),
                             tr("本次构建没有启用 qpdf，无法可靠清理隐藏信息。"));
        return;
    }
    if (QMessageBox::question(
            this, tr("清理隐藏信息"),
            tr("将删除文档属性、XMP、文档脚本、自动动作和嵌入文件索引。"
               "页面正文、可见批注和表单值会保留。继续吗？"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    QString error;
    const QByteArray source = document_.serialize(&error);
    if (source.isEmpty()) {
        QMessageBox::warning(this, tr("清理隐藏信息"), error);
        return;
    }
    const QByteArray sanitized = pdf::sanitizeHiddenData(source, &error);
    if (sanitized.isEmpty()) {
        QMessageBox::warning(this, tr("清理隐藏信息"), error);
        return;
    }
    auto reopened = pdf::openDocumentFromBytes(sanitized, filePath_);
    if (!reopened.document.isValid()) {
        QMessageBox::warning(this, tr("清理隐藏信息"),
                             tr("清理结果校验失败：%1").arg(reopened.errorMessage));
        return;
    }
    setDocument(std::move(reopened.document), filePath_);
    setModified(true);
    emit statusMessage(tr("隐藏信息已清理；请保存文档以写入磁盘"));
    trace(QStringLiteral("hidden data sanitized"));
}

void PdfView::redactSelection() {
    if (!document_.isValid() || textController_ == nullptr) return;
    const int pageIndex = textController_->selectionPage();
    const QVector<QRectF> displayRects = textController_->selectionRects();
    if (pageIndex < 0 || displayRects.isEmpty()) {
        QMessageBox::information(this, tr("密文遮盖"), tr("请先选中要永久遮盖的文字区域。"));
        return;
    }
    auto page = document_.page(pageIndex);
    if (!page.has_value()) return;
    if (page->rotationDegrees() != 0) {
        QMessageBox::warning(this, tr("密文遮盖"),
                             tr("为避免旋转坐标误判，请先把该页旋转回 0° 再执行遮盖。"));
        return;
    }
    const pdf::PdfDocument before = document_.backupPage(pageIndex);
    if (QMessageBox::warning(
            this, tr("永久密文遮盖"),
            tr("遮盖会永久删除该页原始内容，并把整页转换为 144 DPI 位图。"
               "该页将失去文本搜索和内容编辑能力，操作不可在保存后恢复。继续吗？"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    QRectF userRect;
    for (const QRectF& rect : displayRects) {
        userRect = userRect.united(
            QRectF(displayToUser(pageIndex, rect.topLeft()),
                   displayToUser(pageIndex, rect.bottomRight())).normalized());
    }
    if (userRect.isEmpty() || !page->redactByRasterizing(userRect, 2.0)) {
        QMessageBox::warning(this, tr("密文遮盖"), tr("无法安全地应用遮盖，文档没有修改。"));
        return;
    }
    refreshAfterDecoration();
    if (before.isValid()) recordContentEdit(pageIndex, before, tr("密文遮盖"));
    setModified(true);
    emit statusMessage(tr("密文遮盖已烧入第 %1 页；请保存文档").arg(pageIndex + 1));
    trace(QStringLiteral("redaction rasterized page=%1").arg(pageIndex + 1));
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
    setModified(true);
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
    setModified(true);
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
    setModified(true);
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

// 把 [start, end) 这一段页写成一个独立文件。
//
// 为什么抽出来：按页数拆、按书签拆、按体积拆是同一件事的三个入口，
// 差别只在"从哪里断"。共用这一段写出逻辑，才不会三处各写一遍、各错一遍。
bool PdfView::writePageRange(int start, int end, const QString& path, QString* error) {
    if (end <= start) {
        if (error != nullptr) {
            *error = tr("这一段没有页。");
        }
        return false;
    }
    pdf::PdfDocument part = document_.backupPage(start);
    if (!part.isValid()) {
        if (error != nullptr) {
            *error = tr("第 %1 页备份失败。").arg(start + 1);
        }
        return false;
    }
    for (int page = start + 1; page < end; ++page) {
        if (!part.restorePageFrom(document_.backupPage(page), part.pageCount())) {
            if (error != nullptr) {
                *error = tr("第 %1 页写入失败。").arg(page + 1);
            }
            return false;
        }
    }
    const QByteArray bytes = part.serialize(error);
    if (bytes.isEmpty()) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = tr("写入 %1 失败。").arg(path);
        }
        return false;
    }
    file.write(bytes);
    file.close();
    return true;
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
        const QString base = QFileInfo(filePath_).completeBaseName();
        const QString name = QStringLiteral("%1_%2-%3.pdf")
                                 .arg(base.isEmpty() ? QStringLiteral("part") : base)
                                 .arg(start + 1)
                                 .arg(end);
        QString error;
        if (writePageRange(start, end, QDir(directory).filePath(name), &error)) {
            ++written;
        } else {
            trace(QStringLiteral("split part failed range=%1-%2 error=%3")
                      .arg(start + 1).arg(end).arg(error));
        }
    }
    trace(QStringLiteral("split pages=%1 perFile=%2 written=%3")
              .arg(count).arg(perFile).arg(written));
    emit statusMessage(tr("已拆分出 %1 个文件").arg(written));
}

// 按书签拆分：每个顶层书签起一份文件。
//
// 为什么按顶层书签而不是所有书签：子书签通常只是章节里的小节，
// 按它拆会切出一堆碎片。顶层书签才是"章"的粒度。
void PdfView::splitDocumentByOutlineDialog() {
    if (!document_.isValid()) {
        return;
    }
    const auto outlineTree = outline();
    QVector<int> starts;
    for (const pdf::OutlineItem& item : outlineTree) {
        if (item.pageIndex >= 0 && item.pageIndex < document_.pageCount()) {
            starts.append(item.pageIndex);
        }
    }
    // 去重并升序：多个书签可能指向同一页，那一页只能归到第一段里。
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    if (starts.size() < 2) {
        QMessageBox::information(this, tr("按书签拆分"),
                                 tr("书签少于两个（或者都指向同一页），没法按书签拆。"));
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(this, tr("选择输出目录"));
    if (directory.isEmpty()) {
        return;
    }
    const int count = document_.pageCount();
    const QString base = QFileInfo(filePath_).completeBaseName();
    int written = 0;
    for (int i = 0; i < starts.size(); ++i) {
        const int start = starts.at(i);
        // 最后一段一直到最后；中间段到下一个书签之前。
        const int end = (i + 1 < starts.size()) ? starts.at(i + 1) : count;
        if (end <= start) {
            continue;
        }
        const QString name = QStringLiteral("%1_%2-%3.pdf")
                                 .arg(base.isEmpty() ? QStringLiteral("part") : base)
                                 .arg(start + 1)
                                 .arg(end);
        QString error;
        if (writePageRange(start, end, QDir(directory).filePath(name), &error)) {
            ++written;
        } else {
            trace(QStringLiteral("split by outline failed range=%1-%2 error=%3")
                      .arg(start + 1).arg(end).arg(error));
        }
    }
    trace(QStringLiteral("split by outline parts=%1 written=%2").arg(starts.size()).arg(written));
    emit statusMessage(tr("已按书签拆分出 %1 个文件").arg(written));
}

// 按体积拆分：每份文件不超过用户给的兆数上限。
//
// 体积是按每页序列化后的字节数累加估出来的，不是精确值——
// 合并成一个文件时还要加交叉引用表和文档目录，实际会略大于累加值。
// 这个偏差不影响使用：切出来的文件会比上限略大一点，但不会差出数量级。
void PdfView::splitDocumentBySizeDialog() {
    if (!document_.isValid()) {
        return;
    }
    bool accepted = false;
    const double limitMb = QInputDialog::getDouble(
        this, tr("按体积拆分"), tr("每个文件最多多少兆:"), 10.0, 0.1, 2048.0, 1, &accepted);
    if (!accepted || limitMb <= 0.0) {
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(this, tr("选择输出目录"));
    if (directory.isEmpty()) {
        return;
    }
    const qint64 limit = static_cast<qint64>(limitMb * 1024 * 1024);
    const int count = document_.pageCount();
    const QString base = QFileInfo(filePath_).completeBaseName();
    int start = 0;
    qint64 accumulated = 0;
    int written = 0;
    for (int page = 0; page < count; ++page) {
        const pdf::PdfDocument single = document_.backupPage(page);
        qint64 pageSize = 0;
        if (single.isValid()) {
            QString sizeError;
            const QByteArray bytes = single.serialize(&sizeError);
            pageSize = bytes.isEmpty() ? 0 : bytes.size();
        }
        // 已经在当前段里，且再加一页就超限 → 先把当前段落盘，再开新段。
        if (page > start && accumulated + pageSize > limit) {
            const QString name = QStringLiteral("%1_%2-%3.pdf")
                                     .arg(base.isEmpty() ? QStringLiteral("part") : base)
                                     .arg(start + 1)
                                     .arg(page);
            QString error;
            if (writePageRange(start, page, QDir(directory).filePath(name), &error)) {
                ++written;
            } else {
                trace(QStringLiteral("split by size failed range=%1-%2 error=%3")
                          .arg(start + 1).arg(page).arg(error));
            }
            start = page;
            accumulated = 0;
        }
        accumulated += pageSize;
    }
    if (start < count) {
        const QString name = QStringLiteral("%1_%2-%3.pdf")
                                 .arg(base.isEmpty() ? QStringLiteral("part") : base)
                                 .arg(start + 1)
                                 .arg(count);
        QString error;
        if (writePageRange(start, count, QDir(directory).filePath(name), &error)) {
            ++written;
        } else {
            trace(QStringLiteral("split by size failed range=%1-%2 error=%3")
                      .arg(start + 1).arg(count).arg(error));
        }
    }
    trace(QStringLiteral("split by size parts=%1 written=%2").arg(written).arg(written));
    emit statusMessage(tr("已按体积拆分出 %1 个文件").arg(written));
}

// 把当前页移到指定位置。
//
// 引擎的 movePage 早就支持任意位置，界面只给了上移/下移两个按钮，
// 于是"把第 30 页挪到第 2 页"这种最常见的整理动作做不了。
//
// 走 AnnotationController 而不是直接动 document_：控制器持有真正的文档，
// 并且知道批注属于哪一页；绕开它移动，批注就会留在原来的页码上。
void PdfView::moveCurrentPageToDialog() {
    if (!requirePermission(effectivePermissions().allowAssembly, tr("移动页面"))) {
        return;
    }
    if (!annotationController_) {
        return;
    }
    const int current = currentPage();
    const int count = annotationController_->pageCount();
    bool accepted = false;
    const int target = QInputDialog::getInt(
        this, tr("移动页面"), tr("把当前页移到第几页:"), current + 1, 1, count, 1, &accepted);
    if (!accepted) {
        return;
    }
    const int to = target - 1;
    if (to == current) {
        return;
    }
    if (!annotationController_->movePage(current, to)) {
        emit statusMessage(tr("移动页面失败"));
        return;
    }
    goToPage(to);
    trace(QStringLiteral("move page to position from=%1 to=%2").arg(current + 1).arg(to + 1));
    emit statusMessage(tr("第 %1 页已移到第 %2 位").arg(current + 1).arg(to + 1));
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
    setModified(true);
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
    setModified(true);
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
    setModified(true);
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
    // 渲染、文本、链接和索引控制器都持有独立的 PDFium 文档副本。
    // 只清缓存会让它们继续读取修改前的内容，必须基于刚同步的字节整体重建。
    stopRenderThread();
    startRenderThread();
    setupControllers();
    emit controllersRefreshed();
    if (layout_.pageCount() != document_.pageCount()) {
        rebuildLayout();
    }
    if (canvas_ != nullptr) {
        canvas_->viewport()->update();
    }
    requestVisiblePages();
}

QVector<pdf::FormField> PdfView::formFields() const {
    return document_.formFields();
}

bool PdfView::setFormFieldValue(int pageIndex, const QString& name, const QString& value) {
    if (!requirePermission(effectivePermissions().allowFormFilling, tr("填写表单"))) return false;
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
    setModified(true);
    emit formFieldsChanged();
    return true;
}

bool PdfView::flattenFormsOnPage(int pageIndex) {
    if (!requirePermission(effectivePermissions().allowModifying, tr("扁平化表单"))) return false;
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
    setModified(true);
    emit formFieldsChanged();
    return true;
}

void PdfView::createFormFieldDialog(const QString& kind) {
    if (!requirePermission(effectivePermissions().allowModifying, tr("创建表单域"))) return;
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("创建表单域"), tr("请先打开一份 PDF。"));
        return;
    }
    if (canvas_ == nullptr) return;
    if (annotationController_ != nullptr)
        annotationController_->setTool(AnnotationController::Tool::None);
    canvas_->setFormFieldPlacementKind(kind);
    emit statusMessage(tr("请在页面上拖出表单字段的位置；按其他工具可取消。"));
}

void PdfView::createFormFieldAt(const QString& kind, int pageIndex, const QRectF& rect) {
    if (!document_.isValid() || rect.width() < 2.0 || rect.height() < 2.0) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("字段属性"));
    auto* form = new QFormLayout(&dialog);
    auto* nameEdit = new QLineEdit(&dialog);
    const QString defaultName = QStringLiteral("%1_%2")
                                    .arg(kind.isEmpty() ? QStringLiteral("field") : kind)
                                    .arg(document_.formFields().size() + 1);
    nameEdit->setText(defaultName);
    auto* tooltipEdit = new QLineEdit(&dialog);
    auto* readOnly = new QCheckBox(tr("只读"), &dialog);
    auto* required = new QCheckBox(tr("必填"), &dialog);
    auto* multiline = new QCheckBox(tr("允许多行"), &dialog);
    auto* optionsEdit = new QTextEdit(&dialog);
    optionsEdit->setPlaceholderText(tr("每行一个选项"));
    optionsEdit->setFixedHeight(90);
    // 单选分组：同名的单选按钮共享一个父字段（/Parent），勾一个其余自动取消。
    // 默认用字段名当组名，用户改成同一个名字就归成一组。
    auto* groupEdit = new QLineEdit(&dialog);
    groupEdit->setPlaceholderText(tr("同名的单选按钮归为一组"));
    // 条形码内容：Code 128 只能编 ASCII，留空时用字段名。
    auto* barcodeEdit = new QLineEdit(&dialog);
    barcodeEdit->setPlaceholderText(tr("条码内容（只能是 ASCII）"));
    auto* barcodeCaptionEdit = new QLineEdit(&dialog);
    barcodeCaptionEdit->setPlaceholderText(tr("条码下方的说明文字"));
    form->addRow(tr("字段名称"), nameEdit);
    form->addRow(tr("工具提示"), tooltipEdit);
    form->addRow(QString(), readOnly);
    form->addRow(QString(), required);
    if (kind == QStringLiteral("text")) form->addRow(QString(), multiline);
    if (kind == QStringLiteral("radio")) form->addRow(tr("单选分组"), groupEdit);
    if (kind == QStringLiteral("barcode")) {
        form->addRow(tr("条码内容"), barcodeEdit);
        form->addRow(tr("条码说明"), barcodeCaptionEdit);
    }
    const bool hasOptions = kind == QStringLiteral("combo") || kind == QStringLiteral("list");
    if (hasOptions) form->addRow(tr("选项"), optionsEdit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("创建表单域"), tr("字段名称不能为空。"));
        return;
    }
    for (const pdf::FormField& existing : document_.formFields()) {
        if (existing.name == name) {
            QMessageBox::warning(this, tr("创建表单域"), tr("字段名称已存在，请换一个名称。"));
            return;
        }
    }

    pdf::FormFieldKind fieldKind = pdf::FormFieldKind::Text;
    if (kind == QStringLiteral("checkbox")) fieldKind = pdf::FormFieldKind::CheckBox;
    else if (kind == QStringLiteral("radio")) fieldKind = pdf::FormFieldKind::RadioButton;
    else if (kind == QStringLiteral("combo")) fieldKind = pdf::FormFieldKind::ComboBox;
    else if (kind == QStringLiteral("list")) fieldKind = pdf::FormFieldKind::ListBox;
    else if (kind == QStringLiteral("button")) fieldKind = pdf::FormFieldKind::PushButton;
    else if (kind == QStringLiteral("barcode")) fieldKind = pdf::FormFieldKind::Barcode;

    QString serializeError;
    const QByteArray source = document_.serialize(&serializeError);
    if (source.isEmpty()) {
        QMessageBox::warning(this, tr("创建表单域"),
                             tr("无法序列化文档：%1").arg(serializeError));
        return;
    }
    pdf::FormFieldInsert insert;
    insert.pageIndex = pageIndex;
    insert.name = name;
    insert.toolTip = tooltipEdit->text().trimmed();
    insert.rect = rect.normalized();
    insert.kind = fieldKind;
    insert.readOnly = readOnly->isChecked();
    insert.required = required->isChecked();
    insert.multiline = multiline->isChecked();
    if (kind == QStringLiteral("radio")) {
        // 分组名留空就退回字段名：单个按钮不受影响，同名字段才会归成一组。
        insert.groupName = groupEdit->text().trimmed().isEmpty() ? name
                                                                : groupEdit->text().trimmed();
    }
    if (kind == QStringLiteral("barcode")) {
        insert.barcodeText = barcodeEdit->text().trimmed();
        insert.barcodeCaption = barcodeCaptionEdit->text().trimmed();
    }
    // 外观统一由「表单工具设置」决定，用户不必逐个域调。
    insert.appearance = pdf::FormToolSettings::load().appearance();
    if (hasOptions)
        insert.options = optionsEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
    const pdf::FormFieldWriteResult written = pdf::appendFormField(source, insert);
    if (!written.ok) {
        QMessageBox::warning(this, tr("创建表单域"), written.error);
        return;
    }
    auto reopened = pdf::openDocumentFromBytes(written.bytes, document_.filePath());
    if (!reopened.document.isValid()) {
        QMessageBox::warning(this, tr("创建表单域"),
                             tr("字段写入后校验失败：%1").arg(reopened.errorMessage));
        return;
    }
    document_ = std::move(reopened.document);
    cache_.clear();
    if (canvas_ != nullptr) canvas_->viewport()->update();
    modified_ = true;
    emit formFieldsChanged();
    emit statusMessage(tr("已在第 %1 页创建表单字段“%2”").arg(pageIndex + 1).arg(name));
}

bool PdfView::editFormFieldProperties(int pageIndex, const QString& name) {
    const QVector<pdf::FormField> fields = document_.formFields();
    auto found = std::find_if(fields.cbegin(), fields.cend(), [&](const pdf::FormField& field) {
        return field.pageIndex == pageIndex && field.name == name;
    });
    if (found == fields.cend()) return false;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("字段属性"));
    auto* form = new QFormLayout(&dialog);
    auto* nameEdit = new QLineEdit(found->name, &dialog);
    nameEdit->setReadOnly(true);
    nameEdit->setToolTip(tr("字段重命名需要同步计算脚本引用，当前保持原名称。"));
    auto* tooltipEdit = new QLineEdit(found->toolTip, &dialog);
    auto* readOnly = new QCheckBox(tr("只读"), &dialog);
    readOnly->setChecked(found->readOnly);
    auto* required = new QCheckBox(tr("必填"), &dialog);
    required->setChecked(found->required);
    auto* multiline = new QCheckBox(tr("允许多行"), &dialog);
    multiline->setChecked(found->multiline);
    form->addRow(tr("字段名称"), nameEdit);
    form->addRow(tr("工具提示"), tooltipEdit);
    form->addRow(QString(), readOnly);
    form->addRow(QString(), required);
    if (found->type == 6) form->addRow(QString(), multiline);
    QTextEdit* optionsEdit = nullptr;
    if (found->type == 4 || found->type == 5) {
        optionsEdit = new QTextEdit(&dialog);
        QStringList optionLines;
        for (const QString& option : found->options) optionLines.append(option);
        optionsEdit->setPlainText(optionLines.join('\n'));
        optionsEdit->setPlaceholderText(tr("每行一个选项"));
        form->addRow(tr("选项"), optionsEdit);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return false;
    const QString newName = nameEdit->text().trimmed();
    if (newName.isEmpty()) return false;
    for (const pdf::FormField& field : fields) {
        if ((field.pageIndex != pageIndex || field.name != name) && field.name == newName) {
            QMessageBox::warning(this, tr("字段属性"), tr("字段名称已存在。"));
            return false;
        }
    }
    if (!document_.setFormFieldProperties(pageIndex, name, newName,
                                           tooltipEdit->text().trimmed(),
                                           readOnly->isChecked(), required->isChecked(),
                                           multiline->isChecked())) return false;
    QString error;
    QByteArray bytes = document_.serialize(&error);
    if (bytes.isEmpty()) return false;
    if (optionsEdit != nullptr) {
        const QStringList options = optionsEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
        const pdf::FormFieldWriteResult updated = pdf::updateFormFieldOptions(bytes, name, options);
        if (!updated.ok) {
            QMessageBox::warning(this, tr("字段属性"), updated.error);
            return false;
        }
        bytes = updated.bytes;
    }
    auto reopened = pdf::openDocumentFromBytes(bytes, document_.filePath());
    if (!reopened.document.isValid()) return false;
    document_ = std::move(reopened.document);
    cache_.clear();
    if (canvas_ != nullptr) canvas_->viewport()->update();
    modified_ = true;
    emit formFieldsChanged();
    return true;
}

bool PdfView::createFormFieldWith(const pdf::FormFieldInsert& insert, QString* reason) {
    if (!requirePermission(effectivePermissions().allowModifying, tr("创建表单域"))) {
        if (reason != nullptr) *reason = tr("这份文档不允许修改。");
        return false;
    }
    if (!document_.isValid()) {
        if (reason != nullptr) *reason = tr("请先打开一份 PDF。");
        return false;
    }
    QString serializeError;
    const QByteArray source = document_.serialize(&serializeError);
    if (source.isEmpty()) {
        if (reason != nullptr)
            *reason = tr("无法序列化文档：%1").arg(serializeError);
        return false;
    }
    const pdf::FormFieldWriteResult written = pdf::appendFormField(source, insert);
    if (!written.ok) {
        if (reason != nullptr) *reason = written.error;
        return false;
    }
    // 写完必须重新打开校验：字段字典写坏了的话，PDFium 可能照样能渲染，
    // 但别的阅读器会认为这份文件是坏的。
    auto reopened = pdf::openDocumentFromBytes(written.bytes, document_.filePath());
    if (!reopened.document.isValid()) {
        if (reason != nullptr)
            *reason = tr("字段写入后校验失败：%1").arg(reopened.errorMessage);
        return false;
    }
    document_ = std::move(reopened.document);
    cache_.clear();
    if (canvas_ != nullptr) canvas_->viewport()->update();
    modified_ = true;
    emit formFieldsChanged();
    return true;
}

QVector<pdf::FormFieldScript> PdfView::formFieldScripts() const {
    if (!document_.isValid()) return {};
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (bytes.isEmpty()) return {};
    return pdf::readFormFieldScripts(bytes);
}

QStringList PdfView::formCalculationOrder() const {
    if (!document_.isValid()) return {};
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (bytes.isEmpty()) return {};
    return pdf::readCalculationOrder(bytes);
}

bool PdfView::saveFormScripts(const QVector<pdf::FormFieldScript>& scripts,
                              const QStringList& order, QString* reason) {
    if (!requirePermission(effectivePermissions().allowModifying, tr("写入表单脚本"))) {
        if (reason != nullptr) *reason = tr("这份文档不允许修改。");
        return false;
    }
    QString serializeError;
    const QByteArray source = document_.serialize(&serializeError);
    if (source.isEmpty()) {
        if (reason != nullptr)
            *reason = tr("无法序列化文档：%1").arg(serializeError);
        return false;
    }
    const pdf::FormScriptWriteResult written = pdf::writeFormScripts(source, scripts, order);
    if (!written.ok) {
        if (reason != nullptr) *reason = written.error;
        return false;
    }
    auto reopened = pdf::openDocumentFromBytes(written.bytes, document_.filePath());
    if (!reopened.document.isValid()) {
        if (reason != nullptr)
            *reason = tr("写入后校验失败：%1").arg(reopened.errorMessage);
        return false;
    }
    document_ = std::move(reopened.document);
    cache_.clear();
    if (canvas_ != nullptr) canvas_->viewport()->update();
    modified_ = true;
    emit formFieldsChanged();
    return true;
}

int PdfView::resetFormFields() {
    // 有默认值的恢复默认值，没有的清空——这才是"重置"，而不是"抹掉"。
    QHash<QString, QString> defaults;
    for (const pdf::FormFieldScript& script : formFieldScripts())
        defaults.insert(script.fieldName, script.defaultValue);
    int count = 0;
    for (const pdf::FormField& field : document_.formFields()) {
        if (field.readOnly) continue;
        const QString value = defaults.value(field.name);
        if (setFormFieldValue(field.pageIndex, field.name, value)) ++count;
    }
    return count;
}

void PdfView::mergeDocumentDialog() {
    if (!requirePermission(effectivePermissions().allowAssembly, tr("合并页面"))) return;
    trace(QStringLiteral("document merge dialog"));
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
    setModified(true);
    trace(QStringLiteral("document merged pages=%1").arg(document_.pageCount()));
    emit statusMessage(tr("已合并 %1").arg(QFileInfo(path).fileName()));
}

void PdfView::extractCurrentPageDialog() {
    if (!requirePermission(effectivePermissions().allowAssembly, tr("抽取页面"))) return;
    trace(QStringLiteral("page extract dialog"));
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
    setModified(true);
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
    setModified(true);
    emit statusMessage(tr("已删除选区内的图片"));
}

void PdfView::replaceSelectionTextDialog() {
    if (!document_.isValid() || textController_ == nullptr) {
        return;
    }
    const int pageIndex = textController_->selectionPage();
    const QVector<QRectF> displayRects = textController_->selectionRects();
    const QString originalText = textController_->selectedText();
    if (pageIndex < 0 || displayRects.isEmpty()) {
        QMessageBox::information(this, tr("替换文字"), tr("先选中要替换的文字。"));
        return;
    }
    bool accepted = false;
    const QString text = QInputDialog::getText(this, tr("替换文字"),
                                               tr("新文字:"), QLineEdit::Normal,
                                               QString(), &accepted);
    if (!accepted) {
        return;
    }
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return;
    }
    const pdf::PdfDocument before = document_.backupPage(pageIndex);
    const auto extracted = page->extractText();
    if (before.isValid() && extracted && !originalText.isEmpty() &&
        extracted->plainText().count(originalText, Qt::CaseSensitive) == 1) {
        QString serializeError;
        const QByteArray input = document_.serialize(&serializeError);
        int count = 0;
        QString rewriteError;
        const QByteArray rewritten = pdf::ContentStream::rewritePdfText(
            input, {pageIndex}, originalText, text, Qt::CaseSensitive, &count, &rewriteError);
        QString reloadError;
        if (count == 1 && !rewritten.isEmpty()) {
            page.reset();
        }
        if (count == 1 && !rewritten.isEmpty() && document_.reloadFromBytes(rewritten, &reloadError)) {
            refreshAfterDecoration();
            recordContentEdit(pageIndex, before, tr("安全替换选中文字"));
            setModified(true);
            emit statusMessage(tr("已安全替换选中文字"));
            return;
        }
        if (!page.has_value()) page = document_.page(pageIndex);
    }
    QRectF userRect;
    for (const QRectF& rect : displayRects) {
        const QPointF a = displayToUser(pageIndex, rect.topLeft());
        const QPointF b = displayToUser(pageIndex, rect.bottomRight());
        userRect = userRect.united(QRectF(a, b).normalized());
    }
    const auto textObjects = page->objectsInRect(userRect, 0.5);
    QVector<int> textIndexes;
    for (const auto& object : textObjects)
        if (object.type == pdf::PageObjectType::Text) textIndexes.append(object.index);
    const double fontSize = std::max(8.0, userRect.height() * 0.8);
    const bool replaced = textIndexes.size() == 1
        ? page->setTextObjectText(textIndexes.constFirst(), text)
        : page->replaceTextInRect(userRect, text, fontSize, QColor(20, 24, 32));
    if (!replaced) {
        QMessageBox::warning(this, tr("替换文字"), tr("替换失败。"));
        return;
    }
    refreshAfterDecoration();
    if (before.isValid()) recordContentEdit(pageIndex, before, tr("替换选中文字"));
    setModified(true);
    emit statusMessage(tr("已替换选中文字（行级）"));
}

void PdfView::editSelectedObjectsDialog() {
    if (!document_.isValid() || textController_ == nullptr) return;
    if (!requirePermission(effectivePermissions().allowModifying, tr("编辑页面对象"))) return;
    if (selectedObjectPage_ >= 0 && !selectedObjectIndexes_.isEmpty()) {
        auto selectedPage = document_.page(selectedObjectPage_);
        QRectF selectedBounds;
        if (selectedPage) {
            const auto objects = selectedPage->objectsInRect(
                QRectF(0, 0, selectedPage->widthPoints(), selectedPage->heightPoints()), 0.0);
            for (const auto& object : objects)
                if (selectedObjectIndexes_.contains(object.index))
                    selectedBounds = selectedBounds.united(object.bounds);
        }
        if (!selectedBounds.isEmpty()) {
            editObjectsInRect(selectedObjectPage_, selectedBounds, true);
            return;
        }
    }
    const int pageIndex = textController_->selectionPage();
    const QVector<QRectF> displayRects = textController_->selectionRects();
    if (pageIndex < 0 || displayRects.isEmpty()) {
        canvas_->beginObjectSelection();
        emit statusMessage(tr("请在页面上拖框选择一个或多个对象"));
        return;
    }
    auto page = document_.page(pageIndex);
    if (!page) return;
    QRectF userRect;
    for (const QRectF& rect : displayRects)
        userRect = userRect.united(QRectF(displayToUser(pageIndex, rect.topLeft()),
                                         displayToUser(pageIndex, rect.bottomRight())).normalized());
    editObjectsInRect(pageIndex, userRect);
}

void PdfView::editObjectsInRect(int pageIndex, const QRectF& userRect, bool useCurrentSelection) {
    auto page = document_.page(pageIndex);
    if (!page || userRect.isEmpty()) return;
    QVector<pdf::PageObjectInfo> objects = page->objectsInRect(userRect);
    if (useCurrentSelection && pageIndex == selectedObjectPage_ && !selectedObjectIndexes_.isEmpty())
        objects.erase(std::remove_if(objects.begin(), objects.end(), [this](const auto& object) {
            return !selectedObjectIndexes_.contains(object.index);
        }), objects.end());
    if (!useCurrentSelection) {
        const auto allObjects = page->objectsInRect(
            QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
        QSet<int> groups;
        for (const auto& object : objects) if (object.groupId >= 0) groups.insert(object.groupId);
        for (const auto& object : allObjects)
            if (groups.contains(object.groupId) &&
                std::none_of(objects.cbegin(), objects.cend(), [&object](const auto& selected) {
                    return selected.index == object.index;
                })) objects.append(object);
    }
    if (objects.isEmpty()) {
        QMessageBox::information(this, tr("编辑对象"), tr("选区内没有可编辑的页面对象。"));
        return;
    }
    QVector<int> indexes;
    for (const auto& object : objects) indexes.append(object.index);
    selectedObjectPage_ = pageIndex;
    selectedObjectIndexes_ = indexes;
    canvas_->setObjectSelection(pageIndex, userRect);
    PageTransform selectionTransform;
    if (transformForIndexPage(pageIndex, &selectionTransform)) {
        QVector<QRectF> guides;
        const auto allObjects = page->objectsInRect(
            QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
        for (const auto& object : allObjects)
            if (!indexes.contains(object.index)) guides.append(selectionTransform.toDisplay(object.bounds));
        canvas_->setObjectSnapGuides(guides);
    }
    QStringList actions{tr("移动"), tr("缩放"), tr("删除"), tr("文字属性"),
                        tr("精确几何"), tr("旋转"), tr("透明度")};
    const int imageCount = std::count_if(objects.cbegin(), objects.cend(), [](const auto& object) {
        return object.type == pdf::PageObjectType::Image;
    });
    if (imageCount == 1) actions.append({tr("替换图片对象"), tr("裁剪图片对象")});
    if (objects.size() > 1) actions.append(tr("组合"));
    if (std::any_of(objects.cbegin(), objects.cend(), [](const auto& object) { return object.groupId >= 0; }))
        actions.append(tr("取消组合"));
    actions.append(std::any_of(objects.cbegin(), objects.cend(), [](const auto& object) { return object.locked; })
                       ? tr("解锁") : tr("锁定"));
    if (objects.size() == 1) actions.append({tr("置于顶层"), tr("置于底层"),
                                             tr("上移一层"), tr("下移一层")});
    bool accepted = false;
    const QString action = QInputDialog::getItem(this, tr("编辑对象"),
                                                  tr("已选择 %1 个对象：").arg(objects.size()),
                                                  actions, 0, false, &accepted);
    if (!accepted) return;
    const bool hasLocked = std::any_of(objects.cbegin(), objects.cend(),
                                       [](const auto& object) { return object.locked; });
    if (hasLocked && action != tr("解锁")) {
        QMessageBox::information(this, tr("编辑对象"), tr("选区包含锁定对象，请先解锁"));
        return;
    }
    const pdf::PdfDocument before = document_.backupPage(pageIndex);
    if (!before.isValid()) {
        QMessageBox::warning(this, tr("编辑对象"), tr("无法创建页面安全备份，文档没有修改。"));
        return;
    }
    bool changed = false;
    if (action == actions[0]) {
        const double dx = QInputDialog::getDouble(this, tr("移动对象"), tr("水平移动（点）:"), 0, -10000, 10000, 1, &accepted);
        if (!accepted) return;
        const double dy = QInputDialog::getDouble(this, tr("移动对象"), tr("垂直移动（点）:"), 0, -10000, 10000, 1, &accepted);
        if (!accepted) return;
        changed = page->transformObjects(indexes, dx, dy);
    } else if (action == actions[1]) {
        const double horizontal = QInputDialog::getDouble(this, tr("缩放对象"), tr("水平缩放（%）:"), 100, 1, 1000, 1, &accepted);
        if (!accepted) return;
        const double vertical = QInputDialog::getDouble(this, tr("缩放对象"), tr("垂直缩放（%）:"), 100, 1, 1000, 1, &accepted);
        if (!accepted) return;
        changed = page->transformObjects(indexes, 0, 0, horizontal / 100.0, vertical / 100.0);
    } else if (action == actions[2]) {
        if (QMessageBox::question(this, tr("删除对象"), tr("永久删除选中的 %1 个页面对象？").arg(objects.size())) != QMessageBox::Yes) return;
        changed = page->removeObjects(indexes);
    } else if (action == actions[3]) {
        QVector<int> textIndexes;
        double initialSize = 12.0;
        QString initialFamily = QStringLiteral("Helvetica");
        QColor initialColor = Qt::black;
        for (const auto& object : objects) if (object.type == pdf::PageObjectType::Text) {
            textIndexes.append(object.index);
            if (object.fontSize > 0) initialSize = object.fontSize;
            if (!object.fontFamily.isEmpty()) initialFamily = object.fontFamily;
            initialColor = object.fillColor;
        }
        if (textIndexes.isEmpty()) {
            QMessageBox::information(this, tr("文字属性"), tr("选区内没有文字对象。"));
            return;
        }
        const QStringList families{QStringLiteral("Helvetica"), QStringLiteral("Helvetica Bold"),
                                   QStringLiteral("Helvetica Oblique"), QStringLiteral("Helvetica Bold Oblique"),
                                   QStringLiteral("Times Roman"), QStringLiteral("Times Bold"),
                                   QStringLiteral("Times Italic"), QStringLiteral("Times Bold Italic"),
                                   QStringLiteral("Courier"), QStringLiteral("Courier Bold"),
                                   QStringLiteral("Courier Oblique"), QStringLiteral("Courier Bold Oblique"),
                                   QStringLiteral("Microsoft YaHei"),
                                   QStringLiteral("SimSun"), QStringLiteral("SimHei")};
        int familyIndex = families.indexOf(initialFamily);
        if (familyIndex < 0) familyIndex = 0;
        const QString family = QInputDialog::getItem(this, tr("文字属性"), tr("字体:"),
                                                      families, familyIndex, false, &accepted);
        if (!accepted) return;
        const double size = QInputDialog::getDouble(this, tr("文字属性"), tr("字号:"), initialSize, 1, 500, 1, &accepted);
        if (!accepted) return;
        const QColor color = QColorDialog::getColor(initialColor, this, tr("文字颜色"));
        if (!color.isValid()) return;
        const double opacity = QInputDialog::getDouble(this, tr("文字属性"), tr("透明度（%）:"),
                                                        initialColor.alphaF() * 100.0, 0, 100, 1, &accepted);
        if (!accepted) return;
        const double horizontal = QInputDialog::getDouble(this, tr("文字属性"), tr("水平缩放（%）:"),
                                                           100, 1, 1000, 1, &accepted);
        if (!accepted) return;
        const double baseline = QInputDialog::getDouble(this, tr("文字属性"), tr("基线偏移（点）:"),
                                                         0, -1000, 1000, 1, &accepted);
        if (!accepted) return;
        changed = page->setTextObjectProperties(textIndexes, family, size, color,
                                                opacity / 100.0, horizontal / 100.0, baseline);
    } else if (action == actions[4]) {
        const double x = QInputDialog::getDouble(this, tr("精确几何"), tr("X 坐标（点）:"),
                                                  userRect.left(), -100000, 100000, 2, &accepted);
        if (!accepted) return;
        const double y = QInputDialog::getDouble(this, tr("精确几何"), tr("Y 坐标（点）:"),
                                                  userRect.top(), -100000, 100000, 2, &accepted);
        if (!accepted) return;
        const double width = QInputDialog::getDouble(this, tr("精确几何"), tr("宽度（点）:"),
                                                      userRect.width(), 0.01, 100000, 2, &accepted);
        if (!accepted) return;
        const double height = QInputDialog::getDouble(this, tr("精确几何"), tr("高度（点）:"),
                                                       userRect.height(), 0.01, 100000, 2, &accepted);
        if (!accepted) return;
        changed = page->transformObjects(indexes, x - userRect.left(), y - userRect.top(),
                                         width / userRect.width(), height / userRect.height());
    } else if (action == actions[5]) {
        const double degrees = QInputDialog::getDouble(this, tr("旋转对象"), tr("旋转角度（度）:"),
                                                        0, -3600, 3600, 1, &accepted);
        if (!accepted) return;
        changed = page->rotateObjects(indexes, degrees);
    } else if (action == tr("透明度")) {
        const double opacity = QInputDialog::getDouble(this, tr("透明度"), tr("透明度（%）:"),
                                                        100, 0, 100, 1, &accepted);
        if (!accepted) return;
        changed = page->setObjectOpacity(indexes, opacity / 100.0);
    } else if (action == tr("替换图片对象")) {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("选择替换图片"), QString(), tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (path.isEmpty()) return;
        const QImage image(path);
        if (image.isNull()) return;
        const auto imageObject = std::find_if(objects.cbegin(), objects.cend(), [](const auto& object) {
            return object.type == pdf::PageObjectType::Image;
        });
        changed = imageObject != objects.cend() && page->replaceImageObject(imageObject->index, image);
    } else if (action == tr("裁剪图片对象")) {
        const double left = QInputDialog::getDouble(this, tr("裁剪图片对象"), tr("左侧裁剪（%）:"),
                                                     0, 0, 99, 1, &accepted);
        if (!accepted) return;
        const double top = QInputDialog::getDouble(this, tr("裁剪图片对象"), tr("顶部裁剪（%）:"),
                                                    0, 0, 99, 1, &accepted);
        if (!accepted) return;
        const double right = QInputDialog::getDouble(this, tr("裁剪图片对象"), tr("右侧裁剪（%）:"),
                                                      0, 0, 99, 1, &accepted);
        if (!accepted) return;
        const double bottom = QInputDialog::getDouble(this, tr("裁剪图片对象"), tr("底部裁剪（%）:"),
                                                       0, 0, 99, 1, &accepted);
        if (!accepted || left + right >= 100 || top + bottom >= 100) return;
        const auto imageObject = std::find_if(objects.cbegin(), objects.cend(), [](const auto& object) {
            return object.type == pdf::PageObjectType::Image;
        });
        changed = imageObject != objects.cend() && page->cropImageObject(
            imageObject->index, QRectF(left / 100.0, top / 100.0,
                                       1.0 - (left + right) / 100.0,
                                       1.0 - (top + bottom) / 100.0));
    } else if (action == tr("组合")) {
        static int nextGroupId = 1;
        changed = page->setObjectsGroup(indexes, nextGroupId++);
    } else if (action == tr("取消组合")) {
        changed = page->clearObjectsGroup(indexes);
    } else if (action == tr("锁定")) {
        changed = page->setObjectsLocked(indexes, true);
    } else if (action == tr("解锁")) {
        changed = page->setObjectsLocked(indexes, false);
    } else {
        const int index = indexes.constFirst();
        const int count = page->objectsInRect(
            QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0).size();
        const int target = action == tr("置于顶层") ? count - 1
            : action == tr("置于底层") ? 0
            : action == tr("上移一层") ? qMin(count - 1, index + 1)
                                        : qMax(0, index - 1);
        changed = page->reorderObject(index, target);
    }
    if (!changed) {
        QMessageBox::warning(this, tr("编辑对象"), tr("页面对象没有修改。"));
        return;
    }
    refreshAfterDecoration();
    setModified(true);
    recordContentEdit(pageIndex, before, action);
    if (action == actions[2] || action == tr("置于顶层") || action == tr("置于底层") ||
        action == tr("上移一层") || action == tr("下移一层")) {
        canvas_->clearObjectSelection();
        selectedObjectPage_ = -1;
        selectedObjectIndexes_.clear();
    }
    emit statusMessage(tr("已修改 %1 个页面对象").arg(objects.size()));
}

void PdfView::selectObjectAtPoint(int pageIndex, const QPointF& userPoint, bool additive) {
    auto page = document_.page(pageIndex);
    if (!page) return;
    constexpr double kHitRadius = 4.0;
    QVector<pdf::PageObjectInfo> hits = page->objectsInRect(
        QRectF(userPoint.x() - kHitRadius, userPoint.y() - kHitRadius,
               kHitRadius * 2.0, kHitRadius * 2.0), 0.0);
    hits.erase(std::remove_if(hits.begin(), hits.end(), [userPoint](const auto& object) {
        return !object.bounds.adjusted(-2.0, -2.0, 2.0, 2.0).contains(userPoint);
    }), hits.end());
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
        return a.index > b.index;
    });
    if (hits.isEmpty()) {
        if (!additive) {
            selectedObjectPage_ = -1;
            selectedObjectIndexes_.clear();
            canvas_->clearObjectSelection();
        }
        emit statusMessage(tr("当前位置没有页面对象"));
        return;
    }
    const bool samePoint = objectCyclePage_ == pageIndex &&
                           QLineF(objectCyclePoint_, userPoint).length() <= kHitRadius;
    objectCyclePosition_ = samePoint ? (objectCyclePosition_ + 1) % hits.size() : 0;
    objectCyclePage_ = pageIndex;
    objectCyclePoint_ = userPoint;
    const pdf::PageObjectInfo& hit = hits.at(objectCyclePosition_);
    const auto all = page->objectsInRect(
        QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
    QVector<int> hitIndexes{hit.index};
    if (hit.groupId >= 0) {
        hitIndexes.clear();
        for (const auto& object : all)
            if (object.groupId == hit.groupId) hitIndexes.append(object.index);
    }
    if (!additive || selectedObjectPage_ != pageIndex) {
        selectedObjectIndexes_ = hitIndexes;
    } else if (selectedObjectIndexes_.contains(hit.index)) {
        for (int index : hitIndexes) selectedObjectIndexes_.removeAll(index);
    } else {
        for (int index : hitIndexes)
            if (!selectedObjectIndexes_.contains(index)) selectedObjectIndexes_.append(index);
    }
    selectedObjectPage_ = selectedObjectIndexes_.isEmpty() ? -1 : pageIndex;
    if (selectedObjectIndexes_.isEmpty()) {
        canvas_->clearObjectSelection();
        return;
    }
    QRectF bounds;
    QVector<QRectF> snapRects;
    PageTransform transform;
    transformForIndexPage(pageIndex, &transform);
    for (const auto& object : all)
        if (selectedObjectIndexes_.contains(object.index)) bounds = bounds.united(object.bounds);
        else snapRects.append(transform.toDisplay(object.bounds));
    canvas_->setObjectSnapGuides(snapRects);
    canvas_->setObjectSelection(pageIndex, bounds);
    emit statusMessage(hits.size() > 1
        ? tr("已选择 %1 个对象；再次单击可切换下层对象").arg(selectedObjectIndexes_.size())
        : tr("已选择 %1 个对象").arg(selectedObjectIndexes_.size()));
}

void PdfView::transformSelectedObjects(int pageIndex, const QRectF& oldRect,
                                       const QRectF& newRect) {
    if (pageIndex != selectedObjectPage_ || selectedObjectIndexes_.isEmpty() ||
        oldRect.isEmpty() || newRect.isEmpty()) {
        return;
    }
    if (!requirePermission(effectivePermissions().allowModifying, tr("编辑页面对象"))) {
        canvas_->setObjectSelection(pageIndex, oldRect);
        return;
    }
    auto page = document_.page(pageIndex);
    if (page) {
        const auto objects = page->objectsInRect(
            QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
        if (std::any_of(objects.cbegin(), objects.cend(), [this](const auto& object) {
                return selectedObjectIndexes_.contains(object.index) && object.locked;
            })) {
            canvas_->setObjectSelection(pageIndex, oldRect);
            emit statusMessage(tr("选区包含锁定对象，请先解锁"));
            return;
        }
    }
    const pdf::PdfDocument before = document_.backupPage(pageIndex);
    if (!page || !before.isValid()) {
        canvas_->setObjectSelection(pageIndex, oldRect);
        return;
    }
    const double scaleX = newRect.width() / oldRect.width();
    const double scaleY = newRect.height() / oldRect.height();
    if (!page->transformObjects(selectedObjectIndexes_, newRect.left() - oldRect.left(),
                                newRect.top() - oldRect.top(), scaleX, scaleY)) {
        canvas_->setObjectSelection(pageIndex, oldRect);
        emit statusMessage(tr("页面对象没有修改。"));
        return;
    }
    refreshAfterDecoration();
    setModified(true);
    recordContentEdit(pageIndex, before, tr("变换页面对象"));
    emit statusMessage(tr("已变换 %1 个页面对象").arg(selectedObjectIndexes_.size()));
}

void PdfView::rotateSelectedObjects(int pageIndex, double degrees) {
    if (pageIndex != selectedObjectPage_ || selectedObjectIndexes_.isEmpty()) return;
    auto page = document_.page(pageIndex);
    if (page) {
        const auto objects = page->objectsInRect(
            QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
        if (std::any_of(objects.cbegin(), objects.cend(), [this](const auto& object) {
                return selectedObjectIndexes_.contains(object.index) && object.locked;
            })) {
            emit statusMessage(tr("选区包含锁定对象，请先解锁"));
            return;
        }
    }
    const pdf::PdfDocument before = document_.backupPage(pageIndex);
    if (!page || !before.isValid() || !page->rotateObjects(selectedObjectIndexes_, -degrees)) {
        emit statusMessage(tr("页面对象没有修改。"));
        return;
    }
    refreshAfterDecoration();
    recordContentEdit(pageIndex, before, tr("旋转页面对象"));
    auto refreshed = document_.page(pageIndex);
    QRectF bounds;
    if (refreshed) {
        const auto objects = refreshed->objectsInRect(
            QRectF(0, 0, refreshed->widthPoints(), refreshed->heightPoints()), 0.0);
        for (const auto& object : objects)
            if (selectedObjectIndexes_.contains(object.index)) bounds = bounds.united(object.bounds);
    }
    if (!bounds.isEmpty()) canvas_->setObjectSelection(pageIndex, bounds);
    setModified(true);
    emit statusMessage(tr("已旋转 %1 个页面对象").arg(selectedObjectIndexes_.size()));
}

void PdfView::beginInlineTextEdit(int pageIndex, const QPointF& userPoint) {
    auto page = document_.page(pageIndex);
    if (!page) return;
    const double radius = 4.0;
    auto objects = page->objectsInRect(
        QRectF(userPoint.x() - radius, userPoint.y() - radius, radius * 2, radius * 2), 0.0);
    std::sort(objects.begin(), objects.end(), [](const auto& a, const auto& b) {
        return a.index > b.index;
    });
    const auto found = std::find_if(objects.begin(), objects.end(), [userPoint](const auto& object) {
        return object.type == pdf::PageObjectType::Text &&
               object.bounds.adjusted(-2, -2, 2, 2).contains(userPoint);
    });
    if (found == objects.end()) {
        emit statusMessage(tr("双击位置没有可编辑文字"));
        return;
    }
    if (found->locked) {
        emit statusMessage(tr("文字对象已锁定，请先解锁"));
        return;
    }
    PageTransform transform;
    if (!transformForIndexPage(pageIndex, &transform)) return;
    selectedObjectPage_ = pageIndex;
    selectedObjectIndexes_ = {found->index};
    canvas_->setObjectSelection(pageIndex, found->bounds);
    canvas_->beginInlineTextEdit(pageIndex, found->index,
                                 transform.toDisplay(found->bounds), found->text,
                                 found->fontSize);
}

void PdfView::commitInlineTextEdit(int pageIndex, int objectIndex, const QString& text) {
    if (!requirePermission(effectivePermissions().allowModifying, tr("编辑文字"))) return;
    auto page = document_.page(pageIndex);
    const pdf::PdfDocument before = document_.backupPage(pageIndex);
    if (!page || !before.isValid() || !page->setTextObjectText(objectIndex, text)) {
        QMessageBox::warning(this, tr("编辑文字"),
                             tr("当前字体不包含输入字符，文字没有修改。"));
        return;
    }
    refreshAfterDecoration();
    recordContentEdit(pageIndex, before, tr("就地编辑文字"));
    setModified(true);
    emit statusMessage(tr("文字已更新"));
}

// 表格检测与单元格改写。
//
// 为什么不是直接列对象：页面上一堆散落的文本对象对用户没有意义，
// "这是不是一张表、哪一格写了什么"才是一条能走完的任务。
// 先把选区里的文本对象检测成行列网格，再按格改写，底层已有
// detectTableInRect / setTableCellText，这里只补"人能操作"的那一层。
void PdfView::detectTableDialog() {
    if (!document_.isValid()) {
        return;
    }
    const int pageIndex = currentPage();
    auto page = document_.page(pageIndex);
    if (!page) {
        return;
    }

    // 有选区就用选区，否则用整页——用户框出来的范围是最准的信号。
    QRectF detectRect(0, 0, page->widthPoints(), page->heightPoints());
    if (selectedObjectPage_ == pageIndex && !selectedObjectIndexes_.isEmpty()) {
        const auto objects = page->objectsInRect(detectRect, 0.0);
        QRectF unionRect;
        for (const auto& object : objects) {
            if (!selectedObjectIndexes_.contains(object.index)) {
                continue;
            }
            unionRect = unionRect.isNull() ? object.bounds : unionRect.united(object.bounds);
        }
        if (!unionRect.isNull()) {
            detectRect = unionRect;
        }
    }

    auto table = page->detectTableInRect(detectRect);
    if (!table.isValid()) {
        QMessageBox::information(this, tr("表格检测"),
                                 tr("这里没有检测到表格结构（至少要两行两列）。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("表格检测"));
    dialog.resize(680, 440);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("检测到 %1 行 × %2 列。选中一格后可以改写它的文字。")
                                     .arg(table.rowCount)
                                     .arg(table.columnCount),
                                 &dialog));

    auto* grid = new QTableWidget(table.rowCount, table.columnCount, &dialog);
    grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid->setSelectionBehavior(QAbstractItemView::SelectItems);
    grid->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(grid);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* editButton = buttons->addButton(tr("修改选中格"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // 每次改完都要重新检测：对象改写后旧的 objectId 索引可能已经失效，
    // 拿旧结构继续写会写到错的格子上。
    const auto fillGrid = [&grid](const pdf::TableStructure& current) {
        grid->clear();
        grid->setRowCount(current.rowCount);
        grid->setColumnCount(current.columnCount);
        for (int row = 0; row < current.rowCount; ++row) {
            for (int column = 0; column < current.columnCount; ++column) {
                const pdf::TableCell* cell = current.cell(row, column);
                grid->setItem(row, column, new QTableWidgetItem(cell ? cell->text : QString()));
            }
        }
    };
    fillGrid(table);

    connect(editButton, &QPushButton::clicked, &dialog, [this, &dialog, grid, fillGrid,
                                                         detectRect, pageIndex]() {
        const auto selected = grid->selectedItems();
        if (selected.isEmpty()) {
            QMessageBox::information(&dialog, tr("表格检测"), tr("先选中一个单元格。"));
            return;
        }
        const int row = selected.first()->row();
        const int column = selected.first()->column();
        bool accepted = false;
        const QString text = QInputDialog::getText(
            &dialog, tr("修改单元格"), tr("第 %1 行第 %2 列的新文字:").arg(row + 1).arg(column + 1),
            QLineEdit::Normal, selected.first()->text(), &accepted);
        if (!accepted) {
            return;
        }
        auto current = document_.page(pageIndex);
        if (!current) {
            return;
        }
        const pdf::PdfDocument before = document_.backupPage(pageIndex);
        auto refreshed = current->detectTableInRect(detectRect);
        if (!refreshed.isValid() || !current->setTableCellText(refreshed, row, column, text)) {
            QMessageBox::warning(&dialog, tr("表格检测"),
                                 tr("这一格改不了（可能不是单个文本对象，或者已经变了）。"));
            return;
        }
        recordContentEdit(pageIndex, before, tr("修改表格单元格"));
        setModified(true);
        refreshAfterDecoration();
        auto after = document_.page(pageIndex);
        if (after) {
            fillGrid(after->detectTableInRect(detectRect));
        }
        emit statusMessage(tr("已修改表格第 %1 行第 %2 列").arg(row + 1).arg(column + 1));
    });

    dialog.exec();
}

void PdfView::recordContentEdit(int pageIndex, const pdf::PdfDocument& before,
                                const QString& name) {
    const pdf::PdfDocument after = document_.backupPage(pageIndex);
    if (!before.isValid() || !after.isValid()) return;
    contentUndo_.append(ContentEditRecord{pageIndex, before, after, name});
    constexpr int kMaxContentHistory = 50;
    if (contentUndo_.size() > kMaxContentHistory) contentUndo_.removeFirst();
    contentRedo_.clear();
    if (annotationController_) annotationController_->discardRedoHistory();
    editUndoTimeline_.append(EditHistoryKind::Content);
    editRedoTimeline_.clear();
    updateAnnotationToolBar();
}

bool PdfView::restoreContentSnapshot(const pdf::PdfDocument& snapshot, int pageIndex) {
    if (!snapshot.isValid() || pageIndex < 0 || pageIndex >= document_.pageCount()) return false;
    const pdf::PdfDocument current = document_.backupPage(pageIndex);
    if (!current.isValid() || !document_.deletePage(pageIndex)) return false;
    if (!document_.restorePageFrom(snapshot, pageIndex)) {
        document_.restorePageFrom(current, pageIndex);
        return false;
    }
    selectedObjectPage_ = -1;
    selectedObjectIndexes_.clear();
    canvas_->clearObjectSelection();
    refreshAfterDecoration();
    setModified(true);
    return true;
}

void PdfView::reflowSelectionDialog(bool paragraph) {
    if (!document_.isValid() || textController_ == nullptr) return;
    if (!requirePermission(effectivePermissions().allowModifying, tr("流式编辑"))) return;
    int pageIndex = textController_->selectionPage();
    QVector<QRectF> displayRects = textController_->selectionRects();
    QString source = textController_->selectedText();
    if (selectedObjectPage_ >= 0 && selectedObjectIndexes_.size() > 1) {
        auto selectedPage = document_.page(selectedObjectPage_);
        PageTransform transform;
        if (selectedPage && transformForIndexPage(selectedObjectPage_, &transform)) {
            auto selected = selectedPage->objectsInRect(
                QRectF(0, 0, selectedPage->widthPoints(), selectedPage->heightPoints()), 0.0);
            selected.erase(std::remove_if(selected.begin(), selected.end(), [this](const auto& object) {
                return object.type != pdf::PageObjectType::Text ||
                       !selectedObjectIndexes_.contains(object.index);
            }), selected.end());
            std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) {
                return !qFuzzyCompare(a.bounds.top(), b.bounds.top())
                    ? a.bounds.top() > b.bounds.top() : a.bounds.left() < b.bounds.left();
            });
            if (selected.size() > 1) {
                pageIndex = selectedObjectPage_;
                source.clear();
                displayRects.clear();
                for (const auto& object : selected) {
                    if (!source.isEmpty()) source += QLatin1Char(' ');
                    source += object.text;
                    displayRects.append(transform.toDisplay(object.bounds));
                }
            }
        }
    }
    if (pageIndex < 0 || displayRects.isEmpty() || source.trimmed().isEmpty()) {
        QMessageBox::information(this, tr("流式编辑"), tr("先选中要重排的文字。"));
        return;
    }
    auto page = document_.page(pageIndex);
    if (!page) return;
    QRectF rect;
    for (const QRectF& displayRect : displayRects)
        rect = rect.united(QRectF(displayToUser(pageIndex, displayRect.topLeft()),
                                 displayToUser(pageIndex, displayRect.bottomRight())).normalized());
    bool accepted = false;
    const double fontSize = QInputDialog::getDouble(this, tr("流式编辑"), tr("字号:"),
                                                     qMax(8.0, rect.height() * 0.8), 1, 200, 1, &accepted);
    if (!accepted) return;
    pdf::TextStyle style;
    style.fontSize = fontSize;
    style.lineHeight = QInputDialog::getDouble(this, tr("流式编辑"), tr("行距（倍）:"),
                                                1.2, 0.5, 5.0, 2, &accepted);
    if (!accepted) return;
    style.characterSpacing = QInputDialog::getDouble(this, tr("流式编辑"), tr("字距（点）:"),
                                                       0, -20, 100, 2, &accepted);
    if (!accepted) return;
    style.horizontalScale = QInputDialog::getDouble(this, tr("流式编辑"), tr("水平缩放（%）:"),
                                                      100, 1, 1000, 1, &accepted) / 100.0;
    if (!accepted) return;
    style.baselineShift = QInputDialog::getDouble(this, tr("流式编辑"), tr("基线偏移（点）:"),
                                                   0, -1000, 1000, 1, &accepted);
    if (!accepted) return;
    style.opacity = QInputDialog::getDouble(this, tr("流式编辑"), tr("透明度（%）:"),
                                             100, 0, 100, 1, &accepted) / 100.0;
    if (!accepted) return;
    style.color = QColorDialog::getColor(Qt::black, this, tr("文字颜色"));
    if (!style.color.isValid()) return;
    const QStringList directions{tr("横排"), tr("竖排")};
    const QString direction = QInputDialog::getItem(this, tr("流式编辑"), tr("书写方向:"),
                                                     directions, 0, false, &accepted);
    if (!accepted) return;
    style.vertical = direction == directions[1];
    const QStringList alignments{tr("左对齐"), tr("居中"), tr("右对齐"), tr("两端对齐")};
    const QString alignment = QInputDialog::getItem(this, tr("流式编辑"), tr("对齐方式:"),
                                                     alignments, 0, false, &accepted);
    if (!accepted) return;
    style.alignment = alignment == alignments[1] ? pdf::TextAlignment::Center
        : alignment == alignments[2] ? pdf::TextAlignment::Right
        : alignment == alignments[3] ? pdf::TextAlignment::Justify
                                     : pdf::TextAlignment::Left;
    const QStringList families{QStringLiteral("Helvetica"), QStringLiteral("Helvetica Bold"),
                               QStringLiteral("Helvetica Oblique"), QStringLiteral("Helvetica Bold Oblique"),
                               QStringLiteral("Times Roman"), QStringLiteral("Times Bold"),
                               QStringLiteral("Times Italic"), QStringLiteral("Times Bold Italic"),
                               QStringLiteral("Courier"), QStringLiteral("Courier Bold"),
                               QStringLiteral("Courier Oblique"), QStringLiteral("Courier Bold Oblique"),
                               QStringLiteral("Microsoft YaHei"),
                               QStringLiteral("SimSun"), QStringLiteral("SimHei")};
    style.family = QInputDialog::getItem(this, tr("流式编辑"), tr("字体:"), families, 0,
                                         false, &accepted);
    if (!accepted) return;
    const double pageHeight = page->heightPoints();
    QVector<QRectF> linkedFrames;
    if (paragraph && displayRects.size() > 1) {
        for (const QRectF& displayRect : displayRects) {
            const QRectF userFrame = QRectF(displayToUser(pageIndex, displayRect.topLeft()),
                                             displayToUser(pageIndex, displayRect.bottomRight())).normalized();
            linkedFrames.append(QRectF(userFrame.left(), pageHeight - userFrame.bottom(),
                                       userFrame.width(), qMax(userFrame.height(), fontSize * 1.5)));
        }
        std::sort(linkedFrames.begin(), linkedFrames.end(), [](const QRectF& a, const QRectF& b) {
            return !qFuzzyCompare(a.top(), b.top()) ? a.top() < b.top() : a.left() < b.left();
        });
    }
    const QRectF localBox(0, 0, rect.width(), paragraph ? qMax(rect.height(), fontSize * 8) : fontSize * 1.5);
    const pdf::ReflowResult layout = !linkedFrames.isEmpty()
        ? pdf::TextReflow::layoutFrames(source, linkedFrames, style)
        : paragraph ? pdf::TextReflow::layoutParagraph(source.simplified(), localBox, style)
                    : pdf::TextReflow::layoutLine(source.simplified(), localBox, style);
    // Never delete the original objects when the destination frames cannot
    // hold all source text.  The old implementation reported overflow only
    // after saving the partial result, which silently discarded the tail.
    if (layout.lines.isEmpty() || layout.overflow) {
        QMessageBox::warning(
            this, tr("流式编辑"),
            layout.lines.isEmpty()
                ? tr("当前文本框无法容纳任何文字，请增大文本框或减小字号。")
                : tr("链接文本框空间不足，仍有 %1 个字符未排入；原内容未修改。")
                      .arg(layout.remainingText.size()));
        return;
    }
    const pdf::PdfDocument backup = document_.backupPage(pageIndex);
    if (!backup.isValid()) {
        QMessageBox::warning(this, tr("流式编辑"), tr("无法创建页面安全备份，文档没有修改。"));
        return;
    }
    const auto rollback = [this, pageIndex, &backup] {
        if (document_.deletePage(pageIndex)) document_.restorePageFrom(backup, pageIndex);
        refreshAfterDecoration();
    };
    QVector<int> textIndexes;
    if (!linkedFrames.isEmpty()) {
        for (const QRectF& displayRect : displayRects) {
            const QRectF userFrame = QRectF(displayToUser(pageIndex, displayRect.topLeft()),
                                             displayToUser(pageIndex, displayRect.bottomRight())).normalized();
            for (const auto& object : page->objectsInRect(userFrame, 0.5))
                if (object.type == pdf::PageObjectType::Text && !textIndexes.contains(object.index))
                    textIndexes.append(object.index);
        }
    } else {
        for (const auto& object : page->objectsInRect(rect, 0.5))
            if (object.type == pdf::PageObjectType::Text) textIndexes.append(object.index);
    }
    if (textIndexes.isEmpty()) {
        QMessageBox::warning(this, tr("流式编辑"), tr("无法安全移除原文字对象。"));
        return;
    }
    if (!page->removeObjects(textIndexes)) {
        rollback();
        QMessageBox::warning(this, tr("流式编辑"), tr("无法安全移除原文字对象。"));
        return;
    }
    bool ok = true;
    for (const pdf::ReflowLine& line : layout.lines) {
        const bool linked = !linkedFrames.isEmpty();
        const double baselineY = linked ? pageHeight - line.baseline.y()
                                        : rect.bottom() - line.baseline.y();
        const double baselineX = linked ? line.baseline.x() : rect.left() + line.baseline.x();
        if (qFuzzyIsNull(style.characterSpacing)) {
            ok = page->addTextLayer(line.text, baselineX, baselineY + style.baselineShift,
                                    fontSize, style.color, 0.0, style.opacity, style.family,
                                    style.horizontalScale) && ok;
        } else {
            QFont metricsFont(style.family);
            metricsFont.setPointSizeF(fontSize);
            const QFontMetricsF metrics(metricsFont);
            QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, line.text);
            finder.toStart();
            qsizetype start = 0;
            double x = baselineX;
            for (qsizetype end = finder.toNextBoundary(); end >= 0;
                 start = end, end = finder.toNextBoundary()) {
                const QString grapheme = line.text.mid(start, end - start);
                ok = page->addTextLayer(grapheme, x, baselineY + style.baselineShift,
                                        fontSize, style.color, 0.0, style.opacity, style.family,
                                        style.horizontalScale) && ok;
                x += metrics.horizontalAdvance(grapheme) * style.horizontalScale +
                     style.characterSpacing;
            }
        }
    }
    if (!ok) {
        rollback();
        QMessageBox::warning(this, tr("流式编辑"), tr("部分文字无法用当前字体写入，已恢复原页面。"));
        return;
    }
    refreshAfterDecoration();
    setModified(true);
    recordContentEdit(pageIndex, backup,
                      paragraph ? tr("段落重排") : tr("行内自动换行"));
    emit statusMessage(paragraph ? tr("已完成段落重排") : tr("已完成行内自动换行"));
}

void PdfView::searchReplaceDialog() {
    if (!document_.isValid()) {
        return;
    }
    bool accepted = false;
    const QString query = QInputDialog::getText(this, tr("搜索替换"), tr("查找内容:"),
                                                 QLineEdit::Normal, QString(), &accepted);
    if (!accepted || query.isEmpty()) {
        return;
    }
    const QString replacement = QInputDialog::getText(
        this, tr("搜索替换"), tr("替换为:"), QLineEdit::Normal,
        QString(), &accepted);
    if (!accepted) {
        return;
    }
    const QStringList scopes{tr("当前页"), tr("整个文档")};
    const QString scope = QInputDialog::getItem(this, tr("搜索替换"), tr("替换范围:"),
                                                 scopes, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    const int firstPage = scope == scopes.constFirst() ? currentPage() : 0;
    const int lastPage = scope == scopes.constFirst() ? currentPage() : document_.pageCount() - 1;
    int replacementCount = 0;
    for (int pageIndex = firstPage; pageIndex <= lastPage; ++pageIndex) {
        const pdf::PdfDocument before = document_.backupPage(pageIndex);
        QString serializeError;
        const QByteArray input = document_.serialize(&serializeError);
        int safeCount = 0;
        QString rewriteError;
        const QByteArray rewritten = pdf::ContentStream::rewritePdfText(
            input, {pageIndex}, query, replacement, Qt::CaseInsensitive, &safeCount, &rewriteError);
        if (safeCount <= 0 || rewritten.isEmpty()) continue;
        QString reloadError;
        if (!before.isValid() || !document_.reloadFromBytes(rewritten, &reloadError)) {
            QMessageBox::warning(this, tr("搜索替换"),
                                 tr("安全内容流重写失败：%1").arg(
                                     reloadError.isEmpty() ? rewriteError : reloadError));
            return;
        }
        replacementCount += safeCount;
        recordContentEdit(pageIndex, before, tr("安全搜索替换"));
    }
    if (replacementCount > 0) {
        refreshAfterDecoration();
        setModified(true);
        emit statusMessage(tr("已安全替换 %1 处").arg(replacementCount));
        return;
    }
    for (int pageIndex = firstPage; pageIndex <= lastPage; ++pageIndex) {
        auto page = document_.page(pageIndex);
        if (!page.has_value()) {
            continue;
        }
        const std::optional<pdf::TextPage> textPage = page->extractText();
        if (!textPage.has_value() || textPage->chars.isEmpty()) {
            continue;
        }

        struct Line {
            QVector<pdf::TextChar> chars;
            QRectF rect;
        };
        QVector<Line> lines;
        for (const pdf::TextChar& character : textPage->chars) {
            if (character.text.trimmed().isEmpty() || character.box.isEmpty()) {
                continue;
            }
            int bestLine = -1;
            double bestOverlap = 0.0;
            for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
                const QRectF overlap = lines.at(lineIndex).rect.intersected(character.box);
                const double denominator = qMin(lines.at(lineIndex).rect.height(),
                                                character.box.height());
                const double ratio = denominator > 0.0 ? overlap.height() / denominator : 0.0;
                if (ratio > bestOverlap) {
                    bestOverlap = ratio;
                    bestLine = lineIndex;
                }
            }
            if (bestLine < 0 || bestOverlap < 0.45) {
                Line line;
                line.chars.append(character);
                line.rect = character.box;
                lines.append(line);
            } else {
                lines[bestLine].chars.append(character);
                lines[bestLine].rect = lines.at(bestLine).rect.united(character.box);
            }
        }
        for (Line& line : lines) {
            std::sort(line.chars.begin(), line.chars.end(), [](const auto& a, const auto& b) {
                return a.box.left() < b.box.left();
            });
            QString original;
            for (const pdf::TextChar& character : std::as_const(line.chars)) {
                original += character.text;
            }
            QString replaced = original;
            const int count = replaced.count(query, Qt::CaseInsensitive);
            if (count <= 0) {
                continue;
            }
            replaced.replace(query, replacement, Qt::CaseInsensitive);
            const double fontSize = std::max(8.0, line.rect.height() * 0.8);
            if (page->replaceTextInRect(line.rect, replaced, fontSize, QColor(20, 24, 32))) {
                replacementCount += count;
            }
        }
    }
    if (replacementCount <= 0) {
        QMessageBox::information(this, tr("搜索替换"), tr("没有找到匹配内容。"));
        return;
    }
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("已替换 %1 处").arg(replacementCount));
}

void PdfView::addLinkToSelectionDialog() {
    if (!document_.isValid() || textController_ == nullptr) {
        return;
    }
    const int pageIndex = textController_->selectionPage();
    const QVector<QRectF> displayRects = textController_->selectionRects();
    if (pageIndex < 0 || displayRects.isEmpty()) {
        QMessageBox::information(this, tr("添加链接"), tr("先选中要添加链接的文字或区域。"));
        return;
    }
    bool accepted = false;
    const QString input = QInputDialog::getText(
        this, tr("添加链接"), tr("网址:"), QLineEdit::Normal, QStringLiteral("https://"),
        &accepted).trimmed();
    if (!accepted || input.isEmpty()) {
        return;
    }
    QUrl url = QUrl::fromUserInput(input);
    if (!url.isValid() || url.scheme().isEmpty()) {
        QMessageBox::warning(this, tr("添加链接"), tr("请输入有效的网址。"));
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
    if (!page->addUriLink(userRect, url.toEncoded())) {
        QMessageBox::warning(this, tr("添加链接"), tr("链接没有写入页面。"));
        return;
    }
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("已添加链接"));
}

void PdfView::addInternalLinkToSelectionDialog() {
    if (!document_.isValid() || textController_ == nullptr) {
        return;
    }
    const int sourcePage = textController_->selectionPage();
    const QVector<QRectF> displayRects = textController_->selectionRects();
    if (sourcePage < 0 || displayRects.isEmpty()) {
        QMessageBox::information(this, tr("页面链接"), tr("先选中要添加链接的文字或区域。"));
        return;
    }
    bool accepted = false;
    const int targetPage = QInputDialog::getInt(
        this, tr("页面链接"), tr("跳转到页码:"), currentPage() + 1,
        1, document_.pageCount(), 1, &accepted) - 1;
    if (!accepted) {
        return;
    }
    QRectF userRect;
    for (const QRectF& rect : displayRects) {
        userRect = userRect.united(
            QRectF(displayToUser(sourcePage, rect.topLeft()),
                   displayToUser(sourcePage, rect.bottomRight())).normalized());
    }
    if (userRect.isEmpty()) {
        return;
    }

    QString error;
    const QByteArray serialized = document_.serialize(&error);
    if (serialized.isEmpty()) {
        QMessageBox::warning(this, tr("页面链接"), error);
        return;
    }
    QByteArray dictionary = "<< /Type /Annot /Subtype /Link /Rect [";
    dictionary += QByteArray::number(userRect.left(), 'f', 2) + ' ';
    dictionary += QByteArray::number(userRect.top(), 'f', 2) + ' ';
    dictionary += QByteArray::number(userRect.right(), 'f', 2) + ' ';
    dictionary += QByteArray::number(userRect.bottom(), 'f', 2);
    dictionary += "] /Border [0 0 0] /Dest [@PAGE0 0 R /Fit] >>";
    pdf::InlineAnnotationInsert insert;
    insert.pageIndex = sourcePage;
    insert.dictionary = dictionary;
    insert.targetPages.append(targetPage);
    const pdf::InlineWriteResult written = pdf::injectInlineAnnotations(serialized, {insert});
    if (!written.ok) {
        QMessageBox::warning(this, tr("页面链接"), written.error);
        return;
    }
    pdf::PdfOpenResult reopened = pdf::openDocumentFromBytes(written.bytes, filePath_);
    if (!reopened.document.isValid()) {
        QMessageBox::warning(this, tr("页面链接"), reopened.errorMessage);
        return;
    }
    const int pageBefore = currentPage();
    const double zoomBefore = zoom();
    setDocument(std::move(reopened.document), filePath_);
    goToPage(pageBefore);
    setZoom(zoomBefore);
    setModified(true);
    emit statusMessage(tr("已添加跳转到第 %1 页的链接").arg(targetPage + 1));
}

void PdfView::detectWebLinks() {
    if (!document_.isValid()) {
        return;
    }
    int added = 0;
    for (int pageIndex = 0; pageIndex < document_.pageCount(); ++pageIndex) {
        auto page = document_.page(pageIndex);
        if (page.has_value()) {
            added += page->addDetectedUriLinks();
        }
    }
    if (added <= 0) {
        QMessageBox::information(this, tr("网络链接"), tr("没有发现需要添加的网址链接。"));
        return;
    }
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("已创建 %1 个网络链接").arg(added));
}

// 显示空间（页面左上原点、y 向下）→ PDF 用户空间（左下原点、y 向上）。
QPointF PdfView::displayToUser(int pageIndex, const QPointF& displayPoint) const {
    PageTransform transform;
    if (annotationController_ != nullptr &&
        annotationController_->transformForPage(pageIndex, &transform)) {
        return transform.toUserSpace(displayPoint);
    }
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
    setModified(true);
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
    setModified(true);
    emit statusMessage(tr("已插入录音批注"));
}

void PdfView::activateSignaturePlacement(const QByteArray& image, const QString& name) {
    QImage decoded;
    if (!annotationController_ || !decoded.loadFromData(image)) {
        QMessageBox::warning(this, tr("电子签章"), tr("签章图片无效，无法放置。"));
        return;
    }
    pendingSignatureImage_ = image;
    pendingSignatureName_ = name;
    annotationController_->setTool(AnnotationController::Tool::ImageStamp);
    emit statusMessage(tr("请在页面上单击签章位置；按其他工具可取消。"));
}

bool PdfView::applyRidingSeamSignature(const QByteArray& imageBytes, const QString& name) {
    QImage image;
    if (!annotationController_ || pageCount_ < 1 || !image.loadFromData(imageBytes)) return false;

    // 沿图片宽度均分：每页只保存自己那一条，合拢纸张才能看到完整签章。
    const int pages = pageCount_;
    int created = 0;
    for (int pageIndex = 0; pageIndex < pages; ++pageIndex) {
        const int left = image.width() * pageIndex / pages;
        const int right = image.width() * (pageIndex + 1) / pages;
        const QImage slice = image.copy(left, 0, qMax(1, right - left), image.height());
        QByteArray sliceBytes;
        QBuffer buffer(&sliceBytes);
        if (!buffer.open(QIODevice::WriteOnly) || !slice.save(&buffer, "PNG")) continue;

        auto page = document_.page(pageIndex);
        if (!page.has_value()) continue;
        constexpr double kSliceWidth = 18.0;
        const double sliceHeight = qMin(page->heightPoints() * 0.45,
                                        kSliceWidth * slice.height() / qMax(1, slice.width()));
        const QRectF rect(page->widthPoints() - kSliceWidth,
                          page->heightPoints() / 2.0 - sliceHeight / 2.0,
                          kSliceWidth, sliceHeight);
        if (annotationController_->addImageStamp(
                pageIndex, rect, sliceBytes, tr("骑缝章：%1 (%2/%3)").arg(name).arg(pageIndex + 1).arg(pages)) != 0) {
            ++created;
        }
    }
    if (created == pages) {
        emit statusMessage(tr("骑缝章已拆分到 %1 页；保存文档后写入 PDF。").arg(pages));
        return true;
    }
    emit statusMessage(tr("骑缝章只写入了 %1/%2 页。").arg(created).arg(pages));
    return false;
}


void PdfView::setToolBarsVisible(bool visible) {
    for (const char* name : {"documentToolBar", "annotationToolBar", "shapeToolBar"}) {
        if (auto* bar = findChild<QToolBar*>(QString::fromLatin1(name))) {
            bar->setVisible(visible);
        }
    }
}

void PdfView::activateAnnotationTool(const QString& id) {
    if (!requirePermission(effectivePermissions().allowAnnotations, tr("添加批注"))) return;
    if (canvas_ != nullptr) canvas_->setFormFieldPlacementKind(QString());
    if (id != QStringLiteral("imagestamp")) {
        pendingSignatureImage_.clear();
        pendingSignatureName_.clear();
    }
    if (id == QStringLiteral("undo")) {
        undoAnnotation();
        return;
    }
    if (id == QStringLiteral("redo")) {
        redoAnnotation();
        return;
    }
    QToolButton* target = nullptr;
    if (id == QStringLiteral("highlight")) target = highlightButton_;
    else if (id == QStringLiteral("underline")) target = underlineButton_;
    else if (id == QStringLiteral("strikethrough")) target = strikeOutButton_;
    else if (id == QStringLiteral("squiggly")) target = squigglyButton_;
    else if (id == QStringLiteral("note")) target = noteButton_;
    else if (id == QStringLiteral("freetext")) target = freeTextButton_;
    else if (id == QStringLiteral("stamp")) target = stampButton_;
    else if (id == QStringLiteral("imagestamp")) target = imageStampButton_;
    else if (id == QStringLiteral("sound")) target = soundButton_;
    else if (id == QStringLiteral("attachment")) target = attachmentButton_;
    else if (id == QStringLiteral("measure")) target = measureButton_;
    else if (id == QStringLiteral("perimeter")) target = perimeterButton_;
    else if (id == QStringLiteral("area")) target = areaButton_;
    else if (id == QStringLiteral("square")) target = squareButton_;
    else if (id == QStringLiteral("ellipse")) target = ellipseButton_;
    else if (id == QStringLiteral("line")) target = lineButton_;
    else if (id == QStringLiteral("arrow")) target = arrowButton_;
    else if (id == QStringLiteral("polygon")) target = polygonButton_;
    else if (id == QStringLiteral("cloud")) target = cloudButton_;
    else if (id == QStringLiteral("ink")) target = inkButton_;
    else if (id == QStringLiteral("eraser")) target = eraserButton_;
    if (target != nullptr) {
        target->click();
    }
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
    if (!editUndoTimeline_.isEmpty()) {
        const EditHistoryKind kind = editUndoTimeline_.takeLast();
        if (kind == EditHistoryKind::Content && !contentUndo_.isEmpty()) {
        const ContentEditRecord record = contentUndo_.takeLast();
        if (restoreContentSnapshot(record.before, record.pageIndex)) {
            contentRedo_.append(record);
            editRedoTimeline_.append(kind);
            updateAnnotationToolBar();
            emit statusMessage(tr("已撤销：%1").arg(record.name));
            return;
        }
        contentUndo_.append(record);
        editUndoTimeline_.append(kind);
        } else if (kind == EditHistoryKind::Annotation && annotationController_) {
            replayingEditHistory_ = true;
            const bool ok = annotationController_->undo();
            replayingEditHistory_ = false;
            knownAnnotationUndoCount_ = annotationController_->undoCount();
            if (ok) {
                editRedoTimeline_.append(kind);
                updateAnnotationToolBar();
                return;
            }
            editUndoTimeline_.append(kind);
        }
    }
    if (!annotationController_) {
        return;
    }
    if (!annotationController_->undo()) {
        emit statusMessage(tr("没有可撤销的批注操作"));
    }
}

void PdfView::redoAnnotation() {
    if (!editRedoTimeline_.isEmpty()) {
        const EditHistoryKind kind = editRedoTimeline_.takeLast();
        if (kind == EditHistoryKind::Content && !contentRedo_.isEmpty()) {
        const ContentEditRecord record = contentRedo_.takeLast();
        if (restoreContentSnapshot(record.after, record.pageIndex)) {
            contentUndo_.append(record);
            editUndoTimeline_.append(kind);
            updateAnnotationToolBar();
            emit statusMessage(tr("已重做：%1").arg(record.name));
            return;
        }
        contentRedo_.append(record);
        editRedoTimeline_.append(kind);
        } else if (kind == EditHistoryKind::Annotation && annotationController_) {
            replayingEditHistory_ = true;
            const bool ok = annotationController_->redo();
            replayingEditHistory_ = false;
            knownAnnotationUndoCount_ = annotationController_->undoCount();
            if (ok) {
                editUndoTimeline_.append(kind);
                updateAnnotationToolBar();
                return;
            }
            editRedoTimeline_.append(kind);
        }
    }
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
    const bool canUndoUnified = !editUndoTimeline_.isEmpty();
    const bool canRedoUnified = !editRedoTimeline_.isEmpty();
    const bool undoIsContent = canUndoUnified && editUndoTimeline_.constLast() == EditHistoryKind::Content &&
                               !contentUndo_.isEmpty();
    const bool redoIsContent = canRedoUnified && editRedoTimeline_.constLast() == EditHistoryKind::Content &&
                               !contentRedo_.isEmpty();
    undoButton_->setEnabled(canUndoUnified || (hasDocument && annotationController_->canUndo()));
    redoButton_->setEnabled(canRedoUnified || (hasDocument && annotationController_->canRedo()));
    undoButton_->setToolTip(undoIsContent
                                ? tr("撤销 %1 (Ctrl+Z)").arg(contentUndo_.constLast().name)
                            : hasDocument && annotationController_->canUndo()
                                ? annotationController_->undoText()
                                : tr("撤销上一步批注操作 (Ctrl+Z)"));
    redoButton_->setToolTip(redoIsContent
                                ? tr("重做 %1 (Ctrl+Shift+Z)").arg(contentRedo_.constLast().name)
                            : hasDocument && annotationController_->canRedo()
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
    trace(QStringLiteral("layout viewport=%1 canvas=%2 content=%3 zoom=%4")
              .arg(canvas_->viewport()->width())
              .arg(canvas_->width())
              .arg(layout_.totalWidth())
              .arg(zoom_));
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
    if (!requirePermission(effectivePermissions().allowAssembly, tr("插入页面"))) return;
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
    if (!requirePermission(effectivePermissions().allowAssembly, tr("删除页面"))) return;
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
    if (!requirePermission(effectivePermissions().allowAssembly, tr("旋转页面"))) return;
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
    if (!requirePermission(effectivePermissions().allowAssembly, tr("旋转页面"))) return;
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
    if (!requirePermission(effectivePermissions().allowAssembly, tr("移动页面"))) return;
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
    if (!requirePermission(effectivePermissions().allowAssembly, tr("移动页面"))) return;
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
    if (!requirePermission(effectivePermissions().allowPrinting, tr("打印"))) return;
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
    if (!requirePermission(effectivePermissions().allowCopying, tr("导出页面图片"))) return;
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
    if (!requirePermission(effectivePermissions().allowCopying, tr("导出文本"))) return;
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

void PdfView::showAccessibilityCheck() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("完整检查"), tr("先打开一份 PDF。"));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("辅助功能检查"));
    dialog.resize(680, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* report = new QTextBrowser(&dialog);
    report->setHtml(accessibilityReportHtml(document_, filePath_));
    report->setAccessibleName(tr("辅助功能检查结果"));
    layout->addWidget(report);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close,
                                         &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存报告"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked,
            this, &PdfView::exportAccessibilityReport);
    layout->addWidget(buttons);
    dialog.exec();
}

void PdfView::exportAccessibilityReport() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("辅助功能报告"), tr("先打开一份 PDF。"));
        return;
    }
    const QString base = filePath_.isEmpty() ? QStringLiteral("document")
                                              : QFileInfo(filePath_).completeBaseName();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存辅助功能报告"), base + QStringLiteral("-accessibility.html"),
        tr("HTML 文件 (*.html)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("保存失败"), file.errorString());
        return;
    }
    file.write(accessibilityReportHtml(document_, filePath_).toUtf8());
    file.close();
    emit statusMessage(tr("辅助功能报告已保存"));
}

void PdfView::showReadingOrder() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("阅读顺序"), tr("先打开一份 PDF。"));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("阅读顺序 - 第 %1 页").arg(currentPage() + 1));
    dialog.resize(620, 500);
    auto* layout = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(tr("以下内容按 PDF 文本层的逻辑顺序显示，可用于人工核对屏幕阅读器的阅读次序。"),
                            &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* text = new QTextEdit(&dialog);
    text->setReadOnly(true);
    const std::optional<pdf::PdfPage> page = document_.page(currentPage());
    const std::optional<pdf::TextPage> extracted = page ? page->extractText() : std::nullopt;
    text->setPlainText(extracted ? extracted->plainText() : tr("本页没有可提取文字。"));
    text->setAccessibleName(tr("当前页阅读顺序"));
    layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void PdfView::showThesisCheck() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("论文查重"), tr("先打开一份 PDF。"));
        return;
    }
    const ThesisCheckResult result = checkInternalRepetition(document_);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("论文查重（本地自查）"));
    dialog.resize(680, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* report = new QTextBrowser(&dialog);
    report->setHtml(thesisResultHtml(result, filePath_));
    report->setAccessibleName(tr("论文重复段落自查结果"));
    layout->addWidget(report);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    QStringList history = QSettings().value(QStringLiteral("thesis/checkHistory")).toStringList();
    const QString fileName = filePath_.isEmpty() ? tr("未命名文档") : QFileInfo(filePath_).fileName();
    history.prepend(QStringLiteral("%1\t%2\t%3\t%4")
                        .arg(QDateTime::currentDateTime().toString(Qt::ISODate), fileName)
                        .arg(result.textCharacters)
                        .arg(result.rate(), 0, 'f', 1));
    while (history.size() > 30) history.removeLast();
    QSettings().setValue(QStringLiteral("thesis/checkHistory"), history);
    dialog.exec();
}

void PdfView::showThesisCheckHistory() {
    const QStringList history =
        QSettings().value(QStringLiteral("thesis/checkHistory")).toStringList();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("查重历史"));
    dialog.resize(660, 440);
    auto* layout = new QVBoxLayout(&dialog);
    auto* browser = new QTextBrowser(&dialog);
    QString html = QStringLiteral("<h2>%1</h2>").arg(tr("本地自查历史"));
    if (history.isEmpty()) {
        html += QStringLiteral("<p>%1</p>").arg(tr("还没有查重记录。"));
    } else {
        html += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"6\" border=\"1\">"
                               "<tr><th>%1</th><th>%2</th><th>%3</th><th>%4</th></tr>")
                    .arg(tr("检查时间"), tr("文档名称"), tr("分析字符"), tr("文内重复比例"));
        for (const QString& entry : history) {
            const QStringList fields = entry.split('\t');
            if (fields.size() != 4) continue;
            const QDateTime time = QDateTime::fromString(fields.at(0), Qt::ISODate);
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4%</td></tr>")
                        .arg(time.isValid() ? time.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                            : fields.at(0).toHtmlEscaped(),
                             fields.at(1).toHtmlEscaped(), fields.at(2).toHtmlEscaped(),
                             fields.at(3).toHtmlEscaped());
        }
        html += QStringLiteral("</table><p><small>%1</small></p>")
                    .arg(tr("记录仅保存在本机，最多保留最近 30 次。"));
    }
    browser->setHtml(html);
    browser->setAccessibleName(tr("论文查重历史"));
    layout->addWidget(browser);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
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
