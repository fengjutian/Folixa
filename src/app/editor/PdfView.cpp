#include "app/editor/PdfView.h"

#include "app/controllers/AudioRecorder.h"
#include "app/controllers/DocumentOutput.h"
#include "app/dialogs/EncryptionDialog.h"
#include "app/dialogs/HiddenDataDialog.h"
#include "app/panels/LastWavePanels.h"
#include "app/controllers/LastWaveWriter.h"
#include "app/layout/PdfPageOperations.h"
#include "app/ui/Language.h"
#include "app/layout/PageChunkBuilder.h"
#include "app/dialogs/ReadingOptionsDialog.h"
#include "app/ui/ReadingScheme.h"
#include "app/dialogs/StampNumberDialog.h"
#include "app/layout/PageTextLines.h"
#include "app/util/WizardPageRange.h"
#include "doc/batch/BatchProcessor.h"
#include "doc/batch/WizardActions.h"
#include "doc/export/OfficeExport.h"
#include "doc/export/PdfToOffice.h"
#include "doc/model/ChunkBuilder.h"
#include "doc/model/DocumentStatistics.h"
#include "pdf/annot/AnnotationSummary.h"
#include "pdf/annot/AnnotationShapes.h"
#include "pdf/ops/DocumentMerger.h"
#include "pdf/document/DocumentProperties.h"
#include "pdf/forms/FormData.h"
#include "pdf/forms/FormFieldWriter.h"
#include "pdf/forms/FormToolSettings.h"
#include "pdf/security/PdfSecurity.h"
#include "pdf/ops/BatchReplace.h"
#include "pdf/document/NamedDestination.h"
#include "doc/model/OutlineGenerator.h"
#include "pdf/ops/PageLabels.h"
#include "pdf/ops/PageSplitPlanner.h"
#include "pdf/security/Redaction.h"
#include "pdf/security/CertificateChain.h"
#include "pdf/security/SignatureInspector.h"
#include "pdf/security/SignatureVerifier.h"
#include "pdf/security/SignatureWorkflow.h"
#include "pdf/ops/SpellChecker.h"
#include "pdf/compliance/StructTreeReader.h"
#include "pdf/content/ParagraphFormat.h"
#include "pdf/compliance/TagTreeWriter.h"
#include "speech/SpeechEngine.h"
#include "pdf/content/PageDecorations.h"
#include "pdf/annot/SoundFormat.h"
#include "pdf/annot/Xfdf.h"
#include "app/editor/PresentationWindow.h"
#include "app/layout/ViewModes.h"
#include "app/editor/ZoomMemory.h"
#include "app/editor/ViewMemory.h"
#include "app/editor/PageCanvas.h"
#include "app/editor/RenderThread.h"
#include "app/controllers/TextController.h"
#include "app/layout/TextLayout.h"
#include "app/controllers/NavigationController.h"
#include "app/controllers/AnnotationController.h"
#include "app/controllers/FormInteractionController.h"
#include "app/controllers/FormDocumentController.h"
#include "app/controllers/IndexController.h"
#include "app/controllers/DocumentQaController.h"
#include "app/layout/PageTransform.h"
#include "app/ui/TraceText.h"
#include "doc/model/DocumentChunk.h"
#include "doc/persist/RecoveryLog.h"
#include "pdf/annot/Annotation.h"
#include "pdf/document/DocumentWriter.h"
#include "pdf/annot/PageAnnotsInjector.h"
#include "pdf/ops/OutlineWriter.h"
#include "pdf/content/TextReflow.h"
#include "pdf/content/ContentStream.h"
#include "pdf/content/TextSplice.h"
#include "i18n/Translate.h"

#include <QColorDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QAbstractItemView>
#include <QApplication>
#include <QAction>
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
#include <QHeaderView>
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
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
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
#include <optional>

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
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " + message + "\n").toUtf8());
    }
}

}  // namespace

PdfView::PdfView(QWidget* parent) : QWidget(parent), cache_(kCacheBytes) {
    buildUi();

    // 朗读引擎：自己起子进程合成语音，逐词回报「读到哪个词了」。
    speechEngine_ = std::make_unique<speech::SpeechEngine>(this);
    connect(speechEngine_.get(), &speech::SpeechEngine::wordStarted, this,
            [this](int index, const QString& word) {
        if (speechPage_ < 0 || index < 0 || index >= speechWords_.size()) {
            return;
        }
        const speech::SpeechWord& current = speechWords_.at(index);
        // 词的偏移在整页文本里，要换算回阅读顺序区间才能拿到高亮矩形。
        int firstOrder = -1;
        int lastOrder = -1;
        for (int unit = current.start; unit < current.start + current.length; ++unit) {
            if (unit < 0 || unit >= pageOrderPerUnit_.size()) {
                continue;
            }
            const int order = pageOrderPerUnit_.at(unit);
            if (firstOrder < 0) {
                firstOrder = order;
            }
            lastOrder = order;
        }
        if (firstOrder >= 0 && lastOrder >= firstOrder && textController_ != nullptr) {
            const TextLayout* layout = textController_->layout(speechPage_);
            if (layout != nullptr) {
                canvas_->setSpeechHighlight(speechPage_,
                                            layout->highlightRects(firstOrder, lastOrder + 1));
            }
        }
        emit statusMessage(tr("正在朗读：%1").arg(word));
    });
    connect(speechEngine_.get(), &speech::SpeechEngine::finished, this, [this]() {
        canvas_->setSpeechHighlight(-1, QVector<QRectF>());
        speechPage_ = -1;
        // 智能朗读是分段念的：一段念完接下一段。
        if (!narrationSegments_.isEmpty()) {
            speakNextNarrationSegment();
            return;
        }
        emit statusMessage(tr("朗读结束"));
    });
    connect(speechEngine_.get(), &speech::SpeechEngine::failed, this, [this](const QString& message) {
        canvas_->setSpeechHighlight(-1, QVector<QRectF>());
        speechPage_ = -1;
        emit statusMessage(tr("朗读失败：%1").arg(message));
    });

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
    // 区域高亮与箭头框：画布只负责采集几何，判定与外观流由领域层算。
    connect(canvas_, &PageCanvas::regionHighlightRequested, this,
            &PdfView::createRegionHighlight);
    connect(canvas_, &PageCanvas::calloutRequested, this, &PdfView::createCallout);
    // 画布的步骤提示（箭头框的第二步等）直接进状态栏。
    connect(canvas_, &PageCanvas::hintMessage, this,
            [this](const QString& text) { emit statusMessage(text); });
    // 鼠标快捷操作：Ctrl+滚轮缩放、侧键前进后退、右键菜单。
    connect(canvas_, &PageCanvas::zoomAtRequested, this, &PdfView::zoomAtPoint);
    connect(canvas_, &PageCanvas::backRequested, this, &PdfView::navigateBack);
    connect(canvas_, &PageCanvas::forwardRequested, this, &PdfView::navigateForward);
    connect(canvas_, &PageCanvas::contextMenuRequested, this, &PdfView::showCanvasContextMenu);
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

    // 画布工具按钮：按钮 ↔ 工具枚举的对应关系只在这张表里维护。
    //
    // 三个坑一次解决：
    //   1) 所有工具按钮都必须 setCheckable：非 checkable 的 QToolButton
    //      发 clicked(bool) 时 checked 恒为 false，槽里 `checked ? X : None`
    //      会永远走 None —— 14 个工具点了没反应就是这个原因。
    //   2) 互斥不能靠 setAutoExclusive：按钮分属两条工具栏，父控件不同就
    //      不在同一个组里；这里在 toggled 里统一把别的按钮取消选中。
    //   3) 点击分发也由表生成，新增工具只要往表里加一行。
    annotationToolButtons_ = {
        {noteButton_, AnnotationController::Tool::Note},
        {squareButton_, AnnotationController::Tool::Square},
        {ellipseButton_, AnnotationController::Tool::Ellipse},
        {lineButton_, AnnotationController::Tool::Line},
        {arrowButton_, AnnotationController::Tool::Arrow},
        {polygonButton_, AnnotationController::Tool::Polygon},
        {cloudButton_, AnnotationController::Tool::Cloud},
        {freeTextButton_, AnnotationController::Tool::FreeText},
        {stampButton_, AnnotationController::Tool::Stamp},
        {measureButton_, AnnotationController::Tool::Measure},
        {attachmentButton_, AnnotationController::Tool::Attachment},
        {imageStampButton_, AnnotationController::Tool::ImageStamp},
        {soundButton_, AnnotationController::Tool::Sound},
        {perimeterButton_, AnnotationController::Tool::MeasurePerimeter},
        {areaButton_, AnnotationController::Tool::MeasureArea},
        {inkButton_, AnnotationController::Tool::Ink},
        {eraserButton_, AnnotationController::Tool::Eraser},
    };
    for (const auto& entry : annotationToolButtons_) {
        QToolButton* button = entry.first;
        const AnnotationController::Tool tool = entry.second;
        button->setCheckable(true);
        connect(button, &QToolButton::toggled, this, [this, button](bool checked) {
            if (!checked) {
                return;
            }
            for (const auto& other : annotationToolButtons_) {
                if (other.first != button && other.first->isChecked()) {
                    // 阻断信号：否则被取消的按钮会再发一次 clicked(false)，
                    // 把刚设好的工具又清成 None。
                    QSignalBlocker blocker(other.first);
                    other.first->setChecked(false);
                }
            }
        });
        connect(button, &QToolButton::clicked, this, [this, tool](bool checked) {
            if (annotationController_) {
                annotationController_->setTool(checked ? tool : AnnotationController::Tool::None);
            }
        });
    }

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

bool PdfView::load(const QString& path, QString* errorMessage) {
    // 按路径打开：文件留在磁盘上，PDFium 按需读块，
    // 不会因为打开一份 1 GB 的文档就把 1 GB 读进内存。
    const pdf::PdfOpenResult result = pdf::openDocument(path);
    if (!result.document.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorMessage;
        }
        return false;
    }

    setDocument(result.document, path);
    return true;
}

void PdfView::setDocumentPassword(const QString& password) {
    documentPassword_ = password;
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
    // 页面布局同样按文档记住：上次用双页看的，这次打开还是双页。
    // （没记过就是默认的连续长卷。）
    const auto rememberedLayout = rememberedLayoutForFile(path);
    if (rememberedLayout.has_value()) {
        layout_.setLayoutMode(*rememberedLayout);
        trace(QStringLiteral("layout restored mode=%1").arg(static_cast<int>(*rememberedLayout)));
    } else {
        layout_.setLayoutMode(PageLayout::LayoutMode::Continuous);
    }
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
        // 反向同步：外部代码也会改工具（编号图章、图片图章、callout、放完批注回到
        // 选择态），按钮选中态必须跟着走。映射仍只来自那一张表。
        for (const auto& entry : annotationToolButtons_) {
            const bool shouldCheck = entry.second == tool;
            if (entry.first->isChecked() != shouldCheck) {
                QSignalBlocker blocker(entry.first);
                entry.first->setChecked(shouldCheck);
            }
        }
        // 工具换了，连续批注要跟着记住现在挂的是哪个工具。
        syncContinuousModeWithTool();
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
                finishAnnotationPlacement();
            });
    connect(canvas_, &PageCanvas::squareAnnotationRequested, this,
            [this](int pageIndex, const QRectF& userRect) {
                if (annotationController_) {
                    annotationController_->addSquare(pageIndex, userRect);
                    finishAnnotationPlacement();
                }
            });
    connect(canvas_, &PageCanvas::measureShapeRequested, this,
            [this](int pageIndex, int mode, const QVector<QPointF>& vertices) {
                if (annotationController_) {
                    annotationController_->addMeasureShape(
                        static_cast<MeasureMode>(mode), pageIndex, vertices, 1.0);
                    finishAnnotationPlacement();
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
                } else {
                    finishAnnotationPlacement();
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
                finishAnnotationPlacement();
            });
    connect(canvas_, &PageCanvas::attachmentRequested, this,
            [this](int pageIndex, const QPointF& userPoint) {
                if (!annotationController_) {
                    return;
                }
                // 文件已经选好（录制中心 / 外部挑来的）：直接落下，不再弹选择框。
                if (!pendingAttachmentData_.isEmpty()) {
                    const QString name = pendingAttachmentName_;
                    const QByteArray data = pendingAttachmentData_;
                    pendingAttachmentName_.clear();
                    pendingAttachmentData_.clear();
                    annotationController_->addAttachment(pageIndex, userPoint, name, data);
                    annotationController_->setTool(AnnotationController::Tool::None);
                    emit statusMessage(tr("附件已放置；保存文档后写入 PDF。"));
                    return;
                }
                const QString path = QFileDialog::getOpenFileName(this, tr("添加附件"));
                if (path.isEmpty()) {
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
                finishAnnotationPlacement();
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
                finishAnnotationPlacement();
            });
    // 编号图章：编号在领域层的计数器里，放一个涨一个，不打断用户。
    connect(canvas_, &PageCanvas::numberStampRequested, this,
            [this](int pageIndex, const QPointF& userPoint) {
                if (!annotationController_) {
                    return;
                }
                const QString label = stampSequence_.next();
                constexpr double kWidth = 140.0;
                constexpr double kHeight = 48.0;
                const QRectF rect(userPoint.x() - kWidth / 2.0, userPoint.y() - kHeight / 2.0,
                                  kWidth, kHeight);
                annotationController_->addStamp(pageIndex, rect, label);
                finishAnnotationPlacement();
            });
    connect(canvas_, &PageCanvas::freeTextRequested, this,
            [this](int pageIndex, const QRectF& userRect) {
                bool accepted = false;
                const QString text = QInputDialog::getMultiLineText(
                    this, tr("文本框"), tr("输入文字（中文会由本程序绘制，其他阅读器按字体回退显示）:"),
                    QString(), &accepted);
                if (accepted && !text.trimmed().isEmpty() && annotationController_) {
                    annotationController_->addFreeText(pageIndex, userRect, text);
                    finishAnnotationPlacement();
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
                finishAnnotationPlacement();
            });
    connect(canvas_, &PageCanvas::polygonAnnotationRequested, this,
            [this](int pageIndex, int type, const QVector<QPointF>& vertices) {
                if (annotationController_) {
                    annotationController_->addPolygon(static_cast<pdf::AnnotationType>(type),
                                                      pageIndex, vertices);
                    finishAnnotationPlacement();
                }
            });
    connect(canvas_, &PageCanvas::ellipseAnnotationRequested, this,
            [this](int pageIndex, const QRectF& userRect) {
                if (annotationController_) {
                    annotationController_->addShape(pdf::AnnotationType::Ellipse, pageIndex,
                                                    userRect);
                    finishAnnotationPlacement();
                }
            });
    connect(canvas_, &PageCanvas::inkAnnotationRequested, this,
            [this](int pageIndex, const QVector<QPointF>& stroke) {
                if (annotationController_) {
                    annotationController_->addInk(pageIndex, stroke);
                    finishAnnotationPlacement();
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
    connect(textController_.get(), &TextController::textPageReady, this, [this](int pageIndex) {
        canvas_->viewport()->update();
        // 文本层是后台提取的：用户点的朗读可能要等它提取完才开得起来。
        if (pendingSpeechPage_ >= 0 && pendingSpeechPage_ == pageIndex) {
            const int waiting = pendingSpeechPage_;
            pendingSpeechPage_ = -1;
            startSpeechOnPage(waiting);
        }
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
    // 渲染失败不能全吞掉：坏页会显示成空白，用户只会以为文档坏了。
    // 只在失败的是当前页时报一句，避免一次缩放把几十页的失败刷满状态栏。
    connect(renderThread_, &RenderThread::pageFailed, this,
            [this](int pageIndex, qint64, int) {
                trace(QStringLiteral("render failed page=%1").arg(pageIndex + 1));
                if (pageIndex >= 0 && pageIndex == currentPage()) {
                    emit statusMessage(
                        tr("第 %1 页渲染失败：页面内容可能已损坏").arg(pageIndex + 1));
                }
            });

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

    // 页号已经重排：按页号记的对象选择必须清掉，否则“编辑对象”会作用到
    // 原页码上的另一页（删第 1 页后，第 5 页的选择变成了第 6 页）。
    selectedObjectPage_ = -1;
    selectedObjectIndexes_.clear();
    objectCyclePage_ = -1;
    objectCyclePosition_ = -1;
    canvas_->clearObjectSelection();

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
    documentPassword_.clear();
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
    // 控制器已经全部换掉：外界（主窗口、索引面板、AI 面板）拿着的旧指针必须重取。
    emit controllersRefreshed();
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

double PdfView::minZoomFactor() {
    return kMinZoom;
}

double PdfView::maxZoomFactor() {
    return kMaxZoom;
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
    // 明确指定比例 = 退出「适合宽度 / 适合页面」。
    // 目标比例与当前相同也要退出：视图设置面板里选「实际大小」必须真的不再适配，
    // 否则会停在 fit 标志上（比例一样，模式却没换）。
    fitToWidth_ = false;
    fitToPage_ = false;
    applyZoom(factor, true);
}

void PdfView::applyZoom(double factor, bool recordHistory) {
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
    if (recordHistory && !restoringHistory_) {
        history_.record(captureViewState());
        emit historyChanged(history_.canGoBack(), history_.canGoForward());
    }
}

void PdfView::zoomAtPoint(double factor, int pageIndex, const QPointF& pagePoint,
                          const QPoint& viewportPoint) {
    if (!std::isfinite(factor) || factor <= 0.0 || canvas_ == nullptr) {
        return;
    }
    const double before = zoom_;
    // 滚轮缩放不进历史栈：一个手势会触发几十次 setZoom，
    // 每次都记一条的话「后退」要按几十下才回到原处。
    applyZoom(zoom_ * factor, false);
    if (qFuzzyCompare(before, zoom_)) {
        return;
    }
    if (pageIndex >= 0) {
        canvas_->restoreZoomAnchor(pageIndex, pagePoint, viewportPoint);
    }
    trace(QStringLiteral("wheel zoom page=%1 factor=%2").arg(pageIndex).arg(zoom_));
}

void PdfView::showCanvasContextMenu(const QPoint& viewportPoint) {
    if (canvas_ == nullptr) {
        return;
    }
    QMenu menu(this);
    const QString selection = textController_ ? textController_->selectedText() : QString();

    QAction* copyAction = menu.addAction(tr("复制"));
    copyAction->setEnabled(!selection.isEmpty());
    connect(copyAction, &QAction::triggered, this, &PdfView::copySelection);

    QAction* selectAllAction = menu.addAction(tr("全选当前页"));
    selectAllAction->setEnabled(textController_ != nullptr);
    connect(selectAllAction, &QAction::triggered, this, &PdfView::selectAllOnCurrentPage);

    menu.addSeparator();
    QAction* zoomInAction = menu.addAction(tr("放大"));
    connect(zoomInAction, &QAction::triggered, this, &PdfView::zoomIn);
    QAction* zoomOutAction = menu.addAction(tr("缩小"));
    connect(zoomOutAction, &QAction::triggered, this, &PdfView::zoomOut);
    QAction* fitWidthAction = menu.addAction(tr("适宽"));
    connect(fitWidthAction, &QAction::triggered, this, &PdfView::zoomToFitWidth);
    QAction* fitPageAction = menu.addAction(tr("整页"));
    connect(fitPageAction, &QAction::triggered, this, &PdfView::zoomToFitPage);

    menu.addSeparator();
    QAction* backAction = menu.addAction(tr("后退"));
    backAction->setEnabled(history_.canGoBack());
    connect(backAction, &QAction::triggered, this, &PdfView::navigateBack);
    QAction* forwardAction = menu.addAction(tr("前进"));
    forwardAction->setEnabled(history_.canGoForward());
    connect(forwardAction, &QAction::triggered, this, &PdfView::navigateForward);

    menu.exec(canvas_->viewport()->mapToGlobal(viewportPoint));
}

void PdfView::selectAllOnCurrentPage() {
    if (textController_ == nullptr || canvas_ == nullptr) {
        return;
    }
    const int page = canvas_->currentPage();
    const TextLayout* layout = textController_->layout(page);
    if (layout == nullptr || layout->charCount() == 0) {
        return;
    }
    textController_->setSelection(page, 0, layout->charCount());
    canvas_->viewport()->update();
    trace(QStringLiteral("select all page=%1 chars=%2").arg(page).arg(layout->charCount()));
}

void PdfView::zoomIn() {
    setZoom(zoom_ * kZoomStep);
}void PdfView::zoomOut() {
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
    setUserRotation(userRotation_ + 90);
}

void PdfView::rotateLeft() {
    setUserRotation(userRotation_ + 270);
}

// 设置视图旋转。只认 90 的倍数：页面自带的 /Rotate 也是 90 的倍数，
// 别的角度在 PDF 里没有意义（渲染与文字定位都按整 90 度算）。
void PdfView::setUserRotation(int degrees) {
    const int normalized = ((degrees % 360) + 360) % 360;
    if (normalized % 90 != 0 || normalized == userRotation_) {
        return;
    }
    userRotation_ = normalized;
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
    // 页面布局也按文档记住：同一份文档下次打开还是上次的排布方式，
    // 与缩放记忆同一族（见 app/editor/ViewMemory.h）。
    if (!filePath_.isEmpty()) {
        rememberLayoutForFile(filePath_, mode);
    }
    emit zoomChanged(zoom_);
}

PageLayout::LayoutMode PdfView::layoutMode() const {
    return layout_.layoutMode();
}

bool PdfView::zoomFitsWidth() const {
    return fitToWidth_;
}

bool PdfView::zoomFitsPage() const {
    return fitToPage_;
}

int PdfView::userRotation() const {
    return userRotation_;
}

bool PdfView::rulerVisible() const {
    return canvas_ != nullptr && canvas_->rulerVisible();
}

void PdfView::goToPage(int index) {
    if (canvas_ != nullptr) {
        canvas_->goToPage(index);
    }
    // 跳页是"用户主动换位置"的最强信号，记进历史。缩放同理（见 setZoom）。
    history_.record(captureViewState());
}

void PdfView::goToBlock(int pageIndex, const QRectF& userRect) {
    if (pageIndex < 0 || pageIndex >= document_.pageCount()) {
        return;
    }
    goToPage(pageIndex);
    if (userRect.isEmpty()) {
        return;
    }
    // 选中原型里的对象：用户跳过去一眼就能看到「说的是这一块」。
    editObjectsInRect(pageIndex, userRect);
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
    QString error;
    const QByteArray source = document_.serialize(&error);
    if (source.isEmpty()) {
        QMessageBox::warning(this, tr("清理隐藏信息"), error);
        return;
    }

    // 加密文档：字节一直是加密的，qpdf 不给密码就解不开。
    //
    // 这里问一次密码，记住它（保存时还要用同一把钥匙）。
    QString password = documentPassword_;
    if (pdf::requiresPasswordToOpen(source, nullptr) && password.isEmpty()) {
        bool accepted = false;
        password = QInputDialog::getText(this, tr("需要密码"),
                                         tr("这份文档已加密，请输入打开密码："),
                                         QLineEdit::Password, QString(), &accepted);
        if (!accepted || password.isEmpty()) {
            QMessageBox::information(
                this, tr("清理隐藏信息"),
                tr("这份文档已加密，需要正确的打开密码才能检查和清理隐藏信息。"));
            return;
        }
    }

    // 先列清单，再让用户逐项勾选。
    //
    // 为什么不再直接问一句「确定全清吗」：原来那个组合入口会把嵌入文件本体
    // 一起删掉，用户点一下就丢了附件，而且事后不知道自己丢了什么。
    const QVector<pdf::HiddenDataCategory> categories =
        pdf::enumerateHiddenData(source, &error, password);
    if (categories.isEmpty()) {
        QMessageBox::warning(this, tr("清理隐藏信息"), error);
        return;
    }
    HiddenDataDialog dialog(this);
    dialog.setCategories(categories);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QStringList selected = dialog.selectedCategoryIds();
    if (selected.isEmpty()) {
        // 一项都没勾 = 什么都不做。
        //
        // 绝不能把空集合当成「全清」：用户在空对话框上按确定，意思是「先别动」。
        QMessageBox::information(this, tr("清理隐藏信息"),
                                 tr("没有勾选任何类别，文档保持原样。"));
        return;
    }
    QSet<QString> selectedIds;
    for (const QString& id : selected) {
        selectedIds.insert(id);
    }
    const QByteArray sanitized =
        pdf::sanitizeHiddenDataSelective(source, selectedIds, &error, password);
    if (sanitized.isEmpty()) {
        QMessageBox::warning(this, tr("清理隐藏信息"), error);
        return;
    }
    auto reopened = pdf::openDocumentFromBytes(sanitized, filePath_, password);
    if (!reopened.document.isValid()) {
        QMessageBox::warning(this, tr("清理隐藏信息"),
                             tr("清理结果校验失败：%1").arg(reopened.errorMessage));
        return;
    }
    setDocument(std::move(reopened.document), filePath_);
    // 记住密码：清理后的文档仍然是加密的，保存时还要用它。
    setDocumentPassword(password);
    setModified(true);
    QStringList clearedTitles;
    for (const QString& id : selected) {
        clearedTitles.append(hiddenDataCategoryTitle(id));
    }
    emit statusMessage(tr("已清理 %1 类隐藏数据：%2；请保存文档以写入磁盘")
                           .arg(selected.size())
                           .arg(clearedTitles.join(QStringLiteral("、"))));
    trace(QStringLiteral("hidden data sanitized categories=%1")
              .arg(clearedTitles.join(QLatin1Char(','))));
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
    // 逐块转换，**不要先并成一个包围盒**：多行选区并集会把两次选中的
    // 第一行与最后一行之间的整块内容一起栅格化删除，用户没选中的文字也没了。
    QVector<QRectF> userRects;
    userRects.reserve(displayRects.size());
    for (const QRectF& rect : displayRects) {
        const QRectF converted =
            QRectF(displayToUser(pageIndex, rect.topLeft()),
                   displayToUser(pageIndex, rect.bottomRight())).normalized();
        if (!converted.isEmpty()) {
            userRects.append(converted);
        }
    }
    if (userRects.isEmpty() || !page->redactByRasterizing(userRects, 2.0)) {
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
    // 切法交给 pdf 层算：界面只负责选参数、拼文件名、写文件。
    const QVector<pdf::PageRange> ranges = pdf::PageSplitPlanner::byCount(count, perFile);
    const QString base = QFileInfo(filePath_).completeBaseName();
    int written = 0;
    for (const pdf::PageRange& range : ranges) {
        const QString name = QStringLiteral("%1_%2-%3.pdf")
                                 .arg(base.isEmpty() ? QStringLiteral("part") : base)
                                 .arg(range.first + 1)
                                 .arg(range.last);
        QString error;
        if (writePageRange(range.first, range.last, QDir(directory).filePath(name), &error)) {
            ++written;
        } else {
            trace(QStringLiteral("split part failed range=%1-%2 error=%3")
                      .arg(range.first + 1).arg(range.last).arg(error));
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
    // 切法交给 pdf 层算：只看顶层书签、起点去重升序、最后一段到文档末尾。
    const QVector<pdf::PageRange> ranges =
        pdf::PageSplitPlanner::byOutline(document_.pageCount(), outline());
    if (ranges.size() < 2) {
        QMessageBox::information(this, tr("按书签拆分"),
                                 tr("书签少于两个（或者都指向同一页），没法按书签拆。"));
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(this, tr("选择输出目录"));
    if (directory.isEmpty()) {
        return;
    }
    const QString base = QFileInfo(filePath_).completeBaseName();
    int written = 0;
    for (const pdf::PageRange& range : ranges) {
        const QString name = QStringLiteral("%1_%2-%3.pdf")
                                 .arg(base.isEmpty() ? QStringLiteral("part") : base)
                                 .arg(range.first + 1)
                                 .arg(range.last);
        QString error;
        if (writePageRange(range.first, range.last, QDir(directory).filePath(name), &error)) {
            ++written;
        } else {
            trace(QStringLiteral("split by outline failed range=%1-%2 error=%3")
                      .arg(range.first + 1).arg(range.last).arg(error));
        }
    }
    trace(QStringLiteral("split by outline parts=%1 written=%2").arg(ranges.size()).arg(written));
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
    // 先量每页单独序列化后的字节数。体积是估出来的，不是精确值——
    // 合并成一个文件时还要加交叉引用表和文档目录，实际会略大于累加值。
    // 这个偏差不影响使用：切出来的文件会比上限略大一点，但不会差出数量级。
    QVector<qint64> pageSizes;
    pageSizes.reserve(count);
    for (int page = 0; page < count; ++page) {
        const pdf::PdfDocument single = document_.backupPage(page);
        qint64 pageSize = 0;
        if (single.isValid()) {
            QString sizeError;
            const QByteArray bytes = single.serialize(&sizeError);
            pageSize = bytes.isEmpty() ? 0 : bytes.size();
        }
        pageSizes.append(pageSize);
    }
    // 切法交给 pdf 层算（单页超限就自成一档，不会把一页切两半）。
    const QVector<pdf::PageRange> ranges = pdf::PageSplitPlanner::bySize(pageSizes, limit);
    const QString base = QFileInfo(filePath_).completeBaseName();
    int written = 0;
    for (const pdf::PageRange& range : ranges) {
        const QString name = QStringLiteral("%1_%2-%3.pdf")
                                 .arg(base.isEmpty() ? QStringLiteral("part") : base)
                                 .arg(range.first + 1)
                                 .arg(range.last);
        QString error;
        if (writePageRange(range.first, range.last, QDir(directory).filePath(name), &error)) {
            ++written;
        } else {
            trace(QStringLiteral("split by size failed range=%1-%2 error=%3")
                      .arg(range.first + 1).arg(range.last).arg(error));
        }
    }
    trace(QStringLiteral("split by size parts=%1 written=%2").arg(ranges.size()).arg(written));
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
    // 范围换算交给 pdf 层算（1 基含两端 → 0 基半开区间，越界自动夹住）。
    const QVector<pdf::PageRange> ranges = pdf::PageSplitPlanner::extract(count, first, last);
    if (ranges.isEmpty()) {
        QMessageBox::warning(this, tr("提取页面"), tr("提取失败。"));
        return;
    }
    const pdf::PageRange range = ranges.first();
    pdf::PdfDocument part = document_.backupPage(range.first);
    if (!part.isValid()) {
        return;
    }
    for (int page = range.first + 1; page < range.last; ++page) {
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
    // 先把要插入的页全部备份出来，再动主文档：源 PDF 第 k 页坏了时，
    // 不能留下“前 k-1 页已经插进去、布局却没重排”的半成品。
    QVector<pdf::PdfDocument> backups;
    backups.reserve(sourceCount);
    for (int index = 0; index < sourceCount; ++index) {
        const pdf::PdfDocument backup = opened.document.backupPage(index);
        if (!backup.isValid()) {
            QMessageBox::warning(this, tr("插入页面"),
                                 tr("第 %1 页读不出来，没有插入任何页面。").arg(index + 1));
            return;
        }
        backups.append(backup);
    }
    for (int index = 0; index < backups.size(); ++index) {
        if (!document_.restorePageFrom(backups.at(index), insertAt + index)) {
            // 回滚已经插入的页，保证“失败时文档保持原样”。
            for (int inserted = index - 1; inserted >= 0; --inserted) {
                document_.deletePage(insertAt + inserted);
            }
            handlePageStructureChanged();
            QMessageBox::warning(this, tr("插入页面"), tr("插入失败，文档已恢复到操作前。"));
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

// ---------------------------------------------------------- 文档级附件 ----

QVector<pdf::DocumentAttachment> PdfView::attachments() const {
    return document_.attachments();
}

QByteArray PdfView::attachmentData(const QString& name, QString* error) const {
    const QVector<pdf::DocumentAttachment> entries = document_.attachments();
    for (int index = 0; index < entries.size(); ++index) {
        if (entries.at(index).name == name) {
            return document_.attachmentData(index, error);
        }
    }
    if (error != nullptr) {
        *error = tr("这份文档里没有叫「%1」的附件。").arg(name);
    }
    return {};
}

bool PdfView::addAttachment(const QString& name, const QByteArray& data,
                            const QString& description, QString* error) {
    if (!requirePermission(effectivePermissions().allowModifying, tr("添加附件"))) {
        if (error != nullptr) {
            *error = tr("这份文档不允许修改。");
        }
        return false;
    }
    if (!document_.addAttachment(name, data, description, error)) {
        return false;
    }
    setModified(true);
    emit attachmentsChanged();
    return true;
}

bool PdfView::removeAttachment(const QString& name, QString* error) {
    if (!requirePermission(effectivePermissions().allowModifying, tr("删除附件"))) {
        if (error != nullptr) {
            *error = tr("这份文档不允许修改。");
        }
        return false;
    }
    const QVector<pdf::DocumentAttachment> entries = document_.attachments();
    for (int index = 0; index < entries.size(); ++index) {
        if (entries.at(index).name != name) {
            continue;
        }
        if (!document_.removeAttachment(index, error)) {
            return false;
        }
        setModified(true);
        emit attachmentsChanged();
        return true;
    }
    if (error != nullptr) {
        *error = tr("这份文档里没有叫「%1」的附件。").arg(name);
    }
    return false;
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

bool PdfView::setFormFieldValueWithScripts(int pageIndex, const QString& name,
                                           const QString& value, QString* reason) {
    if (!requirePermission(effectivePermissions().allowFormFilling, tr("填写表单"))) {
        if (reason != nullptr) *reason = tr("这份文档不允许填写表单。");
        return false;
    }
    // 只接管文本域与复选 / 单选；下拉、列表、签名仍走 PDFium 的原路径。
    for (const pdf::FormField& field : document_.formFields()) {
        if (field.name != name || field.pageIndex != pageIndex) continue;
        if (field.type != 2 && field.type != 3 && field.type != 6) {
            return setFormFieldValue(pageIndex, name, value);
        }
        break;
    }
    QString error;
    const QByteArray source = document_.serialize(&error);
    if (source.isEmpty()) {
        if (reason != nullptr) *reason = tr("无法序列化文档：%1").arg(error);
        return false;
    }

    if (!formInteractionController_) {
        formInteractionController_ = std::make_unique<FormInteractionController>();
    }
    const FormInteractionController::MutationResult mutation =
        formInteractionController_->setValue(source, name, value);
    if (!mutation.ok()) {
        if (reason != nullptr) *reason = mutation.error;
        return false;
    }

    const pdf::PdfDocument before = document_.backupPage(pageIndex);
    if (!document_.reloadFromBytes(mutation.bytes, &error)) {
        if (reason != nullptr) *reason = tr("写回后重新打开失败：%1").arg(error);
        return false;
    }
    QString serializeError;
    const QByteArray refreshed = document_.serialize(&serializeError);
    if (!refreshed.isEmpty()) document_.syncBytes(refreshed);
    cache_.clear();
    if (canvas_ != nullptr) canvas_->viewport()->update();
    if (before.isValid()) recordContentEdit(pageIndex, before, tr("填写表单"));
    setModified(true);
    emit formFieldsChanged();
    return true;
}

int PdfView::applyFormFieldValues(const QVector<QPair<QString, QString>>& values,
                                  QString* reason) {
    if (values.isEmpty()) return 0;
    if (!requirePermission(effectivePermissions().allowFormFilling, tr("填写表单"))) {
        if (reason != nullptr) *reason = tr("这份文档不允许填写表单。");
        return 0;
    }
    QString error;
    const QByteArray source = document_.serialize(&error);
    if (source.isEmpty()) {
        if (reason != nullptr) *reason = tr("无法序列化文档：%1").arg(error);
        return 0;
    }

    if (!formInteractionController_) {
        formInteractionController_ = std::make_unique<FormInteractionController>();
    }
    const FormInteractionController::MutationResult mutation =
        formInteractionController_->applyValues(source, values);
    const auto failureText = [this](const FormInteractionController::Failure& failure) {
        switch (failure.kind) {
        case FormInteractionController::Failure::Kind::MissingField:
            return tr("没有字段 %1").arg(failure.fieldName);
        case FormInteractionController::Failure::Kind::ReadOnly:
            return tr("%1 是只读的").arg(failure.fieldName);
        case FormInteractionController::Failure::Kind::Rejected:
            return tr("%1：%2").arg(failure.fieldName, failure.detail);
        }
        return QString();
    };
    if (mutation.applied == 0) {
        if (reason != nullptr) {
            *reason = !mutation.error.isEmpty()
                          ? mutation.error
                          : (mutation.failures.isEmpty()
                                 ? tr("没有可写入的字段")
                                 : failureText(mutation.failures.first()));
        }
        return 0;
    }
    if (!mutation.ok()) {
        if (reason != nullptr) *reason = mutation.error;
        return 0;
    }

    const pdf::PdfDocument before = mutation.firstPage >= 0
                                        ? document_.backupPage(mutation.firstPage)
                                        : pdf::PdfDocument();
    if (!document_.reloadFromBytes(mutation.bytes, &error)) {
        if (reason != nullptr) *reason = tr("写回后重新打开失败：%1").arg(error);
        return 0;
    }
    QString serializeError;
    const QByteArray refreshed = document_.serialize(&serializeError);
    if (!refreshed.isEmpty()) document_.syncBytes(refreshed);
    cache_.clear();
    if (canvas_ != nullptr) canvas_->viewport()->update();
    if (before.isValid() && mutation.firstPage >= 0) {
        recordContentEdit(mutation.firstPage, before, tr("批量填写表单"));
    }
    setModified(true);
    emit formFieldsChanged();
    if (!mutation.failures.isEmpty() && reason != nullptr) {
        *reason = tr("%1 个字段没写进去：%2")
                      .arg(mutation.failures.size())
                      .arg(failureText(mutation.failures.first()));
    }
    return mutation.applied;
}

bool PdfView::validateFormKeystroke(int pageIndex, const QString& name, const QString& candidate,
                                    QString* reason) {
    Q_UNUSED(pageIndex)
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (!formInteractionController_) {
        formInteractionController_ = std::make_unique<FormInteractionController>();
    }
    return formInteractionController_->validateKeystroke(bytes, name, candidate, reason);
}

bool PdfView::applyFormFieldRichValue(int pageIndex, const QString& name,
                                      const QVector<pdf::FormTextRun>& runs, QString* reason) {
    if (!requirePermission(effectivePermissions().allowFormFilling, tr("填写表单"))) {
        if (reason != nullptr) *reason = tr("这份文档不允许填写表单。");
        return false;
    }
    QString error;
    const QByteArray source = document_.serialize(&error);
    if (source.isEmpty()) {
        if (reason != nullptr) *reason = tr("无法序列化文档：%1").arg(error);
        return false;
    }
    if (!formInteractionController_) {
        formInteractionController_ = std::make_unique<FormInteractionController>();
    }
    const FormInteractionController::MutationResult mutation =
        formInteractionController_->writeRichValue(source, name, runs);
    if (!mutation.ok()) {
        if (reason != nullptr) *reason = mutation.error;
        return false;
    }
    const pdf::PdfDocument before = document_.backupPage(pageIndex);
    if (!document_.reloadFromBytes(mutation.bytes, &error)) {
        if (reason != nullptr) *reason = tr("写回后重新打开失败：%1").arg(error);
        return false;
    }
    QString serializeError;
    const QByteArray refreshed = document_.serialize(&serializeError);
    if (!refreshed.isEmpty()) document_.syncBytes(refreshed);
    cache_.clear();
    if (canvas_ != nullptr) canvas_->viewport()->update();
    if (before.isValid()) recordContentEdit(pageIndex, before, tr("填写表单"));
    setModified(true);
    emit formFieldsChanged();
    return true;
}
bool PdfView::flattenFormsOnPage(int pageIndex) {
    if (!requirePermission(effectivePermissions().allowModifying, tr("扁平化表单"))) return false;
    QString error;
    const QByteArray source = document_.serialize(&error);
    if (source.isEmpty()) return false;
    if (!formDocumentController_) {
        formDocumentController_ = std::make_unique<FormDocumentController>();
    }
    const FormDocumentController::Result result =
        formDocumentController_->flattenPage(source, pageIndex);
    if (!result.ok() || !document_.reloadFromBytes(result.bytes, &error)) return false;
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
    if (!formDocumentController_) {
        formDocumentController_ = std::make_unique<FormDocumentController>();
    }
    const FormDocumentController::Result result =
        formDocumentController_->appendField(source, insert);
    if (!result.ok()) {
        QMessageBox::warning(this, tr("创建表单域"), result.error);
        return;
    }
    QString reloadError;
    if (!document_.reloadFromBytes(result.bytes, &reloadError)) {
        QMessageBox::warning(this, tr("创建表单域"),
                             tr("字段写入后校验失败：%1").arg(reloadError));
        return;
    }
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
    QString error;
    const QByteArray source = document_.serialize(&error);
    if (source.isEmpty()) return false;
    std::optional<QStringList> options;
    if (optionsEdit != nullptr) {
        options = optionsEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
    }
    if (!formDocumentController_) {
        formDocumentController_ = std::make_unique<FormDocumentController>();
    }
    const FormDocumentController::Result result = formDocumentController_->updateFieldProperties(
        source, pageIndex, name, newName, tooltipEdit->text().trimmed(),
        readOnly->isChecked(), required->isChecked(), multiline->isChecked(), options);
    if (!result.ok()) {
        QMessageBox::warning(this, tr("字段属性"), result.error);
        return false;
    }
    if (!document_.reloadFromBytes(result.bytes, &error)) return false;
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
    if (!formDocumentController_) {
        formDocumentController_ = std::make_unique<FormDocumentController>();
    }
    const FormDocumentController::Result result =
        formDocumentController_->appendField(source, insert);
    if (!result.ok()) {
        if (reason != nullptr) *reason = result.error;
        return false;
    }
    QString reloadError;
    if (!document_.reloadFromBytes(result.bytes, &reloadError)) {
        if (reason != nullptr) *reason = tr("字段写入后校验失败：%1").arg(reloadError);
        return false;
    }
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
    if (!formInteractionController_) {
        formInteractionController_ = std::make_unique<FormInteractionController>();
    }
    return formInteractionController_->scripts(bytes);
}

QStringList PdfView::formCalculationOrder() const {
    if (!document_.isValid()) return {};
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (bytes.isEmpty()) return {};
    if (!formInteractionController_) {
        formInteractionController_ = std::make_unique<FormInteractionController>();
    }
    return formInteractionController_->calculationOrder(bytes);
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
    if (!formInteractionController_) {
        formInteractionController_ = std::make_unique<FormInteractionController>();
    }
    const FormInteractionController::MutationResult mutation =
        formInteractionController_->writeScripts(source, scripts, order);
    if (!mutation.ok()) {
        if (reason != nullptr) *reason = mutation.error;
        return false;
    }
    auto reopened = pdf::openDocumentFromBytes(mutation.bytes, document_.filePath());
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
    QString error;
    const QByteArray bytes = document_.serialize(&error);
    if (bytes.isEmpty()) return 0;
    if (!formInteractionController_) {
        formInteractionController_ = std::make_unique<FormInteractionController>();
    }
    // 一次载入、一次计算、一次写回：重置 50 个字段不会把文档重写 50 遍。
    return applyFormFieldValues(formInteractionController_->resetUpdates(bytes));
}


// 设置页码标签：把「页码怎么显示」写进 PDF 的 /PageLabels。
//
// 为什么要单独一个功能：页码显示规则是 PDF 的**文档级属性**（catalog 的 /PageLabels），
// 它决定阅读器状态栏与「跳转页」框里显示什么；
// 页面正文里印着的页码文字是另一件事（页眉页脚装饰），两者不能混为一谈。
void PdfView::setReadingScheme(theme::ReadingScheme scheme) {
    if (canvas_ != nullptr) {
        canvas_->setReadingScheme(scheme);
    }
}

// --------------------------------------------------------- 视图模式 ----
//
// 五个开关全都只是「怎么显示」：标尺、边缘自动滚动、翻页过渡、纯文本查看器、
// 逆序镜像。文档一个字节都没动，也不进撤销栈 —— 用户按 Ctrl+Z 不该把标尺撤掉。

void PdfView::setRulerVisible(bool visible) {
    if (canvas_ != nullptr) {
        canvas_->setRulerVisible(visible);
    }
}

void PdfView::setRulerUnit(int unitIndex) {
    if (canvas_ == nullptr) {
        return;
    }
    // 单位用整数值传进来（Ribbon 上的按钮只认序号），这里映射成领域层的枚举。
    viewmodes::RulerUnit unit = viewmodes::RulerUnit::Millimeters;
    switch (unitIndex) {
        case 0: unit = viewmodes::RulerUnit::Points; break;
        case 1: unit = viewmodes::RulerUnit::Millimeters; break;
        case 2: unit = viewmodes::RulerUnit::Inches; break;
        default: break;
    }
    canvas_->setRulerUnit(unit);
}

void PdfView::setAutoScrollEnabled(bool enabled) {
    if (canvas_ != nullptr) {
        canvas_->setAutoScrollEnabled(enabled);
    }
}

void PdfView::setPageTransition(int transitionIndex) {
    if (canvas_ == nullptr) {
        return;
    }
    viewmodes::PageTransition transition = viewmodes::PageTransition::None;
    switch (transitionIndex) {
        case 1: transition = viewmodes::PageTransition::Fade; break;
        case 2: transition = viewmodes::PageTransition::SlideLeft; break;
        case 3: transition = viewmodes::PageTransition::SlideRight; break;
        default: break;
    }
    canvas_->setPageTransition(transition);
}

void PdfView::setMirrorEnabled(bool enabled) {
    if (canvas_ != nullptr) {
        canvas_->setMirrorEnabled(enabled);
    }
}

void PdfView::setPanModeEnabled(bool enabled) {
    if (canvas_ != nullptr) {
        canvas_->setPanModeEnabled(enabled);
    }
}

bool PdfView::panModeEnabled() const {
    return canvas_ != nullptr && canvas_->panModeEnabled();
}

// 纯文本查看器：把当前页的文本层按阅读顺序显示成纯文本。
//
// 行与行之间的空格由领域层保证（词与词之间的空格是文本层里真实存在的字符），
// 这里只是把结果摆进文本框，不做自己的拼接。
void PdfView::showPlainTextViewer() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("纯文本查看器"), tr("先打开一份 PDF。"));
        return;
    }
    const int pageIndex = currentPage();
    const std::optional<pdf::PdfPage> page = document_.page(pageIndex);
    const std::optional<pdf::TextPage> text = page ? page->extractText() : std::nullopt;
    if (!text) {
        QMessageBox::information(this, tr("纯文本查看器"), tr("这一页没有可提取的文字。"));
        return;
    }
    const QString plain = viewmodes::pageAsPlainText(*text);
    if (plain.isEmpty()) {
        QMessageBox::information(this, tr("纯文本查看器"), tr("这一页没有可提取的文字。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("纯文本查看器 - 第 %1 页").arg(pageIndex + 1));
    dialog.resize(720, 640);
    auto* layout = new QVBoxLayout(&dialog);
    auto* editor = new QPlainTextEdit(&dialog);
    editor->setObjectName(QStringLiteral("plainText.viewer"));
    editor->setReadOnly(true);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setPlainText(plain);
    layout->addWidget(editor);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    dialog.exec();
}

// 整篇纯文本：一页一页接起来，页与页之间空一行。
void PdfView::showWholeDocumentPlainText() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("纯文本查看器"), tr("先打开一份 PDF。"));
        return;
    }
    QString all;
    for (int index = 0; index < document_.pageCount(); ++index) {
        const std::optional<pdf::PdfPage> page = document_.page(index);
        const std::optional<pdf::TextPage> text = page ? page->extractText() : std::nullopt;
        if (!text) {
            continue;
        }
        const QString plain = viewmodes::pageAsPlainText(*text);
        if (plain.isEmpty()) {
            continue;
        }
        if (!all.isEmpty()) {
            all += QLatin1Char('\n');
            all += QLatin1Char('\n');
        }
        all += plain;
    }
    if (all.isEmpty()) {
        QMessageBox::information(this, tr("纯文本查看器"), tr("这份文档没有可提取的文字。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("纯文本查看器 - 整篇"));
    dialog.resize(720, 640);
    auto* layout = new QVBoxLayout(&dialog);
    auto* editor = new QPlainTextEdit(&dialog);
    editor->setObjectName(QStringLiteral("plainText.document"));
    editor->setReadOnly(true);
    editor->setPlainText(all);
    layout->addWidget(editor);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    dialog.exec();
}


// 给当前页起个名字（命名位置），并能按名字跳回来。
//
// 为什么单独一个功能：命名目标（/Dests）是 PDF 里**跨工具通用**的跳转方式，
// 别的阅读器、别的程序也认；书签只在本程序的书签面板里能用。
void PdfView::namedDestinationDialog() {
    if (!document_.isValid()) {
        return;
    }
    QString error;
    const QByteArray source = document_.serialize(&error);
    if (source.isEmpty()) {
        QMessageBox::warning(this, tr("命名位置"), error);
        return;
    }
    const QVector<pdf::NamedDestination> existing = pdf::readNamedDestinations(source, &error);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("命名位置"));
    dialog.resize(520, 440);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("给当前页起一个名字，之后可以按名字跳回来。"), &dialog));

    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("namedDestination.list"));
    for (const pdf::NamedDestination& destination : existing) {
        list->addItem(QStringLiteral("%1（第 %2 页）")
                          .arg(destination.name)
                          .arg(destination.pageIndex + 1));
    }
    layout->addWidget(list);

    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setObjectName(QStringLiteral("namedDestination.name"));
    nameEdit->setPlaceholderText(tr("名称（例如 封面）"));
    layout->addWidget(nameEdit);

    auto* jumpButton = new QPushButton(tr("跳转到所选"), &dialog);
    jumpButton->setObjectName(QStringLiteral("namedDestination.jump"));
    layout->addWidget(jumpButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("写入"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(jumpButton, &QPushButton::clicked, &dialog, [&list, &existing, this, &dialog] {
        const int row = list->currentRow();
        if (row < 0 || row >= existing.size()) {
            return;
        }
        const pdf::NamedDestination& destination = existing.at(row);
        if (destination.pageIndex >= 0) {
            goToPage(destination.pageIndex);
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) {
        return;
    }
    // 写回是覆盖语义：已有的加上新的一条一起写，否则旧的名字会丢。
    QVector<pdf::NamedDestination> targets = existing;
    bool replaced = false;
    for (pdf::NamedDestination& destination : targets) {
        if (destination.name == name) {
            destination = pdf::NamedDestination{name, currentPage(), {}, 0.0, false};
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        targets.append(pdf::NamedDestination{name, currentPage(), {}, 0.0, false});
    }
    const QByteArray written = pdf::writeNamedDestinations(source, targets, &error);
    if (written.isEmpty()) {
        QMessageBox::warning(this, tr("命名位置"),
                             error.isEmpty() ? tr("写入失败，文档没有修改。") : error);
        return;
    }
    QString reloadError;
    auto reopened = pdf::openDocumentFromBytes(written, filePath_);
    if (!reopened.document.isValid()) {
        QMessageBox::warning(this, tr("命名位置"),
                             tr("写入后的文档无法重新打开，文档没有修改：%1")
                                 .arg(reopened.errorMessage));
        return;
    }
    setDocument(std::move(reopened.document), filePath_);
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("已写入命名位置：%1").arg(name));
    trace(QStringLiteral("named destination written"));
}


// 通用批处理：对一个目录里的 PDF 依次执行几步处理，产出 JSON 报告。
//
// 为什么是「目录 + 步骤」而不是「文件 + 步骤」：批量场景里人手里通常是一堆
// 文件，不会逐个挑；目录扫一遍更贴近实际用法。
void PdfView::batchProcessDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("批处理"));
    dialog.resize(620, 520);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        tr("选一个目录，把里面的 PDF 依次处理一遍，结束时给一份 JSON 报告。"),
        &dialog));

    auto* inputRow = new QHBoxLayout;
    auto* inputEdit = new QLineEdit(&dialog);
    inputEdit->setObjectName(QStringLiteral("batch.inputDirectory"));
    inputEdit->setReadOnly(true);
    inputRow->addWidget(inputEdit);
    auto* inputButton = new QPushButton(tr("选择目录…"), &dialog);
    inputRow->addWidget(inputButton);
    layout->addLayout(inputRow);
    connect(inputButton, &QPushButton::clicked, &dialog, [inputEdit, &dialog] {
        const QString path = QFileDialog::getExistingDirectory(
            &dialog, tr("选择要批处理的目录"), inputEdit->text());
        if (!path.isEmpty()) {
            inputEdit->setText(path);
        }
    });

    auto* outputRow = new QHBoxLayout;
    auto* outputEdit = new QLineEdit(&dialog);
    outputEdit->setObjectName(QStringLiteral("batch.outputDirectory"));
    outputEdit->setPlaceholderText(tr("输出目录（留空则只跑处理、不落盘）"));
    outputRow->addWidget(outputEdit);
    layout->addLayout(outputRow);

    // 源文件的打开密码：批处理要读的是磁盘上的原始字节，加密文档不给密码
    // 就打不开。留空表示这批文件都没加密。
    auto* passwordRow = new QHBoxLayout;
    auto* passwordEdit = new QLineEdit(&dialog);
    passwordEdit->setObjectName(QStringLiteral("batch.sourcePassword"));
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setPlaceholderText(tr("源文件打开密码（目录里有加密文件时填）"));
    passwordRow->addWidget(passwordEdit);
    layout->addLayout(passwordRow);

    auto* steps = new QListWidget(&dialog);
    steps->setObjectName(QStringLiteral("batch.steps"));
    const QStringList stepNames{tr("清理隐藏信息"), tr("移除嵌入文件"), tr("加密（AES-256）"),
                               tr("什么也不做（只出报告）")};
    for (const QString& name : stepNames) {
        QListWidgetItem* item = new QListWidgetItem(name, steps);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    layout->addWidget(steps);

    auto* report = new QPlainTextEdit(&dialog);
    report->setObjectName(QStringLiteral("batch.report"));
    report->setReadOnly(true);
    layout->addWidget(report);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("开始"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, &dialog, inputEdit, outputEdit,
                                                             passwordEdit, steps, report, &stepNames] {
        const QString directory = inputEdit->text().trimmed();
        if (directory.isEmpty()) {
            QMessageBox::information(&dialog, tr("批处理"), tr("先选一个目录。"));
            return;
        }
        QDir source(directory);
        const QStringList files = source.entryList(QStringList{QStringLiteral("*.pdf")}, QDir::Files,
                                                    QDir::Name);
        if (files.isEmpty()) {
            QMessageBox::information(&dialog, tr("批处理"), tr("这个目录里没有 PDF。"));
            return;
        }
        // 加密步要先收密码：密码用「加密与权限」对话框收，与单文档加密同一套语义，
        // 不能两处各写一套规则。取消就整批不做——悄悄跳过加密那一步，
        // 用户会以为文件已经加密过了。
        bool wantsEncrypt = false;
        for (int index = 0; index < steps->count(); ++index) {
            const QListWidgetItem* item = steps->item(index);
            if (item->checkState() == Qt::Checked && item->text() == stepNames.at(2)) {
                wantsEncrypt = true;
            }
        }
        pdf::PdfEncryptionOptions encryptOptions;
        if (wantsEncrypt) {
            EncryptionDialog encryptDialog(doc::defaultEncryptOptions(QString()), true, &dialog);
            if (encryptDialog.exec() != QDialog::Accepted || !encryptDialog.encryptionEnabled()) {
                return;
            }
            encryptOptions = encryptDialog.options();
            if (encryptOptions.userPassword.isEmpty()) {
                QMessageBox::warning(&dialog, tr("批处理"), tr("打开密码不能为空。"));
                return;
            }
        }
        QVector<doc::BatchStep> batchSteps;
        for (int index = 0; index < steps->count(); ++index) {
            const QListWidgetItem* item = steps->item(index);
            if (item->checkState() != Qt::Checked) {
                continue;
            }
            const QString title = item->text();
            // 源文件的打开密码：加密文档不给密码就打不开，
            // 清理与移除嵌入文件这两步都要用它。
            const QString sourcePassword = passwordEdit->text();
            if (title == stepNames.at(0)) {
                // 批处理是对一批文件跑固定步骤，没有逐项勾选的界面，
                // 所以这里用与界面一致的默认集合。
                //
                // 关键差别：不再顺手删掉嵌入文件本体 —— 批处理里有独立的
                // 「移除嵌入文件」一步，要不要删由用户自己勾那一步。
                const QStringList defaultIds = HiddenDataDialog::defaultCleanupCategoryIds();
                QSet<QString> cleanupIds;
                for (const QString& id : defaultIds) {
                    cleanupIds.insert(id);
                }
                batchSteps.append(doc::BatchStep{
                    title, [cleanupIds, sourcePassword](const QByteArray& in, QString* err) {
                        return pdf::sanitizeHiddenDataSelective(in, cleanupIds, err, sourcePassword);
                    }});
            } else if (title == stepNames.at(1)) {
                batchSteps.append(doc::BatchStep{title, [sourcePassword](const QByteArray& in, QString* err) {
                    return pdf::removeEmbeddedFiles(in, err, sourcePassword);
                }});
            } else if (title == stepNames.at(2)) {
                // 加密步走 doc 层：加密完会做一次「用密码重新打开」的自检，
                // 打不开就不产出文件，不会给出一堆谁也读不了的 PDF。
                //
                // 源文件自己就是加密的时，用源文件密码先解开旧的再加密。
                batchSteps.append(
                    doc::makeEncryptStep(title, encryptOptions, sourcePassword));
            } else {
                // 「什么也不做」：原样返回，报告里照样有它的字节数。
                batchSteps.append(doc::BatchStep{title, [](const QByteArray& in, QString*) {
                    return in;
                }});
            }
        }
        if (batchSteps.isEmpty()) {
            QMessageBox::information(&dialog, tr("批处理"), tr("至少勾一个处理步骤。"));
            return;
        }
        QStringList inputPaths;
        for (const QString& name : files) {
            inputPaths.append(source.filePath(name));
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const doc::BatchReport result = doc::runBatch(inputPaths, QStringList(), batchSteps, true,
                                                       outputEdit->text().trimmed());
        QApplication::restoreOverrideCursor();
        report->setPlainText(QString::fromUtf8(result.toJson()));
        emit statusMessage(tr("批处理完成：成功 %1，失败 %2")
                               .arg(result.succeededCount)
                               .arg(result.failedCount));
    });
    dialog.exec();
}

void PdfView::pageLabelsDialog() {
    if (!document_.isValid()) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("页码标签"));
    dialog.resize(420, 240);
    auto* layout = new QVBoxLayout(&dialog);

    auto* styleBox = new QComboBox(&dialog);
    styleBox->setObjectName(QStringLiteral("pageLabels.style"));
    styleBox->addItem(tr("阿拉伯数字 (1, 2, 3)"), QStringLiteral("/D"));
    styleBox->addItem(tr("大写罗马 (I, II, III)"), QStringLiteral("/R"));
    styleBox->addItem(tr("小写罗马 (i, ii, iii)"), QStringLiteral("/r"));
    styleBox->addItem(tr("大写字母 (A, B, C)"), QStringLiteral("/A"));
    styleBox->addItem(tr("小写字母 (a, b, c)"), QStringLiteral("/a"));
    layout->addWidget(styleBox);

    auto* prefixEdit = new QLineEdit(&dialog);
    prefixEdit->setObjectName(QStringLiteral("pageLabels.prefix"));
    prefixEdit->setPlaceholderText(tr("前缀（例如 A-）"));
    layout->addWidget(prefixEdit);

    auto* startSpin = new QSpinBox(&dialog);
    startSpin->setObjectName(QStringLiteral("pageLabels.start"));
    startSpin->setRange(1, 1000000);
    startSpin->setValue(1);
    layout->addWidget(startSpin);

    // 预览用和写入完全相同的换算函数：界面显示的必须就是文件里存的。
    auto* preview = new QLabel(&dialog);
    preview->setObjectName(QStringLiteral("pageLabels.preview"));
    preview->setWordWrap(true);
    layout->addWidget(preview);
    const auto refreshPreview = [&]() {
        const QVector<pdf::PageLabelRange> ranges{
            pdf::PageLabelRange{0, 0, styleBox->currentData().toString(), prefixEdit->text()}};
        QStringList samples;
        const int shown = qMin(6, document_.pageCount());
        for (int index = 0; index < shown; ++index) {
            samples.append(QStringLiteral("%1 → %2")
                               .arg(index + 1)
                               .arg(pdf::pageLabelText(ranges, index, startSpin->value())));
        }
        preview->setText(samples.join(QStringLiteral("    ")));
    };
    connect(styleBox, &QComboBox::currentIndexChanged, &dialog, refreshPreview);
    connect(prefixEdit, &QLineEdit::textChanged, &dialog, refreshPreview);
    connect(startSpin, &QSpinBox::valueChanged, &dialog, refreshPreview);
    refreshPreview();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("写入"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString error;
    const QByteArray source = document_.serialize(&error);
    if (source.isEmpty()) {
        QMessageBox::warning(this, tr("页码标签"), error);
        return;
    }
    pdf::PageLabelScheme scheme;
    scheme.firstPageNumber = startSpin->value();
    scheme.ranges.append(pdf::PageLabelRange{0, 0, styleBox->currentData().toString(),
                                              prefixEdit->text()});
    const QByteArray labeled = pdf::writePageLabels(source, scheme, &error);
    if (labeled.isEmpty()) {
        QMessageBox::warning(this, tr("页码标签"),
                             error.isEmpty() ? tr("写入失败，文档没有修改。") : error);
        return;
    }
    QString reloadError;
    auto reopened = pdf::openDocumentFromBytes(labeled, filePath_);
    if (!reopened.document.isValid()) {
        QMessageBox::warning(this, tr("页码标签"),
                             tr("写入后的文档无法重新打开，文档没有修改：%1")
                                 .arg(reopened.errorMessage));
        return;
    }
    setDocument(std::move(reopened.document), filePath_);
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("已写入页码标签"));
    trace(QStringLiteral("page labels written"));
}

void PdfView::mergeDocumentDialog() {
    if (!requirePermission(effectivePermissions().allowAssembly, tr("合并页面"))) return;
    trace(QStringLiteral("document merge dialog"));
    if (!document_.isValid()) {
        return;
    }
    // 一次可以选多份：福昕能一次并多份，我们原来只能并一份。
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("合并 PDF"), QString(),
                                                           tr("PDF (*.pdf)"));
    if (paths.isEmpty()) {
        return;
    }
    QString error;
    const QByteArray currentBytes = document_.serialize(&error);
    if (currentBytes.isEmpty()) {
        QMessageBox::warning(this, tr("合并 PDF"), error);
        return;
    }
    // 当前文档排在第一位：合并结果 = 当前文档 + 选中的这些文件。
    QVector<pdf::MergeInput> inputs;
    inputs.append(pdf::MergeInput{currentBytes, QFileInfo(filePath_).fileName()});
    for (const QString& path : paths) {
        const auto opened = pdf::openDocument(path);
        if (!opened.document.isValid()) {
            QMessageBox::warning(this, tr("合并 PDF"),
                                 tr("打不开 %1：%2")
                                     .arg(QFileInfo(path).fileName(), opened.errorMessage));
            return;
        }
        QString serializeError;
        const QByteArray bytes = opened.document.serialize(&serializeError);
        if (bytes.isEmpty()) {
            QMessageBox::warning(this, tr("合并 PDF"),
                                 tr("读不出 %1：%2")
                                     .arg(QFileInfo(path).fileName(), serializeError));
            return;
        }
        inputs.append(pdf::MergeInput{bytes, QFileInfo(path).fileName()});
    }
    pdf::MergeOptions options;
    pdf::MergeOutcome outcome;
    if (!pdf::mergeDocuments(inputs, options, &outcome, &error)) {
        QMessageBox::warning(this, tr("合并 PDF"), error.isEmpty() ? tr("合并失败。") : error);
        return;
    }
    auto reopened = pdf::openDocumentFromBytes(outcome.bytes, filePath_);
    if (!reopened.document.isValid()) {
        QMessageBox::warning(this, tr("合并 PDF"),
                             tr("合并结果校验失败：%1").arg(reopened.errorMessage));
        return;
    }
    setDocument(std::move(reopened.document), filePath_);
    refreshAfterDecoration();
    rebuildLayout();
    setModified(true);
    trace(QStringLiteral("document merged pages=%1").arg(outcome.pageCount));
    emit statusMessage(tr("已合并 %1 份文件：共 %2 页").arg(paths.size()).arg(outcome.pageCount));
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

    // 首选：字形级替换。只改选中的那几个字，沿用原字体与字宽；
    // 原字体写不出新字时直接拒绝，绝不静默换成别的字体（那正是白块路径）。
    const auto extractedForSplice = page->extractText();
    if (!originalText.isEmpty() && extractedForSplice &&
        extractedForSplice->plainText().count(originalText, Qt::CaseSensitive) == 1) {
        QString spliceSerializeError;
        const QByteArray spliceInput = document_.serialize(&spliceSerializeError);
        if (!spliceInput.isEmpty()) {
            pdf::TextSpliceTarget target;
            target.pageIndex = pageIndex;
            target.find = originalText;
            target.replace = text;
            const pdf::TextSpliceResult spliced = pdf::splicePageText(spliceInput, target);
            if (spliced.ok()) {
                QString reloadError;
                if (document_.reloadFromBytes(spliced.bytes, &reloadError)) {
                    refreshAfterDecoration();
                    recordContentEdit(pageIndex, before, tr("字形级替换选中文字"));
                    setModified(true);
                    emit statusMessage(tr("已替换选中文字（保留原字体）"));
                    return;
                }
            } else if (spliced.report.error.contains(QStringLiteral("写不出"))) {
                // 明确告诉用户「原字体没有这些字」，并停止；不要退回白块路径。
                QMessageBox::warning(this, tr("替换文字"),
                                     tr("原字体写不出新文字：%1；这次替换已取消。")
                                         .arg(spliced.report.error));
                return;
            }
        }
    }

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
    // 段落排版：只有选到文字对象时才有意义。
    if (std::any_of(objects.cbegin(), objects.cend(),
                    [](const auto& object) { return object.type == pdf::PageObjectType::Text; })) {
        actions.append(tr("段落排版"));
    }
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
    if (action == tr("段落排版")) {
        // 段落排版要重写整页的内容流，与这里的对象级编辑不是一回事
        // （页面级撤销快照会失效），所以单独走一条路。
        applyParagraphFormat(pageIndex, objects);
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

// 段落排版：把选中的文字对象按段落参数重排一次。
//
// 三件事全部交给领域层：断行与位置（pdf::layoutParagraph）、
// 内容流（pdf::buildParagraphContentStream）、页面坐标。
// 界面只负责收参数与把结果写回去。
void PdfView::applyParagraphFormat(int pageIndex, const QVector<pdf::PageObjectInfo>& objects) {
    if (!document_.isValid()) {
        return;
    }
    QStringList texts;
    QVector<int> textIndexes;
    QRectF bounds;
    for (const pdf::PageObjectInfo& object : objects) {
        if (object.type != pdf::PageObjectType::Text) {
            continue;
        }
        if (object.text.trimmed().isEmpty()) {
            continue;
        }
        texts.append(object.text);
        textIndexes.append(object.index);
        bounds = bounds.united(object.bounds);
    }
    if (texts.isEmpty()) {
        QMessageBox::information(this, tr("段落排版"), tr("选区内没有可排版的文字。"));
        return;
    }

    ParagraphFormatDialog dialog(this);
    dialog.setText(texts.join(QLatin1Char('\n')));
    dialog.setBox(bounds);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // 位置与断行：界面不算，全交给领域层。
    const pdf::ParagraphFormat format = dialog.format();
    const pdf::ParagraphPlan plan = pdf::layoutParagraph(dialog.text(), dialog.box(), format);
    if (!plan.ok()) {
        QMessageBox::warning(this, tr("段落排版"), plan.error);
        return;
    }
    // 内容流：复用领域层给的写法，界面不自己拼 BT/ET。
    const QByteArray content =
        pdf::buildParagraphContentStream(plan, QStringLiteral("AipdfPara"));
    if (content.isEmpty()) {
        QMessageBox::warning(this, tr("段落排版"), tr("没有生成可写入的内容流。"));
        return;
    }

    // 先备份这一页：写失败时要能原样退回去。
    const pdf::PdfDocument backup = document_.backupPage(pageIndex);
    if (!backup.isValid()) {
        QMessageBox::warning(this, tr("段落排版"), tr("无法创建页面安全备份，文档没有修改。"));
        return;
    }
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return;
    }
    // 原来的文字对象先去掉：新段落是替掉它的，不是叠在它上面。
    if (!page->removeObjects(textIndexes)) {
        QMessageBox::warning(this, tr("段落排版"), tr("没能移除原来的文字对象，文档没有修改。"));
        return;
    }
    QString serializeError;
    const QByteArray bytes = document_.serialize(&serializeError);
    if (bytes.isEmpty()) {
        document_.restorePageFrom(backup, pageIndex);
        QMessageBox::warning(this, tr("段落排版"),
                             serializeError.isEmpty() ? tr("读不到文档内容，文档没有修改。")
                                                     : serializeError);
        return;
    }
    QString appendError;
    const QByteArray updated = appendContentStreamToPage(
        bytes, pageIndex, content, QStringLiteral("AipdfPara"), &appendError);
    if (updated.isEmpty()) {
        document_.restorePageFrom(backup, pageIndex);
        QMessageBox::warning(this, tr("段落排版"), appendError);
        return;
    }
    QString replaceError;
    if (!replaceDocumentBytes(updated, &replaceError)) {
        document_.restorePageFrom(backup, pageIndex);
        QMessageBox::warning(this, tr("段落排版"), replaceError);
        return;
    }
    canvas_->clearObjectSelection();
    selectedObjectPage_ = -1;
    selectedObjectIndexes_.clear();
    emit statusMessage(tr("已按段落参数重排 %1 个文字对象").arg(textIndexes.size()));
}

// 段落排版入口（Ribbon 上用）：拿当前选区里的文字对象来排。
void PdfView::paragraphFormatDialog() {
    if (!document_.isValid() || textController_ == nullptr) {
        QMessageBox::information(this, tr("段落排版"), tr("先打开一份 PDF。"));
        return;
    }
    const int pageIndex = textController_->selectionPage();
    const QVector<QRectF> displayRects = textController_->selectionRects();
    if (pageIndex < 0 || displayRects.isEmpty()) {
        QMessageBox::information(this, tr("段落排版"), tr("先选中一段文字。"));
        return;
    }
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return;
    }
    QRectF userRect;
    for (const QRectF& rect : displayRects) {
        userRect = userRect.united(QRectF(displayToUser(pageIndex, rect.topLeft()),
                                         displayToUser(pageIndex, rect.bottomRight())).normalized());
    }
    applyParagraphFormat(pageIndex, page->objectsInRect(userRect));
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
        if (input.isEmpty()) continue;
        // 字形级替换：一次把这一页上的所有匹配换掉，沿用原字体与字宽。
        pdf::TextSpliceTarget target;
        target.pageIndex = pageIndex;
        target.find = query;
        target.replace = replacement;
        target.sensitivity = Qt::CaseInsensitive;
        target.occurrence = -1;
        const pdf::TextSpliceResult spliced = pdf::splicePageText(input, target);
        if (!spliced.ok()) {
            if (spliced.report.error.contains(QStringLiteral("写不出"))) {
                QMessageBox::warning(this, tr("搜索替换"),
                                     tr("原字体写不出新文字：%1；这次替换已取消。")
                                         .arg(spliced.report.error));
                return;
            }
            continue;
        }
        QString reloadError;
        if (!before.isValid() || !document_.reloadFromBytes(spliced.bytes, &reloadError)) {
            QMessageBox::warning(this, tr("搜索替换"),
                                 tr("安全内容流重写失败：%1").arg(reloadError));
            return;
        }
        replacementCount += spliced.report.occurrences;
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

// 插入类命令的默认落点：内容区左上角，留一点边距。
//
// 为什么不用页面正中：正文框与引用通常写在段落开头，
// 放正中会盖住正在读的内容。
static QRectF defaultInsertRect(const pdf::PdfDocument& document, int pageIndex,
                                 double width, double height) {
    auto page = document.page(pageIndex);
    if (!page.has_value()) {
        return QRectF();
    }
    const pdf::MediaBox box = page->contentBox();
    if (box.width() <= 0.0 || box.height() <= 0.0) {
        return QRectF();
    }
    return QRectF(box.left + 36.0, box.top - 48.0, width, height);
}

// 交叉引用：插一段「见第 N 页」式的引用文字，并让它点得动。
//
// 为什么文字与链接分两步写：可见文字走批注文本框（我们自己画外观，
// 中文才不会被别的阅读器显示成方块），跳转走 PDF 的 Link 批注
// （PDF 里唯一能跨页跳的批注类型）。两者叠在同一块区域上。
void PdfView::insertCrossReferenceDialog() {
    if (!document_.isValid()) {
        return;
    }
    const auto entries = pdf::flattenOutline(outline());
    if (entries.empty()) {
        QMessageBox::information(this, tr("交叉引用"),
                                 tr("文档里还没有书签。先在「编辑 > 导航 > 书签」里建书签，再来做交叉引用。"));
        return;
    }
    QStringList labels;
    labels.reserve(static_cast<int>(entries.size()));
    for (const pdf::OutlineEntry& entry : entries) {
        // 层级用缩进体现：书签面板是扁平的，引用列表要让人一眼看出层级。
        const QString indent(static_cast<int>(entry.depth) * 2, QLatin1Char(' '));
        labels.append(indent + entry.title + QStringLiteral(" · 第 %1 页").arg(entry.pageIndex + 1));
    }
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(this, tr("交叉引用"), tr("引用哪个书签:"),
                                                 labels, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    const int choice = labels.indexOf(chosen);
    if (choice < 0) {
        return;
    }
    const pdf::OutlineEntry target = entries.at(choice);
    if (target.pageIndex < 0 || target.pageIndex >= document_.pageCount()) {
        QMessageBox::warning(this, tr("交叉引用"), tr("这个书签没有有效的目标页。"));
        return;
    }
    const QString defaultText = target.title.trimmed().isEmpty()
        ? QStringLiteral("第 %1 页").arg(target.pageIndex + 1)
        : target.title.trimmed();
    const QString text = QInputDialog::getText(this, tr("交叉引用"), tr("引用文字:"),
                                               QLineEdit::Normal, defaultText, &accepted);
    if (!accepted || text.trimmed().isEmpty()) {
        return;
    }

    const int pageIndex = currentPage();
    QRectF userRect;
    if (textController_ != nullptr && textController_->selectionPage() == pageIndex) {
        const QVector<QRectF> displayRects = textController_->selectionRects();
        for (const QRectF& rect : displayRects) {
            userRect = userRect.united(
                QRectF(displayToUser(pageIndex, rect.topLeft()),
                       displayToUser(pageIndex, rect.bottomRight())).normalized());
        }
    }
    if (userRect.width() <= 1.0 || userRect.height() <= 1.0) {
        userRect = defaultInsertRect(document_, pageIndex, 220.0, 20.0);
    }
    if (userRect.isEmpty()) {
        return;
    }

    // 链接批注：先写进文件字节，再重开文档。
    QString error;
    const QByteArray serialized = document_.serialize(&error);
    if (serialized.isEmpty()) {
        QMessageBox::warning(this, tr("交叉引用"), error);
        return;
    }
    QByteArray dictionary = "<< /Type /Annot /Subtype /Link /Rect [";
    dictionary += QByteArray::number(userRect.left(), 'f', 2) + ' ';
    dictionary += QByteArray::number(userRect.top(), 'f', 2) + ' ';
    dictionary += QByteArray::number(userRect.right(), 'f', 2) + ' ';
    dictionary += QByteArray::number(userRect.bottom(), 'f', 2);
    dictionary += "] /Border [0 0 0] /Contents (" + text.trimmed().toUtf8() + ") /Dest [@PAGE0 0 R /Fit] >>";
    pdf::InlineAnnotationInsert insert;
    insert.pageIndex = pageIndex;
    insert.dictionary = dictionary;
    insert.targetPages.append(target.pageIndex);
    const pdf::InlineWriteResult written = pdf::injectInlineAnnotations(serialized, {insert});
    if (!written.ok) {
        QMessageBox::warning(this, tr("交叉引用"), written.error);
        return;
    }
    pdf::PdfOpenResult reopened = pdf::openDocumentFromBytes(written.bytes, filePath_);
    if (!reopened.document.isValid()) {
        QMessageBox::warning(this, tr("交叉引用"), reopened.errorMessage);
        return;
    }
    const int pageBefore = currentPage();
    const double zoomBefore = zoom();
    setDocument(std::move(reopened.document), filePath_);
    goToPage(pageBefore);
    setZoom(zoomBefore);
    // 可见文字：重开之后批注模型是新的，这时候加进去才不会被重开冲掉。
    if (annotationController_ != nullptr) {
        annotationController_->addFreeText(pageIndex, userRect, text.trimmed(), 11.0);
    }
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("已插入交叉引用，跳转到第 %1 页").arg(target.pageIndex + 1));
}

// 添加文章框：在当前页放一个多行文本框。
//
// 与批注里的「文本框」是同一套外观（FreeText），区别只在用途：
// 文章框是正文容器，通常放多段文字，所以默认给一个更大的框与字号。
void PdfView::addArticleFrameDialog() {
    if (!document_.isValid()) {
        return;
    }
    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(
        this, tr("添加文章框"), tr("框里的文字:"), QString(), &accepted);
    if (!accepted || text.trimmed().isEmpty()) {
        return;
    }
    const double width = QInputDialog::getDouble(this, tr("添加文章框"), tr("宽度（点）:"),
                                                 260.0, 60.0, 2000.0, 1, &accepted);
    if (!accepted) {
        return;
    }
    const double height = QInputDialog::getDouble(this, tr("添加文章框"), tr("高度（点）:"),
                                                  160.0, 30.0, 2000.0, 1, &accepted);
    if (!accepted) {
        return;
    }
    const double fontSize = QInputDialog::getDouble(this, tr("添加文章框"), tr("字号（点）:"),
                                                    12.0, 6.0, 72.0, 1, &accepted);
    if (!accepted) {
        return;
    }
    const int pageIndex = currentPage();
    const QRectF userRect = defaultInsertRect(document_, pageIndex, width, height);
    if (userRect.isEmpty()) {
        QMessageBox::warning(this, tr("添加文章框"), tr("这一页放不下这个框。"));
        return;
    }
    if (annotationController_ == nullptr ||
        annotationController_->addFreeText(pageIndex, userRect, text.trimmed(), fontSize) == 0) {
        QMessageBox::warning(this, tr("添加文章框"), tr("添加失败。"));
        return;
    }
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("已添加文章框"));
}

// 整篇文档切块：与建索引同一套排版规则。
//
// 为什么同步做：字数统计与自动创建书签都是用户点一次要一个结果的交互，
// 而建索引是后台渐进式（几百页要几秒）。这里抽文本会卡一下界面，
// 所以外面要显示等待光标。
QVector<doc::DocumentChunk> PdfView::buildDocumentChunks() const {
    QVector<doc::DocumentChunk> chunks;
    int order = 0;
    for (int pageIndex = 0; pageIndex < document_.pageCount(); ++pageIndex) {
        auto page = document_.page(pageIndex);
        if (!page.has_value()) {
            continue;
        }
        const auto text = page->extractText();
        if (!text.has_value()) {
            continue;
        }
        const QVector<doc::ChunkLine> lines = buildPageLines(*text);
        if (lines.isEmpty()) {
            continue;
        }
        const QVector<doc::DocumentChunk> pageChunks =
            doc::ChunkBuilder::build(pageIndex, lines, QString(), order);
        order += pageChunks.size();
        chunks += pageChunks;
    }
    return chunks;
}

// 字数统计：总字数、字符数、页数与每页分布。
void PdfView::showDocumentStatisticsDialog() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("字数统计"), tr("先打开一份 PDF。"));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QVector<doc::DocumentChunk> chunks = buildDocumentChunks();
    QApplication::restoreOverrideCursor();

    const doc::DocumentStatistics stats =
        doc::DocumentStatisticsBuilder::fromChunks(chunks, document_.pageCount());

    QDialog dialog(this);
    dialog.setWindowTitle(tr("字数统计"));
    dialog.resize(520, 460);
    auto* layout = new QVBoxLayout(&dialog);
    auto* summary = new QLabel(
        tr("总字数：%1\n字符数：%2（不含空白 %3）\n页数：%4\n识别到的标题：%5")
            .arg(stats.words)
            .arg(stats.characters)
            .arg(stats.charactersNoSpaces)
            .arg(stats.pages)
            .arg(stats.headings),
        &dialog);
    summary->setTextFormat(Qt::PlainText);
    layout->addWidget(summary);
    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels(QStringList{tr("页"), tr("字数"), tr("字符数")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setRowCount(stats.perPage.size());
    for (int row = 0; row < stats.perPage.size(); ++row) {
        const doc::PageStatistics& page = stats.perPage.at(row);
        table->setItem(row, 0, new QTableWidgetItem(QString::number(page.pageIndex + 1)));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(page.words)));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(page.characters)));
    }
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

// 文档属性：读 PDF 的 /Info，可改可写回。
void PdfView::showDocumentPropertiesDialog() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("文档属性"), tr("先打开一份 PDF。"));
        return;
    }
    QString error;
    const QByteArray serialized = document_.serialize(&error);
    if (serialized.isEmpty()) {
        QMessageBox::warning(this, tr("文档属性"), error);
        return;
    }
    pdf::DocumentProperties properties;
    if (!pdf::readDocumentProperties(serialized, &properties, &error)) {
        QMessageBox::warning(this, tr("文档属性"), error);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("文档属性"));
    dialog.resize(480, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    struct Field {
        QString label;
        QString* value;
        bool editable;
    };
    const QVector<Field> fields = {
        {tr("标题:"), &properties.title, true},
        {tr("作者:"), &properties.author, true},
        {tr("主题:"), &properties.subject, true},
        {tr("关键字:"), &properties.keywords, true},
        {tr("创建程序:"), &properties.creator, true},
        {tr("生成程序:"), &properties.producer, true},
        {tr("创建时间:"), &properties.creationDate, false},
        {tr("修改时间:"), &properties.modificationDate, false},
    };
    for (const Field& field : fields) {
        auto* edit = new QLineEdit(*field.value, &dialog);
        edit->setReadOnly(!field.editable);
        edit->setObjectName(QStringLiteral("docProperty.") + field.label.left(field.label.size() - 1));
        form->addRow(field.label, edit);
        // 日期只读；其余字段改了要能存回去。
        if (field.editable) {
            connect(edit, &QLineEdit::textChanged, &dialog, [field](const QString& text) {
                *field.value = text;
            });
        }
    }
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QByteArray written = pdf::writeDocumentProperties(serialized, properties, &error);
    if (written.isEmpty()) {
        QMessageBox::warning(this, tr("文档属性"), error);
        return;
    }
    QString reloadError;
    if (!document_.reloadFromBytes(written, &reloadError)) {
        QMessageBox::warning(this, tr("文档属性"), reloadError);
        return;
    }
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("已保存文档属性"));
}

// 自动创建书签：优先读文档的标签树（H1–H6），没有标签才回退到字高估算。
void PdfView::createOutlineFromHeadingsDialog() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("自动创建书签"), tr("先打开一份 PDF。"));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QVector<doc::DocumentChunk> chunks = buildDocumentChunks();
    QString documentError;
    const QByteArray bytes = documentBytes(&documentError);
    QApplication::restoreOverrideCursor();

    pdf::OutlineGeneratorOptions options;
    // 有结构标签就按标签树判标题，没有才回退到字高估算——
    // 回退时领域层会把「这是估算的」写进 note，界面上要照实说。
    const pdf::OutlineGenerationResult generated = pdf::generateOutline(bytes, chunks, options);
    if (generated.outline.empty()) {
        QMessageBox::information(this, tr("自动创建书签"),
                                 generated.note.isEmpty()
                                     ? tr("没有识别到标题：这份文档既没有可用的标题元素，也没有字高比正文大的行。")
                                     : generated.note);
        return;
    }
    const std::vector<pdf::OutlineItem>& outlineItems = generated.outline;
    const auto entries = pdf::flattenOutline(outlineItems);
    QStringList preview;
    for (int i = 0; i < entries.size() && i < 20; ++i) {
        preview.append(QString(entries.at(static_cast<size_t>(i)).depth * 2, QLatin1Char(' ')) +
                       entries.at(static_cast<size_t>(i)).title);
    }
    if (entries.size() > preview.size()) {
        preview.append(tr("…还有 %1 条").arg(entries.size() - preview.size()));
    }
    const QString source = generated.isEstimated()
                               ? generated.note
                               : tr("已生成 %1 条书签（读自文档的标签树）。").arg(entries.size());
    QMessageBox box(this);
    box.setWindowTitle(tr("自动创建书签"));
    box.setText(tr("%1\n\n%2\n\n确定要写入文档吗？")
                     .arg(source)
                     .arg(preview.join('\n')));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::Yes);
    if (box.exec() != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!replaceOutline(outlineItems, &error)) {
        QMessageBox::warning(this, tr("自动创建书签"), error);
        return;
    }
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("已生成 %1 条书签").arg(entries.size()));
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

void PdfView::activateAttachmentPlacement(const QString& fileName, const QByteArray& data) {
    if (!annotationController_ || fileName.isEmpty() || data.isEmpty()) {
        return;
    }
    pendingAttachmentName_ = fileName;
    pendingAttachmentData_ = data;
    annotationController_->setTool(AnnotationController::Tool::Attachment);
    emit statusMessage(tr("请在页面上单击放置附件的位置；按其他工具可取消。"));
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
    // 切到别的工具就取消「等着落地的附件」：否则下一次点页面会挂上一个
    // 用户早就忘了的文件。
    if (id != QStringLiteral("attachment")) {
        pendingAttachmentName_.clear();
        pendingAttachmentData_.clear();
    }
    if (id == QStringLiteral("undo")) {
        undoAnnotation();
        return;
    }
    if (id == QStringLiteral("redo")) {
        redoAnnotation();
        return;
    }
    // 编号图章：放之前先问一次编号规则（形式、起始号、前后缀），
    // 问完就切工具，之后每放一个编号自动加一。
    if (id == QStringLiteral("numberstamp")) {
        activateNumberStampTool();
        return;
    }
    // 箭头框与区域高亮：工具栏上没有对应的按钮（按钮在 Ribbon 的「注释」标签页），
    // 所以直接切工具，并把工具栏上其它按钮的选中态清掉 ——
    // 免得界面上显示的还是上一个工具。
    if (id == QStringLiteral("callout") || id == QStringLiteral("regionHighlight")) {
        if (annotationController_ != nullptr) {
            annotationController_->setTool(id == QStringLiteral("callout")
                                              ? AnnotationController::Tool::Callout
                                              : AnnotationController::Tool::RegionHighlight);
            // 阻断信号：setChecked(false) 会触发 clicked(false)，把刚设好的
            // callout / regionHighlight 工具又清回 None。
            for (const auto& entry : annotationToolButtons_) {
                if (entry.first->isChecked()) {
                    QSignalBlocker blocker(entry.first);
                    entry.first->setChecked(false);
                }
            }
        }
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
    // 循环是放映的选项：带着当前的循环设置开窗。
    presentation->setLoop(presentationLoop_);
    // 记下窗口指针：放映期间改循环开关要能立刻作用到正在放的窗口上。
    presentationWindow_ = presentation;
    connect(presentation, &QObject::destroyed, this, [this]() {
        presentationWindow_ = nullptr;
        // 放映窗口带 WA_DeleteOnClose，关掉就销毁 —— 录制据此收尾。
        emit presentationClosed();
    });
    presentation->start();
}

void PdfView::setPresentationLoop(bool enabled) {
    presentationLoop_ = enabled;
    // 正在放映的话立刻生效，不用等到下一次放映。
    if (presentationWindow_ != nullptr) {
        presentationWindow_->setLoop(enabled);
    }
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
    finishAnnotationPlacement();
}

// 区域高亮：画布给了顶点（用户空间），这里做判定、算外观流、落进模型。
//
// 三步判定与几何全部交给领域层（pdf::AnnotationShapes）：
// 自交的区域高亮画出来会翻车，必须在生成外观流之前就拒掉。
void PdfView::createRegionHighlight(int pageIndex, const QVector<QPointF>& userPoints) {
    if (!annotationController_) {
        return;
    }
    if (userPoints.size() < 3) {
        emit statusMessage(tr("区域高亮至少需要 3 个顶点"));
        return;
    }
    if (pdf::polygonIsSelfIntersecting(userPoints)) {
        QMessageBox::warning(this, tr("区域高亮"),
                             tr("这个区域自交了（轮廓自己交叉），画不出高亮，请重新圈一次。"));
        return;
    }

    pdf::RegionHighlightSpec spec;
    spec.polygon = userPoints;
    spec.opacity = 0.35;
    spec.color = QColor(255, 214, 0);
    QString error;
    const QByteArray appearance = pdf::buildRegionHighlightAppearanceStream(spec, &error);
    if (appearance.isEmpty()) {
        QMessageBox::warning(this, tr("区域高亮"),
                             error.isEmpty() ? tr("区域高亮生成失败。") : error);
        return;
    }
    const QByteArray resources = pdf::regionHighlightResources(spec.opacity);

    pdf::Annotation annotation;
    annotation.pageIndex = pageIndex;
    // 类型用 Polygon：PDF 规范里区域高亮就是多边形，
    // 区别只在外观（半透明填充而不是描边），那由 customAppearance 表达。
    annotation.type = pdf::AnnotationType::Polygon;
    annotation.vertices = userPoints;
    annotation.rect = pdf::polygonBounds(userPoints);
    annotation.color = spec.color;
    annotation.opacity = spec.opacity;
    annotation.customAppearance = appearance;
    annotation.customAppearanceResources = resources;

    if (annotationController_->addPreparedAnnotation(annotation) == 0) {
        emit statusMessage(tr("区域高亮没有添加成功"));
        return;
    }
    emit statusMessage(tr("已添加区域高亮"));
    finishAnnotationPlacement();
}

// callout 箭头框：注释框 + 箭头尖 → 领域层算外观流 → 落进模型。
void PdfView::createCallout(int pageIndex, const QRectF& userBox, const QPointF& userTip) {
    if (!annotationController_) {
        return;
    }
    pdf::CalloutSpec spec;
    spec.box = userBox;
    spec.tip = userTip;
    // 直箭头：折点与箭头尖重合。折线箭头需要第三步交互，这一版没做。
    spec.knee = userTip;
    spec.strokeWidth = 1.0;
    spec.color = QColor(226, 62, 62);

    QString error;
    // 用 Extended 版本：箭头可以伸出注释框（福昕那种「框在别处、箭头指向正文」）。
    const QByteArray appearance = pdf::buildCalloutAppearanceStreamExtended(spec, &error);
    if (appearance.isEmpty()) {
        QMessageBox::warning(this, tr("箭头框"), error.isEmpty() ? tr("箭头框生成失败。") : error);
        return;
    }

    const QRectF box = spec.box.normalized();
    pdf::Annotation annotation;
    annotation.pageIndex = pageIndex;
    // callout 在 PDF 规范里没有对应的 Subtype，用 Square + 自定义 /AP 是通行做法。
    annotation.type = pdf::AnnotationType::Square;
    annotation.color = spec.color;
    annotation.lineWidth = spec.strokeWidth;
    // /Rect 必须盖住箭头：注释框 ∪ 箭头。
    annotation.rect = pdf::calloutBounds(spec);
    annotation.customAppearance = appearance;

    // 画布要按「注释框 + 箭头」把 callout 画出来，PDF 写入层却不写 Square 的 /Vertices，
    // 所以几何放在 vertices 里给画布用（写盘时这一项会被忽略）。
    // 前 4 个点是注释框的四角（逆时针，左下角起），后面是箭头的折线点。
    annotation.vertices.append(QPointF(box.x(), box.y()));
    annotation.vertices.append(QPointF(box.x() + box.width(), box.y()));
    annotation.vertices.append(QPointF(box.x() + box.width(), box.y() + box.height()));
    annotation.vertices.append(QPointF(box.x(), box.y() + box.height()));
    const QVector<QPointF> localArrow = pdf::calloutArrowPoints(spec);
    for (const QPointF& point : localArrow) {
        // calloutArrowPoints 给的是相对注释框左下角的坐标，换算回页面坐标。
        annotation.vertices.append(point + QPointF(box.x(), box.y()));
    }

    if (annotationController_->addPreparedAnnotation(annotation) == 0) {
        emit statusMessage(tr("箭头框没有添加成功"));
        return;
    }
    emit statusMessage(tr("已添加箭头框"));
    finishAnnotationPlacement();
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
                                  encryptionEnabled_ ? &encryptionOptions_ : nullptr,
                                  documentPassword_);
    if (!saved.ok) {
        if (errorMessage != nullptr) {
            *errorMessage = saved.error;
        }
        trace(QStringLiteral("save failed path=%1").arg(targetPath));
        return false;
    }
    if (!saved.warning.isEmpty()) {
        trace(QStringLiteral("appearance not written: %1").arg(saved.warning));
        // 文件本身是好的，但批注在别的阅读器里可能看不见。以前只写进轨迹日志，
        // 用户完全不知道；现在必须当面提示。
        emit statusMessage(tr("批注外观流没有写入：在其它阅读器里可能看不见批注"));
        QMessageBox::warning(
            this, tr("保存完成，但有提示"),
            tr("%1\n\n文档本身已经保存成功；批注在本程序里正常，但在福昕、"
               "Acrobat 等阅读器里可能显示不出来。")
                .arg(saved.warning));
    }
    if (saved.signatureCount > 0) {
        // 保存是整份重写，签名覆盖的字节已经变了：明确告知失效，而不是让用户
        // 打开阅读器才发现“签名无效”。
        emit statusMessage(tr("文档包含数字签名，本次保存已使签名失效"));
        QMessageBox::information(
            this, tr("数字签名已失效"),
            tr("这份文档包含 %1 个数字签名。保存会重写整份文件，签名覆盖的内容"
               "已经改变，在阅读器里会被判定为无效。\n\n"
               "如果需要保留签名，请在签署完成后不要再次修改并保存这份文件。")
                .arg(saved.signatureCount));
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
    auto* hint = new QLabel(tr("以下内容按页面上的视觉顺序排列（多栏与表格已按位置重排），可用于人工核对屏幕阅读器的阅读次序。"),
                            &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* text = new QTextEdit(&dialog);
    text->setReadOnly(true);
    const std::optional<pdf::PdfPage> page = document_.page(currentPage());
    const std::optional<pdf::TextPage> extracted = page ? page->extractText() : std::nullopt;
    // 阅读顺序优先用排版后的视觉顺序：PDFium 给的是内容流顺序，
    // 多栏、表格、图注混排时直接显示会是乱的（与屏幕阅读器看到的也不一致）。
    QString ordered;
    if (textController_ != nullptr) {
        const TextLayout* textLayout = textController_->layout(currentPage());
        if (textLayout != nullptr && !textLayout->isEmpty()) {
            ordered = textLayout->textForRange(0, textLayout->charCount());
        }
    }
    if (ordered.isEmpty() && extracted.has_value()) {
        ordered = extracted->plainText();
    }
    text->setPlainText(ordered.isEmpty() ? tr("本页没有可提取文字。") : ordered);
    text->setAccessibleName(tr("当前页阅读顺序"));
    layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    // 读文档里既有的标签树：别的工具（Word、Acrobat）写出来的树形状各不相同，
    // 这里按 PDF 32000-1 的通用规则解析，不只是「读回自己写的」。
    QPushButton* structTreeButton =
        buttons->addButton(tr("读取文档标签树"), QDialogButtonBox::ActionRole);
    structTreeButton->setObjectName(QStringLiteral("readOrder.structTree"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(structTreeButton, &QPushButton::clicked, &dialog, [this] { showStructTreeDialog(); });
    layout->addWidget(buttons);
    dialog.exec();
}

// 读取文档里既有的标签树。
//
// 没有标签树不是错误：界面要明确说「这份文档没有标签树」，
// 不能显示成空列表让人以为读过了。
void PdfView::showStructTreeDialog() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("文档标签树"), tr("先打开一份 PDF。"));
        return;
    }
    QString serializeError;
    const QByteArray bytes = document_.serialize(&serializeError);
    if (bytes.isEmpty()) {
        QMessageBox::warning(this, tr("文档标签树"),
                             serializeError.isEmpty() ? tr("读不到文档内容。") : serializeError);
        return;
    }
    const pdf::StructTreeReport report = pdf::readStructTree(bytes);
    StructTreeDialog dialog(this);
    dialog.setReport(report);
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


// --------------------------------------------------- 朗读（系统语音合成）----
//
// 为什么朗读要自己算阅读顺序：PDFium 给的字符顺序是内容流顺序，
// 多栏、表格、图注混排时读出来是乱序。朗读必须按视觉顺序走，
// 所以这里用 TextController 排好的阅读顺序，而不是原始字符顺序。

void PdfView::speakCurrentPage() {
    if (!document_.isValid() || textController_ == nullptr) {
        return;
    }
    if (!speech::SpeechEngine::isSupported()) {
        QMessageBox::information(this, tr("朗读"),
                                 tr("系统没有可用的语音：%1")
                                     .arg(speechEngine_->unavailableReason()));
        return;
    }
    const int pageIndex = currentPage();
    if (!textController_->hasTextPage(pageIndex)) {
        // 文本层是后台提取的：先排队，提取完再读。
        pendingSpeechPage_ = pageIndex;
        textController_->requestTextPage(pageIndex);
        emit statusMessage(tr("正在提取文本，请稍候"));
        return;
    }
    startSpeechOnPage(pageIndex);
}

void PdfView::stopSpeaking() {
    pendingSpeechPage_ = -1;
    // 智能朗读的分段队列也要停：不然下一段会在用户以为已经停掉之后接着念。
    narrationSegments_.clear();
    narrationSegmentIndex_ = -1;
    if (speechEngine_ != nullptr) {
        speechEngine_->stop();
    }
    canvas_->setSpeechHighlight(-1, QVector<QRectF>());
    speechPage_ = -1;
}

bool PdfView::collectPageText(int pageIndex) {
    pageText_.clear();
    pageOrderPerUnit_.clear();
    if (textController_ == nullptr) {
        return false;
    }
    const TextLayout* layout = textController_->layout(pageIndex);
    if (layout == nullptr || layout->isEmpty()) {
        return false;
    }
    const QVector<int>& order = layout->readingOrder();
    const QVector<LayoutChar>& chars = layout->chars();
    for (int position = 0; position < order.size(); ++position) {
        const int source = order.at(position);
        if (source < 0 || source >= chars.size()) {
            continue;
        }
        const QString& piece = chars.at(source).text;
        if (piece.isEmpty()) {
            continue;
        }
        pageText_ += piece;
        // 一个字符可能不止一个 UTF-16 码元：按实际长度记，别想当然当成 1。
        for (int unit = 0; unit < piece.size(); ++unit) {
            pageOrderPerUnit_.append(position);
        }
    }
    return !pageText_.isEmpty();
}

void PdfView::startSpeechOnPage(int pageIndex) {
    if (!document_.isValid() || textController_ == nullptr) {
        return;
    }
    if (!collectPageText(pageIndex)) {
        QMessageBox::information(this, tr("朗读"), tr("这一页没有可朗读的文字。"));
        return;
    }
    speechWords_ = speech::splitIntoWords(pageText_);
    if (speechWords_.isEmpty()) {
        QMessageBox::information(this, tr("朗读"), tr("这一页没有可朗读的文字。"));
        return;
    }
    speechPage_ = pageIndex;
    emit statusMessage(tr("开始朗读"));
    speechEngine_->speak(pageText_);
}


// ------------------------------------------ 批量替换 / 拼写 / 密文处理 ----

// 批量替换：一次改多条规则，规则按表里的顺序依次生效。
//
// 为什么做成一张规则表而不是一问一答：一条一条问没法表达
// 「先把 A 换成 B，再把 B 换成 C」这种顺序语义，
// 用户也没法在执行前看到整批要做什么。
void PdfView::batchReplaceDialog() {
    if (!document_.isValid()) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("批量替换"));
    dialog.resize(760, 460);

    auto* layout = new QVBoxLayout(&dialog);

    auto* scopeRow = new QHBoxLayout;
    scopeRow->addWidget(new QLabel(tr("替换范围:"), &dialog));
    auto* scopeBox = new QComboBox(&dialog);
    scopeBox->setObjectName(QStringLiteral("batchReplace.scope"));
    scopeBox->addItem(tr("整个文档"));
    scopeBox->addItem(tr("当前页"));
    scopeRow->addWidget(scopeBox);
    scopeRow->addStretch(1);
    layout->addLayout(scopeRow);

    auto* table = new QTableWidget(0, 5, &dialog);
    table->setObjectName(QStringLiteral("batchReplace.rules"));
    table->setHorizontalHeaderLabels({tr("查找"), tr("替换为"), tr("区分大小写"), tr("全字匹配"), tr("正则")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table);

    auto* addRule = new QPushButton(tr("添加规则"), &dialog);
    addRule->setObjectName(QStringLiteral("batchReplace.addRule"));
    auto* removeRule = new QPushButton(tr("删除选中规则"), &dialog);
    removeRule->setObjectName(QStringLiteral("batchReplace.removeRule"));
    auto* ruleButtons = new QHBoxLayout;
    ruleButtons->addWidget(addRule);
    ruleButtons->addWidget(removeRule);
    ruleButtons->addStretch(1);
    layout->addLayout(ruleButtons);

    // 规则行：前三列是文本，后三列是勾选框。
    // 勾选框放在单元格里而不是用文字列，是为了让「区分大小写 / 全字 / 正则」
    // 一眼能看出是开关，不用猜。
    auto addRuleRow = [&table, &dialog](const QString& find, const QString& replace) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(find));
        table->setItem(row, 1, new QTableWidgetItem(replace));
        for (int column = 2; column < 5; ++column) {
            auto* box = new QCheckBox(&dialog);
            box->setObjectName(QStringLiteral("batchReplace.rule%1.%2").arg(row).arg(column));
            table->setCellWidget(row, column, box);
        }
    };
    addRuleRow(QString(), QString());

    connect(addRule, &QPushButton::clicked, &dialog, [&addRuleRow] {
        addRuleRow(QString(), QString());
    });
    connect(removeRule, &QPushButton::clicked, &dialog, [&table] {
        const QList<QModelIndex> rows = table->selectionModel()->selectedRows();
        for (int i = rows.size() - 1; i >= 0; --i) {
            table->removeRow(rows.at(i).row());
        }
    });

    auto* dialogButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    dialogButtons->button(QDialogButtonBox::Ok)->setText(tr("开始替换"));
    layout->addWidget(dialogButtons);
    connect(dialogButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<pdf::ReplaceRule> rules;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* findItem = table->item(row, 0);
        const QTableWidgetItem* replaceItem = table->item(row, 1);
        if (findItem == nullptr || findItem->text().isEmpty()) {
            // 查找内容为空的规则直接丢掉：它本来就不可能命中任何东西。
            continue;
        }
        pdf::ReplaceRule rule;
        rule.find = findItem->text();
        rule.replace = replaceItem != nullptr ? replaceItem->text() : QString();
        for (int column = 2; column < 5; ++column) {
            auto* box = qobject_cast<QCheckBox*>(table->cellWidget(row, column));
            if (box == nullptr) {
                continue;
            }
            if (column == 2) {
                rule.caseSensitive = box->isChecked();
            } else if (column == 3) {
                rule.wholeWord = box->isChecked();
            } else {
                rule.regex = box->isChecked();
            }
        }
        rules.append(rule);
    }
    if (rules.isEmpty()) {
        QMessageBox::information(this, tr("批量替换"), tr("没有可执行的替换规则。"));
        return;
    }

    pdf::BatchReplaceOptions options;
    if (scopeBox->currentIndex() == 1) {
        options.pageIndexes.append(currentPage());
    }

    QString serializeError;
    const QByteArray input = document_.serialize(&serializeError);
    if (input.isEmpty()) {
        QMessageBox::warning(this, tr("批量替换"), serializeError);
        return;
    }
    pdf::BatchReplaceResult result;
    QString error;
    const QByteArray rewritten = pdf::applyBatchReplace(input, rules, options, &result, &error);
    if (rewritten.isEmpty()) {
        QMessageBox::warning(this, tr("批量替换"),
                             error.isEmpty() ? tr("替换失败，文档没有修改。") : error);
        return;
    }
    QString reloadError;
    if (!document_.reloadFromBytes(rewritten, &reloadError)) {
        QMessageBox::warning(this, tr("批量替换"),
                             tr("替换结果无法重新打开，文档没有修改：%1").arg(reloadError));
        return;
    }
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("批量替换完成：共改了 %1 处").arg(result.replacements));

    // 逐条结果里有失败的规则要报出来：否则用户以为全部生效了。
    QStringList failed;
    for (const pdf::ReplaceRuleResult& rule : result.perRule) {
        if (!rule.ok) {
            failed.append(tr("「%1」：%2").arg(rule.find, rule.error));
        }
    }
    if (!failed.isEmpty()) {
        QMessageBox::warning(this, tr("批量替换"),
                             tr("以下规则没有生效：\n%1").arg(failed.join('\n')));
    }
}

// 拼写检查：列出可疑的词，给出建议，双击跳到那一处。
//
// 为什么只标「明显可疑」的词：词表覆盖不到的词（专有名词、技术术语）会被漏判。
// 漏判只是少画一条波浪线；误判却会在满篇正常文字里到处画红线，用户很快就不信这条波浪线了。
void PdfView::spellCheckDialog() {
    if (!document_.isValid()) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("拼写检查"));
    dialog.resize(720, 480);

    auto* layout = new QVBoxLayout(&dialog);
    auto* scopeRow = new QHBoxLayout;
    scopeRow->addWidget(new QLabel(tr("检查范围:"), &dialog));
    auto* scopeBox = new QComboBox(&dialog);
    scopeBox->setObjectName(QStringLiteral("spellCheck.scope"));
    scopeBox->addItem(tr("当前页"));
    scopeBox->addItem(tr("整个文档"));
    scopeRow->addWidget(scopeBox);
    scopeRow->addStretch(1);
    layout->addLayout(scopeRow);

    auto* table = new QTableWidget(0, 3, &dialog);
    table->setObjectName(QStringLiteral("spellCheck.issues"));
    table->setHorizontalHeaderLabels({tr("页"), tr("可疑的词"), tr("建议")});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table);

    auto* dialogButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(dialogButtons);
    connect(dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(dialogButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

    // 每条问题带着它所在的页与在该页文本里的区间，跳转时要用。
    struct IssueRow {
        int pageIndex = 0;
        int start = 0;
        int length = 0;
        QString word;
    };
    QVector<IssueRow> issueRows;

    // 逐页取文本：整篇检查要调 PDFium 提取每一页，放到后台线程做，
    // 主线程做会卡住界面。这里先在主线程做当前页，整篇走等待光标。
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // 词表来源是可插拔的：内置英文词表 + 内置中文词表 + 用户放在默认目录下的词表。
    // 加载失败的词表必须报出来——用户会以为自己的词表生效了。
    pdf::SpellChecker::Options spellOptions;
    spellOptions.chineseEnabled = true;
    spellOptions.englishEnabled = true;
    spellOptions.dictionaryPaths = pdf::SpellChecker::defaultDictionaryPaths();
    QVector<pdf::DictionaryLoadResult> dictionaryResults;
    const pdf::SpellChecker checker = pdf::SpellChecker::create(spellOptions, &dictionaryResults);
    const bool wholeDocument = scopeBox->currentIndex() == 1;
    const int firstPage = wholeDocument ? 0 : currentPage();
    const int lastPage = wholeDocument ? document_.pageCount() - 1 : currentPage();
    for (int pageIndex = firstPage; pageIndex <= lastPage; ++pageIndex) {
        QString pageText;
        if (wholeDocument) {
            auto page = document_.page(pageIndex);
            if (!page.has_value()) {
                continue;
            }
            const std::optional<pdf::TextPage> extracted = page->extractText();
            if (!extracted.has_value() || extracted->chars.isEmpty()) {
                continue;
            }
            for (const pdf::TextChar& character : extracted->chars) {
                pageText += character.text;
            }
        } else {
            // 当前页：优先用排好阅读顺序的文本层。
            // 排版结果还没有时不能静默跳过这一页——那会让用户以为
            // 「这一页没有拼写问题」，其实是根本没查。
            if (!collectPageText(pageIndex)) {
                auto fallbackPage = document_.page(pageIndex);
                if (!fallbackPage.has_value()) {
                    continue;
                }
                const std::optional<pdf::TextPage> extracted = fallbackPage->extractText();
                if (!extracted.has_value() || extracted->chars.isEmpty()) {
                    continue;
                }
                for (const pdf::TextChar& character : extracted->chars) {
                    pageText += character.text;
                }
            } else {
                pageText = pageText_;
            }
        }
        for (const pdf::SpellIssue& issue : checker.check(pageText)) {
            IssueRow row;
            row.pageIndex = pageIndex;
            row.start = issue.start;
            row.length = issue.length;
            row.word = issue.word;
            issueRows.append(row);
        }
    }
    QApplication::restoreOverrideCursor();

    // 词表加载失败的，要当面说：用户会以为自己的词表生效了。
    QStringList dictionaryFailures;
    for (const pdf::DictionaryLoadResult& result : dictionaryResults) {
        if (result.ok) {
            continue;
        }
        dictionaryFailures.append(result.error.isEmpty()
                                      ? tr("词表 %1 加载失败").arg(result.path)
                                      : result.error);
    }
    if (!dictionaryFailures.isEmpty()) {
        QMessageBox::warning(&dialog, tr("拼写检查"),
                             tr("以下词表没有加载成功，本次检查没用到它们：\n%1")
                                 .arg(dictionaryFailures.join('\n')));
    }

    for (const IssueRow& issue : issueRows) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(QString::number(issue.pageIndex + 1)));
        table->setItem(row, 1, new QTableWidgetItem(issue.word));
        table->setItem(row, 2, new QTableWidgetItem(checker.suggestions(issue.word).join(QStringLiteral("、"))));
    }

    // 双击跳到那一处：页码 + 选中那个词。
    connect(table, &QTableWidget::cellDoubleClicked, &dialog,
            [this, &issueRows, &table, &scopeBox](int row, int) {
        if (row < 0 || row >= issueRows.size()) {
            return;
        }
        const IssueRow& issue = issueRows.at(row);
        goToPage(issue.pageIndex);
        const TextLayout* layoutOfPage = textController_ != nullptr
                                             ? textController_->layout(issue.pageIndex)
                                             : nullptr;
        if (layoutOfPage == nullptr) {
            // 这一页没有排版结果（整篇检查时未必都排过）：跳到页就够了，
            // 选不中具体那个词也不能算失败。
            return;
        }
        // 该页文本的偏移 → 阅读顺序区间。
        // 只有当前页作用域下 pageOrderPerUnit_ 才是这一页的映射；
        // 整篇检查时它是上一页的残留，不能拿来用。
        if (scopeBox->currentIndex() != 0) {
            return;
        }
        int firstOrder = -1;
        int lastOrder = -1;
        for (int unit = issue.start; unit < issue.start + issue.length; ++unit) {
            if (unit < 0 || unit >= pageOrderPerUnit_.size()) {
                continue;
            }
            const int order = pageOrderPerUnit_.at(unit);
            if (firstOrder < 0) {
                firstOrder = order;
            }
            lastOrder = order;
        }
        if (firstOrder >= 0 && lastOrder >= firstOrder) {
            textController_->setSelection(issue.pageIndex, firstOrder, lastOrder + 1);
        }
    });

    dialog.exec();
}

// 密文四件套：标记 / 马赛克 / 擦除 / 应用密文。
//
// 为什么不是四个分散的按钮：这四件事对同一块区域做的是不同深度的处理，
// 放在一起让用户按「要多狠」选，比让用户先知道四个技术名词更容易。
//
// 诚实性边界（界面上必须写清楚）：标记只是盖一块黑块，原文仍在文件里；
// 马赛克与擦除改的是渲染层，原字形数据未必真的从字节里消失；
// 应用密文最接近「落实」，但整页会变成位图。
void PdfView::redactionModeDialog() {
    if (!document_.isValid() || textController_ == nullptr) {
        return;
    }
    const int pageIndex = textController_->selectionPage();
    const QVector<QRectF> displayRects = textController_->selectionRects();
    if (pageIndex < 0 || displayRects.isEmpty()) {
        QMessageBox::information(this, tr("密文处理"), tr("请先选中要处理的文字区域。"));
        return;
    }
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return;
    }
    if (page->rotationDegrees() != 0) {
        QMessageBox::warning(this, tr("密文处理"),
                             tr("为避免旋转坐标误判，请先把该页旋转回 0° 再执行。"));
        return;
    }

    const QStringList modes{
        tr("标记为密文（只盖黑块，原文仍在文件里，文字层照样能搜到）"),
        tr("马赛克（把区域像素化，文字层里读不到该区域）"),
        tr("擦除（删掉该区域的对象，页面其余部分仍是矢量文字）"),
        tr("应用密文（整页位图化，文字层全部消失，页面失去搜索与编辑能力）"),
    };
    bool accepted = false;
    const QString chosen =
        QInputDialog::getItem(this, tr("密文处理"), tr("处理方式:"), modes, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    const int modeIndex = modes.indexOf(chosen);
    if (modeIndex < 0) {
        return;
    }

    QRectF userRect;
    for (const QRectF& rect : displayRects) {
        userRect = userRect.united(
            QRectF(displayToUser(pageIndex, rect.topLeft()),
                   displayToUser(pageIndex, rect.bottomRight())).normalized());
    }
    if (userRect.isEmpty()) {
        QMessageBox::warning(this, tr("密文处理"), tr("选区无效，文档没有修改。"));
        return;
    }

    const pdf::RedactionMark mark{userRect, pageIndex};
    const QVector<pdf::RedactionMark> marks{mark};

    QString error;
    bool ok = false;
    QString title;
    switch (modeIndex) {
        case 0:
            title = tr("标记为密文");
            ok = pdf::markRedactions(document_, marks, &error);
            break;
        case 1:
            title = tr("马赛克");
            ok = pdf::mosaicRedactions(document_, marks, &error);
            break;
        case 2:
            title = tr("擦除");
            ok = pdf::eraseRedactions(document_, marks, &error);
            break;
        default:
            title = tr("应用密文");
            ok = pdf::applyRedactions(document_, marks, &error);
            break;
    }
    if (!ok) {
        QMessageBox::warning(this, title,
                             error.isEmpty() ? tr("处理失败，文档没有修改。") : error);
        return;
    }
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("%1 已完成").arg(title));
}


// ------------------------------------------------------- 验签：签名列表 ----
//
// 为什么要有这个面板：pyHanko 的验签只能给一段纯文本，
// 用户看不到「这份文件里有几个签名、分别是谁签的、证书链长什么样」。
// 这个面板把结构化的签名信息摆出来，证书链一眼能看全；
// pyHanko 仍然负责真正的密码学验签，两边互补而不是互相替代。
void PdfView::showSignatureListDialog() {
    if (!document_.isValid()) {
        return;
    }
    if (!pdf::signatureInspectionAvailable()) {
        QMessageBox::information(this, tr("签名列表"),
                                 tr("这个版本没有启用签名解析（构建时没有 qpdf）。"));
        return;
    }
    QString serializeError;
    const QByteArray bytes = document_.serialize(&serializeError);
    if (bytes.isEmpty()) {
        QMessageBox::warning(this, tr("签名列表"), serializeError);
        return;
    }
    QString error;
    const QVector<pdf::SignatureInfo> signatures = pdf::inspectSignatures(bytes, &error);
    if (signatures.isEmpty()) {
        QMessageBox::information(this, tr("签名列表"),
                                 error.isEmpty() ? tr("这份文档里没有数字签名。") : error);
        return;
    }

    // 密码学判定：验签要拿**整份文档的字节**去算摘要，
    // 只给签名域是判不出来的（摘要覆盖的是整个 /ByteRange）。
    QString verdictError;
    const QVector<pdf::SignatureCryptoVerdict> verdicts =
        pdf::verifyDocumentSignatures(bytes, &verdictError);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("签名列表"));
    dialog.resize(900, 600);

    auto* layout = new QVBoxLayout(&dialog);

    auto* table = new QTableWidget(signatures.size(), 6, &dialog);
    table->setObjectName(QStringLiteral("signature.list"));
    table->setHorizontalHeaderLabels(
        {tr("签名域"), tr("签署人"), tr("签署时间"), tr("算法"), tr("覆盖检查"), tr("密码学判定")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (int row = 0; row < signatures.size(); ++row) {
        const pdf::SignatureInfo& signature = signatures.at(row);
        table->setItem(row, 0, new QTableWidgetItem(signature.fieldName));
        table->setItem(row, 1, new QTableWidgetItem(signature.signerName));
        table->setItem(row, 2, new QTableWidgetItem(signature.signingTime));
        table->setItem(row, 3, new QTableWidgetItem(signature.subFilter));
        QString coverage;
        if (signature.coversDocument) {
            coverage = tr("覆盖完整");
        } else if (!signature.coverError.isEmpty()) {
            coverage = signature.coverError;
        } else {
            coverage = tr("覆盖不完整");
        }
        table->setItem(row, 4, new QTableWidgetItem(coverage));

        // 判定列：不成立时把原因写出来（是「RSA 对不上」还是「文件被改过」），
        // 不要只写「无效」—— 用户看不出该找谁。
        auto* verdictItem = new QTableWidgetItem();
        if (row < verdicts.size()) {
            const pdf::SignatureCryptoVerdict& verdict = verdicts.at(row);
            if (verdict.signatureValid) {
                verdictItem->setText(tr("签名有效"));
                verdictItem->setForeground(QColor(46, 160, 67));
            } else {
                verdictItem->setText(verdict.failureReason.isEmpty()
                                         ? tr("签名无效")
                                         : verdict.failureReason);
                verdictItem->setForeground(QColor(198, 40, 40));
            }
            if (!verdict.algorithm.isEmpty()) {
                verdictItem->setToolTip(tr("摘要算法：%1").arg(verdict.algorithm));
            }
            if (!verdict.signerSubject.isEmpty()) {
                verdictItem->setToolTip(verdictItem->toolTip() + "\n" +
                                        tr("签名者：%1").arg(verdict.signerSubject));
            }
        } else {
            verdictItem->setText(verdictError.isEmpty() ? tr("无法判定") : verdictError);
            verdictItem->setForeground(QColor(140, 140, 140));
        }
        table->setItem(row, 5, verdictItem);
    }
    layout->addWidget(table);

    layout->addWidget(new QLabel(tr("证书链"), &dialog));
    auto* chainTable = new QTableWidget(0, 8, &dialog);
    chainTable->setObjectName(QStringLiteral("signature.chain"));
    chainTable->setHorizontalHeaderLabels({tr("主体"), tr("签发者"), tr("序列号"), tr("SHA-1 指纹"),
                                           tr("生效"), tr("失效"), tr("状态"), tr("撤销状态")});
    chainTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    chainTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(chainTable);

    auto* detail = new QTextBrowser(&dialog);
    detail->setObjectName(QStringLiteral("signature.detail"));
    layout->addWidget(detail);

    // 选中某个签名 → 换证书链与详情。
    const auto showSignature = [&](int row) {
        if (row < 0 || row >= signatures.size()) {
            return;
        }
        const pdf::SignatureInfo& signature = signatures.at(row);
        // 证书链与撤销状态：走 **Windows 证书存储**（Crypt32 链策略），
        // 结论是「受信任 / 已吊销 / 状态未知 / 已过期」四个**分开**的结论。
        // 撤销信息查不到时报「状态未知」—— 不报「未被吊销」，
        // 那是把「没查出来」说成「查过了、没问题」。
        const QVector<QByteArray> chainDer = pdf::signatureCertificateDer(signature);
        pdf::CertificateChainReport chainReport;
        if (!chainDer.isEmpty()) {
            chainReport = pdf::inspectCertificateChain(chainDer.first(), chainDer.mid(1));
        }
        chainTable->setRowCount(0);
        for (const pdf::SignerCertificate& certificate : signature.chain) {
            const int chainRow = chainTable->rowCount();
            chainTable->insertRow(chainRow);
            chainTable->setItem(chainRow, 0, new QTableWidgetItem(certificate.subject));
            chainTable->setItem(chainRow, 1, new QTableWidgetItem(certificate.issuer));
            chainTable->setItem(chainRow, 2, new QTableWidgetItem(certificate.serialNumber));
            chainTable->setItem(chainRow, 3, new QTableWidgetItem(certificate.thumbprintSha1));
            chainTable->setItem(chainRow, 4, new QTableWidgetItem(certificate.notBefore));
            chainTable->setItem(chainRow, 5, new QTableWidgetItem(certificate.notAfter));
            QString state;
            if (certificate.trusted) {
                state = tr("受信任");
            } else if (certificate.selfSigned) {
                state = tr("自签（不受系统信任）");
            } else {
                state = tr("不受系统信任");
            }
            chainTable->setItem(chainRow, 6, new QTableWidgetItem(state));

            // 撤销状态：按序列号对上链报告里的那一张（两张表的顺序不一定一样）。
            // 对不上就说「状态未知」—— 不能因为没对上就报「未被吊销」。
            QString revocation = tr("状态未知");
            QColor revocationColor(140, 140, 140);
            for (const pdf::ChainCertificateInfo& info : chainReport.certificates) {
                if (info.serialNumber.isEmpty() || info.serialNumber != certificate.serialNumber) {
                    continue;
                }
                revocation = info.revocationStatus.isEmpty() ? tr("状态未知") : info.revocationStatus;
                if (info.revocationState == pdf::RevocationState::Revoked) {
                    revocationColor = QColor(198, 40, 40);
                } else if (info.revocationState == pdf::RevocationState::NotRevoked) {
                    revocationColor = QColor(46, 160, 67);
                }
                break;
            }
            auto* revocationItem = new QTableWidgetItem(revocation);
            revocationItem->setForeground(revocationColor);
            chainTable->setItem(chainRow, 7, revocationItem);
        }
        QStringList ranges;
        for (int i = 0; i + 1 < signature.byteRange.size(); i += 2) {
            ranges.append(tr("从第 %1 字节起共 %2 字节")
                              .arg(signature.byteRange.at(i))
                              .arg(signature.byteRange.at(i + 1)));
        }
        QStringList lines;
        lines.append(tr("签名域：%1").arg(signature.fieldName));
        lines.append(tr("签署人：%1").arg(signature.signerName));
        lines.append(tr("签署时间：%1").arg(signature.signingTime));
        lines.append(tr("签名理由：%1").arg(signature.reason));
        lines.append(tr("算法：%1").arg(signature.subFilter));
        lines.append(tr("覆盖检查：%1")
                         .arg(signature.coversDocument ? tr("覆盖完整") : signature.coverError));
        lines.append(tr("覆盖区间："));
        lines.append(ranges.isEmpty() ? tr("（没有读到覆盖区间）") : ranges.join('\n'));
        // 密码学判定的细节也放进详情面板：摘要算法、签名者、时间戳。
        if (row < verdicts.size()) {
            const pdf::SignatureCryptoVerdict& verdict = verdicts.at(row);
            lines.append(tr("密码学判定：%1")
                             .arg(verdict.signatureValid ? tr("签名有效")
                                                          : (verdict.failureReason.isEmpty()
                                                                 ? tr("签名无效")
                                                                 : verdict.failureReason)));
            lines.append(tr("摘要算法：%1")
                             .arg(verdict.algorithm.isEmpty() ? tr("（未知）") : verdict.algorithm));
            lines.append(tr("内容摘要一致：%1").arg(verdict.digestMatched ? tr("是") : tr("否")));
            lines.append(tr("签名者主体：%1")
                             .arg(verdict.signerSubject.isEmpty() ? tr("（未知）")
                                                                 : verdict.signerSubject));
            if (!verdict.timestamp.isEmpty()) {
                lines.append(tr("时间戳：%1").arg(verdict.timestamp));
                lines.append(tr("时间戳机构：%1")
                                 .arg(verdict.timestampAuthority.isEmpty()
                                          ? tr("（未知）")
                                          : verdict.timestampAuthority));
            }
        }
        // 证书链结论：受信任 / 已吊销 / 状态未知 / 已过期，四个结论**分开**给。
        // 链建不起来时给失败原因，不给一句笼统的「证书有问题」。
        if (!chainReport.error.isEmpty()) {
            lines.append(tr("证书链结论：%1").arg(chainReport.error));
        } else if (!chainReport.summary.isEmpty()) {
            lines.append(tr("证书链结论：%1").arg(chainReport.summary));
        }
        detail->setPlainText(lines.join('\n'));
    };
    connect(table, &QTableWidget::currentCellChanged, &dialog,
            [showSignature](int row, int, int, int) { showSignature(row); });
    if (!signatures.isEmpty()) {
        table->setCurrentCell(0, 0);
        showSignature(0);
    }

    auto* dialogButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(dialogButtons);
    connect(dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(dialogButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

    dialog.exec();
}


// -------------------------------------------------- 电子签章：签署流程 ----
//
// 面板只做三件事：收集输入、把领域层的流程状态显示出来、把用户的动作转给领域层。
// 流程状态与签署人状态**一律**由 pdf::SignatureWorkflow 管，界面不另算一套 ——
// 两处各算一套迟早会打架，而签署记录是拿来追责的，不能有第二种说法。
void PdfView::showSignatureWorkflowDialog() {
    pdf::SignatureWorkflow& workflow = signatureWorkflow_;

    QDialog dialog(this);
    // 名字与 Ribbon 上的按钮（signature.workflow）错开一位：
    // UI Automation 的 AutomationId 是全应用唯一的，同名会让验收脚本认错控件。
    dialog.setObjectName(QStringLiteral("signature.workflowDialog"));
    dialog.setWindowTitle(tr("签署流程"));
    dialog.resize(880, 660);
    auto* layout = new QVBoxLayout(&dialog);

    // 状态与签署人状态的中文标签。
    const auto stateName = [](pdf::WorkflowState state) -> QString {
        switch (state) {
            case pdf::WorkflowState::Draft: return tr("草稿");
            case pdf::WorkflowState::InProgress: return tr("进行中");
            case pdf::WorkflowState::Completed: return tr("已完成");
            case pdf::WorkflowState::Aborted: return tr("已中止");
        }
        return tr("草稿");
    };
    const auto stepName = [](pdf::SignStepStatus status) -> QString {
        switch (status) {
            case pdf::SignStepStatus::Pending: return tr("待签署");
            case pdf::SignStepStatus::Signed: return tr("已签署");
            case pdf::SignStepStatus::Rejected: return tr("已拒签");
            case pdf::SignStepStatus::Skipped: return tr("已跳过");
        }
        return tr("待签署");
    };
    // 事件动作码 → 人看得懂的一句话。
    // 这里的措辞与按钮上的刻意错开（按钮是「开始签署」，事件是「开始签署流程」）：
    // 同一个上下文里同一条文案只能有一个译文，两处共用一个词就会有一处译不准。
    const auto actionName = [](const QString& action) -> QString {
        if (action == QLatin1String("created")) return tr("创建流程");
        if (action == QLatin1String("signatory_added")) return tr("新增签署人");
        if (action == QLatin1String("started")) return tr("开始签署流程");
        if (action == QLatin1String("signed")) return tr("签署完成");
        if (action == QLatin1String("rejected")) return tr("拒签");
        if (action == QLatin1String("skipped")) return tr("跳过签署");
        if (action == QLatin1String("completed")) return tr("完成流程");
        if (action == QLatin1String("aborted")) return tr("中止流程");
        return action;
    };

    auto* form = new QFormLayout();
    auto* titleEdit = new QLineEdit(&dialog);
    titleEdit->setObjectName(QStringLiteral("signature.workflow.title"));
    auto* initiatorEdit = new QLineEdit(&dialog);
    initiatorEdit->setObjectName(QStringLiteral("signature.workflow.initiator"));
    auto* stateLabel = new QLabel(&dialog);
    stateLabel->setObjectName(QStringLiteral("signature.workflow.state"));
    form->addRow(tr("标题"), titleEdit);
    form->addRow(tr("发起人"), initiatorEdit);
    form->addRow(tr("状态"), stateLabel);
    layout->addLayout(form);

    // 签署人名单。
    // 标题用「签署人名单」而不是「签署人」：签名列表那一栏早就有「签署人」，
    // 同一个上下文里同一条文案只能有一个译文，别把人家那一栏的译名改掉。
    layout->addWidget(new QLabel(tr("签署人名单"), &dialog));
    auto* signatoryTable = new QTableWidget(0, 4, &dialog);
    signatoryTable->setObjectName(QStringLiteral("signature.workflow.signatories"));
    signatoryTable->setHorizontalHeaderLabels({tr("姓名"), tr("联系方式"), tr("顺序"), tr("状态")});
    signatoryTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    signatoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    signatoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(signatoryTable);
    auto* signatoryEmpty = new QLabel(tr("（还没有签署人）"), &dialog);
    signatoryEmpty->setObjectName(QStringLiteral("signature.workflow.signatories.empty"));
    layout->addWidget(signatoryEmpty);

    // 添加 / 删除签署人。
    auto* addRow = new QHBoxLayout();
    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setObjectName(QStringLiteral("signature.workflow.newName"));
    nameEdit->setPlaceholderText(tr("姓名"));
    auto* contactEdit = new QLineEdit(&dialog);
    contactEdit->setObjectName(QStringLiteral("signature.workflow.newContact"));
    contactEdit->setPlaceholderText(tr("联系方式"));
    auto* orderSpin = new QSpinBox(&dialog);
    orderSpin->setObjectName(QStringLiteral("signature.workflow.newOrder"));
    orderSpin->setRange(0, 999);
    orderSpin->setSpecialValueText(tr("自动"));
    addRow->addWidget(nameEdit);
    addRow->addWidget(contactEdit);
    addRow->addWidget(orderSpin);
    auto* addButton = new QPushButton(tr("添加签署人"), &dialog);
    addButton->setObjectName(QStringLiteral("signature.workflow.addSignatory"));
    addRow->addWidget(addButton);
    auto* removeButton = new QPushButton(tr("删除签署人"), &dialog);
    removeButton->setObjectName(QStringLiteral("signature.workflow.removeSignatory"));
    addRow->addWidget(removeButton);
    layout->addLayout(addRow);

    // 流程动作。
    auto* actionRow = new QHBoxLayout();
    const auto makeAction = [&](const QString& label, const QString& objectName) {
        auto* button = new QPushButton(label, &dialog);
        button->setObjectName(objectName);
        actionRow->addWidget(button);
        return button;
    };
    auto* startButton = makeAction(tr("开始签署"), QStringLiteral("signature.workflow.start"));
    auto* signedButton = makeAction(tr("标记已签"), QStringLiteral("signature.workflow.signed"));
    auto* rejectButton = makeAction(tr("标记拒签"), QStringLiteral("signature.workflow.rejected"));
    auto* skipButton = makeAction(tr("跳过"), QStringLiteral("signature.workflow.skip"));
    auto* completeButton = makeAction(tr("完成"), QStringLiteral("signature.workflow.complete"));
    auto* abortButton = makeAction(tr("中止"), QStringLiteral("signature.workflow.abort"));
    layout->addLayout(actionRow);

    // 签署记录。
    layout->addWidget(new QLabel(tr("签署记录"), &dialog));
    auto* eventTable = new QTableWidget(0, 4, &dialog);
    eventTable->setObjectName(QStringLiteral("signature.workflow.events"));
    eventTable->setHorizontalHeaderLabels({tr("时间"), tr("谁"), tr("做了什么"), tr("说明")});
    eventTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    eventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(eventTable);
    auto* eventEmpty = new QLabel(tr("（还没有签署记录）"), &dialog);
    eventEmpty->setObjectName(QStringLiteral("signature.workflow.events.empty"));
    layout->addWidget(eventEmpty);

    // 报告。
    auto* reportRow = new QHBoxLayout();
    auto* reportButton = new QPushButton(tr("查看签署报告"), &dialog);
    reportButton->setObjectName(QStringLiteral("signature.workflow.report"));
    reportRow->addWidget(reportButton);
    layout->addLayout(reportRow);

    // 刷新：所有显示都从领域层的数据里来。
    const auto refresh = [&]() {
        stateLabel->setText(stateName(workflow.state()));
        if (!workflow.id().isEmpty()) {
            titleEdit->setText(workflow.title());
            initiatorEdit->setText(workflow.initiator());
            titleEdit->setEnabled(false);
            initiatorEdit->setEnabled(false);
        }

        const QVector<pdf::Signatory> signatories = workflow.signatories();
        signatoryTable->setRowCount(signatories.size());
        for (int row = 0; row < signatories.size(); ++row) {
            const pdf::Signatory& signatory = signatories.at(row);
            signatoryTable->setItem(row, 0, new QTableWidgetItem(signatory.name));
            signatoryTable->setItem(row, 1, new QTableWidgetItem(signatory.contact));
            signatoryTable->setItem(row, 2, new QTableWidgetItem(QString::number(signatory.order)));
            auto* statusItem = new QTableWidgetItem(stepName(signatory.status));
            if (signatory.status == pdf::SignStepStatus::Signed) {
                statusItem->setForeground(QColor(46, 160, 67));
            } else if (signatory.status == pdf::SignStepStatus::Rejected) {
                statusItem->setForeground(QColor(198, 40, 40));
            }
            if (!signatory.rejectReason.isEmpty()) {
                statusItem->setToolTip(signatory.rejectReason);
            }
            signatoryTable->setItem(row, 3, statusItem);
        }
        signatoryEmpty->setVisible(signatories.isEmpty());

        const QVector<pdf::WorkflowEvent> events = workflow.events();
        eventTable->setRowCount(events.size());
        for (int row = 0; row < events.size(); ++row) {
            const pdf::WorkflowEvent& event = events.at(row);
            eventTable->setItem(row, 0, new QTableWidgetItem(event.at.toString(Qt::ISODate)));
            eventTable->setItem(row, 1, new QTableWidgetItem(event.actor));
            eventTable->setItem(row, 2, new QTableWidgetItem(actionName(event.action)));
            eventTable->setItem(row, 3, new QTableWidgetItem(event.detail));
        }
        eventEmpty->setVisible(events.isEmpty());

        // 终态之后不能再改：按钮置灰，避免用户点出「流程已结束，不能再修改」。
        const bool finished = workflow.state() == pdf::WorkflowState::Completed ||
                              workflow.state() == pdf::WorkflowState::Aborted;
        startButton->setEnabled(!finished &&
                                 workflow.state() == pdf::WorkflowState::Draft &&
                                 !workflow.id().isEmpty() && !signatories.isEmpty());
        signedButton->setEnabled(!finished);
        rejectButton->setEnabled(!finished);
        skipButton->setEnabled(!finished);
        completeButton->setEnabled(!finished);
        abortButton->setEnabled(!finished);
        reportButton->setEnabled(!workflow.id().isEmpty());
    };

    // 流程还没创建时，先把标题与发起人落成一条流程。
    const auto ensureCreated = [&]() -> bool {
        if (!workflow.id().isEmpty()) {
            return true;
        }
        QString error;
        const QString title = titleEdit->text().trimmed();
        const QString initiator = initiatorEdit->text().trimmed();
        if (workflow.createWorkflow(title, initiator, &error).isEmpty()) {
            QMessageBox::warning(&dialog, tr("签署流程"),
                                 error.isEmpty() ? tr("创建签署流程失败。") : error);
            return false;
        }
        return true;
    };
    const auto showError = [&](const QString& error) {
        QMessageBox::warning(&dialog, tr("签署流程"),
                             error.isEmpty() ? tr("操作没有成功。") : error);
    };

    connect(addButton, &QPushButton::clicked, &dialog, [&]() {
        if (!ensureCreated()) {
            return;
        }
        pdf::Signatory signatory;
        signatory.name = nameEdit->text();
        signatory.contact = contactEdit->text();
        signatory.order = orderSpin->value();
        QString error;
        workflow.addSignatory(signatory, &error);
        if (!error.isEmpty()) {
            showError(error);
            return;
        }
        nameEdit->clear();
        contactEdit->clear();
        orderSpin->setValue(0);
        refresh();
    });

    connect(removeButton, &QPushButton::clicked, &dialog, [&]() {
        const int row = signatoryTable->currentRow();
        if (row < 0 || row >= workflow.signatories().size()) {
            QMessageBox::information(&dialog, tr("签署流程"), tr("先选中要删除的签署人。"));
            return;
        }
        const QString name = workflow.signatories().at(row).name;
        // 名单只能靠领域层改：这里没有「删除签署人」的接口，
        // 所以如实说明，而不是偷偷改内部状态。
        QMessageBox::information(&dialog, tr("签署流程"),
                                 tr("签署人 %1 不能删除：签署记录要保留完整名单。").arg(name));
    });

    connect(startButton, &QPushButton::clicked, &dialog, [&]() {
        if (!ensureCreated()) {
            return;
        }
        QString error;
        if (!workflow.startSigning(&error)) {
            showError(error);
            return;
        }
        refresh();
    });

    connect(signedButton, &QPushButton::clicked, &dialog, [&]() {
        const QString name = workflow.nextPendingSignatory();
        if (name.isEmpty()) {
            QMessageBox::information(&dialog, tr("签署流程"), tr("没有待签署的人。"));
            return;
        }
        QString error;
        if (!workflow.recordSigned(name, QDateTime::currentDateTime(), &error)) {
            showError(error);
            return;
        }
        refresh();
    });

    connect(rejectButton, &QPushButton::clicked, &dialog, [&]() {
        const QString name = workflow.nextPendingSignatory();
        if (name.isEmpty()) {
            QMessageBox::information(&dialog, tr("签署流程"), tr("没有待签署的人。"));
            return;
        }
        bool ok = false;
        const QString reason = QInputDialog::getText(&dialog, tr("标记拒签"),
                                                     tr("拒签理由（必填）"), QLineEdit::Normal,
                                                     QString(), &ok);
        if (!ok) {
            return;
        }
        if (reason.trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, tr("标记拒签"), tr("拒签必须写理由。"));
            return;
        }
        QString error;
        if (!workflow.recordRejected(name, reason.trimmed(), QDateTime::currentDateTime(), &error)) {
            showError(error);
            return;
        }
        refresh();
    });

    connect(skipButton, &QPushButton::clicked, &dialog, [&]() {
        const QString name = workflow.nextPendingSignatory();
        if (name.isEmpty()) {
            QMessageBox::information(&dialog, tr("签署流程"), tr("没有待签署的人。"));
            return;
        }
        QString error;
        if (!workflow.skipSignatory(name, &error)) {
            showError(error);
            return;
        }
        refresh();
    });

    connect(completeButton, &QPushButton::clicked, &dialog, [&]() {
        QString error;
        if (!workflow.completeWorkflow(&error)) {
            showError(error);
            return;
        }
        refresh();
    });

    connect(abortButton, &QPushButton::clicked, &dialog, [&]() {
        bool ok = false;
        const QString reason = QInputDialog::getText(&dialog, tr("中止流程"),
                                                     tr("中止理由（必填）"), QLineEdit::Normal,
                                                     QString(), &ok);
        if (!ok) {
            return;
        }
        if (reason.trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, tr("中止流程"), tr("中止必须写理由。"));
            return;
        }
        QString error;
        if (!workflow.abortWorkflow(reason.trimmed(), &error)) {
            showError(error);
            return;
        }
        refresh();
    });

    connect(reportButton, &QPushButton::clicked, &dialog, [&]() {
        const pdf::SignatureWorkflow::Report report = workflow.buildReport();
        const QString text = report.toPlainText();
        QDialog reportDialog(&dialog);
        reportDialog.setObjectName(QStringLiteral("signature.workflow.reportDialog"));
        reportDialog.setWindowTitle(tr("签署报告"));
        reportDialog.resize(680, 560);
        auto* reportLayout = new QVBoxLayout(&reportDialog);
        auto* editor = new QPlainTextEdit(&reportDialog);
        editor->setObjectName(QStringLiteral("signature.workflow.reportText"));
        editor->setReadOnly(true);
        editor->setPlainText(text);
        reportLayout->addWidget(editor);
        auto* reportButtons = new QDialogButtonBox(&reportDialog);
        auto* copyButton = reportButtons->addButton(tr("复制到剪贴板"),
                                                    QDialogButtonBox::ActionRole);
        copyButton->setObjectName(QStringLiteral("signature.workflow.reportCopy"));
        auto* exportButton = reportButtons->addButton(tr("导出为文本文件"),
                                                      QDialogButtonBox::ActionRole);
        exportButton->setObjectName(QStringLiteral("signature.workflow.reportExport"));
        reportButtons->addButton(QDialogButtonBox::Close);
        reportLayout->addWidget(reportButtons);
        connect(copyButton, &QPushButton::clicked, &reportDialog, [text]() {
            QApplication::clipboard()->setText(text);
        });
        connect(exportButton, &QPushButton::clicked, &reportDialog, [text, &reportDialog]() {
            QWidget* owner = reportDialog.parentWidget();
            const QString path = QFileDialog::getSaveFileName(
                owner, tr("导出签署报告"), QString(), tr("文本文件 (*.txt)"));
            if (path.isEmpty()) {
                return;
            }
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QMessageBox::warning(owner, tr("导出签署报告"), tr("写不了这个文件。"));
                return;
            }
            file.write(text.toUtf8());
            file.close();
        });
        connect(reportButtons, &QDialogButtonBox::rejected, &reportDialog, &QDialog::reject);
        reportDialog.exec();
    });

    refresh();
    dialog.exec();
}


// -------------------------------------------------- 区域阅读顺序（标签树）----
//
// 写标签树要三样东西：每页的行（user space 矩形）、行的先后顺序、
// 以及把顺序写进 /StructTreeRoot 的能力（pdf::TagTreeWriter）。
//
// 诚实性边界：写进去的是**页粒度以下**的行级顺序（每行一个 Span），
// 不做完整的 PDF/A 标签合规（无 Alt、无 /Lang 全覆盖、无图件替代文本），
// 所以读屏软件能按我们给的顺序念，但拿不到「这是一张什么图」这类补充信息。

// 把整篇文档的行整理成「每页一个区域列表」。
//
// 当前页用用户排好的顺序，其余页用默认顺序（从上到下）。
QVector<QVector<pdf::TaggedRegion>> PdfView::buildTaggedRegions(
    int pageIndex, const QVector<int>& currentPageOrder) const {
    QVector<QVector<pdf::TaggedRegion>> regions(document_.pageCount());
    for (int index = 0; index < document_.pageCount(); ++index) {
        auto page = document_.page(index);
        if (!page.has_value()) {
            continue;
        }
        const std::optional<pdf::TextPage> text = page->extractText();
        if (!text.has_value() || text->chars.isEmpty()) {
            continue;
        }
        const QVector<UserSpaceLine> lines = userSpaceTextLines(*text);
        QVector<pdf::TaggedRegion> pageRegions;
        for (int line = 0; line < lines.size(); ++line) {
            int order = line;
            if (index == pageIndex && line < currentPageOrder.size()) {
                order = currentPageOrder.at(line);
            }
            pageRegions.append(pdf::TaggedRegion{lines.at(line).box, order});
        }
        regions[index] = pageRegions;
    }
    return regions;
}

// 把标签树写回文档。失败时给用户看原因，文档保持原样。
bool PdfView::writeTaggedRegions(const QVector<QVector<pdf::TaggedRegion>>& regions, QString* error) {
    QString serializeError;
    const QByteArray input = document_.serialize(&serializeError);
    if (input.isEmpty()) {
        if (error) *error = serializeError;
        return false;
    }
    QString writeError;
    const QByteArray tagged = pdf::writeTaggedStructure(input, regions, &writeError);
    if (tagged.isEmpty()) {
        if (error) *error = writeError.isEmpty() ? tr("写入标签树失败，文档没有修改。") : writeError;
        return false;
    }
    QString reloadError;
    if (!document_.reloadFromBytes(tagged, &reloadError)) {
        if (error) *error = tr("写入后的文档无法重新打开，文档没有修改：%1").arg(reloadError);
        return false;
    }
    refreshAfterDecoration();
    setModified(true);
    return true;
}

// 看当前文档的标签树里已经写好的顺序。
void PdfView::showRegionReadingOrderDialog() {
    if (!document_.isValid()) {
        return;
    }
    QString serializeError;
    const QByteArray bytes = document_.serialize(&serializeError);
    if (bytes.isEmpty()) {
        QMessageBox::warning(this, tr("区域阅读顺序"), serializeError);
        return;
    }
    QString error;
    const QVector<QVector<int>> order = pdf::readTaggedOrder(bytes, &error);
    bool hasTree = false;
    for (const QVector<int>& page : order) {
        if (!page.isEmpty()) {
            hasTree = true;
            break;
        }
    }
    if (!hasTree) {
        QMessageBox::information(
            this, tr("区域阅读顺序"),
            error.isEmpty()
                ? tr("这份文档里还没有标签树。用「更改区域顺序」或「应用区域顺序」写一份。")
                : error);
        return;
    }
    QStringList lines;
    for (int page = 0; page < order.size(); ++page) {
        if (order.at(page).isEmpty()) {
            continue;
        }
        lines.append(tr("第 %1 页：%2 个区域，顺序 %3")
                         .arg(page + 1)
                         .arg(order.at(page).size())
                         .arg(order.at(page).size() > 1
                                  ? QString::number(order.at(page).first() + 1) + QStringLiteral(" 起")
                                  : QStringLiteral("1")));
    }
    QMessageBox::information(this, tr("区域阅读顺序"),
                             lines.join('\n') + tr("\n\n（显示的是标签树里每页的子元素顺序；"
                                                  "语义树与行级树都能读，取决于这份文档最后写的是哪一棵。）"));
}

// 调整当前页各行的先后顺序，然后写进标签树。
void PdfView::changeRegionReadingOrderDialog() {
    if (!document_.isValid()) {
        return;
    }
    const int pageIndex = currentPage();
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return;
    }
    const std::optional<pdf::TextPage> text = page->extractText();
    if (!text.has_value() || text->chars.isEmpty()) {
        QMessageBox::information(this, tr("更改区域顺序"), tr("这一页没有可标记的文字。"));
        return;
    }
    const QVector<UserSpaceLine> lines = userSpaceTextLines(*text);
    if (lines.isEmpty()) {
        QMessageBox::information(this, tr("更改区域顺序"), tr("这一页没有可标记的文字。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("更改区域顺序"));
    dialog.resize(640, 460);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        tr("下面是当前页按视觉顺序排出的行。调整先后后写进 PDF 的标签树，"
           "读屏软件会按这个顺序念。"),
        &dialog));

    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("regionOrder.lines"));
    for (const UserSpaceLine& line : lines) {
        list->addItem(line.text.trimmed());
    }
    layout->addWidget(list);

    auto* moveUp = new QPushButton(tr("上移"), &dialog);
    auto* moveDown = new QPushButton(tr("下移"), &dialog);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(moveUp);
    buttons->addWidget(moveDown);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(moveUp, &QPushButton::clicked, &dialog, [list, moveUp] {
        const int row = list->currentRow();
        if (row <= 0) {
            return;
        }
        QListWidgetItem* item = list->takeItem(row);
        list->insertItem(row - 1, item);
        list->setCurrentRow(row - 1);
        moveUp->setEnabled(row > 0);
    });
    connect(moveDown, &QPushButton::clicked, &dialog, [list, moveDown] {
        const int row = list->currentRow();
        if (row < 0 || row >= list->count() - 1) {
            return;
        }
        QListWidgetItem* item = list->takeItem(row);
        list->insertItem(row + 1, item);
        list->setCurrentRow(row + 1);
        moveDown->setEnabled(row < list->count() - 1);
    });

    auto* dialogButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    dialogButtons->button(QDialogButtonBox::Ok)->setText(tr("写入"));
    layout->addWidget(dialogButtons);
    connect(dialogButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // 列表里的次序就是新的阅读顺序。
    QVector<int> order;
    for (int row = 0; row < list->count(); ++row) {
        order.append(row);
    }
    QString error;
    if (!writeTaggedRegions(buildTaggedRegions(pageIndex, order), &error)) {
        QMessageBox::warning(this, tr("更改区域顺序"),
                             error.isEmpty() ? tr("写入失败，文档没有修改。") : error);
        return;
    }
    emit statusMessage(tr("已把第 %1 页的阅读顺序写进标签树").arg(pageIndex + 1));
}

// 整篇按「从上到下」的默认顺序写标签树，不做交互。
void PdfView::applyRegionReadingOrder() {
    if (!document_.isValid()) {
        return;
    }
    if (QMessageBox::question(
            this, tr("应用区域顺序"),
            tr("会按「从上到下、从左到右」的顺序给整篇文档写一份标签树。"
               "页面外观不变，但读屏软件会按这个顺序念。继续吗？"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QVector<QVector<pdf::TaggedRegion>> regions = buildTaggedRegions(currentPage(), {});
    QApplication::restoreOverrideCursor();
    QString error;
    if (!writeTaggedRegions(regions, &error)) {
        QMessageBox::warning(this, tr("应用区域顺序"),
                             error.isEmpty() ? tr("写入失败，文档没有修改。") : error);
        return;
    }
    emit statusMessage(tr("已按默认顺序写好标签树"));
}

// ------------------------------------------------- 第二批接线（E/F/G/H）----
//
// 这一组是给新面板用的入口：面板只负责收集输入与显示结果，
// 真正碰文档的活都在这里；判定与边界一律交给领域层。

QByteArray PdfView::documentBytes(QString* error) const {
    if (!document_.isValid()) {
        if (error != nullptr) {
            *error = tr("先打开一份 PDF。");
        }
        return QByteArray();
    }
    QString reason;
    const QByteArray bytes = document_.serialize(&reason);
    if (bytes.isEmpty() && error != nullptr) {
        *error = reason.isEmpty() ? tr("读取文档内容失败。") : reason;
    }
    return bytes;
}

// 用新的字节替换当前文档的内容。
//
// 写回失败时文档保持原样：reloadFromBytes 先解析再换手柄，解析不过就还是旧文档。
bool PdfView::replaceDocumentBytes(const QByteArray& bytes, QString* error) {
    if (!document_.isValid()) {
        if (error != nullptr) {
            *error = tr("先打开一份 PDF。");
        }
        return false;
    }
    if (bytes.isEmpty()) {
        if (error != nullptr) {
            *error = tr("新的内容是空的，文档没有修改。");
        }
        return false;
    }
    QString reloadError;
    if (!document_.reloadFromBytes(bytes, &reloadError)) {
        if (error != nullptr) {
            *error = tr("写入后的文档无法重新打开，文档没有修改：%1").arg(reloadError);
        }
        return false;
    }
    refreshAfterDecoration();
    setModified(true);
    return true;
}

// 某页的有效盒子（PDF user space；优先 CropBox，没有就用 MediaBox）。
//
// 写不可见文字层时字符框必须相对同一个盒子，否则字会整体偏移。
pdf::MediaBox PdfView::pageContentBox(int pageIndex) const {
    pdf::MediaBox box;
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return box;
    }
    return page->contentBox();
}

// 执行一步文档级动作（动作向导用）。
//
// 参数怎么解释、动作能不能做，领域层已经定好了（doc::ActionWizard 的参数表
// 与 validateActionWizard）。这里只把参数翻译成领域层的调用；
// 做不了的步骤要**如实**说做不到，不假装成功。
bool PdfView::applyDocumentAction(doc::WizardActionType type,
                                  const QVector<doc::WizardParam>& params,
                                  QString* reason) {
    if (!document_.isValid()) {
        if (reason != nullptr) {
            *reason = tr("先打开一份 PDF。");
        }
        return false;
    }
    const auto stringParam = [&params](const char* name) {
        for (const doc::WizardParam& param : params) {
            if (param.name == QString::fromLatin1(name)) {
                return param.stringValue;
            }
        }
        return QString();
    };
    const auto numberParam = [&params](const char* name, double fallback) {
        for (const doc::WizardParam& param : params) {
            if (param.name == QString::fromLatin1(name)) {
                return param.numberValue;
            }
        }
        return fallback;
    };
    const auto integerParam = [&params](const char* name, qint64 fallback) {
        for (const doc::WizardParam& param : params) {
            if (param.name == QString::fromLatin1(name)) {
                return param.integerValue;
            }
        }
        return fallback;
    };

    switch (type) {
        case doc::WizardActionType::AddWatermark: {
            const QString text = stringParam("text");
            if (text.isEmpty()) {
                if (reason != nullptr) {
                    *reason = tr("水印这一步没有文字。");
                }
                return false;
            }
            const QString position = stringParam("position");
            if (!position.isEmpty() && position != QStringLiteral("center")) {
                if (reason != nullptr) {
                    *reason = tr("本程序的水印只能放在页面正中（position 只支持 center）。");
                }
                return false;
            }
            pdf::WatermarkOptions options;
            options.text = text;
            options.opacity = numberParam("opacity", 0.15);
            if (!pdf::applyWatermark(document_, options)) {
                if (reason != nullptr) {
                    *reason = tr("加水印失败。");
                }
                return false;
            }
            refreshAfterDecoration();
            setModified(true);
            return true;
        }
        case doc::WizardActionType::AddPageNumbers: {
            const QString position = stringParam("position");
            if (!position.isEmpty() && position != QStringLiteral("bottomRight")) {
                if (reason != nullptr) {
                    *reason = tr("本程序的页码只能放在右下角（position 只支持 bottomRight）。");
                }
                return false;
            }
            const qint64 start = integerParam("startNumber", 1);
            if (start != 1) {
                if (reason != nullptr) {
                    *reason = tr("本程序的页码从第 1 页开始（startNumber 只支持 1）。");
                }
                return false;
            }
            pdf::PageNumberOptions options;
            if (!pdf::applyPageNumbers(document_, options)) {
                if (reason != nullptr) {
                    *reason = tr("加页码失败。");
                }
                return false;
            }
            refreshAfterDecoration();
            setModified(true);
            return true;
        }
        case doc::WizardActionType::AddBookmark: {
            const QString title = stringParam("title");
            if (title.isEmpty()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "加书签这一步没有标题。");
                }
                return false;
            }
            // pageIndex 是 1 基的页码（与界面层的参数表一致）。越界要报错：
            // 指向文档外的书签点开就是坏的（OutlineItem 越界的语义是「目标损坏」）。
            const qint64 pageNumber = integerParam("pageIndex", 1);
            int targetPage = -1;
            if (!resolveWizardBookmarkPage(pageNumber, document_.pageCount(), &targetPage, reason)) {
                return false;
            }
            // 新书签追加到**顶层**：顶层书签才是「章」的粒度，插到某个子节点底下
            // 就变成了那一章里的小节。
            std::vector<pdf::OutlineItem> items = outline();
            pdf::OutlineItem item;
            item.title = title;
            item.pageIndex = targetPage;
            items.push_back(item);
            QString outlineError;
            if (!replaceOutline(items, &outlineError)) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "加书签失败：%1").arg(outlineError);
                }
                return false;
            }
            return true;
        }
        case doc::WizardActionType::SplitDocument: {
            const QString directory = stringParam("outputDirectory");
            if (directory.isEmpty()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "拆分文档这一步没有输出目录。");
                }
                return false;
            }
            const QFileInfo directoryInfo(directory);
            if (!directoryInfo.exists() || !directoryInfo.isDir()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "输出目录不存在：%1").arg(directory);
                }
                return false;
            }
            // 拆分口径：range（按 ranges 给的页区间）、count 或 count:N（每 N 页一份）、
            // outline（按顶层书签）。空字符串按默认的 range 处理。
            WizardSplitMode splitMode = WizardSplitMode::Range;
            int pagesPerFile = 1;
            if (!parseWizardSplitMode(stringParam("mode"), &splitMode, &pagesPerFile, reason)) {
                return false;
            }
            const int pageCount = document_.pageCount();
            // 切法交给 pdf 层算：界面只负责选参数、拼文件名、写文件。
            QVector<pdf::PageRange> ranges;
            if (splitMode == WizardSplitMode::Count) {
                ranges = pdf::PageSplitPlanner::byCount(pageCount, pagesPerFile);
            } else if (splitMode == WizardSplitMode::Outline) {
                ranges = pdf::PageSplitPlanner::byOutline(pageCount, outline());
                if (ranges.size() < 2) {
                    if (reason != nullptr) {
                        *reason = i18n::tr("aipdf::app::PdfView",
                                          "按书签拆分至少要两个有效书签（指向同一页的算一个），"
                                          "这份文档只拆得出 %1 段。")
                                     .arg(ranges.size());
                    }
                    return false;
                }
            } else {
                // range 模式：ranges 里每一项是「1-3」或者「1」（1 基、含两端）。
                QStringList rangeItems;
                for (const doc::WizardParam& param : params) {
                    if (param.name == QStringLiteral("ranges")) {
                        rangeItems = param.listValue;
                        break;
                    }
                }
                if (rangeItems.isEmpty()) {
                    if (reason != nullptr) {
                        *reason = i18n::tr("aipdf::app::PdfView",
                                          "range 模式要至少给一个页区间（比如 1-3）。");
                    }
                    return false;
                }
                for (const QString& item : rangeItems) {
                    int firstPage = 0;
                    int lastPage = 0;
                    if (!parseWizardSplitRangeItem(item, &firstPage, &lastPage, reason)) {
                        return false;
                    }
                    // extract 会把越界的页码夹到文档范围内；这里先自己判越界并报错，
                    // 免得用户以为拆出了他要的那几页（夹完其实是另一段）。
                    if (lastPage > pageCount) {
                        if (reason != nullptr) {
                            *reason = i18n::tr("aipdf::app::PdfView",
                                              "页区间 %1 超出了这份文档的页数（这份文档一共 %2 页）。")
                                         .arg(item.trimmed())
                                         .arg(pageCount);
                        }
                        return false;
                    }
                    ranges += pdf::PageSplitPlanner::extract(pageCount, firstPage, lastPage);
                }
                // 同样的页区间写两遍会落到同一个文件名上（后写的盖掉先写的），
                // 重复的直接跳过。
                QVector<pdf::PageRange> uniqueRanges;
                for (const pdf::PageRange& range : ranges) {
                    bool seen = false;
                    for (const pdf::PageRange& known : uniqueRanges) {
                        if (known.first == range.first && known.last == range.last) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen) {
                        uniqueRanges.append(range);
                    }
                }
                ranges = uniqueRanges;
            }
            if (ranges.isEmpty()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "这一步没有拆出任何文件。");
                }
                return false;
            }
            const QString base = QFileInfo(filePath_).completeBaseName();
            int written = 0;
            QStringList failed;
            for (const pdf::PageRange& range : ranges) {
                const QString name = wizardSplitPartFileName(base, range);
                const QString path = QDir(directory).filePath(name);
                QString writeError;
                if (writePageRange(range.first, range.last, path, &writeError)) {
                    ++written;
                } else {
                    failed.append(QStringLiteral("%1（%2）").arg(name, writeError));
                    trace(QStringLiteral("wizard split failed file=%1 error=%2").arg(name, writeError));
                }
            }
            if (!failed.isEmpty()) {
                // 哪几份失败要写清楚：只说「失败」的话，用户没法知道该重做哪几份。
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView",
                                      "拆分文档：写成功 %1 份，失败 %2 份。失败的是：%3")
                                 .arg(written)
                                 .arg(failed.size())
                                 .arg(failed.join(QStringLiteral("、")));
                }
                return false;
            }
            emit statusMessage(i18n::tr("aipdf::app::PdfView", "已拆分出 %1 个文件").arg(written));
            return true;
        }
        case doc::WizardActionType::End:
            return true;
        case doc::WizardActionType::MergeDocuments: {
            // 合并：把当前文档与指定的几份文件合并成一个新文件写到 outputPath。
            //
            // 为什么不在这里做「合并结果直接替换当前文档」：向导的语义是
            // 「按步骤编排文档级操作」，合并的产物是**另一个文件**
            //（outputPath 是必填参数），不是把当前文档改掉。
            // 当前文档作为第一份参与合并，与合并对话框的既有口径一致
            //（当前文档排在第一位）。
            doc::WizardActionData data;
            QString parseError;
            if (!doc::parseWizardAction(type, params, &data, &parseError)) {
                if (reason != nullptr) {
                    *reason = parseError;
                }
                return false;
            }
            const QString outputPath = data.merge.outputPath;
            const QStringList inputPaths = data.merge.inputPaths;
            if (inputPaths.isEmpty()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "合并这一步没有要合并的文件。");
                }
                return false;
            }
            QString serializeError;
            const QByteArray currentBytes = document_.serialize(&serializeError);
            if (currentBytes.isEmpty()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "读不出当前文档：%1")
                                  .arg(serializeError);
                }
                return false;
            }
            QVector<pdf::MergeInput> inputs;
            inputs.append(pdf::MergeInput{currentBytes, QFileInfo(filePath_).fileName()});
            for (const QString& path : inputPaths) {
                const auto opened = pdf::openDocument(path);
                if (!opened.document.isValid()) {
                    if (reason != nullptr) {
                        *reason = i18n::tr("aipdf::app::PdfView", "打不开 %1：%2")
                                      .arg(QFileInfo(path).fileName(), opened.errorMessage);
                    }
                    return false;
                }
                QString readError;
                const QByteArray bytes = opened.document.serialize(&readError);
                if (bytes.isEmpty()) {
                    if (reason != nullptr) {
                        *reason = i18n::tr("aipdf::app::PdfView", "读不出 %1：%2")
                                      .arg(QFileInfo(path).fileName(), readError);
                    }
                    return false;
                }
                inputs.append(pdf::MergeInput{bytes, QFileInfo(path).fileName()});
            }
            pdf::MergeOptions options;
            pdf::MergeOutcome outcome;
            QString mergeError;
            if (!pdf::mergeDocuments(inputs, options, &outcome, &mergeError)) {
                if (reason != nullptr) {
                    *reason = mergeError.isEmpty()
                                  ? i18n::tr("aipdf::app::PdfView", "合并失败。")
                                  : mergeError;
                }
                return false;
            }
            // 合并结果必须能被重新打开才算数：不交一份打不开的文件。
            const auto reopened = pdf::openDocumentFromBytes(outcome.bytes, outputPath);
            if (!reopened.document.isValid()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "合并结果校验失败：%1")
                                  .arg(reopened.errorMessage);
                }
                return false;
            }
            QFile file(outputPath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "写入 %1 失败。").arg(outputPath);
                }
                return false;
            }
            file.write(outcome.bytes);
            file.close();
            return true;
        }
        case doc::WizardActionType::Encrypt: {
            // 加密：按向导给的密码与权限位，把当前文档重写成加密 PDF。
            //
            // 加密依赖 qpdf；没有 qpdf 的构建要如实说，不能假装加密了。
            if (!pdf::encryptionAvailable()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView",
                                       "本次构建没有启用 qpdf，写不了加密文档。");
                }
                return false;
            }
            doc::WizardActionData data;
            QString parseError;
            if (!doc::parseWizardAction(type, params, &data, &parseError)) {
                if (reason != nullptr) {
                    *reason = parseError;
                }
                return false;
            }
            const QString userPassword = data.encrypt.userPassword;
            const QString ownerPassword = data.encrypt.ownerPassword;
            pdf::PdfEncryptionOptions options;
            options.userPassword = userPassword;
            options.ownerPassword = ownerPassword;
            options.permissions.allowPrinting = data.encrypt.allowPrinting;
            QString serializeError;
            const QByteArray plain = document_.serialize(&serializeError);
            if (plain.isEmpty()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "读不出当前文档：%1")
                                  .arg(serializeError);
                }
                return false;
            }
            QString encryptError;
            const QByteArray encrypted = pdf::encryptPdf(plain, options, &encryptError);
            if (encrypted.isEmpty()) {
                if (reason != nullptr) {
                    *reason = encryptError.isEmpty()
                                  ? i18n::tr("aipdf::app::PdfView", "加密失败。")
                                  : encryptError;
                }
                return false;
            }
            // 加密后的字节要能重新打开（用用户密码）才算成功。
            // encryptPdf 内部已做过一次自检，这里再确认一次是防御性的。
            QString reopenError;
            if (!pdf::openDocumentFromBytes(encrypted, filePath_, userPassword).document.isValid()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "加密结果校验失败：%1")
                                  .arg(reopenError);
                }
                return false;
            }
            // 加密件替换当前文档：后续步骤（在它上面加水印之类）读到的是加密后的内容。
            if (!replaceDocumentBytes(encrypted, reason)) {
                return false;
            }
            // 记下加密设置，保存时仍按加密写。
            encryptionOptions_ = options;
            encryptionEnabled_ = true;
            return true;
        }
        case doc::WizardActionType::ConvertToFormat: {
            // 转格式：把当前文档导成另一种格式写到 outputPath。
            //
            // 支持 docx / xlsx / pptx / rtf / txt。
            // 这几个格式都是**整篇**导出（图片与表格带得过去）；
            // HTML / DXF / OFD 那几个只导文字与几何，不在这里承诺。
            doc::WizardActionData data;
            QString parseError;
            if (!doc::parseWizardAction(type, params, &data, &parseError)) {
                if (reason != nullptr) {
                    *reason = parseError;
                }
                return false;
            }
            const QString format = data.convert.format.trimmed().toLower();
            const QString outputPath = data.convert.outputPath;
            if (outputPath.isEmpty()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "转换这一步没有输出路径。");
                }
                return false;
            }
            QString serializeError;
            const QByteArray bytes = document_.serialize(&serializeError);
            if (bytes.isEmpty()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "读不出当前文档：%1")
                                  .arg(serializeError);
                }
                return false;
            }
            QByteArray exported;
            if (format == QLatin1String("docx")) {
                const doc::WordExportResult result = doc::exportPdfToWord(bytes);
                if (!result.ok) {
                    if (reason != nullptr) {
                        *reason = result.error.isEmpty()
                                      ? i18n::tr("aipdf::app::PdfView", "导出 Word 失败。")
                                      : result.error;
                    }
                    return false;
                }
                exported = result.bytes;
            } else if (format == QLatin1String("xlsx")) {
                const doc::ExcelExportResult result = doc::exportPdfToExcel(bytes);
                if (!result.ok) {
                    if (reason != nullptr) {
                        *reason = result.error.isEmpty()
                                      ? i18n::tr("aipdf::app::PdfView", "导出 Excel 失败。")
                                      : result.error;
                    }
                    return false;
                }
                exported = result.bytes;
            } else if (format == QLatin1String("pptx")) {
                const doc::PptxExportResult result = doc::exportPdfToPptx(bytes);
                if (!result.ok) {
                    if (reason != nullptr) {
                        *reason = result.error.isEmpty()
                                      ? i18n::tr("aipdf::app::PdfView", "导出 PPT 失败。")
                                      : result.error;
                    }
                    return false;
                }
                exported = result.bytes;
            } else if (format == QLatin1String("rtf") || format == QLatin1String("txt")) {
                const doc::ExportFormat exportFormat = (format == QLatin1String("rtf"))
                                                           ? doc::ExportFormat::Rtf
                                                           : doc::ExportFormat::PlainText;
                const QVector<pdf::TextPage> pages = documentTextPages();
                if (pages.isEmpty()) {
                    if (reason != nullptr) {
                        *reason = i18n::tr("aipdf::app::PdfView",
                                           "这份文档没有可导出的文字层。");
                    }
                    return false;
                }
                const QSizeF pageSize = pageSizePoints(0);
                const doc::ExportResult result =
                    doc::exportPagesTo(exportFormat, pages, pageSize);
                if (!result.ok) {
                    if (reason != nullptr) {
                        *reason = result.error.isEmpty()
                                      ? i18n::tr("aipdf::app::PdfView", "导出失败。")
                                      : result.error;
                    }
                    return false;
                }
                exported = result.bytes;
            } else {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView",
                                       "不支持的转换格式：%1。本程序支持 docx / xlsx / pptx / rtf / txt。")
                                  .arg(format);
                }
                return false;
            }
            if (exported.isEmpty()) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "导出结果是空的。");
                }
                return false;
            }
            QFile file(outputPath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView", "写入 %1 失败。").arg(outputPath);
                }
                return false;
            }
            file.write(exported);
            file.close();
            return true;
        }
        case doc::WizardActionType::Print: {
            // 打印：按向导给的份数与页范围打印到指定打印机。
            //
            // 打印机名留空就是默认打印机。页范围用「1-3,5」这种写法，
            // 1 基含两端；留空是整篇。
            doc::WizardActionData data;
            QString parseError;
            if (!doc::parseWizardAction(type, params, &data, &parseError)) {
                if (reason != nullptr) {
                    *reason = parseError;
                }
                return false;
            }
            const QString printerName = data.print.printerName;
            const int copies = data.print.copies;
            const QString pageRange = data.print.pageRange.trimmed();
            OutputRange range;
            if (!pageRange.isEmpty()) {
                QString rangeError;
                if (!parseWizardPageRange(pageRange, document_.pageCount(), &range, &rangeError)) {
                    if (reason != nullptr) {
                        *reason = rangeError;
                    }
                    return false;
                }
            }
            PrintOptions options;
            options.copies = copies;
            options.range = range;
            QPrinter printer(QPrinter::HighResolution);
            if (!printerName.isEmpty()) {
                printer.setPrinterName(printerName);
            }
            // 打印机名给了但系统里没有这台打印机，要当场说清楚：
            // 否则打印会静默落到默认打印机上，用户以为打到了指定的那台。
            if (!printerName.isEmpty() && printer.printerName() != printerName) {
                if (reason != nullptr) {
                    *reason = i18n::tr("aipdf::app::PdfView",
                                       "系统里没有这台打印机：%1。")
                                  .arg(printerName);
                }
                return false;
            }
            QString printError;
            if (!app::printDocument(document_, &printer, options, &printError)) {
                if (reason != nullptr) {
                    *reason = printError.isEmpty()
                                  ? i18n::tr("aipdf::app::PdfView", "打印失败。")
                                  : printError;
                }
                return false;
            }
            return true;
        }
        default:
            break;
    }
    if (reason != nullptr) {
        *reason = tr("动作向导还没有接这一步：%1。请用对应的功能入口单独做。")
                      .arg(doc::wizardActionTypeTitle(type));
    }
    return false;
}

QVector<pdf::TextPage> PdfView::documentTextPages() const {
    QVector<pdf::TextPage> pages;
    if (!document_.isValid()) {
        return pages;
    }
    for (int pageIndex = 0; pageIndex < document_.pageCount(); ++pageIndex) {
        auto page = document_.page(pageIndex);
        if (!page.has_value()) {
            continue;
        }
        const auto text = page->extractText();
        if (!text.has_value()) {
            continue;
        }
        pages.append(*text);
    }
    return pages;
}

QString PdfView::currentPagePlainText() const {
    if (!document_.isValid()) {
        return QString();
    }
    // 优先用文本控制器排好的阅读顺序（多栏文档里内容流顺序是乱的）；
    // 文本层还没抽出来时退回 PDFium 的原始顺序。
    if (textController_ != nullptr) {
        const TextLayout* layout = textController_->layout(currentPage());
        if (layout != nullptr && !layout->isEmpty()) {
            return layout->textForRange(0, layout->readingOrder().size());
        }
    }
    auto page = document_.page(currentPage());
    if (!page.has_value()) {
        return QString();
    }
    const auto text = page->extractText();
    if (!text.has_value()) {
        return QString();
    }
    return text->plainText();
}

QString PdfView::currentSelectionText() const {
    if (textController_ == nullptr) {
        return QString();
    }
    return textController_->selectedText();
}

QString PdfView::wholeDocumentPlainText() const {
    if (!document_.isValid()) {
        return QString();
    }
    QString all;
    for (int pageIndex = 0; pageIndex < document_.pageCount(); ++pageIndex) {
        auto page = document_.page(pageIndex);
        if (!page.has_value()) {
            continue;
        }
        const auto text = page->extractText();
        if (!text.has_value()) {
            continue;
        }
        const QString pageText = text->plainText();
        if (pageText.isEmpty()) {
            continue;
        }
        if (!all.isEmpty()) {
            all += QLatin1Char('\n');
        }
        all += pageText;
    }
    return all;
}

// 智能朗读：清洗 → 排段 → 按段喂给现有的语音引擎。
//
// 为什么分段念而不是一口气念：领域层把标题、列表项、引用分成了不同类型的段，
// 一次念完就把「这是标题」这类信息全丢了。
void PdfView::speakNarration() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("智能朗读"), tr("先打开一份 PDF。"));
        return;
    }
    if (!speech::SpeechEngine::isSupported()) {
        QMessageBox::information(this, tr("智能朗读"),
                                 tr("系统没有可用的语音：%1")
                                     .arg(speechEngine_->unavailableReason()));
        return;
    }
    const QString selection = currentSelectionText();
    const QString source = selection.isEmpty() ? currentPagePlainText() : selection;
    const ai::NarrationPlan plan = ai::planNarration(ai::cleanTextForNarration(source));
    if (!plan.ok) {
        QMessageBox::information(this, tr("智能朗读"), plan.error);
        return;
    }
    narrationSegments_ = plan.segments;
    narrationSegmentIndex_ = -1;
    emit statusMessage(tr("智能朗读：%1 段，预计 %2 秒")
                           .arg(plan.segments.size())
                           .arg(plan.estimatedSeconds));
    speakNextNarrationSegment();
}

void PdfView::speakNextNarrationSegment() {
    if (narrationSegments_.isEmpty() || narrationSegmentIndex_ < 0) {
        return;
    }
    ++narrationSegmentIndex_;
    if (narrationSegmentIndex_ >= narrationSegments_.size()) {
        narrationSegments_.clear();
        narrationSegmentIndex_ = -1;
        emit statusMessage(tr("智能朗读结束"));
        return;
    }
    const ai::NarrationSegment& segment = narrationSegments_.at(narrationSegmentIndex_);
    speechEngine_->speak(segment.text);
}

bool PdfView::printWithOptions(const PrintOptions& options, QString* error) {
    if (!document_.isValid()) {
        if (error != nullptr) {
            *error = tr("先打开一份 PDF。");
        }
        return false;
    }
    QPrinter printer(QPrinter::HighResolution);
    if (!app::printDocument(document_, &printer, options, error)) {
        return false;
    }
    return true;
}

bool PdfView::insertTextAnnotation(const QString& text, double fontSize) {
    if (!document_.isValid() || text.isEmpty() || annotationController_ == nullptr) {
        return false;
    }
    const int pageIndex = currentPage();
    const QRectF rect = defaultInsertRect(document_, pageIndex, 220.0, 40.0);
    if (rect.isEmpty()) {
        emit statusMessage(tr("这一页放不下这个文本框"));
        return false;
    }
    if (annotationController_->addFreeText(pageIndex, rect, text, fontSize) == 0) {
        emit statusMessage(tr("插入文本批注失败"));
        return false;
    }
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("已插入文本批注"));
    return true;
}

bool PdfView::insertGeneratedImage(const QByteArray& imageBytes, const QString& label) {
    if (!document_.isValid() || imageBytes.isEmpty() || annotationController_ == nullptr) {
        return false;
    }
    QImage image;
    if (!image.loadFromData(imageBytes)) {
        emit statusMessage(tr("生成结果不是一张可用的图片"));
        return false;
    }
    const int pageIndex = currentPage();
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return false;
    }
    const pdf::MediaBox box = page->contentBox();
    const double width = 160.0;
    const double height = width * image.height() / qMax(1, image.width());
    const QRectF rect(box.left + box.width() / 2.0 - width / 2.0,
                      box.bottom + box.height() / 2.0 - height / 2.0,
                      width, height);
    if (annotationController_->addImageStamp(pageIndex, rect, imageBytes, label) == 0) {
        emit statusMessage(tr("插入图片失败"));
        return false;
    }
    refreshAfterDecoration();
    setModified(true);
    emit statusMessage(tr("已把生成的图片插入当前页"));
    return true;
}

// ------------------------------------------------------- 连续批注模式 ----

void PdfView::setContinuousAnnotationMode(bool enabled) {
    continuousModeEnabled_ = enabled;
    syncContinuousModeWithTool();
    emit statusMessage(enabled ? tr("已开启连续批注：放完批注不切回选择工具")
                                : tr("已关闭连续批注"));
}

bool PdfView::continuousAnnotationMode() const {
    return continuousModeEnabled_;
}

void PdfView::activateNumberStampTool() {
    if (annotationController_ == nullptr) {
        return;
    }
    StampNumberDialog dialog(stampSequence_.options(), this);
    if (dialog.exec() != QDialog::Accepted) {
        // 取消：既不动工具，也不改编号设置。
        return;
    }
    // 换设置就重新从起始号排：接着旧编号排会让人看不懂。
    stampSequence_.setOptions(dialog.options());
    annotationController_->setTool(AnnotationController::Tool::NumberStamp);
    emit statusMessage(tr("编号图章：在页面上点一下就放一个，编号自动递增"));
}

bool PdfView::afterAnnotationPlacement() {
    const QString toolId =
        annotationController_ != nullptr ? continuousToolId(annotationController_->tool()) : QString();
    // 判据在领域层：先看逐工具的「保持工具选择」，再看全局的连续批注。
    const bool keep = pdf::keepsToolAfterPlacement(toolId, retainedTools_, continuousMode_);
    if (!keep) {
        // 放完就退出的工具（墨迹、橡皮等）在连续模式里自己退出。
        continuousMode_.afterPlacement();
        if (annotationController_ != nullptr) {
            annotationController_->setTool(AnnotationController::Tool::None);
        }
    }
    return keep;
}

void PdfView::finishAnnotationPlacement() {
    afterAnnotationPlacement();
}

void PdfView::syncContinuousModeWithTool() {
    if (annotationController_ == nullptr) {
        continuousMode_.deactivate();
        return;
    }
    const AnnotationController::Tool tool = annotationController_->tool();
    if (!continuousModeEnabled_ || tool == AnnotationController::Tool::None) {
        continuousMode_.deactivate();
        return;
    }
    continuousMode_.activate(continuousToolId(tool), toolSupportsContinuous(tool));
}

bool PdfView::toolSupportsContinuous(AnnotationController::Tool tool) {
    switch (tool) {
    // 连着放会越用越歪的工具：放完就退出连续模式。
    case AnnotationController::Tool::Ink:
    case AnnotationController::Tool::Eraser:
    case AnnotationController::Tool::Sound:
    case AnnotationController::Tool::Attachment:
    case AnnotationController::Tool::ImageStamp:
    case AnnotationController::Tool::Measure:
    case AnnotationController::Tool::MeasurePerimeter:
    case AnnotationController::Tool::MeasureArea:
        return false;
    default:
        return true;
    }
}

QString PdfView::continuousToolId(AnnotationController::Tool tool) {
    switch (tool) {
    case AnnotationController::Tool::Note: return QStringLiteral("note");
    case AnnotationController::Tool::Square: return QStringLiteral("square");
    case AnnotationController::Tool::Ellipse: return QStringLiteral("ellipse");
    case AnnotationController::Tool::Line: return QStringLiteral("line");
    case AnnotationController::Tool::Arrow: return QStringLiteral("arrow");
    case AnnotationController::Tool::Polygon: return QStringLiteral("polygon");
    case AnnotationController::Tool::Cloud: return QStringLiteral("cloud");
    case AnnotationController::Tool::Callout: return QStringLiteral("callout");
    case AnnotationController::Tool::RegionHighlight: return QStringLiteral("regionHighlight");
    case AnnotationController::Tool::FreeText: return QStringLiteral("freetext");
    case AnnotationController::Tool::Stamp: return QStringLiteral("stamp");
    case AnnotationController::Tool::NumberStamp: return QStringLiteral("numberstamp");
    case AnnotationController::Tool::ImageStamp: return QStringLiteral("imagestamp");
    case AnnotationController::Tool::Sound: return QStringLiteral("sound");
    case AnnotationController::Tool::Measure: return QStringLiteral("measure");
    case AnnotationController::Tool::MeasurePerimeter: return QStringLiteral("perimeter");
    case AnnotationController::Tool::MeasureArea: return QStringLiteral("area");
    case AnnotationController::Tool::Attachment: return QStringLiteral("attachment");
    case AnnotationController::Tool::Ink: return QStringLiteral("ink");
    case AnnotationController::Tool::Eraser: return QStringLiteral("eraser");
    case AnnotationController::Tool::None: break;
    }
    return QString();
}

void PdfView::setToolRetained(const QString& toolId, bool retained) {
    retainedTools_.setRetained(toolId, retained);
}

bool PdfView::toolRetained(const QString& toolId) const {
    return retainedTools_.isRetained(toolId);
}

QVector<QString> PdfView::retainedAnnotationTools() const {
    return retainedTools_.retainedTools();
}

void PdfView::clearRetainedAnnotationTools() {
    retainedTools_.clear();
}

QString PdfView::currentPageReadingOrder() const {
    const std::optional<pdf::PdfPage> page = document_.page(currentPage());
    const std::optional<pdf::TextPage> extracted = page ? page->extractText() : std::nullopt;
    // 阅读顺序优先用排版后的视觉顺序：PDFium 给的是内容流顺序，
    // 多栏、表格、图注混排时直接显示会是乱的（与屏幕阅读器看到的也不一致）。
    QString ordered;
    if (textController_ != nullptr) {
        const TextLayout* textLayout = textController_->layout(currentPage());
        if (textLayout != nullptr && !textLayout->isEmpty()) {
            ordered = textLayout->textForRange(0, textLayout->charCount());
        }
    }
    if (ordered.isEmpty() && extracted.has_value()) {
        ordered = extracted->plainText();
    }
    return ordered;
}

void PdfView::showReadingOptions() {
    if (!document_.isValid()) {
        QMessageBox::information(this, tr("阅读选项"), tr("先打开一份 PDF。"));
        return;
    }
    const QString order = currentPageReadingOrder();
    ReadingOptionsDialog dialog(order,
                                speechSettings(),
                                availableSpeechVoices(),
                                this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    setSpeechSettings(dialog.settings());
    emit statusMessage(tr("朗读设置已保存"));
}

speech::SpeechSettings PdfView::speechSettings() const {
    if (speechEngine_ == nullptr) {
        return speech::SpeechSettings();
    }
    return speechEngine_->settings();
}

void PdfView::setSpeechSettings(const speech::SpeechSettings& settings) {
    if (speechEngine_ == nullptr) {
        return;
    }
    speechEngine_->setSettings(settings);
}

QVector<speech::SpeechVoice> PdfView::availableSpeechVoices() const {
    return speech::SpeechEngine::availableVoices();
}

}  // namespace aipdf::app
