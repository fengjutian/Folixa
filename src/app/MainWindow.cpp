#include "app/MainWindow.h"
#include "app/RibbonBar.h"

#include "app/Language.h"
#include "app/TextController.h"
#include "app/PdfView.h"
#include "app/SearchPanel.h"
#include "app/ShareUtils.h"
#include "app/BatchRename.h"
#include "app/DocumentOutput.h"
#include "app/DocumentConverter.h"
#include "app/DigitalSignatureBackend.h"
#include "app/IndexPanel.h"
#include "app/NavigationController.h"
#include "app/AnnotationController.h"
#include "app/AiPanel.h"
#include "app/SettingsDialog.h"
#include "app/SignatureDialog.h"
#include "ai/AuditLog.h"
#include "app/OcrController.h"
#include "tools/ProposalValidator.h"
#include "app/ProposalRunner.h"
#include "app/EditProposalDialog.h"
#include "app/BlankPageDetector.h"
#include "app/BrandMark.h"
#include "app/WelcomePage.h"
#include "ipc/WorkerClient.h"
#include "doc/EditCommand.h"
#include "doc/RecoveryLog.h"
#include "pdf/PdfiumAdapter.h"
#include "pdf/Annotation.h"
#include "pdf/FormData.h"
#include "pdf/Navigation.h"

#include "i18n/Translate.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QShortcut>
#include <QButtonGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QFrame>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QIcon>
#include <QFileDialog>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QInputDialog>
#include <QIntValidator>
#include <QJsonDocument>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSaveFile>
#include <QStatusBar>
#include <QTabBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTime>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <iterator>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace aipdf::app {

namespace {
QString documentSettingsKey(const QString& path) {
    return QStringLiteral("documentManagement/documents/") +
           QString::fromLatin1(QCryptographicHash::hash(
               QFileInfo(path).canonicalFilePath().toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString invoiceArchiveDirectory() {
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                             .filePath(QStringLiteral("invoices"));
    QDir().mkpath(path);
    return path;
}

QString csvCell(QString value) {
    value.replace('"', QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

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

// 命令栏与页面页统一的图标：SVG 资源，18px（20px 画布按 0.9 缩）。
//
// 禁用态自己画：QSvgIconEngine 不保证按 QIcon::Disabled 变灰，
// 而设计规范要求「未实现的功能不得伪装为可用」。同一份 SVG 再渲染一次、
// 用 SourceIn 覆盖成中性灰，禁用按钮就不会带着一条深色笔画装成活按钮。
QIcon commandIcon(const QString& name) {
    const QIcon source(QStringLiteral(":/icons/%1.svg").arg(name));
    QIcon icon;
    // 1x 与 2x 各一份：125% / 150% 缩放下图标不该被拉伸糊掉。
    for (const int scale : {1, 2}) {
        const QSize size(18 * scale, 18 * scale);
        QPixmap normal = source.pixmap(size, QIcon::Normal);
        if (normal.isNull()) {
            continue;
        }
        normal.setDevicePixelRatio(scale);
        icon.addPixmap(normal, QIcon::Normal);
        QPixmap disabled = normal;
        QPainter painter(&disabled);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(disabled.rect(), QColor(180, 189, 203, 170));
        painter.end();
        disabled.setDevicePixelRatio(scale);
        icon.addPixmap(disabled, QIcon::Disabled);
    }
    return icon.isNull() ? source : icon;
}

// 把大纲树灌进目录树控件。目标页解析不出来的条目照样显示，只是不能点。
void addOutlineItems(QTreeWidgetItem* parent, const std::vector<pdf::OutlineItem>& items) {
    for (const pdf::OutlineItem& item : items) {
        auto* node = new QTreeWidgetItem(parent);
        node->setText(0, item.title);
        node->setData(0, Qt::UserRole, item.pageIndex);
        if (item.pageIndex < 0) {
            node->setToolTip(0, i18n::tr("aipdf::app::MainWindow", "该条目没有可跳转的目标"));
        } else {
            node->setToolTip(0, i18n::tr("aipdf::app::MainWindow", "跳转到第 %1 页")
                                   .arg(item.pageIndex + 1));
        }
        addOutlineItems(node, item.children);
    }
}

pdf::OutlineItem* outlineItemAt(std::vector<pdf::OutlineItem>* items, int target, int* cursor) {
    if (items == nullptr || cursor == nullptr) return nullptr;
    for (pdf::OutlineItem& item : *items) {
        if ((*cursor)++ == target) return &item;
        if (pdf::OutlineItem* found = outlineItemAt(&item.children, target, cursor)) return found;
    }
    return nullptr;
}

bool removeOutlineItemAt(std::vector<pdf::OutlineItem>* items, int target, int* cursor) {
    if (items == nullptr || cursor == nullptr) return false;
    for (auto it = items->begin(); it != items->end(); ++it) {
        if ((*cursor)++ == target) { items->erase(it); return true; }
        if (removeOutlineItemAt(&it->children, target, cursor)) return true;
    }
    return false;
}

// 欢迎页「最近文档」的上限：再多就把欢迎页撑成文件管理器的样子。
constexpr int kMaxRecentFiles = 6;

// 读最近文档。存的是绝对路径，但文件可能被删、被改名或被移走——
// 列出来的每一项都要点得开，所以这里顺手把已经不存在的过滤掉。
QStringList recentFilesFromSettings() {
    const QStringList stored =
        QSettings().value(QStringLiteral("startup/recentFiles")).toStringList();
    QStringList available;
    for (const QString& path : stored) {
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            available.append(path);
        }
    }
    return available;
}

// 从拖放数据里挑出 PDF 路径（其他类型一律不接）。
QStringList droppedPdfPaths(const QMimeData* mimeData) {
    QStringList paths;
    if (mimeData == nullptr || !mimeData->hasUrls()) {
        return paths;
    }
    for (const QUrl& url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (QFileInfo(path).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
            paths.append(path);
        }
    }
    return paths;
}
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), workerClient_(new ipc::WorkerClient(this)) {
    // 欢迎页写着「把 PDF 拖到窗口里」，那窗口就得真的收得下。
    setAcceptDrops(true);
    buildUi();

    const QString autoImportDirectory =
        QSettings().value(QStringLiteral("documentManagement/autoImportDirectory")).toString();
    if (QFileInfo(autoImportDirectory).isDir()) {
        autoImportWatcher_ = new QFileSystemWatcher({autoImportDirectory}, this);
        connect(autoImportWatcher_, &QFileSystemWatcher::directoryChanged,
                this, &MainWindow::importWatchedDirectory);
    }


    QTimer::singleShot(0, this, [this] {
        const QRect avail = screen()->availableGeometry();
        resize(qMin(1280, avail.width() - 40), qMin(720, avail.height() - 80));
    });

    // 切换语言后所有登记过的文案就地重译：不重建窗口，文档与面板状态都留着。
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &MainWindow::retranslateUi);

    connect(workerClient_, &ipc::WorkerClient::stateChanged,
            this, [this](const QString& state) {
                // Worker 状态记进轨迹：验收脚本靠它断言「Worker 就绪」。
                // 界面上的状态标签在默认收起的 AI 面板里，UI Automation 读不到，
                // 而面板文案还会随改版变化——断言绑在轨迹上才稳。
                trace(QStringLiteral("worker state=%1").arg(state));
                if (state == QStringLiteral("ready")) {
                    sendHealthCheck();
                }
            });
    connect(workerClient_, &ipc::WorkerClient::responseReceived,
            this, [this](const QString&, const QJsonObject& response) {
                const QJsonObject payload = response.value("payload").toObject();
                if (payload.contains("protocol_version")) {
                    trace(QStringLiteral("worker health protocol=%1 version=%2")
                              .arg(payload.value("protocol_version").toInt())
                              .arg(payload.value("worker_version").toString()));
                }
            });

    workerClient_->start();

    // 启动后看看有没有上次没保存完的工作。
    // 延后到事件循环起来之后再问：这时主窗口已经显示出来了。
    QTimer::singleShot(0, this, &MainWindow::checkRecovery);
}

void MainWindow::buildUi() {
    setWindowTitle(QString("Folixa · AI PDF Editor %1").arg(QStringLiteral(AIPDF_VERSION)));
    // 初始窗口不能超过屏幕可用区域：900 高的窗口在 864 高的屏幕上会把状态栏顶到屏幕外。
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(1280, available.width() - 40), qMin(720, available.height() - 80));

    auto* tabs = new QTabWidget(this);
    tabs_ = tabs;
    tabs->setTabsClosable(true);
    tabs->setDocumentMode(true);
    auto* welcome = createWelcomePage();
    welcome->setProperty("welcomePage", true);
    tabs->addTab(welcome, QString());
    bindWelcomeTab(welcome);
    tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    syncTabBar();
    connect(tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget* page = tabs_->widget(index);
        if (page == nullptr || page->property("welcomePage").toBool()) {
            return;
        }
        tabs_->removeTab(index);
        page->deleteLater();
        if (tabs_->count() == 0) {
            auto* welcomePage = createWelcomePage();
            welcomePage->setProperty("welcomePage", true);
            tabs_->addTab(welcomePage, QString());
            bindWelcomeTab(welcomePage);
            tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
        }
        syncTabBar();
    });
    setCentralWidget(tabs);

    auto* fileMenu = menuBar()->addMenu(QString());
    texts_.bind([this, fileMenu] { fileMenu->setTitle(tr("文件(&F)")); });
    auto* openAction = fileMenu->addAction(QString());
    texts_.bind([this, openAction] { openAction->setText(tr("打开 PDF…")); });
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openPdfDialog);
    fileMenu->addSeparator();
    // 保存与另存为（v0.8.0）：写临时文件、校验、原子替换，失败不动原文件。
    auto* saveAction = fileMenu->addAction(QString());
    texts_.bind([this, saveAction] { saveAction->setText(tr("保存")); });
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setObjectName(QStringLiteral("fileSave"));
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveDocument);
    auto* saveAsAction = fileMenu->addAction(QString());
    texts_.bind([this, saveAsAction] { saveAsAction->setText(tr("另存为…")); });
    saveAsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    saveAsAction->setObjectName(QStringLiteral("fileSaveAs"));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveDocumentAs);

    // 打印与导出：都作用在当前标签页，没有文档时什么也不做。
    auto* printAction = fileMenu->addAction(QString());
    texts_.bind([this, printAction] { printAction->setText(tr("打印…")); });
    printAction->setShortcut(QKeySequence::Print);
    printAction->setObjectName(QStringLiteral("filePrint"));
    connect(printAction, &QAction::triggered, this, [this]() {
        if (auto* view = currentView()) {
            view->printDocument();
        }
    });
    auto* exportImagesAction = fileMenu->addAction(QString());
    texts_.bind([this, exportImagesAction] {
        exportImagesAction->setText(tr("导出页面为图片…"));
    });
    exportImagesAction->setObjectName(QStringLiteral("fileExportImages"));
    connect(exportImagesAction, &QAction::triggered, this, [this]() {
        if (auto* view = currentView()) {
            view->exportPageImages();
        }
    });
    auto* exportTextAction = fileMenu->addAction(QString());
    texts_.bind([this, exportTextAction] { exportTextAction->setText(tr("导出文本…")); });
    exportTextAction->setObjectName(QStringLiteral("fileExportText"));
    connect(exportTextAction, &QAction::triggered, this, [this]() {
        if (auto* view = currentView()) {
            view->exportPlainText();
        }
    });

    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(QString());
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    texts_.bind([this, quitAction] { quitAction->setText(tr("退出")); });

    auto* viewMenu = menuBar()->addMenu(QString());
    texts_.bind([this, viewMenu] { viewMenu->setTitle(tr("视图(&V)")); });
    // 排布方式：三选一，互斥。切换后由 PdfView 重算布局与适配缩放。
    auto* layoutGroup = new QActionGroup(this);
    layoutGroup->setExclusive(true);
    // 排布方式三选一。名称用 QT_TRANSLATE_NOOP 把源文案带下来：
    // 动作是这里新建的，切换语言时要按同一份源文案再翻一次。
    auto addLayoutMode = [&](const char* source, PageLayout::LayoutMode mode) {
        auto* action = viewMenu->addAction(QString());
        action->setCheckable(true);
        layoutGroup->addAction(action);
        texts_.bind([this, action, source] { action->setText(tr(source)); });
        connect(action, &QAction::triggered, this, [this, mode]() {
            if (auto* view = currentView()) {
                view->setLayoutMode(mode);
            }
        });
        return action;
    };
    addLayoutMode(QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "单页"),
                  PageLayout::LayoutMode::Continuous)
        ->setChecked(true);
    addLayoutMode(QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "双页"),
                  PageLayout::LayoutMode::Facing);
    addLayoutMode(QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "双页（封面）"),
                  PageLayout::LayoutMode::FacingCover);
    viewMenu->addSeparator();

    auto* exportXfdfAction = viewMenu->addAction(QString());
    texts_.bind([this, exportXfdfAction] {
        exportXfdfAction->setText(tr("导出批注（XFDF）…"));
    });
    exportXfdfAction->setObjectName(QStringLiteral("annotationsExportXfdf"));
    connect(exportXfdfAction, &QAction::triggered, this, [this]() {
        if (auto* view = currentView()) {
            view->exportAnnotationsXfdf();
        }
    });
    auto* summaryExportAction = viewMenu->addAction(QString());
    texts_.bind([this, summaryExportAction] {
        summaryExportAction->setText(tr("导出批注摘要（HTML）…"));
    });
    summaryExportAction->setObjectName(QStringLiteral("annotationsSummaryExport"));
    connect(summaryExportAction, &QAction::triggered, this, [this]() {
        if (auto* view = currentView()) {
            view->exportAnnotationSummary();
        }
    });
    auto* summaryPrintAction = viewMenu->addAction(QString());
    texts_.bind([this, summaryPrintAction] {
        summaryPrintAction->setText(tr("打印批注摘要…"));
    });
    summaryPrintAction->setObjectName(QStringLiteral("annotationsSummaryPrint"));
    connect(summaryPrintAction, &QAction::triggered, this, [this]() {
        if (auto* view = currentView()) {
            view->printAnnotationSummary();
        }
    });
    auto* importXfdfAction = viewMenu->addAction(QString());
    texts_.bind([this, importXfdfAction] {
        importXfdfAction->setText(tr("导入批注（XFDF）…"));
    });
    importXfdfAction->setObjectName(QStringLiteral("annotationsImportXfdf"));
    connect(importXfdfAction, &QAction::triggered, this, [this]() {
        if (auto* view = currentView()) {
            view->importAnnotationsXfdf();
        }
    });
    viewMenu->addSeparator();

    auto* presentationAction = viewMenu->addAction(QString());
    texts_.bind([this, presentationAction] { presentationAction->setText(tr("放映")); });
    presentationAction->setShortcut(QKeySequence(Qt::Key_F5));
    presentationAction->setObjectName(QStringLiteral("viewPresentation"));
    connect(presentationAction, &QAction::triggered, this, [this]() {
        if (auto* view = currentView()) {
            view->startPresentation();
        }
    });
    menuBar()->hide();

    // 轻量功能导航：仅当前已交付的阅读功能可用，其余明确标记为后续能力。
    auto* commandBar = new QToolBar(this);
    addToolBar(Qt::TopToolBarArea, commandBar);
    texts_.bind([this, commandBar] { commandBar->setWindowTitle(tr("功能导航")); });
    commandBar->setObjectName(QStringLiteral("commandBar"));
    commandBar->setMovable(false);
    commandBar->setFloatable(false);
    commandBar->setIconSize(QSize(18, 18));
    commandBar->setFixedHeight(34);
    auto* appMenuButton = new QToolButton(commandBar);
    appMenuButton->setObjectName(QStringLiteral("appMenuButton"));
    appMenuButton->setIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    appMenuButton->setIconSize(QSize(18, 18));
    appMenuButton->setText(QStringLiteral("工作台"));
    appMenuButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    texts_.bind([this, appMenuButton] { appMenuButton->setToolTip(tr("主菜单")); });
    // 必须走 setMenu：菜单里的「查找…」靠 QAction 快捷键（Ctrl+F）生效，
    // 改成手动 popup 的话这个 QAction 不属于任何窗口，快捷键会静默失效。
    // 按钮宽度与右侧箭头由样式表处理（父级 commandBar 规则 + 隐藏 menu-indicator）。
    appMenuButton->setPopupMode(QToolButton::InstantPopup);
    appMenuButton->setFixedSize(112, 28);
    auto* appMenu = new QMenu(appMenuButton);
    appMenu->addAction(openAction);
    appMenu->addSeparator();
    appMenu->addAction(saveAction);
    appMenu->addAction(saveAsAction);
    appMenu->addSeparator();
    auto* findAction = appMenu->addAction(QString());
    texts_.bind([this, findAction] { findAction->setText(tr("查找…")); });
    findAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    appMenu->addSeparator();
    // 先占位，等导航面板建好再把开关接上：按文本找动作太脆。
    auto* navigationPanelAction = appMenu->addAction(QString());
    appMenu->addSeparator();
    // OCR（v0.13.0）：把当前页的扫描图识别成文字并入索引。
    ocrController_ = new OcrController(this);
    auto* ocrAction = appMenu->addAction(QString());
    texts_.bind([this, ocrAction] { ocrAction->setText(tr("识别当前页（OCR）")); });
    ocrAction->setObjectName(QStringLiteral("ocrCurrentPage"));
    connect(ocrAction, &QAction::triggered, this, [this] {
        PdfView* view = currentView();
        if (view == nullptr) {
            statusBar()->showMessage(tr("先打开一份 PDF"), 5000);
            return;
        }
        const int page = view->currentPage();
        QString reason;
        if (!ocrController_->requestPage(view, page, &reason)) {
            statusBar()->showMessage(reason, 8000);
            trace(QStringLiteral("ocr skipped page=%1 reason=%2").arg(page + 1).arg(reason));
            return;
        }
        statusBar()->showMessage(tr("正在识别第 %1 页…").arg(page + 1));
        trace(QStringLiteral("ocr request page=%1").arg(page + 1));
    });
    connect(ocrController_, &OcrController::pageApplied, this, [this](int page, int chunks) {
        statusBar()->showMessage(
            tr("第 %1 页 OCR 完成：%2 块已加入索引").arg(page + 1).arg(chunks), 10000);
        trace(QStringLiteral("ocr applied page=%1 chunks=%2").arg(page + 1).arg(chunks));
    });
    // 安全（v1.6.0）：保存时 AES-256 加密并写权限位。
    auto* encryptionAction = appMenu->addAction(QString());
    texts_.bind([this, encryptionAction] {
        encryptionAction->setText(tr("加密与权限…"));
    });
    encryptionAction->setObjectName(QStringLiteral("securityEncryption"));
    encryptionAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    connect(encryptionAction, &QAction::triggered, this, [this] {
        PdfView* view = currentView();
        if (view == nullptr) {
            statusBar()->showMessage(tr("先打开一份 PDF"), 5000);
            return;
        }
        view->configureEncryptionDialog();
    });
    // AI 文档操作（v0.14.0）：只读扫描 → 生成提案 → 用户确认 → 走命令系统。
    // 这条路径上没有任何一步能绕过确认框。
    auto* blankAction = appMenu->addAction(QString());
    texts_.bind([this, blankAction] {
        blankAction->setText(tr("AI：找出空白页并提议删除"));
    });
    blankAction->setObjectName(QStringLiteral("aiFindBlankPages"));
    connect(blankAction, &QAction::triggered, this, [this] {
        PdfView* view = currentView();
        if (view == nullptr) {
            statusBar()->showMessage(tr("先打开一份 PDF"), 5000);
            return;
        }
        const int pageCount = view->pageCount();
        const int limit = qMin(pageCount, 200);
        QVector<int> blankPages;
        for (int page = 0; page < limit; ++page) {
            const QImage thumb = view->renderThumbnail(page, 100);
            if (app::looksBlank(thumb)) {
                blankPages.append(page);
            }
        }
        trace(QStringLiteral("ai blank scan pages=%1 blank=%2")
                  .arg(limit)
                  .arg(blankPages.size()));
        if (blankPages.isEmpty()) {
            statusBar()->showMessage(
                tr("扫描了 %1 页，没有发现空白页").arg(limit), 8000);
            return;
        }

        QJsonArray pages;
        for (const int page : blankPages) {
            pages.append(page + 1);
        }
        QJsonArray raw;
        raw.append(QJsonObject{{QStringLiteral("tool"), QStringLiteral("doc.delete_pages")},
                               {QStringLiteral("args"),
                                QJsonObject{{QStringLiteral("pages"), pages}}}});
        const tools::Proposal proposal =
            tools::validateProposal(raw, tools::DocumentSnapshot{pageCount, 0});
        if (!proposal.ok()) {
            statusBar()->showMessage(tr("提案没有通过校验：%1")
                                         .arg(proposal.issues.first().message),
                                     10000);
            return;
        }

        app::EditProposalDialog dialog(proposal, this);
        if (dialog.exec() != QDialog::Accepted) {
            trace(QStringLiteral("ai proposal cancelled blank=%1").arg(blankPages.size()));
            aipdf::ai::ProposalAuditEntry audit;
            audit.documentPath = view->filePath();
            audit.tools = QStringList{QStringLiteral("doc.delete_pages")};
            audit.operationCount = proposal.operations.size();
            audit.confirmed = false;
            audit.result = QStringLiteral("cancelled");
            aipdf::ai::appendProposalAudit(audit);
            statusBar()->showMessage(tr("已取消，文档没有任何改动"), 6000);
            return;
        }

        ProposalRunner::CommandList commands;
        QString error;
        if (!ProposalRunner::buildCommands(proposal, &commands, &error)) {
            statusBar()->showMessage(tr("无法执行：%1").arg(error), 10000);
            return;
        }
        AnnotationController* annotations = view->annotationController();
        const int applied = ProposalRunner::execute(
            &commands,
            [annotations](std::unique_ptr<doc::EditCommand> command) {
                return annotations != nullptr && annotations->applyCommand(std::move(command));
            },
            &error);
        aipdf::ai::ProposalAuditEntry audit;
        audit.documentPath = view->filePath();
        audit.tools = QStringList{QStringLiteral("doc.delete_pages")};
        audit.operationCount = proposal.operations.size();
        audit.appliedCount = applied;
        audit.confirmed = true;
        audit.result = applied > 0 ? QStringLiteral("applied") : QStringLiteral("failed");
        aipdf::ai::appendProposalAudit(audit);
        trace(QStringLiteral("ai proposal applied ops=%1 blank=%2 pages=%3 error=%4")
                  .arg(applied)
                  .arg(blankPages.size())
                  .arg(view->pageCount())
                  .arg(error));
        if (applied == 0) {
            statusBar()->showMessage(tr("执行失败：%1").arg(error), 10000);
        } else {
            statusBar()->showMessage(
                tr("已删除 %1 个空白页，可用 Ctrl+Z 撤销").arg(applied), 10000);
        }
    });
    connect(ocrController_, &OcrController::failed, this, [this](int page, const QString& reason) {
        // 空白页本来就没有文字：自动识别失败不该在状态栏刷一条错误。
        if (!reason.contains(QStringLiteral("没有识别出文字"))) {
        statusBar()->showMessage(tr("第 %1 页 OCR 失败：%2").arg(page + 1).arg(reason), 10000);
        }
        trace(QStringLiteral("ocr failed page=%1 reason=%2").arg(page + 1).arg(reason));
    });
    appMenu->addSeparator();
    // 界面语言：中文是源语言（界面文案本身就是中文），英文走翻译目录；
    // 「跟随系统」按系统语言选，默认就是它。
    auto* languageMenu = appMenu->addMenu(QString());
    texts_.bind([this, languageMenu] { languageMenu->setTitle(tr("语言(&L)")); });
    languageActions_ = new QActionGroup(this);
    languageActions_->setExclusive(true);
    for (const i18n::Language& language : i18n::supportedLanguages()) {
        // 语言名用各自的自称（中文 / English），不翻译。
        QAction* action = languageMenu->addAction(language.nativeName);
        action->setCheckable(true);
        action->setData(language.code);
        languageActions_->addAction(action);
        connect(action, &QAction::triggered, this, [this, code = language.code] {
            applyLanguage(code);
        });
    }
    auto* followSystemAction = languageMenu->addAction(QString());
    followSystemAction->setCheckable(true);
    followSystemAction->setData(i18n::LanguageManager::systemPreference());
    languageActions_->addAction(followSystemAction);
    followSystemAction_ = followSystemAction;
    connect(followSystemAction, &QAction::triggered, this, [this] {
        applyLanguage(i18n::LanguageManager::systemPreference());
    });
    syncLanguageMenu();

    auto* appQuitAction = appMenu->addAction(QString());
    connect(appQuitAction, &QAction::triggered, this, &QWidget::close);
    texts_.bind([this, appQuitAction] { appQuitAction->setText(tr("退出")); });
    appMenuButton->setMenu(appMenu);
    commandBar->addWidget(appMenuButton);
    // 快速访问区保持紧凑，不占 Ribbon 命令区。
    auto addQuickButton = [commandBar](const QIcon& icon, const QString& tip,
                                       const std::function<void()>& handler,
                                       const QString& objectName) {
        auto* button = new QToolButton(commandBar);
        button->setObjectName(objectName);
        button->setProperty("titleQuick", true);
        button->setIcon(icon);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        QObject::connect(button, &QToolButton::clicked, commandBar, handler);
        commandBar->addWidget(button);
        return button;
    };
    addQuickButton(QIcon(QStringLiteral(":/icons/open.svg")), openAction->text(),
                   [this] { openPdfDialog(); }, QStringLiteral("titleOpen"));
    addQuickButton(QIcon(QStringLiteral(":/icons/save.svg")), saveAction->text(),
                   [this] { saveDocument(); }, QStringLiteral("titleSave"));

    auto* titleSpacerLeft = new QWidget(commandBar);
    titleSpacerLeft->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    commandBar->addWidget(titleSpacerLeft);
    auto* globalSearch = new QLineEdit(commandBar);
    globalSearch->setObjectName(QStringLiteral("globalSearch"));
    globalSearch->setClearButtonEnabled(true);
    globalSearch->setMaximumWidth(420);
    globalSearch->setMinimumWidth(300);
    texts_.bind([this, globalSearch] { globalSearch->setPlaceholderText(tr("搜索")); });
    commandBar->addWidget(globalSearch);
    auto* titleSpacerRight = new QWidget(commandBar);
    titleSpacerRight->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    commandBar->addWidget(titleSpacerRight);
    auto* notificationButton = addQuickButton(QIcon(QStringLiteral(":/icons/note.svg")),
                                              QStringLiteral("通知"), [] {},
                                              QStringLiteral("titleNotifications"));
    notificationButton->setEnabled(false);
    auto* accountButton = new QToolButton(commandBar);
    accountButton->setObjectName(QStringLiteral("titleAccount"));
    accountButton->setProperty("titleAccount", true);
    accountButton->setText(QStringLiteral("F"));
    accountButton->setToolTip(QStringLiteral("Folixa"));
    commandBar->addWidget(accountButton);
    connect(findAction, &QAction::triggered, this, [this]() {
        trace(QStringLiteral("open search panel"));
        navigationDock_->setVisible(true);
        if (navigationRail_ != nullptr) {
            // 切到导航栏的搜索页（第四个按钮）。
            if (auto* button = navigationRail_->button(3)) {
                button->click();
            }
        }
        if (searchPanel_ != nullptr) {
            searchPanel_->focusSearch();
        }
    });
    connect(globalSearch, &QLineEdit::returnPressed, findAction, &QAction::trigger);
    // 已交付的功能带图标（与左侧导航轨、文档工具栏同一套 Rounded Linear 资源）；
    // 未交付的保持纯文字并用分隔线隔开，一眼能看出「这些还不能点」。
    commandBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* homeAction = commandBar->addAction(QString());
    homeAction->setIcon(QIcon(QStringLiteral(":/icons/book.svg")));
    texts_.bind([this, homeAction] { homeAction->setText(tr("阅读")); });
    texts_.bind([this, homeAction] { homeAction->setToolTip(tr("阅读视图")); });
    homeAction->setCheckable(true);
    homeAction->setChecked(true);
    // 批注已交付（v0.7.0）：点开就是批注面板。
    auto* annotationAction = commandBar->addAction(QString());
    annotationAction->setIcon(QIcon(QStringLiteral(":/icons/annotation.svg")));
    annotationAction->setObjectName(QStringLiteral("commandAnnotation"));
    texts_.bind([this, annotationAction] { annotationAction->setText(tr("批注")); });
    texts_.bind([this, annotationAction] {
        annotationAction->setToolTip(tr("查看与删除批注"));
    });
    connect(annotationAction, &QAction::triggered, this, [this]() {
        navigationDock_->setVisible(true);
        // 切到导航栏的批注页（第五个按钮）。
        showNavigationPage(4);
        trace(QStringLiteral("open annotation panel"));
    });
    // F7：直接打开批注面板。除了给用户一条快捷键，它也是验收脚本唯一能
    // 可靠"展开面板"的手段（导航栏按钮不支持 InvokePattern）。
    auto* annotationShortcut = new QShortcut(QKeySequence(Qt::Key_F7), this);
    connect(annotationShortcut, &QShortcut::activated, this, [this]() {
        // 面板可能是收起的：只切页不展开，用户（和验收脚本）都看不到东西。
        if (navigationDock_ != nullptr) {
            navigationDock_->setVisible(true);
        }
        showNavigationPage(4);
        trace(QStringLiteral("open annotation panel"));
    });

    // F8：打开表单面板（与 F7 同理，给用户快捷键，也给验收脚本一条可触发的路径）。
    auto* formShortcut = new QShortcut(QKeySequence(Qt::Key_F8), this);
    connect(formShortcut, &QShortcut::activated, this, [this]() {
        if (navigationDock_ != nullptr) {
            navigationDock_->setVisible(true);
        }
        showNavigationPage(5);
        refreshForms(currentView());
        trace(QStringLiteral("open form panel"));
    });

    // 页面管理已交付（v0.8.0）：点开就是页面页（缩略图 + 页面操作按钮）。
    auto* pageAction = commandBar->addAction(QString());
    pageAction->setIcon(QIcon(QStringLiteral(":/icons/pages.svg")));
    pageAction->setObjectName(QStringLiteral("commandPages"));
    texts_.bind([this, pageAction] { pageAction->setText(tr("页面")); });
    texts_.bind([this, pageAction] {
        pageAction->setToolTip(tr("整理页面：插入、删除、旋转与移动"));
    });
    connect(pageAction, &QAction::triggered, this, [this]() {
        navigationDock_->setVisible(true);
        // 切到导航栏的页面页（第一个按钮）。
        showNavigationPage(0);
        trace(QStringLiteral("open page panel"));
    });
    // 本地索引已交付（v0.9.0）：点开就是索引页（状态、检索、结果）。
    auto* indexAction = commandBar->addAction(QString());
    indexAction->setIcon(QIcon(QStringLiteral(":/icons/index.svg")));
    indexAction->setObjectName(QStringLiteral("commandIndex"));
    texts_.bind([this, indexAction] { indexAction->setText(tr("索引")); });
    texts_.bind([this, indexAction] {
        indexAction->setToolTip(tr("建立文档索引并按关键词检索"));
    });
    connect(indexAction, &QAction::triggered, this, [this]() {
        navigationDock_->setVisible(true);
        // 切到导航栏的索引页（最后一个按钮）。
        showNavigationPage(5);
        trace(QStringLiteral("open index panel"));
    });
    // 工具栏里的按钮是 Qt 自己建的 QToolButton：
    // QAction 的对象名不会出现在 UI Automation 里，验收脚本按它找不到人。
    // 给真正的按钮也起一个名字。
    auto nameCommandButton = [commandBar](QAction* action, const QString& id) {
        if (auto* button = qobject_cast<QToolButton*>(commandBar->widgetForAction(action))) {
            button->setObjectName(id);
        }
    };
    nameCommandButton(annotationAction, QStringLiteral("commandAnnotationButton"));
    nameCommandButton(pageAction, QStringLiteral("commandPagesButton"));
    nameCommandButton(indexAction, QStringLiteral("commandIndexButton"));
    // 后续能力与已交付功能之间留一条分隔线。
    //
    // 还没有实现的能力也带图标：它们和已交付功能并排，缺图标会让人以为
    // 是状态异常；图标统一取中性灰的禁用态，仍然一眼看得出「还不能点」。
    struct FutureCommand {
        const char* label;
        QString icon;
    };
    // 未交付的能力：名称用 QT_TRANSLATE_NOOP 带下来，切换语言时就地重译。
    const QVector<FutureCommand> futureCommands = {
        {QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "编辑"), QStringLiteral("edit")},
        {QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "转换"), QStringLiteral("convert")},
        {QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "保护"), QStringLiteral("protect")},
        {QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "表单"), QStringLiteral("form")},
        {QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "工具"), QStringLiteral("tools")},
    };
    for (const FutureCommand& entry : futureCommands) {
        QAction* action = commandBar->addAction(commandIcon(entry.icon), QString());
        action->setEnabled(false);
        action->setVisible(false);
        texts_.bind([this, action, source = entry.label] {
            action->setText(tr(source));
            action->setToolTip(tr("将在后续版本中提供"));
        });
    }
    auto* aiAction = commandBar->addAction(QString());
    texts_.bind([this, aiAction] { aiAction->setText(tr("✦ AI 助手")); });
    aiAction->setObjectName(QStringLiteral("aiCommand"));
    if (auto* aiButton = qobject_cast<QToolButton*>(commandBar->widgetForAction(aiAction))) {
        aiButton->setObjectName(QStringLiteral("aiCommandButton"));
    }
    // 顶部工具栏只保留主菜单与品牌：导航动作已经分别进 Ribbon 和左侧导航轨。
    // 注意不能遍历 commandBar->actions() 全隐藏——菜单按钮与品牌是 QWidgetAction，
    // 一起隐藏会把主菜单也弄没。
    homeAction->setVisible(false);
    annotationAction->setVisible(false);
    pageAction->setVisible(false);
    indexAction->setVisible(false);
    aiAction->setVisible(false);

    // ------------------------------------------------------------- Ribbon ----
    // 顶部标签页 + 命令组：结构对齐福昕的「文件 / 主页 / 注释 / 页面管理 / 视图 / 保护 / AI」。
    // 只放已经能用的命令；没实现的能力用禁用按钮并写明「后续版本」。
    // Ribbon 必须独占一行；否则会和顶部命令栏挤在同一行，被裁到窗口外面。
    // Ribbon 放进顶部 Dock：不动中央 QTabWidget，避免换中央控件时的生命周期问题。
    ribbonBar_ = new RibbonBar(this);
    // 标签条 34 + 分组标题 / 按钮 / 上下留白。比 104 多出的这一截不是装饰：
    // 104 时分组标题正好被挤没（只剩按钮），再叠上横向滚动条就整行消失。
    ribbonBar_->setFixedHeight(112);
    auto* ribbonDock = new QDockWidget(QString(), this);
    ribbonDock->setObjectName(QStringLiteral("ribbonDock"));
    ribbonDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    ribbonDock->setTitleBarWidget(new QWidget(ribbonDock));
    ribbonDock->setWidget(ribbonBar_);
    addDockWidget(Qt::TopDockWidgetArea, ribbonDock);
    // 先隐藏：Ribbon 的宽度/高度还在收敛，隐藏后主窗口与画布保持原来的稳定布局。

    auto addRibbonCommand = [this](QHBoxLayout* group, const char* source, const char* tip,
                                   const QString& icon, auto handler, bool enabled = true,
                                   const QString& objectName = QString()) {
        QToolButton* button = ribbonBar_->addCommand(group, tr(source), QIcon(icon), tr(tip),
                                                     enabled, objectName);
        texts_.bind([this, button, source, tip] {
            button->setText(tr(source));
            button->setToolTip(tr(tip));
        });
        connect(button, &QToolButton::clicked, this, handler);
        return button;
    };

    const int fileTab = ribbonBar_->addTab(tr("文件"));
    auto* fileGroup = ribbonBar_->addGroup(fileTab, tr("文件"));
    addRibbonCommand(fileGroup, "打开", "打开 PDF 文件", ":/icons/book.svg",
                     [this] { openPdfDialog(); }, true, "ribbonOpen");
    addRibbonCommand(fileGroup, "保存", "保存当前文档", ":/icons/save.svg",
                     [this] { saveDocument(); }, true, "document.save");
    addRibbonCommand(fileGroup, "另存为", "另存为新的 PDF", ":/icons/save.svg",
                     [this] { saveDocumentAs(); }, true, "ribbonSaveAs");
    auto* exportGroup = ribbonBar_->addGroup(fileTab, tr("导出"));
    addRibbonCommand(exportGroup, "打印", "打印当前文档", ":/icons/book.svg", [this] {
        if (auto* view = currentView()) { view->printDocument(); }
    }, true, "permission.print");
    addRibbonCommand(exportGroup, "图片", "把页面导出为图片", ":/icons/convert.svg", [this] {
        if (auto* view = currentView()) { view->exportPageImages(); }
    }, true, "permission.exportImages");
    addRibbonCommand(exportGroup, "文本", "导出纯文本", ":/icons/convert.svg", [this] {
        if (auto* view = currentView()) { view->exportPlainText(); }
    }, true, "permission.exportText");

    const int homeTab = ribbonBar_->addTab(tr("主页"));
    auto* zoomGroup = ribbonBar_->addGroup(homeTab, tr("显示"));
    addRibbonCommand(zoomGroup, "缩小", "缩小", ":/icons/zoom-out.svg", [this] {
        if (auto* view = currentView()) { view->zoomOut(); }
    });
    addRibbonCommand(zoomGroup, "放大", "放大", ":/icons/zoom-in.svg", [this] {
        if (auto* view = currentView()) { view->zoomIn(); }
    });
    addRibbonCommand(zoomGroup, "适宽", "适应宽度", ":/icons/fit-width.svg", [this] {
        if (auto* view = currentView()) { view->zoomToFitWidth(); }
    }, true, "ribbonFitWidth");
    addRibbonCommand(zoomGroup, "整页", "适应页面", ":/icons/fit-page.svg", [this] {
        if (auto* view = currentView()) { view->zoomToFitPage(); }
    });
    addRibbonCommand(zoomGroup, "左旋", "向左旋转视图", ":/icons/rotate-left.svg", [this] {
        if (auto* view = currentView()) { view->rotateLeft(); }
    });
    addRibbonCommand(zoomGroup, "右旋", "向右旋转视图", ":/icons/rotate-right.svg", [this] {
        if (auto* view = currentView()) { view->rotateRight(); }
    });
    auto* editGroup = ribbonBar_->addGroup(homeTab, tr("编辑"));
    addRibbonCommand(editGroup, "撤销", "撤销批注操作", ":/icons/undo.svg", [this] {
        if (auto* view = currentView()) { view->undoAnnotation(); }
    });
    addRibbonCommand(editGroup, "重做", "重做批注操作", ":/icons/redo.svg", [this] {
        if (auto* view = currentView()) { view->redoAnnotation(); }
    });
    auto* toolsGroup = ribbonBar_->addGroup(homeTab, tr("工具"));
    addRibbonCommand(toolsGroup, "识别", "识别当前页（OCR）", ":/icons/search.svg",
                     [ocrAction] { ocrAction->trigger(); });
    addRibbonCommand(toolsGroup, "加密", "加密与权限", ":/icons/protect.svg",
                     [encryptionAction] { encryptionAction->trigger(); }, true,
                     "ribbonEncryption");

    const int commentTab = ribbonBar_->addTab(tr("注释"));
    auto addToolCommand = [this, &addRibbonCommand](QHBoxLayout* group, const char* label,
                                                   const char* tip, const QString& icon,
                                                   const char* id) {
        const QString objectName = QStringLiteral("annotation.") + QString::fromLatin1(id);
        addRibbonCommand(group, label, tip, icon, [this, id] {
            if (auto* view = currentView()) {
                view->activateAnnotationTool(QString::fromLatin1(id));
            }
        }, true, objectName);
    };
    auto* markGroup = ribbonBar_->addGroup(commentTab, tr("标记"));
    addToolCommand(markGroup, "高亮", "拖选文字后高亮", ":/icons/highlight.svg", "highlight");
    addToolCommand(markGroup, "下划线", "文字下划线", ":/icons/underline.svg", "underline");
    addToolCommand(markGroup, "删除线", "文字删除线", ":/icons/strikethrough.svg", "strikethrough");
    addToolCommand(markGroup, "波浪线", "文字波浪线", ":/icons/annotation.svg", "squiggly");
    auto* textGroup = ribbonBar_->addGroup(commentTab, tr("文本"));
    addToolCommand(textGroup, "便签", "在页面上放一个便签", ":/icons/note.svg", "note");
    addToolCommand(textGroup, "文本框", "在页面上写一段文字", ":/icons/edit.svg", "freetext");
    addToolCommand(textGroup, "图章", "放置文字图章", ":/icons/annotation.svg", "stamp");
    addToolCommand(textGroup, "图片图章", "用图片做图章", ":/icons/annotation.svg", "imagestamp");
    auto* shapeGroup = ribbonBar_->addGroup(commentTab, tr("图形"));
    addToolCommand(shapeGroup, "矩形", "拖出一个矩形", ":/icons/square.svg", "square");
    addToolCommand(shapeGroup, "椭圆", "拖出一个椭圆", ":/icons/square.svg", "ellipse");
    addToolCommand(shapeGroup, "直线", "拖出一条直线", ":/icons/square.svg", "line");
    addToolCommand(shapeGroup, "箭头", "拖出一个箭头", ":/icons/square.svg", "arrow");
    addToolCommand(shapeGroup, "多边形", "画一个多边形", ":/icons/square.svg", "polygon");
    addToolCommand(shapeGroup, "云线", "画一个云线", ":/icons/square.svg", "cloud");
    auto* inkGroup = ribbonBar_->addGroup(commentTab, tr("手写"));
    addToolCommand(inkGroup, "墨迹", "自由绘制", ":/icons/ink.svg", "ink");
    addToolCommand(inkGroup, "橡皮", "擦掉整笔", ":/icons/annotation.svg", "eraser");
    auto* measureGroup = ribbonBar_->addGroup(commentTab, tr("测量与附件"));
    addToolCommand(measureGroup, "测量", "量距离", ":/icons/annotation.svg", "measure");
    addToolCommand(measureGroup, "周长", "量周长", ":/icons/annotation.svg", "perimeter");
    addToolCommand(measureGroup, "面积", "量面积", ":/icons/annotation.svg", "area");
    addToolCommand(measureGroup, "附件", "添加文件附件", ":/icons/annotation.svg", "attachment");
    addToolCommand(measureGroup, "声音", "录制声音批注", ":/icons/annotation.svg", "sound");
    auto* undoGroup = ribbonBar_->addGroup(commentTab, tr("历史"));
    addToolCommand(undoGroup, "撤销", "撤销批注操作", ":/icons/undo.svg", "undo");
    addToolCommand(undoGroup, "重做", "重做批注操作", ":/icons/redo.svg", "redo");    auto* panelGroup = ribbonBar_->addGroup(commentTab, tr("面板"));
    addRibbonCommand(panelGroup, "批注", "打开批注面板", ":/icons/annotation.svg", [this] {
        navigationDock_->setVisible(true);
        showNavigationPage(4);
    }, true, "ribbonAnnotationPanel");
    auto* reviewGroup = ribbonBar_->addGroup(commentTab, tr("审阅"));
    addRibbonCommand(reviewGroup, "导出批注", "导出批注（XFDF）", ":/icons/convert.svg", [this] {
        if (auto* view = currentView()) { view->exportAnnotationsXfdf(); }
    });
    addRibbonCommand(reviewGroup, "导入批注", "导入批注（XFDF）", ":/icons/convert.svg", [this] {
        if (auto* view = currentView()) { view->importAnnotationsXfdf(); }
    });
    addRibbonCommand(reviewGroup, "摘要", "导出批注摘要 HTML", ":/icons/annotation.svg", [this] {
        if (auto* view = currentView()) { view->exportAnnotationSummary(); }
    });

    const int pageTab = ribbonBar_->addTab(tr("页面管理"));
    auto* pageGroup = ribbonBar_->addGroup(pageTab, tr("页面"));
    addRibbonCommand(pageGroup, "插入", "在当前页后插入空白页", ":/icons/page-insert.svg",
                     [this] { insertBlankPage(); }, true, "permission.pageInsert");
    addRibbonCommand(pageGroup, "删除", "删除当前页", ":/icons/page-delete.svg",
                     [this] { deleteCurrentPage(); }, true, "permission.pageDelete");
    addRibbonCommand(pageGroup, "左旋", "向左旋转页面", ":/icons/rotate-left.svg",
                     [this] { rotateCurrentPageLeft(); }, true, "permission.pageRotateLeft");
    addRibbonCommand(pageGroup, "右旋", "向右旋转页面", ":/icons/rotate-right.svg",
                     [this] { rotateCurrentPageRight(); }, true, "permission.pageRotateRight");
    addRibbonCommand(pageGroup, "上移", "当前页上移", ":/icons/move-up.svg",
                     [this] { moveCurrentPageUp(); }, true, "permission.pageMoveUp");
    addRibbonCommand(pageGroup, "下移", "当前页下移", ":/icons/move-down.svg",
                     [this] { moveCurrentPageDown(); }, true, "permission.pageMoveDown");
    addRibbonCommand(pageGroup, "复制页", "复制当前页", ":/icons/pages.svg", [this] {
        if (auto* view = currentView()) { view->duplicateCurrentPage(); }
    }, true, "ribbonDuplicate");
    addRibbonCommand(pageGroup, "交换", "与下一页交换", ":/icons/move-down.svg", [this] {
        if (auto* view = currentView()) { view->swapCurrentWithNext(); }
    }, true, "ribbonSwap");
    addRibbonCommand(pageGroup, "逆序", "整篇页面逆序", ":/icons/pages.svg", [this] {
        if (auto* view = currentView()) { view->reversePages(); }
    }, true, "ribbonReverse");    auto* assemblyGroup = ribbonBar_->addGroup(pageTab, tr("组装"));
    addRibbonCommand(assemblyGroup, "提取范围", "按页码范围提取新文档", ":/icons/pages.svg", [this] {
        if (auto* view = currentView()) { view->extractPagesDialog(); }
    }, true, "ribbonExtractRange");
    addRibbonCommand(assemblyGroup, "提取", "提取当前页为新文档", ":/icons/pages.svg", [this] {
        if (auto* view = currentView()) { view->extractCurrentPageDialog(); }
    }, true, "ribbonExtractCurrent");
    addRibbonCommand(assemblyGroup, "替换", "用另一份 PDF 的页面替换当前页", ":/icons/convert.svg", [this] {
        if (auto* view = currentView()) { view->replaceCurrentPageDialog(); }
    }, true, "ribbonReplace");
    addRibbonCommand(assemblyGroup, "交叉合并", "与另一份 PDF 逐页交叉合并", ":/icons/pages.svg", [this] {
        if (auto* view = currentView()) { view->crossMergeDialog(); }
    }, true, "ribbonCrossMerge");    addRibbonCommand(assemblyGroup, "调整大小", "调整页面尺寸", ":/icons/fit-page.svg", [this] {
        if (auto* view = currentView()) { view->resizeCurrentPageDialog(); }
    }, true, "ribbonResize");    addRibbonCommand(assemblyGroup, "拆分", "按每 N 页拆分文档", ":/icons/convert.svg", [this] {
        if (auto* view = currentView()) { view->splitDocumentDialog(); }
    }, true, "ribbonSplit");
    addRibbonCommand(assemblyGroup, "合并", "合并另一份 PDF", ":/icons/pages.svg", [this] {
        if (auto* view = currentView()) { view->mergeDocumentDialog(); }
    }, true, "ribbonMerge");
    auto* decorationGroup = ribbonBar_->addGroup(pageTab, tr("装饰"));
    addRibbonCommand(decorationGroup, "水印", "添加水印", ":/icons/edit.svg", [this] {
        if (auto* view = currentView()) { view->applyWatermarkDialog(); }
    });
    addRibbonCommand(decorationGroup, "页眉页脚", "添加页眉页脚", ":/icons/edit.svg", [this] {
        if (auto* view = currentView()) { view->applyHeaderFooterDialog(); }
    });
    addRibbonCommand(decorationGroup, "背景", "设置页面背景色", ":/icons/edit.svg", [this] {
        if (auto* view = currentView()) { view->setPageBackgroundDialog(); }
    }, true, "ribbonBackground");
    addRibbonCommand(decorationGroup, "裁剪", "裁剪当前页", ":/icons/edit.svg", [this] {
        if (auto* view = currentView()) { view->cropCurrentPageDialog(); }
    }, true, "ribbonCrop");
    addRibbonCommand(decorationGroup, "贝茨编号", "添加贝茨编号", ":/icons/edit.svg", [this] {
        if (auto* view = currentView()) { view->applyBatesNumberingDialog(); }
    }, true, "ribbonBates");    addRibbonCommand(decorationGroup, "页码", "添加页码", ":/icons/edit.svg", [this] {
        if (auto* view = currentView()) { view->applyPageNumbersDialog(); }
    });

    const int viewTab = ribbonBar_->addTab(tr("视图"));
    auto* ribbonLayoutRow = ribbonBar_->addGroup(viewTab, tr("页面布局"));
    auto* layoutButtons = new QButtonGroup(this);
    layoutButtons->setExclusive(true);
    auto addLayoutCommand = [this, ribbonLayoutRow, layoutButtons](
                                const char* source, const char* tip,
                                const char* icon, PageLayout::LayoutMode mode, bool checked) {
        QToolButton* button = ribbonBar_->addCommand(ribbonLayoutRow, tr(source),
                                                     QIcon(QString::fromLatin1(icon)), tr(tip),
                                                     true, QString());
        button->setCheckable(true);
        button->setChecked(checked);
        layoutButtons->addButton(button);
        texts_.bind([this, button, source, tip] {
            button->setText(tr(source));
            button->setToolTip(tr(tip));
        });
        connect(button, &QToolButton::clicked, this, [this, mode] {
            if (auto* view = currentView()) { view->setLayoutMode(mode); }
        });
        return button;
    };
    addLayoutCommand("连续", "连续滚动", ":/icons/layout-continuous.svg",
                     PageLayout::LayoutMode::Continuous, true);
    addLayoutCommand("单页", "一次显示一页", ":/icons/layout-single.svg",
                     PageLayout::LayoutMode::SinglePage, false);
    addLayoutCommand("双页", "双页对开", ":/icons/layout-facing.svg",
                     PageLayout::LayoutMode::Facing, false);
    addLayoutCommand("双页封面", "第一页单独作为封面", ":/icons/layout-facing-cover.svg",
                     PageLayout::LayoutMode::FacingCover, false);
    auto* showGroup = ribbonBar_->addGroup(viewTab, tr("显示"));
    addRibbonCommand(showGroup, "放映", "全屏放映", ":/icons/book.svg", [this] {
        if (auto* view = currentView()) { view->startPresentation(); }
    });
    addRibbonCommand(showGroup, "导航", "显示或隐藏导航面板", ":/icons/outline.svg", [this] {
        navigationDock_->setVisible(!navigationDock_->isVisible());
    });
    addRibbonCommand(showGroup, "AI 助手", "显示或隐藏 AI 面板", ":/icons/chat.svg", [this] {
        aiDock_->setVisible(!aiDock_->isVisible());
    });

    const int protectTab = ribbonBar_->addTab(tr("保护"));
    auto* securityGroup = ribbonBar_->addGroup(protectTab, tr("文档安全"));
    addRibbonCommand(securityGroup, "加密与权限", "设置打开密码与权限位",
                     ":/icons/protect.svg", [encryptionAction] { encryptionAction->trigger(); },
                     true, "ribbonSecurity");
    addRibbonCommand(securityGroup, "清理隐藏信息", "删除元数据、脚本、自动动作和嵌入文件索引",
                     ":/icons/protect.svg", [this] {
        if (auto* view = currentView()) view->sanitizeHiddenData();
    }, true, "ribbonSanitize");
    addRibbonCommand(securityGroup, "密文遮盖", "永久删除选区底层内容并烧入遮盖",
                     ":/icons/protect.svg", [this] {
        if (auto* view = currentView()) view->redactSelection();
    }, true, "ribbonRedaction");

    const int aiTab = ribbonBar_->addTab(tr("AI 助手"));
    auto* aiGroup = ribbonBar_->addGroup(aiTab, tr("对话"));
    addRibbonCommand(aiGroup, "打开 AI", "打开 AI 对话面板", ":/icons/chat.svg", [this] {
        if (aiFloatingButton_ != nullptr) { aiFloatingButton_->click(); }
    });
    addRibbonCommand(aiGroup, "总结", "总结当前页", ":/icons/summarize.svg", [this] {
        if (aiPanel_ != nullptr) { aiDock_->show(); aiPanel_->requestSummary(); }
    });
    addRibbonCommand(aiGroup, "解释", "解释选中的文字", ":/icons/explain.svg", [this] {
        if (aiPanel_ != nullptr) { aiDock_->show(); aiPanel_->requestExplain(); }
    });
    auto* aiOpsGroup = ribbonBar_->addGroup(aiTab, tr("文档操作"));
    addRibbonCommand(aiOpsGroup, "找空白页", "找出空白页并提议删除", ":/icons/annotation.svg",
                     [blankAction] { blankAction->trigger(); });
    // 尚未实现的能力先按福昕截图补齐信息架构。按钮保持禁用并明确提示，
    // 只对齐页面，不把视觉存在伪装成功能可用。
    struct PlaceholderGroup {
        const char* title;
        std::initializer_list<const char*> commands;
    };
    auto placeholderIcon = [](const QString& command) {
        const auto containsAny = [&command](std::initializer_list<QString> words) {
            for (const QString& word : words) {
                if (command.contains(word, Qt::CaseInsensitive)) { return true; }
            }
            return false;
        };
        if (containsAny({QStringLiteral("搜索"), QStringLiteral("检索"), QStringLiteral("查重"),
                         QStringLiteral("识别"), QStringLiteral("检查")})) {
            return QStringLiteral(":/icons/search.svg");
        }
        if (containsAny({QStringLiteral("文本"), QStringLiteral("编辑"), QStringLiteral("替换"),
                         QStringLiteral("拼写"), QStringLiteral("文章")})) {
            return QStringLiteral(":/icons/edit.svg");
        }
        if (containsAny({QStringLiteral("页面"), QStringLiteral("空白页"), QStringLiteral("模板")})) {
            return QStringLiteral(":/icons/pages.svg");
        }
        if (containsAny({QStringLiteral("表单"), QStringLiteral("文本域"), QStringLiteral("复选"),
                         QStringLiteral("单选"), QStringLiteral("组合框"), QStringLiteral("列表框"),
                         QStringLiteral("条形码"), QStringLiteral("XFA")})) {
            return QStringLiteral(":/icons/form.svg");
        }
        if (containsAny({QStringLiteral("签"), QStringLiteral("保护"), QStringLiteral("权限")})) {
            return QStringLiteral(":/icons/protect.svg");
        }
        if (containsAny({QStringLiteral("邮件"), QStringLiteral("共享"), QStringLiteral("发送"),
                         QStringLiteral("反馈"), QStringLiteral("邀请")})) {
            return QStringLiteral(":/icons/send.svg");
        }
        if (containsAny({QStringLiteral("图像"), QStringLiteral("图片"), QStringLiteral("截图"),
                         QStringLiteral("形状"), QStringLiteral("标注")})) {
            return QStringLiteral(":/icons/square.svg");
        }
        if (containsAny({QStringLiteral("书签"), QStringLiteral("标签"), QStringLiteral("分类"),
                         QStringLiteral("星标")})) {
            return QStringLiteral(":/icons/bookmark.svg");
        }
        if (containsAny({QStringLiteral("帮助"), QStringLiteral("助手"), QStringLiteral("工具"),
                         QStringLiteral("设置"), QStringLiteral("管理"), QStringLiteral("关于")})) {
            return QStringLiteral(":/icons/tools.svg");
        }
        if (containsAny({QStringLiteral("文件"), QStringLiteral("文档"), QStringLiteral("手册"),
                         QStringLiteral("附件"), QStringLiteral("归档")})) {
            return QStringLiteral(":/icons/book.svg");
        }
        if (containsAny({QStringLiteral("导入"), QStringLiteral("导出"), QStringLiteral("转换"),
                         QStringLiteral("到 "), QStringLiteral("合并"), QStringLiteral("PDF")})) {
            return QStringLiteral(":/icons/convert.svg");
        }
        return QStringLiteral(":/icons/note.svg");
    };
    auto addPlaceholderTab = [this, &addRibbonCommand, &placeholderIcon](
                                 const char* title,
                                 std::initializer_list<PlaceholderGroup> groups) {
        const int tab = ribbonBar_->addTab(tr(title));
        for (const PlaceholderGroup& spec : groups) {
            QHBoxLayout* group = ribbonBar_->addGroup(tab, tr(spec.title));
            for (const char* command : spec.commands) {
                const QString label = tr(command);
                addRibbonCommand(group, command, "将在后续版本中提供",
                                 placeholderIcon(label), [] {}, false);
            }
        }
    };
    const int convertTab = ribbonBar_->addTab(tr("转换"));
    auto* convertInputGroup = ribbonBar_->addGroup(convertTab, tr("转为 PDF"));
    addRibbonCommand(convertInputGroup, "Office 转 PDF",
                     "使用 LibreOffice 把 Word、Excel 或 PowerPoint 转为 PDF",
                     ":/icons/convert.svg", [this] {
        convertDocument(ConversionKind::OfficeToPdf);
    }, true, "convert.officeToPdf");
    addRibbonCommand(convertInputGroup, "HTML 转 PDF",
                     "使用 LibreOffice 把本地 HTML 文档转为 PDF",
                     ":/icons/convert.svg", [this] {
        convertDocument(ConversionKind::HtmlToPdf);
    }, true, "convert.htmlToPdf");
    auto* convertOutputGroup = ribbonBar_->addGroup(convertTab, tr("归档与优化"));
    addRibbonCommand(convertOutputGroup, "PDF/A-2b", "使用 Ghostscript 生成 PDF/A-2b 归档副本",
                     ":/icons/protect.svg", [this] {
        convertDocument(ConversionKind::PdfA2b);
    }, true, "convert.pdfa");
    addRibbonCommand(convertOutputGroup, "优化压缩", "使用 Ghostscript 优化并压缩当前 PDF",
                     ":/icons/convert.svg", [this] {
        convertDocument(ConversionKind::OptimizePdf);
    }, true, "convert.optimize");
    auto* convertUnavailableGroup = ribbonBar_->addGroup(convertTab, tr("更多转换"));
    for (const char* command : {"从扫描仪", "从网页", "到 MS Office", "到 CAD",
                                "PDF 文件包", "印前检查", "PDF 转曲"}) {
        addRibbonCommand(convertUnavailableGroup, command, "将在后续版本中提供",
                         placeholderIcon(tr(command)), [] {}, false);
    }
    const int contentEditTab = ribbonBar_->addTab(tr("编辑"));
    auto addEditTool = [this, &addRibbonCommand](QHBoxLayout* group, const char* label,
                                                 const char* tip, const QString& icon,
                                                 const char* toolId,
                                                 const char* objectName) {
        addRibbonCommand(group, label, tip, icon, [this, toolId] {
            if (auto* view = currentView()) {
                view->activateAnnotationTool(QString::fromLatin1(toolId));
            }
        }, true, QString::fromLatin1(objectName));
    };
    auto addUnavailableEditCommand = [this, &addRibbonCommand, &placeholderIcon](
                                          QHBoxLayout* group, const char* label) {
        addRibbonCommand(group, label, "将在后续版本中提供", placeholderIcon(tr(label)),
                         [] {}, false);
    };

    auto* contentEditGroup = ribbonBar_->addGroup(contentEditTab, tr("内容"));
    addRibbonCommand(contentEditGroup, "编辑文本", "替换当前选中的文字", ":/icons/edit.svg",
                     [this] {
        if (auto* view = currentView()) { view->replaceSelectionTextDialog(); }
    }, true, "edit.replaceText");
    QToolButton* editObjectButton = addRibbonCommand(
        contentEditGroup, "编辑对象", "替换或删除当前选区内的图片", ":/icons/edit.svg",
        [this] {
            if (auto* view = currentView()) { view->replaceSelectionWithImageDialog(); }
        }, true, "edit.replaceObject");
    auto* editObjectMenu = new QMenu(editObjectButton);
    QAction* replaceImageAction = editObjectMenu->addAction(tr("替换图片"));
    replaceImageAction->setObjectName(QStringLiteral("edit.replaceImage"));
    connect(replaceImageAction, &QAction::triggered, this, [this] {
        if (auto* view = currentView()) { view->replaceSelectionWithImageDialog(); }
    });
    QAction* deleteImageAction = editObjectMenu->addAction(tr("删除图片"));
    deleteImageAction->setObjectName(QStringLiteral("edit.deleteImage"));
    connect(deleteImageAction, &QAction::triggered, this, [this] {
        if (auto* view = currentView()) { view->deleteSelectionImagesDialog(); }
    });
    editObjectButton->setMenu(editObjectMenu);
    editObjectButton->setPopupMode(QToolButton::MenuButtonPopup);
    addUnavailableEditCommand(contentEditGroup, "链接并合并文本");
    addUnavailableEditCommand(contentEditGroup, "流式编辑");

    auto* textEditGroup = ribbonBar_->addGroup(contentEditTab, tr("文本工具"));
    addUnavailableEditCommand(textEditGroup, "拼写检查");
    addRibbonCommand(textEditGroup, "搜索替换", "替换当前页或整个文档中的文字", ":/icons/search.svg",
                     [this] {
        if (auto* view = currentView()) { view->searchReplaceDialog(); }
    }, true, "edit.search");
    addRibbonCommand(textEditGroup, "批量替换", "在当前页或整个文档中批量替换",
                     ":/icons/search.svg", [this] {
        if (auto* view = currentView()) { view->searchReplaceDialog(); }
    }, true, "edit.replaceAll");

    auto* insertEditGroup = ribbonBar_->addGroup(contentEditTab, tr("插入"));
    addEditTool(insertEditGroup, "添加文本", "在页面上拖出文本框", ":/icons/edit.svg",
                "freetext", "edit.addText");
    addEditTool(insertEditGroup, "添加图像", "在页面上添加图片", ":/icons/annotation.svg",
                "imagestamp", "edit.addImage");
    QToolButton* addShapeButton = addRibbonCommand(
        insertEditGroup, "添加形状", "选择形状后在页面上绘制", ":/icons/square.svg",
        [this] {
            if (auto* view = currentView()) { view->activateAnnotationTool(QStringLiteral("square")); }
        }, true, "edit.addShape");
    auto* shapeMenu = new QMenu(addShapeButton);
    const struct {
        const char* label;
        const char* id;
    } shapeCommands[] = {
        {"矩形", "square"}, {"椭圆", "ellipse"}, {"直线", "line"},
        {"箭头", "arrow"}, {"多边形", "polygon"}, {"云线", "cloud"},
    };
    for (const auto& shape : shapeCommands) {
        QAction* action = shapeMenu->addAction(tr(shape.label));
        action->setObjectName(QStringLiteral("edit.shape.") + QString::fromLatin1(shape.id));
        connect(action, &QAction::triggered, this, [this, id = shape.id] {
            if (auto* view = currentView()) {
                view->activateAnnotationTool(QString::fromLatin1(id));
            }
        });
    }
    addShapeButton->setMenu(shapeMenu);
    addShapeButton->setPopupMode(QToolButton::MenuButtonPopup);
    addUnavailableEditCommand(insertEditGroup, "添加文章框");

    auto* linkEditGroup = ribbonBar_->addGroup(contentEditTab, tr("链接"));
    QToolButton* addLinkButton = addRibbonCommand(
        linkEditGroup, "添加链接", "给当前选区添加网址或页面链接",
        ":/icons/bookmark.svg", [this] {
        if (auto* view = currentView()) { view->addLinkToSelectionDialog(); }
    }, true, "edit.addLink");
    auto* addLinkMenu = new QMenu(addLinkButton);
    QAction* webLinkAction = addLinkMenu->addAction(tr("网址链接"));
    connect(webLinkAction, &QAction::triggered, this, [this] {
        if (auto* view = currentView()) { view->addLinkToSelectionDialog(); }
    });
    QAction* pageLinkAction = addLinkMenu->addAction(tr("页面链接"));
    connect(pageLinkAction, &QAction::triggered, this, [this] {
        if (auto* view = currentView()) { view->addInternalLinkToSelectionDialog(); }
    });
    addLinkButton->setMenu(addLinkMenu);
    addLinkButton->setPopupMode(QToolButton::MenuButtonPopup);
    addRibbonCommand(linkEditGroup, "网络链接", "识别全文网址并创建可点击链接",
                     ":/icons/search.svg", [this] {
        if (auto* view = currentView()) { view->detectWebLinks(); }
    }, true, "edit.detectWebLinks");

    auto* navigationEditGroup = ribbonBar_->addGroup(contentEditTab, tr("导航"));
    QToolButton* bookmarkButton = addRibbonCommand(
        navigationEditGroup, "书签", "查看或编辑书签", ":/icons/bookmark.svg", [this] {
        navigationDock_->setVisible(true);
        showNavigationPage(2);
    }, true, "edit.bookmarks");
    auto* bookmarkMenu = new QMenu(bookmarkButton);
    QAction* showBookmarksAction = bookmarkMenu->addAction(tr("打开书签面板"));
    connect(showBookmarksAction, &QAction::triggered, this, [this] {
        navigationDock_->setVisible(true);
        showNavigationPage(2);
    });
    QAction* addBookmarkAction = bookmarkMenu->addAction(tr("添加当前页书签"));
    connect(addBookmarkAction, &QAction::triggered, this, &MainWindow::addCurrentPageBookmark);
    QAction* renameBookmarkAction = bookmarkMenu->addAction(tr("重命名选中书签"));
    connect(renameBookmarkAction, &QAction::triggered, this, &MainWindow::renameSelectedBookmark);
    QAction* deleteBookmarkAction = bookmarkMenu->addAction(tr("删除选中书签"));
    connect(deleteBookmarkAction, &QAction::triggered, this, &MainWindow::deleteSelectedBookmark);
    bookmarkButton->setMenu(bookmarkMenu);
    bookmarkButton->setPopupMode(QToolButton::MenuButtonPopup);
    addUnavailableEditCommand(navigationEditGroup, "自动创建书签");
    addUnavailableEditCommand(navigationEditGroup, "交叉引用");

    auto* attachmentEditGroup = ribbonBar_->addGroup(contentEditTab, tr("附件"));
    addEditTool(attachmentEditGroup, "文件附件", "在页面上添加文件附件",
                ":/icons/annotation.svg", "attachment", "edit.attachment");
    addEditTool(attachmentEditGroup, "图像标注", "在页面上添加图片标注",
                ":/icons/annotation.svg", "imagestamp", "edit.imageAnnotation");
    addEditTool(attachmentEditGroup, "音频和视频", "在页面上添加声音批注",
                ":/icons/annotation.svg", "sound", "edit.sound");
    addUnavailableEditCommand(attachmentEditGroup, "添加 3D");
    // 表单不是占位页：仓库已有 AcroForm 枚举、填写、扁平化和数据交换能力，
    // 这里必须把真实入口接出来。建域/XFA 等尚未具备对象级写入能力的命令仍然
    // 保留在 Ribbon 中（不隐藏），但不能伪装成可用功能。
    const int formTab = ribbonBar_->addTab(tr("表单"));
    const auto showFormsPanel = [this] {
        PdfView* view = currentView();
        if (view == nullptr || navigationPages_ == nullptr || navigationDock_ == nullptr) {
            statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
            return;
        }
        navigationPages_->setCurrentIndex(5);
        updateNavigationHeader(5);
        navigationDock_->show();
        navigationDock_->raise();
        refreshForms(view);
    };

    auto* formRecognitionGroup = ribbonBar_->addGroup(formTab, tr("识别"));
    addRibbonCommand(formRecognitionGroup, "表单自动识别", "扫描并列出文档中的 AcroForm 字段",
                     ":/icons/search.svg", showFormsPanel, true, "form.recognize");
    addRibbonCommand(formRecognitionGroup, "设计助手", "打开表单字段面板",
                     ":/icons/form.svg", showFormsPanel, true, "form.assistant");
    addRibbonCommand(formRecognitionGroup, "工具提示", "查看字段名称、类型和值",
                     ":/icons/form.svg", showFormsPanel, true, "form.tooltip");

    auto* formFieldGroup = ribbonBar_->addGroup(formTab, tr("建域"));
    const struct { const char* label; const char* kind; } fieldCommands[] = {
        {"文本域", "text"}, {"复选框", "checkbox"}, {"单选按钮", "radio"},
        {"组合框", "combo"}, {"列表框", "list"}, {"按钮", "button"},
        {"条形码", "barcode"},
    };
    for (const auto& command : fieldCommands) {
        addRibbonCommand(formFieldGroup, command.label, "在当前页创建 AcroForm 字段",
                         placeholderIcon(tr(command.label)), [this, kind = command.kind] {
            if (auto* view = currentView()) {
                view->createFormFieldDialog(QString::fromLatin1(kind));
            }
        }, true, QStringLiteral("form.create.") + QString::fromLatin1(command.kind));
    }

    auto* formTemplateGroup = ribbonBar_->addGroup(formTab, tr("模板"));
    addRibbonCommand(formTemplateGroup, "页面模板", "表单页面模板正在开发",
                     ":/icons/pages.svg", [] {}, false, "form.pageTemplate");
    addRibbonCommand(formTemplateGroup, "编辑静态 XFA", "PDFium 不提供 XFA 编辑引擎",
                     ":/icons/form.svg", [] {}, false, "form.xfa");
    addRibbonCommand(formTemplateGroup, "计算次序", "重新读取计算后的字段值",
                     ":/icons/form.svg", showFormsPanel, true, "form.calculationOrder");

    auto* formDataGroup = ribbonBar_->addGroup(formTab, tr("数据"));
    addRibbonCommand(formDataGroup, "重置表单", "清空所有可写字段",
                     ":/icons/form.svg", [this] {
        PdfView* view = currentView();
        if (view == nullptr) {
            statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
            return;
        }
        const QVector<pdf::FormField> fields = view->formFields();
        int resetCount = 0;
        for (const pdf::FormField& field : fields) {
            if (!field.readOnly && view->setFormFieldValue(field.pageIndex, field.name, QString())) {
                ++resetCount;
            }
        }
        refreshForms(view);
        statusBar()->showMessage(tr("已重置 %1 个表单字段").arg(resetCount), 3000);
    }, true, "form.reset");
    addRibbonCommand(formDataGroup, "表单到工作表", "把字段和值导出为 Excel 可打开的 CSV",
                     ":/icons/convert.svg", [this] { exportFormCsv(); }, true,
                     "form.toWorksheet");
    addRibbonCommand(formDataGroup, "导入", "从 CSV 或 FDF 导入表单数据",
                     ":/icons/convert.svg", [this] { importFormCsv(); }, true, "form.import");
    addRibbonCommand(formDataGroup, "导出", "把表单数据导出为 CSV",
                     ":/icons/convert.svg", [this] { exportFormCsv(); }, true, "form.export");
    addRibbonCommand(formDataGroup, "JavaScript", "刷新计算、校验和格式化后的字段值",
                     ":/icons/form.svg", showFormsPanel, true, "form.javascript");
    const int signatureTab = ribbonBar_->addTab(tr("电子签章"));
    auto chooseSignature = [this](bool ridingSeam) {
        auto* view = currentView();
        if (view == nullptr) {
            statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
            return;
        }
        SignatureDialog dialog(true, this);
        if (dialog.exec() != QDialog::Accepted) return;
        const StoredSignature signature = dialog.selectedSignature();
        if (!signature.isValid()) return;
        if (ridingSeam) view->applyRidingSeamSignature(signature.image, signature.name);
        else view->activateSignaturePlacement(signature.image, signature.name);
    };
    auto* signingGroup = ribbonBar_->addGroup(signatureTab, tr("签署"));
    addRibbonCommand(signingGroup, "对外签署", "需要电子签平台服务", ":/icons/protect.svg",
                     [] {}, false, "signature.external");
    addRibbonCommand(signingGroup, "对内签署", "需要电子签平台服务", ":/icons/protect.svg",
                     [] {}, false, "signature.internal");
    addRibbonCommand(signingGroup, "邀请他人签署", "需要电子签平台服务", ":/icons/protect.svg",
                     [] {}, false, "signature.invite");
    addRibbonCommand(signingGroup, "完成签署", "保存当前已放置的签章", ":/icons/save.svg",
                     [this] { saveDocument(); }, true, "signature.finish");
    auto* certificateGroup = ribbonBar_->addGroup(signatureTab, tr("数字证书"));
    addRibbonCommand(certificateGroup, "数字签名", "使用 PKCS#12 证书创建 PAdES/CMS 数字签名",
                     ":/icons/protect.svg", [this] { digitallySignDocument(); }, true,
                     "signature.digitalSign");
    addRibbonCommand(certificateGroup, "验证签名", "验证签名完整性、证书链、时间戳和文档改动",
                     ":/icons/search.svg", [this] { verifyDigitalSignatures(); }, true,
                     "signature.verify");
    auto* sealGroup = ribbonBar_->addGroup(signatureTab, tr("签章"));
    addRibbonCommand(sealGroup, "管理签章", "导入、手写或删除本地签章", ":/icons/annotation.svg",
                     [this] { SignatureDialog(false, this).exec(); }, true, "signature.manage");
    addRibbonCommand(sealGroup, "骑缝章", "把签章切分并放到文档每一页右侧", ":/icons/pages.svg",
                     [chooseSignature] { chooseSignature(true); }, true, "signature.seam");
    addRibbonCommand(sealGroup, "签章", "选择本地签章并在页面上放置", ":/icons/ink.svg",
                     [chooseSignature] { chooseSignature(false); }, true, "signature.place");
    auto* signatureFlowGroup = ribbonBar_->addGroup(signatureTab, tr("流程"));
    addRibbonCommand(signatureFlowGroup, "签署流程", "云端签署流程尚未连接", ":/icons/protect.svg",
                     [] {}, false, "signature.workflow");
    addRibbonCommand(signatureFlowGroup, "签署报告", "导出当前文档的批注与签章摘要",
                     ":/icons/annotation.svg", [this] {
        if (auto* view = currentView()) view->exportAnnotationSummary();
    }, true, "signature.report");
    addRibbonCommand(signatureFlowGroup, "签署记录", "打开批注与签章记录", ":/icons/annotation.svg",
                     [this] { navigationDock_->setVisible(true); showNavigationPage(4); },
                     true, "signature.records");
    auto* signatureServiceGroup = ribbonBar_->addGroup(signatureTab, tr("服务"));
    addRibbonCommand(signatureServiceGroup, "分享反馈", "需要电子签平台服务", ":/icons/protect.svg",
                     [] {}, false, "signature.feedback");
    addRibbonCommand(signatureServiceGroup, "了解更多", "需要电子签平台服务", ":/icons/protect.svg",
                     [] {}, false, "signature.learn");
    addRibbonCommand(signatureServiceGroup, "我的额度", "需要电子签平台服务", ":/icons/protect.svg",
                     [] {}, false, "signature.quota");
    addRibbonCommand(signatureServiceGroup, "签署设置", "管理本机签章", ":/icons/protect.svg",
                     [this] { SignatureDialog(false, this).exec(); }, true, "signature.settings");
    const int shareTab = ribbonBar_->addTab(tr("共享"));
    auto* shareSendGroup = ribbonBar_->addGroup(shareTab, tr("发送"));
    addRibbonCommand(shareSendGroup, "邮件", "用默认邮件客户端发送当前 PDF",
                     ":/icons/send.svg", [this] { shareByEmail(); }, true, "share.email");
    addRibbonCommand(shareSendGroup, "OneNote", "需要安装并配置 OneNote 桌面集成",
                     ":/icons/note.svg", [] {}, false, "share.oneNote");

    auto* shareCollaborationGroup = ribbonBar_->addGroup(shareTab, tr("协作"));
    addRibbonCommand(shareCollaborationGroup, "共享协作", "复制当前 PDF 的文件链接",
                     ":/icons/send.svg", [this] { copyDocumentLink(); }, true,
                     "share.copyLink");
    addRibbonCommand(shareCollaborationGroup, "PDF 协作", "导出批注供他人审阅",
                     ":/icons/convert.svg", [this] {
        if (auto* view = shareDocument()) { view->exportAnnotationsXfdf(); }
    }, true, "share.pdfCollaboration");

    auto* shareReviewGroup = ribbonBar_->addGroup(shareTab, tr("审阅"));
    addRibbonCommand(shareReviewGroup, "共享审阅", "导出 XFDF 审阅文件",
                     ":/icons/send.svg", [this] {
        if (auto* view = shareDocument()) { view->exportAnnotationsXfdf(); }
    }, true, "share.exportReview");
    addRibbonCommand(shareReviewGroup, "管理共享审阅", "导入收到的 XFDF 审阅文件",
                     ":/icons/convert.svg", [this] {
        if (auto* view = shareDocument()) { view->importAnnotationsXfdf(); }
    }, true, "share.importReview");
    addRibbonCommand(shareReviewGroup, "文件审阅", "导出可打印和转发的批注摘要",
                     ":/icons/annotation.svg", [this] {
        if (auto* view = shareDocument()) { view->exportAnnotationSummary(); }
    }, true, "share.fileReview");
    addRibbonCommand(shareReviewGroup, "邮件审阅", "用默认邮件客户端发起审阅",
                     ":/icons/send.svg", [this] { shareByEmail(); }, true,
                     "share.emailReview");
    addRibbonCommand(shareReviewGroup, "追踪器", "打开批注面板跟踪审阅状态",
                     ":/icons/annotation.svg", [this] { showReviewTracker(); }, true,
                     "share.tracker");

    auto* shareEnterpriseGroup = ribbonBar_->addGroup(shareTab, tr("企业"));
    for (const char* connector : {"SharePoint", "iManage 9", "iManage 10", "eDOCS DM"}) {
        addRibbonCommand(shareEnterpriseGroup, connector, "需要管理员配置企业内容管理连接器",
                         ":/icons/book.svg", [] {}, false,
                         QStringLiteral("share.enterprise.") +
                             QString::fromLatin1(connector).replace(' ', '_'));
    }
    const int accessibilityTab = ribbonBar_->addTab(tr("辅助工具"));
    auto* accessibilityTagGroup = ribbonBar_->addGroup(accessibilityTab, tr("标签"));
    addRibbonCommand(accessibilityTagGroup, "自动添加文档标签",
                     "需要完整的 PDF 标签树写入支持", ":/icons/protect.svg",
                     [] {}, false, "accessibility.autoTagDocument");
    addRibbonCommand(accessibilityTagGroup, "自动添加表单域标签",
                     "需要完整的 PDF 标签树写入支持", ":/icons/form.svg",
                     [] {}, false, "accessibility.autoTagForms");

    auto* accessibilityCheckGroup = ribbonBar_->addGroup(accessibilityTab, tr("检查"));
    addRibbonCommand(accessibilityCheckGroup, "完整检查",
                     "检查文本层、Unicode 映射和表单说明", ":/icons/search.svg", [this] {
        if (auto* view = currentView()) view->showAccessibilityCheck();
        else statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
    }, true, "accessibility.fullCheck");
    addRibbonCommand(accessibilityCheckGroup, "辅助功能报告",
                     "把辅助功能检查结果保存为 HTML", ":/icons/annotation.svg", [this] {
        if (auto* view = currentView()) view->exportAccessibilityReport();
        else statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
    }, true, "accessibility.report");
    addRibbonCommand(accessibilityCheckGroup, "设置助手",
                     "打开显示、字体和语言设置", ":/icons/tools.svg", [this] {
        if (statusSettingsButton_ != nullptr) statusSettingsButton_->click();
    }, true, "accessibility.settingsAssistant");

    auto* accessibilityReadingGroup = ribbonBar_->addGroup(accessibilityTab, tr("阅读"));
    auto showReadingOrder = [this] {
        if (auto* view = currentView()) view->showReadingOrder();
        else statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
    };
    addRibbonCommand(accessibilityReadingGroup, "阅读选项",
                     "预览当前页的逻辑阅读顺序", ":/icons/book.svg",
                     showReadingOrder, true, "accessibility.readingOptions");
    addRibbonCommand(accessibilityReadingGroup, "阅读顺序",
                     "核对当前页文字的逻辑顺序", ":/icons/pages.svg",
                     showReadingOrder, true, "accessibility.readingOrder");
    addRibbonCommand(accessibilityReadingGroup, "替代文本",
                     "需要 PDF 标签树写入支持", ":/icons/edit.svg",
                     [] {}, false, "accessibility.alternativeText");
    addRibbonCommand(accessibilityReadingGroup, "区域阅读顺序",
                     "需要 PDF 标签树写入支持", ":/icons/pages.svg",
                     [] {}, false, "accessibility.regionReadingOrder");
    addRibbonCommand(accessibilityReadingGroup, "更改区域顺序",
                     "需要 PDF 标签树写入支持", ":/icons/move-down.svg",
                     [] {}, false, "accessibility.changeRegionOrder");
    addRibbonCommand(accessibilityReadingGroup, "应用区域顺序",
                     "需要 PDF 标签树写入支持", ":/icons/save.svg",
                     [] {}, false, "accessibility.applyRegionOrder");
    const int thesisTab = ribbonBar_->addTab(tr("论文工具"));
    auto* thesisCheckGroup = ribbonBar_->addGroup(thesisTab, tr("查重"));
    addRibbonCommand(thesisCheckGroup, "论文查重",
                     "本地检查当前 PDF 内部的重复段落", ":/icons/proofread.svg", [this] {
        if (auto* view = currentView()) view->showThesisCheck();
        else statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
    }, true, "thesis.check");
    addRibbonCommand(thesisCheckGroup, "查重历史",
                     "查看保存在本机的自查记录", ":/icons/annotation.svg", [this] {
        if (auto* view = currentView()) view->showThesisCheckHistory();
        else statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
    }, true, "thesis.history");
    // 文档管理全部使用本地能力：元数据写入 QSettings，文件柜、自动导入和发票归档
    // 只访问用户明确选择的目录；打印、图片导出和合并复用已有 PDF 输出通路。
    const int documentManagementTab = ribbonBar_->addTab(tr("文档管理"));

    auto* documentOrganizeGroup =
        ribbonBar_->addGroup(documentManagementTab, tr("组织"));
    addRibbonCommand(documentOrganizeGroup, "文档概述", "查看当前文档页数、大小和管理属性",
                     ":/icons/outline.svg", [this] { showDocumentOverview(); }, true, "documentManagement.radar");
    addRibbonCommand(documentOrganizeGroup, "标签", "编辑当前文档的标签", ":/icons/bookmark.svg",
                     [this] { editDocumentTags(); }, true, "documentManagement.tags");
    addRibbonCommand(documentOrganizeGroup, "分类", "设置当前文档分类", ":/icons/bookmark.svg",
                     [this] { editDocumentCategory(); }, true, "documentManagement.categories");
    addRibbonCommand(documentOrganizeGroup, "星标", "添加或取消星标", ":/icons/bookmark.svg",
                     [this] { toggleDocumentStar(); }, true, "documentManagement.starred");
    addRibbonCommand(documentOrganizeGroup, "文件柜", "打开当前文档所在文件夹", ":/icons/outline.svg",
                     [this] { openDocumentCabinet(); }, true, "documentManagement.cabinet");

    auto* documentSearchGroup = ribbonBar_->addGroup(documentManagementTab, tr("检索"));
    addRibbonCommand(documentSearchGroup, "自动导入", "监控目录并自动打开新增 PDF", ":/icons/page-insert.svg",
                     [this] { configureAutoImport(); }, true, "documentManagement.autoImport");
    addRibbonCommand(documentSearchGroup, "全文检索", "打开本地全文索引与检索",
                     ":/icons/search.svg", [this] { showNavigationPage(6); }, true,
                     "documentManagement.fullTextSearch");

    auto* invoiceGroup = ribbonBar_->addGroup(documentManagementTab, tr("发票"));
    addRibbonCommand(invoiceGroup, "发票归档", "把当前 PDF 复制到本地发票库", ":/icons/save.svg",
                     [this] { archiveInvoice(); }, true, "documentManagement.invoiceArchive");
    addRibbonCommand(invoiceGroup, "发票管理", "打开本地发票库", ":/icons/outline.svg",
                     [this] { openInvoiceManager(); }, true, "documentManagement.invoiceManager");
    addRibbonCommand(invoiceGroup, "生成报销单", "为已归档发票生成 CSV 清单", ":/icons/form.svg",
                     [this] { createReimbursementList(); }, true, "documentManagement.reimbursement");
    addRibbonCommand(invoiceGroup, "票据打印", "打印当前发票", ":/icons/pages.svg",
                     [this] { if (auto* view = currentView()) view->printDocument(); }, true, "documentManagement.invoicePrint");
    addRibbonCommand(invoiceGroup, "发票转图片", "把当前发票逐页导出为图片", ":/icons/square.svg",
                     [this] { if (auto* view = currentView()) view->exportPageImages(); }, true, "documentManagement.invoiceToImage");
    addRibbonCommand(invoiceGroup, "发票合并", "把另一份发票合并到当前文档", ":/icons/page-insert.svg",
                     [this] { if (auto* view = currentView()) view->mergeDocumentDialog(); }, true, "documentManagement.invoiceMerge");

    auto* documentBatchGroup = ribbonBar_->addGroup(documentManagementTab, tr("批量"));
    addRibbonCommand(documentBatchGroup, "批量重命名", "按统一前缀和序号重命名多个文件",
                     ":/icons/edit.svg", [this] { batchRenameDocuments(); }, true,
                     "documentManagement.batchRename");
    addRibbonCommand(documentBatchGroup, "OFD 批量转换", "调用已安装的 OFD 转换器批量转为 PDF",
                     ":/icons/convert.svg", [this] { batchConvertOfd(); }, true,
                     "documentManagement.ofdConvert");
    // 录制：复用 Windows 自带的录屏能力，声音直接写成 PDF 声音批注；
    // 截图保存当前 Folixa 窗口，不把尚未实现的自有视频编码器伪装成可用功能。
    const int recordTab = ribbonBar_->addTab(tr("录制"));
    auto* recordStartGroup = ribbonBar_->addGroup(recordTab, tr("开始录制"));
    addRibbonCommand(recordStartGroup, "录制中心", "选择录制方式并管理输出文件",
                     ":/icons/app.svg", [this] { showRecordingCenter(); }, true,
                     "record.center");
    addRibbonCommand(recordStartGroup, "PDF 录制", "放映当前 PDF，并打开系统录屏工具",
                     ":/icons/book.svg", [this] { startScreenRecording(true); }, true,
                     "record.pdf");
    addRibbonCommand(recordStartGroup, "屏幕录制", "使用 Windows 游戏栏录制 Folixa 窗口",
                     ":/icons/convert.svg", [this] { startScreenRecording(false); }, true,
                     "record.screen");
    auto* recordCaptureGroup = ribbonBar_->addGroup(recordTab, tr("捕获"));
    addRibbonCommand(recordCaptureGroup, "声音录制", "持续录音并插入当前页声音批注",
                     ":/icons/annotation.svg", [this] {
        if (PdfView* view = currentView()) {
            view->recordSoundAnnotationDialog();
        } else {
            statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
        }
    }, true, "record.audio");
    addRibbonCommand(recordCaptureGroup, "截图", "把当前 Folixa 窗口保存为 PNG 图片",
                     ":/icons/square.svg", [this] { captureWindowScreenshot(); }, true,
                     "record.screenshot");
    auto* recordManageGroup = ribbonBar_->addGroup(recordTab, tr("管理"));
    addRibbonCommand(recordManageGroup, "打开录制文件夹", "查看屏幕录制和截图文件",
                     ":/icons/pages.svg", [this] { openRecordingFolder(); }, true,
                     "record.openFolder");
    addRibbonCommand(recordManageGroup, "录制列表", "在批注面板中查看声音录制",
                     ":/icons/annotation.svg", [this] {
        if (currentView() == nullptr) {
            statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
            return;
        }
        navigationDock_->setVisible(true);
        showNavigationPage(4);
        statusBar()->showMessage(tr("声音录制保存在声音批注中。"), 5000);
    }, true, "record.list");
    // 帮助页不能继续使用整页占位按钮：这些入口不依赖 PDF 编辑能力，应该在
    // 离线安装包里也能给出明确、可执行的结果。商业版的「立即购买 / 激活」不属于
    // 本项目，刻意不创建按钮（而不是创建后禁用），避免误导用户。
    const int helpTab = ribbonBar_->addTab(tr("帮助"));
    auto* helpGroup = ribbonBar_->addGroup(helpTab, tr("帮助"));
    auto openHelpUrl = [this](const QUrl& url) {
        if (!QDesktopServices::openUrl(url)) {
            QMessageBox::warning(this, tr("无法打开链接"),
                                 tr("无法调用系统浏览器，请手动访问：\n%1")
                                     .arg(url.toString()));
        }
    };
    addRibbonCommand(helpGroup, "用户手册", "打开 Folixa 用户手册", ":/icons/book.svg",
                     [this] {
                         QMessageBox manual(this);
                         manual.setWindowTitle(tr("Folixa 用户手册"));
                         manual.setIcon(QMessageBox::Information);
                         manual.setTextFormat(Qt::RichText);
                         manual.setText(tr(
                             "<h3>快速开始</h3>"
                             "<p><b>打开文档</b>：使用“文件 &gt; 打开”，把 PDF 拖入窗口，"
                             "或在命令行中传入文件路径。</p>"
                             "<p><b>阅读</b>：在“主页”调整缩放和旋转；在“视图”切换"
                             "连续、单页或双页布局。</p>"
                             "<p><b>编辑</b>：使用“编辑”“注释”“页面管理”和“表单”"
                             "中的工具。修改后按 Ctrl+S 保存，或使用“另存为”保留原文件。</p>"
                             "<p><b>搜索与 AI</b>：左侧搜索面板用于全文查找；AI 助手只会在"
                             "你主动操作时发送所选文档内容。</p>"
                             "<p><b>获取帮助</b>：遇到问题可打开“帮助中心”或“发表想法”。</p>"));
                         manual.setStandardButtons(QMessageBox::Close);
                         manual.exec();
                     }, true, "help.userManual");
    addRibbonCommand(helpGroup, "帮助中心", "查看常见问题和使用帮助", ":/icons/tools.svg",
                     [openHelpUrl] {
                         openHelpUrl(QUrl(QStringLiteral(
                             "https://github.com/fengjutian/Folixa/issues")));
                     }, true, "help.center");
    addRibbonCommand(helpGroup, "命令行帮助", "查看命令行用法", ":/icons/note.svg", [this] {
        QMessageBox::information(
            this, tr("命令行帮助"),
            tr("用法：\n"
               "  AiPdfEditor.exe [PDF 文件]\n\n"
               "示例：\n"
               "  AiPdfEditor.exe C:\\Documents\\example.pdf\n\n"
               "把 PDF 路径作为第一个参数传入，即可在启动时直接打开文档。"));
    }, true, "help.commandLine");
    addRibbonCommand(helpGroup, "发表想法", "提交建议或问题", ":/icons/send.svg",
                     [openHelpUrl] {
                         openHelpUrl(QUrl(QStringLiteral(
                             "https://github.com/fengjutian/Folixa/issues/new/choose")));
                     }, true, "help.feedback");
    addRibbonCommand(helpGroup, "在线教程", "查看构建与使用教程", ":/icons/book.svg",
                     [openHelpUrl] {
                         openHelpUrl(QUrl(QStringLiteral(
                             "https://github.com/fengjutian/Folixa/blob/main/README.md")));
                     }, true, "help.tutorial");
    addRibbonCommand(helpGroup, "插件管理", "Folixa 当前不支持第三方插件",
                     ":/icons/tools.svg", [] {}, false, "help.plugins");

    auto* aboutGroup = ribbonBar_->addGroup(helpTab, tr("关于"));
    addRibbonCommand(aboutGroup, "默认 PDF 阅读器", "打开 Windows 默认应用设置",
                     ":/icons/book.svg", [this] {
#ifdef Q_OS_WIN
        if (!QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:defaultapps")))) {
            QMessageBox::warning(this, tr("无法打开设置"),
                                 tr("请在 Windows 设置中打开“应用 > 默认应用”，"
                                    "然后为 .pdf 选择 Folixa。"));
        }
#else
        QMessageBox::information(this, tr("默认 PDF 阅读器"),
                                 tr("请在系统设置中把 Folixa 设为 .pdf 文件的默认应用。"));
#endif
    }, true, "help.defaultPdfReader");
    addRibbonCommand(aboutGroup, "插件商店", "Folixa 当前不提供插件商店",
                     ":/icons/tools.svg", [] {}, false, "help.pluginStore");
    addRibbonCommand(aboutGroup, "关于", "查看版本和项目信息", ":/icons/tools.svg", [this] {
        QMessageBox::about(
            this, tr("关于 Folixa"),
            tr("<h3>Folixa · AI PDF Editor %1</h3>"
               "<p>原生、轻量的 PDF 阅读与编辑工具。</p>"
               "<p>基于 Qt 6 与 PDFium 构建。</p>"
               "<p>项目主页：<a href=\"https://github.com/fengjutian/Folixa\">"
               "github.com/fengjutian/Folixa</a></p>")
                .arg(QCoreApplication::applicationVersion()));
    }, true, "help.about");

    auto* productGroup = ribbonBar_->addGroup(helpTab, tr("产品"));
    addRibbonCommand(productGroup, "产品信息", "查看 Folixa 产品信息", ":/icons/note.svg",
                     [this] {
                         QMessageBox::information(
                             this, tr("产品信息"),
                             tr("Folixa %1\n\n"
                                "本地优先的原生 PDF 阅读与编辑器。\n"
                                "支持阅读、批注、页面管理、表单、OCR、全文检索与 AI 辅助。\n\n"
                                "Folixa 不需要购买或激活，也不提供付费订阅。")
                                 .arg(QCoreApplication::applicationVersion()));
                     }, true, "help.productInfo");
    addRibbonCommand(productGroup, "订阅", "Folixa 当前不提供付费订阅",
                     ":/icons/note.svg", [] {}, false, "help.subscription");

    ribbonBar_->setTabOrder({tr("文件"), tr("主页"), tr("转换"), tr("编辑"),
                             tr("页面管理"), tr("注释"), tr("视图"), tr("表单"),
                             tr("保护"), tr("电子签章"), tr("共享"), tr("辅助工具"),
                             tr("论文工具"), tr("文档管理"), tr("录制"), tr("帮助"),
                             tr("AI 助手")});


    navigationDock_ = new QDockWidget(QString(), this);
    texts_.bind([this] { navigationDock_->setWindowTitle(tr("导航")); });
    navigationDock_->setObjectName(QStringLiteral("navigationDock"));
    navigationDock_->setMinimumWidth(52);
    navigationDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    navigationDock_->setTitleBarWidget(new QWidget(navigationDock_));
    auto* navigation = new QWidget(navigationDock_);
    auto* navigationLayout = new QHBoxLayout(navigation);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    navigationLayout->setSpacing(0);
    auto* rail = new QFrame(navigation);
    rail->setObjectName(QStringLiteral("navigationRail"));
    rail->setFixedWidth(52);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(6, 10, 6, 10);
    railLayout->setSpacing(8);
    auto* navigationPages = new QStackedWidget(navigation);
    navigationPages_ = navigationPages;
    // 导航栏按钮与页签一一对应。
    //
    // 两者必须写在同一张表里：v0.7.0 真的踩过一次——
    // 批注页插在搜索页之前，两处列表的下标就错开了，
    // 点「搜索」打开的是批注页，Ctrl+F 也跟着失灵。
    struct RailEntry {
        QString icon;
        const char* tip;
        // 每个按钮给一个独立的对象名：验收脚本靠它定位「目录」「书签」页签，
        // 靠按钮文字（图标按钮没有文字）会随主题改版一起失效。
        QString id;
        QWidget* page = nullptr;
    };
    QVector<RailEntry> railEntries;
    railEntries.append({QStringLiteral(":/icons/pages.svg"),
                         QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "页面"),
                         QStringLiteral("railButton.pages"), createPagesTab()});
    // 目录页与书签页：同一份大纲的两种看法（树 / 扁平列表）。
    railEntries.append({QStringLiteral(":/icons/outline.svg"),
                         QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "目录"),
                         QStringLiteral("railButton.outline"), createOutlineTab()});
    railEntries.append({QStringLiteral(":/icons/bookmark.svg"),
                         QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "书签"),
                         QStringLiteral("railButton.bookmarks"), createBookmarkTab()});
    // 搜索页：全文搜索（v0.5.0）。
    searchPanel_ = new SearchPanel(navigationPages);
    railEntries.append({QStringLiteral(":/icons/search.svg"),
                         QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "搜索"),
                         QStringLiteral("railButton.search"), searchPanel_});
    // 批注页：批注列表（v0.7.0）。
    railEntries.append({QStringLiteral(":/icons/annotation.svg"),
                         QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "批注"),
                         QStringLiteral("railButton.annotations"), createAnnotationTab()});
    // 索引页：本地索引与检索（v0.9.0）。放在最后，页签下标不变。
    indexPanel_ = new IndexPanel(navigationPages);
    // 表单页：AcroForm 字段列表与填写（v1.5.0）。
    railEntries.append({QStringLiteral(":/icons/form.svg"),
                        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "表单"),
                        QStringLiteral("railButton.forms"), createFormTab()});
    railEntries.append({QStringLiteral(":/icons/index.svg"),
                         QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "索引"),
                         QStringLiteral("railButton.index"), indexPanel_});

    // 页签顺序 = 按钮下标：按钮 i 对应第 i 个页签。
    for (const RailEntry& entry : railEntries) {
        navigationPages->addWidget(entry.page);
    }

    auto* railGroup = new QButtonGroup(rail);
    navigationRail_ = railGroup;
    railGroup->setExclusive(true);
    for (int index = 0; index < railEntries.size(); ++index) {
        const RailEntry& entry = railEntries.at(index);
        auto* button = new QToolButton(rail);
        button->setObjectName(entry.id);
        // 样式表靠这个属性选中导航栏按钮：objectName 必须保持
        // 「railButton.<页>」这种带点的形式给 UI Automation 用，
        // 而 QSS 的 #railButton 是精确匹配，带点就匹配不上（按钮会退化成
        // 普通白色按钮），所以另用一个属性做样式钩子。
        button->setProperty("railButton", true);
        button->setIcon(QIcon(entry.icon));
        button->setIconSize(QSize(20, 20));
        texts_.bind([this, button, source = entry.tip] {
            button->setToolTip(tr(source));
        });
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setChecked(index == 0);
        railGroup->addButton(button, index);
        railLayout->addWidget(button);
    }
    railLayout->addStretch();
    connect(railGroup, &QButtonGroup::idClicked, this, [this, navigationPages](int index) {
        const bool wasVisible = navigationPages->isVisible();
        const bool samePage = navigationPages->currentIndex() == index;
        navigationPages->setCurrentIndex(index);
        const bool shouldShow = !wasVisible || !samePage;
        setNavigationExpanded(shouldShow);
        updateNavigationHeader(index);
        traceNavigationPanel(index);
    });
    connect(pagesList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        auto* view = qobject_cast<PdfView*>(tabs_->currentWidget());
        if (view != nullptr) {
            view->goToPage(item->data(Qt::UserRole).toInt());
        }
    });
    connect(searchPanel_, &SearchPanel::navigateToPage, this, [this](int pageIndex) {
        auto* view = qobject_cast<PdfView*>(tabs_->currentWidget());
        if (view != nullptr) {
            view->goToPage(pageIndex);
        }
    });
    // 索引结果 → 跳到那一页并高亮那一块。
    connect(indexPanel_, &IndexPanel::chunkActivated, this,
            [this](int pageIndex, const QString& chunkId) {
                auto* view = qobject_cast<PdfView*>(tabs_->currentWidget());
                if (view != nullptr) {
                    view->showIndexedChunk(pageIndex, chunkId);
                }
            });
    connect(outlineTree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item == nullptr) {
            return;
        }
        navigateToOutlineTarget(item->data(0, Qt::UserRole).toInt(), item->text(0));
    });
    connect(bookmarkList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item == nullptr) {
            return;
        }
        navigateToOutlineTarget(item->data(Qt::UserRole).toInt(),
                               item->data(Qt::UserRole + 1).toString());
    });
    connect(annotationList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item == nullptr) {
            return;
        }
        const int pageIndex = item->data(Qt::UserRole + 1).toInt();
        if (pageIndex < 0) {
            return;
        }
        PdfView* view = currentView();
        if (view != nullptr) {
            view->goToPage(pageIndex);
        }
        trace(QStringLiteral("annotation goto page=%1").arg(pageIndex));
    });
    connect(annotationColorButton_, &QPushButton::clicked, this,
            &MainWindow::pickAnnotationColor);
    connect(annotationApplyAppearanceButton_, &QPushButton::clicked, this,
            &MainWindow::applyAnnotationAppearance);
    connect(annotationReplyButton_, &QPushButton::clicked, this,
            &MainWindow::replyToSelectedAnnotation);
    connect(annotationStateButton_, &QPushButton::clicked, this,
            &MainWindow::setSelectedAnnotationState);
    connect(annotationRemoveButton_, &QPushButton::clicked, this,
            &MainWindow::removeSelectedAnnotation);
    navigationLayout->addWidget(rail);
    auto* pageArea = new QWidget(navigation);
    auto* pageAreaLayout = new QVBoxLayout(pageArea);
    pageAreaLayout->setContentsMargins(0, 0, 0, 0);
    pageAreaLayout->setSpacing(0);
    navigationHeader_ = new QLabel(pageArea);
    navigationHeader_->setObjectName(QStringLiteral("navigationHeader"));
    navigationHeader_->setText(tr("页面"));
    pageAreaLayout->addWidget(navigationHeader_);
    pageAreaLayout->addWidget(navigationPages, 1);
    navigationLayout->addWidget(pageArea, 1);
    navigationDock_->setWidget(navigation);
    addDockWidget(Qt::LeftDockWidgetArea, navigationDock_);
    // 导航面板建好了，占位动作现在才有开关可接。
    navigationPanelAction->setCheckable(true);
    navigationPanelAction->setChecked(true);
    texts_.bind([this, navigationPanelAction] {
        navigationPanelAction->setText(tr("显示导航面板"));
    });
    connect(navigationPanelAction, &QAction::toggled, navigationDock_,
            &QDockWidget::setVisible);
    connect(navigationDock_, &QDockWidget::visibilityChanged, navigationPanelAction,
            &QAction::setChecked);
    // 视图菜单里的开关文案跟着 dock 标题走（Qt 只给一次初值）。
    QAction* navigationToggleAction = navigationDock_->toggleViewAction();
    texts_.bind([this, navigationToggleAction] {
        navigationToggleAction->setText(tr("导航"));
    });
    viewMenu->addAction(navigationToggleAction);

    aiDock_ = new QDockWidget(QString(), this);
    texts_.bind([this] { aiDock_->setWindowTitle(tr("AI 助手")); });
    aiDock_->setObjectName(QStringLiteral("aiDock"));
    aiDock_->setMinimumWidth(280);
    aiDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    // AI 面板（v0.10.0）：三个受限功能 + 流式显示 + 取消 + 费用指标。
    // 面板自己管 Worker 客户端、状态标签与对话区。
    aiPanel_ = new AiPanel(aiDock_);
    aiPanel_->setWorkerClient(workerClient_);
    aiDock_->setWidget(aiPanel_);
    addDockWidget(Qt::RightDockWidgetArea, aiDock_);
    resizeDocks({navigationDock_, aiDock_}, {52, 340}, Qt::Horizontal);
    QAction* aiToggleAction = aiDock_->toggleViewAction();
    texts_.bind([this, aiToggleAction] { aiToggleAction->setText(tr("AI 助手")); });
    viewMenu->addAction(aiToggleAction);

    // AI 回答里的引用 → 跳到那一页并高亮那一块。
    connect(aiPanel_, &AiPanel::citationActivated, this,
            [this](int pageIndex, const QString& chunkId) {
                auto* view = qobject_cast<PdfView*>(tabs_->currentWidget());
                if (view != nullptr) {
                    view->showIndexedChunk(pageIndex, chunkId);
                }
            });

    aiQuickCard_ = new QFrame(tabs_);
    aiQuickCard_->setObjectName(QStringLiteral("aiQuickCard"));
    aiQuickCard_->setFixedWidth(252);
    auto* quickCardLayout = new QVBoxLayout(aiQuickCard_);
    quickCardLayout->setContentsMargins(16, 14, 16, 16);
    quickCardLayout->setSpacing(6);
    auto* quickCardTitle = new QLabel(QStringLiteral("✦  Folixa AI"), aiQuickCard_);
    quickCardTitle->setObjectName(QStringLiteral("aiQuickTitle"));
    quickCardLayout->addWidget(quickCardTitle);
    // 一行上下文说明：AI 动作作用于什么、为什么现在是灰的，都写在这里。
    aiQuickSubtitle_ = new QLabel(QString(), aiQuickCard_);
    aiQuickSubtitle_->setObjectName(QStringLiteral("aiQuickSubtitle"));
    aiQuickSubtitle_->setWordWrap(true);
    quickCardLayout->addWidget(aiQuickSubtitle_);
    quickCardLayout->addSpacing(4);

    struct QuickAction {
        const char* label;
        QString icon;
        const char* tip;
    };
    // 快捷卡是常驻的：名称与提示都带源文案下来，切换语言时就地重译。
    const QuickAction quickActions[] = {
        {QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "开始聊天"),
         QStringLiteral(":/icons/chat.svg"),
         QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "打开完整对话面板，自己提问")},
        {QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "改写"),
         QStringLiteral(":/icons/rewrite.svg"),
         QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "将在后续版本中提供")},
        {QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "校对"),
         QStringLiteral(":/icons/proofread.svg"),
         QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "将在后续版本中提供")},
        {QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "解释"),
         QStringLiteral(":/icons/explain.svg"),
         QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "解释页面上选中的文字")},
        {QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "总结"),
         QStringLiteral(":/icons/summarize.svg"),
         QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "总结当前页")},
    };
    for (int index = 0; index < static_cast<int>(std::size(quickActions)); ++index) {
        const QuickAction& action = quickActions[index];
        auto* button = new QPushButton(QIcon(action.icon), QString(), aiQuickCard_);
        button->setObjectName(QStringLiteral("aiQuickAction"));
        button->setIconSize(QSize(16, 16));
        texts_.bind([this, button, label = action.label, tip = action.tip] {
            button->setText(tr(label));
            button->setToolTip(tr(tip));
        });
        button->setCursor(Qt::PointingHandCursor);
        if (index == 0) {
            // 主操作：整张卡里只有它现在就能用，给一点 AI 辅助色把它拎出来。
            button->setProperty("primary", true);
            connect(button, &QPushButton::clicked, this, [this] {
                aiQuickCard_->hide();
                aiDock_->show();
                aiDock_->raise();
                if (aiPanel_ != nullptr) {
                    aiPanel_->focusPrompt();
                }
            });
        } else if (index == 3 || index == 4) {
            // 「解释」与「总结」已经真的能用了，直接接过去。
            connect(button, &QPushButton::clicked, this, [this, index] {
                aiQuickCard_->hide();
                aiDock_->show();
                aiDock_->raise();
                if (aiPanel_ == nullptr) {
                    return;
                }
                if (index == 4) {
                    aiPanel_->requestSummary();
                } else {
                    aiPanel_->requestExplain();
                }
            });
        } else {
            // 还没实现的动作保持禁用，而不是点了什么也不发生。
            button->setEnabled(false);
        }
        aiQuickButtons_.append(button);
        quickCardLayout->addWidget(button);
    }
    // 浮层投影（设计规范 shadow.md：0,6 / 18 / 12%），让卡片和页面分开。
    auto* cardShadow = new QGraphicsDropShadowEffect(aiQuickCard_);
    cardShadow->setBlurRadius(18);
    cardShadow->setOffset(0, 6);
    cardShadow->setColor(QColor(32, 42, 58, 31));
    aiQuickCard_->setGraphicsEffect(cardShadow);
    aiQuickCard_->hide();
    updateAiQuickActions();
    updateNavigationHeader(navigationPages_ != nullptr ? navigationPages_->currentIndex() : 0);

    aiFloatingButton_ = new QPushButton(QStringLiteral("✦"), tabs_);
    aiFloatingButton_->setObjectName(QStringLiteral("aiFloatingButton"));
    aiFloatingButton_->setFixedSize(44, 44);
    texts_.bind([this] { aiFloatingButton_->setToolTip(tr("AI 快捷操作")); });
    connect(aiFloatingButton_, &QPushButton::clicked, this, [this] {
        const bool show = !aiQuickCard_->isVisible();
        // 每次展开前按当前文档/选区重新算一遍可用状态：
        // 「解释」要有选区，「总结」要有文档，禁用时要写明原因。
        if (show) {
            updateAiQuickActions();
    updateNavigationHeader(navigationPages_ != nullptr ? navigationPages_->currentIndex() : 0);
        }
        aiQuickCard_->setVisible(show);
        positionFloatingUi();
    });
    connect(aiAction, &QAction::triggered, aiFloatingButton_, &QPushButton::click);
    // 切换文档 / 换标签时，卡片上的动作要跟着当前文档与选区走。
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
        if (aiQuickCard_ != nullptr && aiQuickCard_->isVisible()) {
            updateAiQuickActions();
    updateNavigationHeader(navigationPages_ != nullptr ? navigationPages_->currentIndex() : 0);
        }
    });

    auto* focusAction = viewMenu->addAction(QString());
    texts_.bind([this, focusAction] { focusAction->setText(tr("专注模式")); });
    focusAction->setCheckable(true);
    focusAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    connect(focusAction, &QAction::toggled, this, [this](bool enabled) {
        if (enabled) {
            navigationWasVisible_ = navigationDock_->isVisible();
            aiWasVisible_ = aiDock_->isVisible();
            navigationDock_->hide();
            aiDock_->hide();
        } else {
            navigationDock_->setVisible(navigationWasVisible_);
            aiDock_->setVisible(aiWasVisible_);
        }
    });

    QSettings settings;
    // v6：修掉「退出时把面板可见性写成 false」的持久化错误。老配置里的 false
    // 很可能是这个 bug 留下的，不是用户真的想关掉导航栏，所以升版本时重置一次。
    constexpr int kWorkspaceLayoutVersion = 6;
    const bool isNewWorkspaceLayout =
        settings.value(QStringLiteral("workspace/layoutVersion"), 0).toInt() < kWorkspaceLayoutVersion;
    if (isNewWorkspaceLayout) {
        settings.setValue(QStringLiteral("workspace/layoutVersion"), kWorkspaceLayoutVersion);
        settings.setValue(QStringLiteral("workspace/navigationVisible"), true);
        settings.setValue(QStringLiteral("workspace/aiVisible"), false);
    }
    navigationUserVisible_ =
        settings.value(QStringLiteral("workspace/navigationVisible"), true).toBool();
    navigationDock_->setVisible(navigationUserVisible_);
    // AI 面板不在启动时恢复：设计上 AI 默认是右下浮动入口，完整对话由用户主动打开。
    // 另外「面板可见 + Worker 不可用」在启动阶段会让进程直接退出（worker state=unavailable
    // 之后 exit -1），先把这条路径关掉。
    aiDock_->hide();
    navigationPages_->hide();
    QTimer::singleShot(0, this, [this] {
        // 导航栏是核心界面，启动时一律显示：如果这里去「尊重上次关闭状态」，
        // 探针设置里残留的 navigationVisible=false 会让后续验收找不到 rail
        // 按钮，隐藏后面板也会改变画布偏移、拖垮依赖坐标的验收用例。
        // 收起形态的宽度问题由 setNavigationExpanded 负责。
        navigationDock_->show();
        setNavigationExpanded(false);
        positionFloatingUi();
    });
    connect(navigationDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        // 窗口关闭/最小化时 dock 也会收到一次 false，那不是用户的选择。
        if (!isVisible()) {
            return;
        }
        // 欢迎页我们自己把导航栏收起来（QDockWidget 的信号还可能晚一拍才到），
        // 这不算用户关闭，不能写进设置。
        if (navHiddenForWelcome_ && !visible) {
            return;
        }
        navigationUserVisible_ = visible;
        QSettings().setValue(QStringLiteral("workspace/navigationVisible"), visible);
    });
    connect(aiDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (!isVisible()) {
            return;
        }
        QSettings().setValue(QStringLiteral("workspace/aiVisible"), visible);
    });

    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
        auto* view = qobject_cast<PdfView*>(tabs_->currentWidget());
        updatePermissionActionState();
        refreshThumbnails(view);
        // 目录与书签跟着当前文档走。
        bindNavigation(view);
        // 批注列表也跟着当前文档走。
        bindAnnotations(view);
        // 搜索面板跟着当前文档走：换标签页就换控制器。
        if (searchPanel_ != nullptr) {
            searchPanel_->setController(view != nullptr ? view->textController() : nullptr);
        }
        // 索引面板也跟着当前文档走。
        bindIndex(view);
        // AI 面板也要跟着当前文档走：它要拿当前页的文本与选区。
        if (aiPanel_ != nullptr) {
            aiPanel_->setTextSource(view != nullptr ? view->textController() : nullptr);
            aiPanel_->setCurrentPage(view != nullptr ? view->currentPage() : 0);
            // 整篇问答也跟着当前文档走：检索要用它的索引与向量索引。
            aiPanel_->setQaController(view != nullptr ? view->qaController() : nullptr);
        }
    });

    // 底部页码与缩放（右下：第 1 / 12 页，再往右是 − 100% +）。
    // 页码必须挂常驻区：addWidget 的普通控件会被任何 showMessage 临时隐藏，
    // 刚打开文档时「已打开 …」一出现，页码就跟着消失，读屏与验收都读不到。
    auto* pagePrefix = new QLabel(this);
    texts_.bind([this, pagePrefix] { pagePrefix->setText(tr("第")); });
    statusPageEdit_ = new QLineEdit(this);
    statusPageEdit_->setObjectName(QStringLiteral("statusPageInput"));
    statusPageEdit_->setFixedWidth(44);
    statusPageEdit_->setAlignment(Qt::AlignCenter);
    statusPageEdit_->setValidator(new QIntValidator(1, 99999, statusPageEdit_));
    statusPageTotal_ = new QLabel(this);
    statusPageTotal_->setObjectName(QStringLiteral("secondaryText"));
    statusPageTotal_->setText(tr("/ 0 页"));
    connect(statusPageEdit_, &QLineEdit::returnPressed, this, [this] {
        PdfView* view = currentView();
        if (view == nullptr) {
            return;
        }
        bool ok = false;
        const int page = statusPageEdit_->text().toInt(&ok);
        if (ok) {
            view->goToPage(page - 1);
        }
    });

    auto* zoomOutButton = new QToolButton(this);
    zoomOutButton->setObjectName(QStringLiteral("statusZoomOut"));
    zoomOutButton->setText(QStringLiteral("−"));
    auto* zoomInButton = new QToolButton(this);
    zoomInButton->setObjectName(QStringLiteral("statusZoomIn"));
    zoomInButton->setText(QStringLiteral("+"));
    statusZoomLabel_ = new QLabel(QStringLiteral("100%"), this);
    statusZoomLabel_->setObjectName(QStringLiteral("statusZoomLabel"));
    statusZoomLabel_->setMinimumWidth(44);
    statusZoomLabel_->setAlignment(Qt::AlignCenter);
    statusZoomSlider_ = new QSlider(Qt::Horizontal, this);
    statusZoomSlider_->setObjectName(QStringLiteral("statusZoomSlider"));
    statusZoomSlider_->setRange(25, 400);
    statusZoomSlider_->setValue(100);
    statusZoomSlider_->setFixedWidth(110);
    statusDocumentControls_ = new QWidget(this);
    statusDocumentControls_->setObjectName(QStringLiteral("statusDocumentControls"));
    statusDocumentControls_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    auto* statusControlsLayout = new QHBoxLayout(statusDocumentControls_);
    statusControlsLayout->setSizeConstraint(QLayout::SetFixedSize);
    statusControlsLayout->setContentsMargins(0, 0, 0, 0);
    statusControlsLayout->setSpacing(6);
    statusControlsLayout->addWidget(pagePrefix);
    statusControlsLayout->addWidget(statusPageEdit_);
    statusControlsLayout->addWidget(statusPageTotal_);
    statusControlsLayout->addSpacing(8);
    statusControlsLayout->addWidget(zoomOutButton);
    statusControlsLayout->addWidget(statusZoomSlider_);
    statusControlsLayout->addWidget(zoomInButton);
    statusControlsLayout->addWidget(statusZoomLabel_);
    statusBar()->addPermanentWidget(statusDocumentControls_);
    connect(zoomOutButton, &QToolButton::clicked, this, [this] {
        if (auto* view = currentView()) { view->zoomOut(); }
    });
    connect(zoomInButton, &QToolButton::clicked, this, [this] {
        if (auto* view = currentView()) { view->zoomIn(); }
    });
    connect(statusZoomSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (updatingStatusZoom_) {
            return;
        }
        if (auto* view = currentView()) {
            view->setZoom(value / 100.0);
        }
    });
    statusSettingsButton_ = new QToolButton(this);
    statusSettingsButton_->setObjectName(QStringLiteral("statusSettingsButton"));
    texts_.bind([this] {
        statusSettingsButton_->setText(QStringLiteral("⚙"));
        statusSettingsButton_->setToolTip(tr("设置主题、字体和 AI 服务"));
    });
    connect(statusSettingsButton_, &QToolButton::clicked, this, [this] {
        SettingsDialog dialog(this);
        connect(&dialog, &SettingsDialog::aiSettingsRequested, this, [this] {
            if (aiPanel_ != nullptr) {
                aiPanel_->openSettings();
            }
        });
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const QFont selected = dialog.selectedFont();
        qApp->setFont(selected);
        QSettings settings;
        settings.setValue(QStringLiteral("appearance/theme"), QStringLiteral("light"));
        settings.setValue(QStringLiteral("appearance/fontFamily"), selected.family());
        settings.setValue(QStringLiteral("appearance/fontPointSize"), selected.pointSize());
        settings.setValue(QStringLiteral("workspace/navigationVisible"),
                          dialog.navigationVisibleOnStartup());
        settings.setValue(QStringLiteral("workspace/aiVisible"), dialog.aiVisibleOnStartup());
        i18n::LanguageManager::instance().setPreference(dialog.languagePreference());

        navigationUserVisible_ = dialog.navigationVisibleOnStartup();
        if (navigationDock_ != nullptr && !navHiddenForWelcome_) {
            navigationDock_->setVisible(navigationUserVisible_);
        }
        if (aiDock_ != nullptr && currentView() != nullptr) {
            aiDock_->setVisible(dialog.aiVisibleOnStartup());
        }
        statusBar()->showMessage(tr("设置已保存"), 3000);
    });
    // 不注册成 QStatusBar 的 normal widget：showMessage() 会隐藏所有 normal widget。
    // 直接作为子控件固定在左侧，任务提示和设置入口因此可以同时显示。
    statusSettingsButton_->setParent(statusBar());
    statusSettingsButton_->show();
    statusSettingsButton_->raise();
    statusMessageLabel_ = new QLabel(statusBar());
    statusMessageLabel_->setObjectName(QStringLiteral("statusMessageLabel"));
    statusMessageLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    statusMessageLabel_->setTextFormat(Qt::PlainText);
    connect(statusBar(), &QStatusBar::messageChanged, statusMessageLabel_, &QLabel::setText);
    statusMessageLabel_->show();
    statusMessageLabel_->raise();
    positionStatusBarUi();
    statusBar()->clearMessage();
    // 性能指标放在状态栏的常驻区：放工具栏会因为文字宽度变化
    // 触发工具栏溢出，导致画布高度变化、滚动范围漂移。
    metricsLabel_ = new QLabel(QString(), this);
    // 指标文字由文档视图按需发过来；切换语言后让它按新语言再报一次。
    texts_.bind([this] {
        metricsLabel_->setToolTip(tr("页面缓存占用与待完成渲染任务数"));
        if (auto* view = currentView()) {
            view->refreshMetrics();
        } else {
            metricsLabel_->setText(tr("缓存 0 MiB · 待渲染 0"));
        }
    });
    statusBar()->addPermanentWidget(metricsLabel_);
    // 验收探针通过 trace 文件运行；正式用户只有显式开启诊断时才看到指标。
    metricsLabel_->setVisible(qEnvironmentVariableIsSet("AIPDF_DIAGNOSTICS") ||
                              qEnvironmentVariableIsSet("AIPDF_TRACE_FILE") ||
                              QCoreApplication::arguments().contains(QStringLiteral("--diagnostics")));

    // 窗口尺寸放到最后定：布局建好之后才知道自身的尺寸诉求，
    // 在构造开头 resize() 会被后续布局覆盖（窗口比内容还矮，底边直接被裁掉）。
    // 1080p + 125% 缩放的机器上工作区放不下 1440×900，按工作区收一次。
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        const QSize fitted(std::min(1280, available.width() - 40),
                           std::min(720, available.height() - 80));
        resize(fitted);
        move(available.center() - QPoint(fitted.width() / 2, fitted.height() / 2));
    }
    syncTabBar();
}

void MainWindow::syncTabBar() {
    if (tabs_ == nullptr) {
        return;
    }
    const QWidget* page = tabs_->widget(0);
    const bool onlyWelcome = tabs_->count() == 1 && page != nullptr &&
                             page->property("welcomePage").toBool();
    tabs_->tabBar()->setVisible(tabs_->count() > 0 && !onlyWelcome);
    if (ribbonBar_ != nullptr) {
        ribbonBar_->setCompactMode(onlyWelcome);
        if (onlyWelcome) {
            ribbonBar_->setCurrentTab(0);
        }
    }
    if (statusDocumentControls_ != nullptr) {
        statusDocumentControls_->setVisible(!onlyWelcome);
    }
    QTimer::singleShot(0, this, [this] { positionStatusBarUi(); });
    // 设置入口位于状态栏，欢迎页也需要保持可见；没有文档时只隐藏页码与缩放。
    statusBar()->setVisible(true);

    // 欢迎页上没有文档：左侧导航栏与 AI 浮动入口都没有操作对象，
    // 留着只是一排点不出结果的图标。打开第一份文档时再按用户原意恢复。
    if (aiFloatingButton_ != nullptr) {
        aiFloatingButton_->setVisible(!onlyWelcome);
        if (onlyWelcome && aiQuickCard_ != nullptr) {
            aiQuickCard_->hide();
        }
    }
    if (navigationDock_ == nullptr) {
        return;
    }
    if (onlyWelcome) {
        // 用 isHidden() 而不是 isVisible()：窗口还没 show 的时候 dock 已经是
        // 「请求显示但实际不可见」，isVisible() 会给 false，收起就漏掉了。
        if (!navigationDock_->isHidden()) {
            navHiddenForWelcome_ = true;
            navigationDock_->hide();
        }
    } else if (navHiddenForWelcome_) {
        navHiddenForWelcome_ = false;
        navigationDock_->setVisible(navigationUserVisible_);
        // 重新露面时回到「图标轨」形态：只 show 不设宽度，dock 会摊成一条空面板。
        setNavigationExpanded(false);
    }
}

QWidget* MainWindow::createWelcomePage() {
    // 视觉全部在 WelcomePage 里，主窗口只负责给数据和接动作：
    // 打开对话框走 openPdfDialog，最近文档直接交给 openPdfFile。
    auto* page = new WelcomePage(recentFilesFromSettings(), this);
    page->setProperty("welcomePage", true);
    connect(page, &WelcomePage::openPdfRequested, this, &MainWindow::openPdfDialog);
    connect(page, &WelcomePage::recentFileRequested, this, &MainWindow::openPdfFile);
    return page;
}

void MainWindow::rememberRecentFile(const QString& filePath) {
    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    if (absolutePath.isEmpty()) {
        return;
    }
    QStringList files = recentFilesFromSettings();
    files.removeAll(absolutePath);
    files.prepend(absolutePath);
    while (files.size() > kMaxRecentFiles) {
        files.removeLast();
    }
    QSettings().setValue(QStringLiteral("startup/recentFiles"), files);
}

QWidget* MainWindow::createNavigationEmptyState(const char* titleSource,
                                                const char* descriptionSource) {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 28, 18, 28);
    auto* title = new QLabel(QString(), page);
    title->setAlignment(Qt::AlignCenter);
    title->setWordWrap(true);
    auto* description = new QLabel(QString(), page);
    description->setObjectName(QStringLiteral("emptyDescription"));
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    // 占位页是常驻的，切换语言时按传进来的源文案重译。
    texts_.bind([this, title, description, titleSource, descriptionSource] {
        title->setText(tr(titleSource));
        description->setText(tr(descriptionSource));
    });
    layout->addStretch();
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addStretch();
    return page;
}

// 页面页：缩略图列表 + 页面管理按钮（v0.8.0）。
//
// 页面管理放在页面页里，而不是另开一个面板：
// 页面上做什么操作，列表就是操作对象，不用换地方。
QWidget* MainWindow::createPagesTab() {
    auto* page = new QWidget(navigationPages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    pagesList_ = new QListWidget(page);
    pagesList_->setObjectName(QStringLiteral("thumbnailList"));
    pagesList_->setViewMode(QListView::IconMode);
    pagesList_->setResizeMode(QListView::Adjust);
    pagesList_->setMovement(QListView::Static);
    pagesList_->setIconSize(QSize(132, 172));
    pagesList_->setGridSize(QSize(166, 206));
    pagesList_->setSpacing(6);
    layout->addWidget(pagesList_, 1);

    // 按钮给独立的对象名：验收脚本靠 AutomationId 定位。
    //
    // 图标与命令栏共用同一套 SVG：这几个按钮以前只有文字，面板上
    // 六个等宽按钮排成三行，扫一眼分不出「插入」和「删除」。
    auto makeButton = [this, page](const char* textSource,
                                   const QString& icon,
                                   const char* tipSource,
                                   const QString& id) {
        auto* button = new QPushButton(commandIcon(icon), QString(), page);
        button->setObjectName(id);
        texts_.bind([this, button, textSource, tipSource] {
            button->setText(tr(textSource));
            button->setToolTip(tr(tipSource));
        });
        return button;
    };

    pageInsertButton_ = makeButton(
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "插入"), QStringLiteral("page-insert"),
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "在当前页后插入一张空白页"),
        QStringLiteral("page.insert"));
    pageDeleteButton_ = makeButton(
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "删除"), QStringLiteral("page-delete"),
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "删除当前页"),
        QStringLiteral("page.delete"));
    pageRotateRightButton_ = makeButton(
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "右转"),
        QStringLiteral("rotate-right"),
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "把当前页顺时针转 90 度"),
        QStringLiteral("page.rotateRight"));
    pageRotateLeftButton_ = makeButton(
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "左转"),
        QStringLiteral("rotate-left"),
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "把当前页逆时针转 90 度"),
        QStringLiteral("page.rotateLeft"));
    pageMoveUpButton_ = makeButton(
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "上移"), QStringLiteral("move-up"),
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "把当前页往前挪一位"),
        QStringLiteral("page.moveUp"));
    pageMoveDownButton_ = makeButton(
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "下移"), QStringLiteral("move-down"),
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "把当前页往后挪一位"),
        QStringLiteral("page.moveDown"));

    auto* firstRow = new QHBoxLayout();
    firstRow->addWidget(pageInsertButton_);
    firstRow->addWidget(pageDeleteButton_);
    auto* secondRow = new QHBoxLayout();
    secondRow->addWidget(pageRotateRightButton_);
    secondRow->addWidget(pageRotateLeftButton_);
    auto* thirdRow = new QHBoxLayout();
    thirdRow->addWidget(pageMoveUpButton_);
    thirdRow->addWidget(pageMoveDownButton_);
    layout->addLayout(firstRow);
    layout->addLayout(secondRow);
    layout->addLayout(thirdRow);

    connect(pageInsertButton_, &QPushButton::clicked, this, &MainWindow::insertBlankPage);
    connect(pageDeleteButton_, &QPushButton::clicked, this, &MainWindow::deleteCurrentPage);
    connect(pageRotateRightButton_, &QPushButton::clicked, this,
            &MainWindow::rotateCurrentPageRight);
    connect(pageRotateLeftButton_, &QPushButton::clicked, this,
            &MainWindow::rotateCurrentPageLeft);
    connect(pageMoveUpButton_, &QPushButton::clicked, this, &MainWindow::moveCurrentPageUp);
    connect(pageMoveDownButton_, &QPushButton::clicked, this, &MainWindow::moveCurrentPageDown);

    return page;
}

QStackedWidget* MainWindow::createOutlineTab() {
    auto* stack = new QStackedWidget(navigationPages_);
    stack->addWidget(createNavigationEmptyState(
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "暂无文档目录"),
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "当前文档没有大纲（书签）。")));
    outlineTree_ = new QTreeWidget(stack);
    outlineTree_->setObjectName(QStringLiteral("outlineTree"));
    outlineTree_->setHeaderHidden(true);
    outlineTree_->setIndentation(14);
    outlineTree_->setUniformRowHeights(true);
    outlineTree_->setRootIsDecorated(true);
    stack->addWidget(outlineTree_);
    outlineStack_ = stack;
    return stack;
}

QStackedWidget* MainWindow::createBookmarkTab() {
    auto* stack = new QStackedWidget(navigationPages_);
    stack->addWidget(createNavigationEmptyState(
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "暂无书签"),
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "当前文档没有可显示的书签。")));
    bookmarkList_ = new QListWidget(stack);
    bookmarkList_->setObjectName(QStringLiteral("bookmarkList"));
    stack->addWidget(bookmarkList_);
    bookmarkStack_ = stack;
    return stack;
}

// 批注页：批注列表 + 删除按钮（v0.7.0）。
QWidget* MainWindow::createAnnotationTab() {
    // 空状态与列表放堆叠页里，**按钮放堆叠页外面**：
    // 没有批注时按钮也该看得见（灰着就行）。藏在另一个页里的话，用户找不到，
    // 自动化也点不到——之前"批注面板按钮找不到"就是这个原因。
    auto* page = new QWidget(navigationPages_);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* stack = new QStackedWidget(page);
    stack->addWidget(createNavigationEmptyState(
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "暂无批注"),
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow",
                          "选中文字后点「高亮」，或用便签、矩形、墨迹工具添加批注。")));
    annotationList_ = new QListWidget(stack);
    stack->addWidget(annotationList_);
    outer->addWidget(stack, 1);

    auto* content = new QWidget(page);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    annotationList_->setObjectName(QStringLiteral("annotationList"));
    annotationList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // 列表必须留在堆叠页里（空状态 / 列表二选一）。
    // 之前这里又把它 addWidget 到下面的控件列，等于把列表从 stack 里抢走：
    // 堆叠页永远显示空状态，列表被挤成 0 高，批注面板什么都看不到。
    // 外观属性：颜色 / 线宽 / 不透明度。多选后一次改完。
    auto* appearanceRow = new QHBoxLayout();
    appearanceRow->setSpacing(6);
    annotationColorButton_ = new QPushButton(QString(), content);
    texts_.bind([this] { annotationColorButton_->setText(tr("颜色…")); });
    annotationColorButton_->setObjectName(QStringLiteral("annotationColor"));
    appearanceRow->addWidget(annotationColorButton_);
    auto* widthLabel = new QLabel(QString(), content);
    texts_.bind([this, widthLabel] { widthLabel->setText(tr("线宽")); });
    appearanceRow->addWidget(widthLabel);
    annotationWidthSpin_ = new QDoubleSpinBox(content);
    annotationWidthSpin_->setObjectName(QStringLiteral("annotationWidth"));
    annotationWidthSpin_->setRange(0.0, 8.0);
    annotationWidthSpin_->setSingleStep(0.5);
    texts_.bind([this] { annotationWidthSpin_->setSpecialValueText(tr("默认")); });
    appearanceRow->addWidget(annotationWidthSpin_);
    auto* opacityLabel = new QLabel(QString(), content);
    texts_.bind([this, opacityLabel] { opacityLabel->setText(tr("透明")); });
    appearanceRow->addWidget(opacityLabel);
    annotationOpacitySlider_ = new QSlider(Qt::Horizontal, content);
    annotationOpacitySlider_->setObjectName(QStringLiteral("annotationOpacity"));
    annotationOpacitySlider_->setRange(10, 100);
    annotationOpacitySlider_->setValue(100);
    appearanceRow->addWidget(annotationOpacitySlider_, 1);
    layout->addLayout(appearanceRow);
    annotationApplyAppearanceButton_ = new QPushButton(QString(), content);
    texts_.bind([this] {
        annotationApplyAppearanceButton_->setText(tr("应用到所选批注"));
    });
    annotationApplyAppearanceButton_->setObjectName(QStringLiteral("annotationApplyAppearance"));
    layout->addWidget(annotationApplyAppearanceButton_);

    annotationReplyButton_ = new QPushButton(QString(), content);
    texts_.bind([this] { annotationReplyButton_->setText(tr("回复所选")); });
    annotationReplyButton_->setObjectName(QStringLiteral("annotationReply"));
    layout->addWidget(annotationReplyButton_);
    annotationStateButton_ = new QPushButton(QString(), content);
    texts_.bind([this] { annotationStateButton_->setText(tr("设置审阅状态")); });
    annotationStateButton_->setObjectName(QStringLiteral("annotationState"));
    layout->addWidget(annotationStateButton_);
    annotationRemoveButton_ = new QPushButton(QString(), content);
    texts_.bind([this] { annotationRemoveButton_->setText(tr("删除所选批注")); });
    annotationRemoveButton_->setObjectName(QStringLiteral("annotationRemove"));
    layout->addWidget(annotationRemoveButton_);

    outer->addWidget(content);
    annotationStack_ = stack;
    return page;
}

// 表单页：字段列表 + 填值 + 扁平化（v1.5.0）。
QWidget* MainWindow::createFormTab() {
    auto* page = new QWidget(navigationPages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    formStack_ = new QStackedWidget(page);
    formStack_->addWidget(createNavigationEmptyState(
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow", "这份文档没有表单字段"),
        QT_TRANSLATE_NOOP("aipdf::app::MainWindow",
                          "带 AcroForm 的 PDF 会在这里列出可填写的字段。")));
    formTable_ = new QTableWidget(formStack_);
    formTable_->setObjectName(QStringLiteral("formTable"));
    formTable_->setColumnCount(4);
    formTable_->setHorizontalHeaderLabels(QStringList{ tr("页"), tr("名称"), tr("类型"), tr("值") });
    formTable_->horizontalHeader()->setStretchLastSection(true);
    formTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    formTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    formStack_->addWidget(formTable_);
    layout->addWidget(formStack_, 1);

    auto* editor = new QWidget(page);
    auto* editorLayout = new QVBoxLayout(editor);
    editorLayout->setContentsMargins(6, 6, 6, 6);
    editorLayout->setSpacing(6);
    formValueEdit_ = new QLineEdit(editor);
    formValueEdit_->setObjectName(QStringLiteral("formValueInput"));
    formValueEdit_->setPlaceholderText(tr("输入要写入的值"));
    editorLayout->addWidget(formValueEdit_);
    auto* buttons = new QGridLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setHorizontalSpacing(6);
    buttons->setVerticalSpacing(6);
    formApplyButton_ = new QPushButton(tr("写入所选字段"), editor);
    formApplyButton_->setObjectName(QStringLiteral("formApply"));
    formFlattenButton_ = new QPushButton(tr("扁平化本页"), editor);
    formFlattenButton_->setObjectName(QStringLiteral("formFlatten"));
    formPropertiesButton_ = new QPushButton(tr("字段属性…"), editor);
    formPropertiesButton_->setObjectName(QStringLiteral("formProperties"));
    formExportCsvButton_ = new QPushButton(tr("导出 CSV…"), editor);
    formExportCsvButton_->setObjectName(QStringLiteral("formExportCsv"));
    formImportCsvButton_ = new QPushButton(tr("导入 CSV…"), editor);
    formImportCsvButton_->setObjectName(QStringLiteral("formImportCsv"));
    formExportFdfButton_ = new QPushButton(tr("导出 FDF…"), editor);
    formExportFdfButton_->setObjectName(QStringLiteral("formExportFdf"));
    formImportFdfButton_ = new QPushButton(tr("导入 FDF…"), editor);
    formImportFdfButton_->setObjectName(QStringLiteral("formImportFdf"));
    buttons->addWidget(formApplyButton_, 0, 0);
    buttons->addWidget(formFlattenButton_, 0, 1);
    buttons->addWidget(formPropertiesButton_, 1, 0, 1, 2);
    buttons->addWidget(formExportCsvButton_, 2, 0);
    buttons->addWidget(formImportCsvButton_, 2, 1);
    buttons->addWidget(formExportFdfButton_, 3, 0);
    buttons->addWidget(formImportFdfButton_, 3, 1);
    editorLayout->addLayout(buttons);
    layout->addWidget(editor);

    connect(formApplyButton_, &QPushButton::clicked, this, &MainWindow::applyFormValue);
    connect(formFlattenButton_, &QPushButton::clicked, this, &MainWindow::flattenCurrentPageForms);
    const auto editSelectedFormProperties = [this] {
        PdfView* view = currentView();
        if (view == nullptr || formTable_ == nullptr) return;
        const int row = formTable_->currentRow();
        if (row < 0 || formTable_->item(row, 0) == nullptr || formTable_->item(row, 1) == nullptr)
            return;
        const int pageIndex = formTable_->item(row, 0)->text().toInt() - 1;
        const QString name = formTable_->item(row, 1)->text();
        if (view->editFormFieldProperties(pageIndex, name)) refreshForms(view);
    };
    connect(formPropertiesButton_, &QPushButton::clicked, this, editSelectedFormProperties);
    connect(formTable_, &QTableWidget::cellDoubleClicked, this,
            [editSelectedFormProperties](int, int) { editSelectedFormProperties(); });
    connect(formExportCsvButton_, &QPushButton::clicked, this, &MainWindow::exportFormCsv);
    connect(formImportCsvButton_, &QPushButton::clicked, this, &MainWindow::importFormCsv);
    connect(formExportFdfButton_, &QPushButton::clicked, this, [this]() {
        if (auto* view = currentView()) {
            view->exportFormFdfDialog();
        }
    });
    connect(formImportFdfButton_, &QPushButton::clicked, this, [this]() {
        if (auto* view = currentView()) {
            view->importFormFdfDialog();
            refreshForms(view);
        }
    });
    connect(formTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto items = formTable_->selectedItems();
        if (items.isEmpty() || formValueEdit_ == nullptr) {
            return;
        }
        // 第四列是当前值：选中一行就把值带进输入框，省一次手打。
        const int row = items.first()->row();
        if (auto* valueItem = formTable_->item(row, 3)) {
            formValueEdit_->setText(valueItem->text());
        }
    });
    return page;
}

void MainWindow::refreshForms(PdfView* view) {
    if (formTable_ == nullptr || formStack_ == nullptr) {
        return;
    }
    formTable_->setRowCount(0);
    const QVector<pdf::FormField> fields = view != nullptr ? view->formFields() : QVector<pdf::FormField>();
    formStack_->setCurrentIndex(fields.isEmpty() ? 0 : 1);
    formTable_->setRowCount(fields.size());
    for (int row = 0; row < fields.size(); ++row) {
        const pdf::FormField& field = fields.at(row);
        formTable_->setItem(row, 0, new QTableWidgetItem(QString::number(field.pageIndex + 1)));
        formTable_->setItem(row, 1, new QTableWidgetItem(field.name));
        formTable_->setItem(row, 2, new QTableWidgetItem(formFieldTypeName(field.type)));
        formTable_->setItem(row, 3, new QTableWidgetItem(field.value));
    }
    trace(QStringLiteral("panel forms items=%1").arg(fields.size()));
}

QString MainWindow::formFieldTypeName(int type) {
    switch (type) {
        case 1:
            return tr("按钮");
        case 2:
            return tr("复选框");
        case 3:
            return tr("单选");
        case 4:
            return tr("下拉框");
        case 5:
            return tr("列表框");
        case 6:
            return tr("文本框");
        case 7:
            return tr("签名");
        default:
            return tr("未知");
    }
}

void MainWindow::applyFormValue() {
    PdfView* view = currentView();
    if (view == nullptr || formTable_ == nullptr || formValueEdit_ == nullptr) {
        return;
    }
    const auto items = formTable_->selectedItems();
    if (items.isEmpty()) {
        statusBar()->showMessage(tr("先选中一个字段"), 3000);
        return;
    }
    const int row = items.first()->row();
    const QString name = formTable_->item(row, 1) != nullptr ? formTable_->item(row, 1)->text() : QString();
    const int pageIndex = formTable_->item(row, 0) != nullptr
                              ? formTable_->item(row, 0)->text().toInt() - 1
                              : 0;
    if (name.isEmpty()) {
        return;
    }
    if (view->setFormFieldValue(pageIndex, name, formValueEdit_->text())) {
        statusBar()->showMessage(tr("已写入 %1").arg(name), 3000);
        refreshForms(view);
    } else {
        statusBar()->showMessage(tr("写入失败：这个字段可能只读"), 4000);
    }
}

void MainWindow::flattenCurrentPageForms() {
    PdfView* view = currentView();
    if (view == nullptr) {
        return;
    }
    const int page = view->currentPage();
    if (view->flattenFormsOnPage(page)) {
        statusBar()->showMessage(tr("已扁平化第 %1 页的表单").arg(page + 1), 3000);
        refreshForms(view);
    } else {
        statusBar()->showMessage(tr("扁平化失败"), 3000);
    }
}

void MainWindow::exportFormCsv() {
    PdfView* view = currentView();
    if (view == nullptr) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("导出表单数据"), QString(),
                                                      tr("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        statusBar()->showMessage(tr("写不了这个文件"), 3000);
        return;
    }
    file.write(pdf::formValuesToCsv(view->formFields()));
    file.close();
    statusBar()->showMessage(tr("已导出表单数据"), 3000);
}

void MainWindow::importFormCsv() {
    PdfView* view = currentView();
    if (view == nullptr) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("导入表单数据"), QString(),
                                                      tr("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage(tr("读不了这个文件"), 3000);
        return;
    }
    const auto values = pdf::formValuesFromCsv(file.readAll());
    file.close();
    // 按名字匹配现有字段：CSV 里多的字段忽略，少的保持原值。
    const QVector<pdf::FormField> fields = view->formFields();
    int applied = 0;
    for (const pdf::FormValue& value : values) {
        for (const pdf::FormField& field : fields) {
            if (field.name == value.name && !field.readOnly) {
                if (view->setFormFieldValue(field.pageIndex, field.name, value.value)) {
                    ++applied;
                }
                break;
            }
        }
    }
    refreshForms(view);
    statusBar()->showMessage(tr("导入了 %1 个字段").arg(applied), 4000);
}

void MainWindow::bindAnnotations(PdfView* view) {
    AnnotationController* controller =
        view != nullptr ? view->annotationController() : nullptr;
    if (controller == boundAnnotations_) {
        return;
    }
    // 换文档时把上一个控制器的连接断掉，否则刷新会打到别的文档上。
    if (annotationConnection_) {
        QObject::disconnect(annotationConnection_);
        annotationConnection_ = QMetaObject::Connection();
    }
    boundAnnotations_ = controller;
    if (controller != nullptr) {
        annotationConnection_ = connect(controller, &AnnotationController::annotationsChanged,
                                       this, [this]() {
                                           refreshAnnotations(currentView());
                                       });
    }
    refreshAnnotations(view);
    refreshForms(view);
}

// 索引面板跟着当前文档走。
//
// 换文档时旧控制器的连接要断掉：面板自己会断（setController 里处理），
// 这里只要把新的指过去。
void MainWindow::bindIndex(PdfView* view) {
    if (indexPanel_ == nullptr) {
        return;
    }
    indexPanel_->setController(view != nullptr ? view->indexController() : nullptr);
}

void MainWindow::refreshAnnotations(PdfView* view) {
    if (annotationList_ == nullptr) {
        return;
    }
    annotationList_->clear();

    AnnotationController* controller =
        view != nullptr ? view->annotationController() : nullptr;
    const QVector<pdf::Annotation> annotations =
        controller != nullptr ? controller->allAnnotations() : QVector<pdf::Annotation>();

    if (annotationStack_ != nullptr) {
        annotationStack_->setCurrentIndex(annotations.isEmpty() ? 0 : 1);
    }
    if (annotationRemoveButton_ != nullptr) {
        annotationRemoveButton_->setEnabled(!annotations.isEmpty());
    }

    for (const pdf::Annotation& annotation : annotations) {
        auto* item = new QListWidgetItem(annotationList_);
        QString summary = pdf::annotationTypeName(annotation.type);
        if (!annotation.contents.isEmpty()) {
            summary += QStringLiteral(" · ") + annotation.contents;
        }
        item->setText(tr("第 %1 页 · %2").arg(annotation.pageIndex + 1).arg(summary));
        item->setData(Qt::UserRole, static_cast<qulonglong>(annotation.id));
        item->setData(Qt::UserRole + 1, annotation.pageIndex);
        item->setToolTip(tr("跳转到第 %1 页").arg(annotation.pageIndex + 1));
    }
    trace(QStringLiteral("panel annotations items=%1").arg(annotations.size()));
}

void MainWindow::applyAnnotationAppearance() {
    if (annotationList_ == nullptr || boundAnnotations_ == nullptr) {
        return;
    }
    QVector<quint64> ids;
    for (QListWidgetItem* item : annotationList_->selectedItems()) {
        ids.append(static_cast<quint64>(item->data(Qt::UserRole).toULongLong()));
    }
    if (ids.isEmpty()) {
        statusBar()->showMessage(tr("先选中要改的批注"), 3000);
        return;
    }
    const QColor color = annotationPendingColor_;
    const double width = annotationWidthSpin_ != nullptr ? annotationWidthSpin_->value() : 0.0;
    const double opacity =
        annotationOpacitySlider_ != nullptr ? annotationOpacitySlider_->value() / 100.0 : 0.0;
    if (boundAnnotations_->updateAppearance(ids, color, width, opacity)) {
        statusBar()->showMessage(tr("已更新 %1 条批注的外观").arg(ids.size()), 3000);
    }
}

void MainWindow::pickAnnotationColor() {
    const QColor chosen = QColorDialog::getColor(annotationPendingColor_, this, tr("批注颜色"));
    if (chosen.isValid()) {
        annotationPendingColor_ = chosen;
        if (annotationColorButton_ != nullptr) {
            annotationColorButton_->setStyleSheet(
                QStringLiteral("background:%1;color:%2;")
                    .arg(chosen.name(), chosen.lightness() > 128 ? QStringLiteral("#202A3A")
                                                                 : QStringLiteral("#FFFFFF")));
        }
    }
}

void MainWindow::replyToSelectedAnnotation() {
    if (annotationList_ == nullptr || boundAnnotations_ == nullptr) {
        return;
    }
    QListWidgetItem* item = annotationList_->currentItem();
    if (item == nullptr) {
        return;
    }
    const quint64 id = static_cast<quint64>(item->data(Qt::UserRole).toULongLong());
    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(this, tr("回复批注"), tr("回复内容:"),
                                                        QString(), &accepted);
    if (!accepted || text.trimmed().isEmpty()) {
        return;
    }
    const QString author = qEnvironmentVariable("USERNAME");
    if (boundAnnotations_->addReply(id, text, author) == 0) {
        statusBar()->showMessage(tr("回复没有添加成功"), 3000);
        return;
    }
    statusBar()->showMessage(tr("已添加回复"), 3000);
}

void MainWindow::setSelectedAnnotationState() {
    if (annotationList_ == nullptr || boundAnnotations_ == nullptr) {
        return;
    }
    QListWidgetItem* item = annotationList_->currentItem();
    if (item == nullptr) {
        return;
    }
    const quint64 id = static_cast<quint64>(item->data(Qt::UserRole).toULongLong());
    const QStringList states{ QStringLiteral("Accepted"), QStringLiteral("Rejected"),
                              QStringLiteral("Cancelled"), QStringLiteral("Completed"),
                              tr("清除状态") };
    bool accepted = false;
    const QString chosen =
        QInputDialog::getItem(this, tr("审阅状态"), tr("状态:"), states, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    const QString state = (chosen == tr("清除状态")) ? QString() : chosen;
    boundAnnotations_->setReviewState(id, state);
}

void MainWindow::removeSelectedAnnotation() {
    if (annotationList_ == nullptr || boundAnnotations_ == nullptr) {
        return;
    }
    QListWidgetItem* item = annotationList_->currentItem();
    if (item == nullptr) {
        return;
    }
    const quint64 id = static_cast<quint64>(item->data(Qt::UserRole).toULongLong());
    if (boundAnnotations_->removeAnnotation(id)) {
        statusBar()->showMessage(tr("已删除批注"), 3000);
    }
}

PdfView* MainWindow::currentView() const {
    return qobject_cast<PdfView*>(tabs_->currentWidget());
}

// ------------------------------------------------------- 页面管理（v0.8.0）----

void MainWindow::insertBlankPage() {
    auto* view = currentView();
    if (view == nullptr) {
        return;
    }
    view->insertBlankPageAfterCurrent();
}

void MainWindow::deleteCurrentPage() {
    auto* view = currentView();
    if (view == nullptr) {
        return;
    }
    view->deleteCurrentPage();
}

void MainWindow::rotateCurrentPageRight() {
    auto* view = currentView();
    if (view == nullptr) {
        return;
    }
    view->rotateCurrentPageRight();
}

void MainWindow::rotateCurrentPageLeft() {
    auto* view = currentView();
    if (view == nullptr) {
        return;
    }
    view->rotateCurrentPageLeft();
}

void MainWindow::moveCurrentPageUp() {
    auto* view = currentView();
    if (view == nullptr) {
        return;
    }
    view->moveCurrentPageUp();
}

void MainWindow::moveCurrentPageDown() {
    auto* view = currentView();
    if (view == nullptr) {
        return;
    }
    view->moveCurrentPageDown();
}

void MainWindow::updatePageActionState() {
    if (pageInsertButton_ == nullptr) {
        return;
    }
    auto* view = currentView();
    const bool hasDocument = view != nullptr && view->pageCount() > 0;
    const int current = view != nullptr ? view->currentPage() : -1;
    const int count = view != nullptr ? view->pageCount() : 0;
    const bool assemblyAllowed = view == nullptr || view->effectivePermissions().allowAssembly;

    pageInsertButton_->setEnabled(hasDocument && assemblyAllowed);
    pageDeleteButton_->setEnabled(hasDocument && count > 1 && assemblyAllowed);
    pageRotateRightButton_->setEnabled(hasDocument && assemblyAllowed);
    pageRotateLeftButton_->setEnabled(hasDocument && assemblyAllowed);
    pageMoveUpButton_->setEnabled(hasDocument && current > 0 && assemblyAllowed);
    pageMoveDownButton_->setEnabled(hasDocument && current >= 0 && current < count - 1 && assemblyAllowed);
}

void MainWindow::updatePermissionActionState() {
    PdfView* view = currentView();
    const pdf::DocumentPermissions permissions = view != nullptr
        ? view->effectivePermissions() : pdf::DocumentPermissions{};
    for (QToolButton* button : ribbonBar_->findChildren<QToolButton*>()) {
        const QString name = button->objectName();
        if (name.startsWith(QStringLiteral("annotation."))) {
            button->setEnabled(view != nullptr && permissions.allowAnnotations);
        } else if (name.startsWith(QStringLiteral("form.create."))) {
            button->setEnabled(view != nullptr && permissions.allowModifying);
        } else if (name.startsWith(QStringLiteral("form."))) {
            button->setEnabled(view != nullptr && permissions.allowFormFilling);
        } else if (name.startsWith(QStringLiteral("permission.page")) ||
                   name == QStringLiteral("ribbonDuplicate") ||
                   name == QStringLiteral("ribbonSwap") ||
                   name == QStringLiteral("ribbonReverse") ||
                   name == QStringLiteral("ribbonExtractRange") ||
                   name == QStringLiteral("ribbonExtractCurrent") ||
                   name == QStringLiteral("ribbonMerge") ||
                   name == QStringLiteral("ribbonReplace") ||
                   name == QStringLiteral("ribbonCrossMerge") ||
                   name == QStringLiteral("ribbonResize") ||
                   name == QStringLiteral("ribbonSplit")) {
            button->setEnabled(view != nullptr && permissions.allowAssembly);
        } else if (name == QStringLiteral("permission.print")) {
            button->setEnabled(view != nullptr && permissions.allowPrinting);
        } else if (name == QStringLiteral("permission.exportImages") ||
                   name == QStringLiteral("permission.exportText")) {
            button->setEnabled(view != nullptr && permissions.allowCopying);
        } else if (name.startsWith(QStringLiteral("edit.")) ||
                   name == QStringLiteral("ribbonRedaction") ||
                   name == QStringLiteral("ribbonSanitize")) {
            button->setEnabled(view != nullptr && permissions.allowModifying);
        }
    }
    const bool canAnnotate = view != nullptr && permissions.allowAnnotations;
    if (annotationRemoveButton_ != nullptr) annotationRemoveButton_->setEnabled(canAnnotate);
    if (annotationReplyButton_ != nullptr) annotationReplyButton_->setEnabled(canAnnotate);
    if (annotationApplyAppearanceButton_ != nullptr)
        annotationApplyAppearanceButton_->setEnabled(canAnnotate);
    const bool canFill = view != nullptr && permissions.allowFormFilling;
    if (formApplyButton_ != nullptr) formApplyButton_->setEnabled(canFill);
    if (formFlattenButton_ != nullptr)
        formFlattenButton_->setEnabled(view != nullptr && permissions.allowModifying);
    updatePageActionState();
}

// --------------------------------------------------------------- 共享 ----

void MainWindow::shareByEmail() {
    PdfView* view = savedShareDocument();
    if (view == nullptr) {
        return;
    }

    const QFileInfo file(view->filePath());
    if (!file.exists()) {
        QMessageBox::warning(this, tr("邮件发送"), tr("当前 PDF 尚未保存到磁盘。"));
        return;
    }

    // mailto 没有跨邮件客户端通用的附件参数。把文件链接写入正文并同时复制到
    // 剪贴板，用户可以在系统默认邮件客户端中粘贴或拖入该文件。
    const QUrl mail = share::emailUrl(file.absoluteFilePath(), tr("请审阅："),
                                      tr("请审阅随邮件发送的 PDF：\n"));
    QGuiApplication::clipboard()->setText(file.absoluteFilePath());

    if (!QDesktopServices::openUrl(mail)) {
        QMessageBox::warning(this, tr("邮件发送"), tr("无法打开系统默认邮件客户端。"));
        return;
    }
    statusBar()->showMessage(tr("已打开邮件客户端；PDF 路径已复制，可粘贴为附件。"), 5000);
    trace(QStringLiteral("share email path=%1").arg(file.absoluteFilePath()));
}

void MainWindow::copyDocumentLink() {
    PdfView* view = savedShareDocument();
    if (view == nullptr) {
        return;
    }
    const QFileInfo file(view->filePath());
    if (!file.exists()) {
        QMessageBox::warning(this, tr("共享协作"), tr("当前 PDF 尚未保存到磁盘。"));
        return;
    }
    const QString link = share::fileLink(file.absoluteFilePath());
    QGuiApplication::clipboard()->setText(link);
    statusBar()->showMessage(tr("已复制当前 PDF 的文件链接。"), 3000);
    trace(QStringLiteral("share copy link path=%1").arg(file.absoluteFilePath()));
}

void MainWindow::showReviewTracker() {
    if (shareDocument() == nullptr) {
        return;
    }
    navigationDock_->setVisible(true);
    showNavigationPage(4);
    trace(QStringLiteral("share open review tracker"));
}

PdfView* MainWindow::shareDocument() {
    PdfView* view = currentView();
    if (view == nullptr) {
        QMessageBox::information(this, tr("共享"), tr("请先打开一份 PDF。"));
    }
    return view;
}

void MainWindow::startScreenRecording(bool presentPdf) {
    if (presentPdf) {
        PdfView* view = currentView();
        if (view == nullptr) {
            statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
            return;
        }
        view->startPresentation();
    }
#ifdef Q_OS_WIN
    if (!QDesktopServices::openUrl(QUrl(QStringLiteral("ms-gamingoverlay:")))) {
        QMessageBox::warning(
            this, tr("屏幕录制"),
            tr("无法打开 Windows 游戏栏。请在“设置 > 游戏 > Xbox Game Bar”中启用它，"
               "或按 Win+Alt+R 开始录制。"));
        return;
    }
    statusBar()->showMessage(
        tr("录制面板已打开；按 Win+Alt+R 开始或停止，视频保存在“视频\\捕获”。"), 10000);
#else
    QMessageBox::information(this, tr("屏幕录制"),
                             tr("请使用系统自带的屏幕录制工具录制 Folixa 窗口。"));
#endif
}

void MainWindow::captureWindowScreenshot() {
    const QString suggested =
        QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
            .filePath(QStringLiteral("Folixa-%1.png")
                          .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存截图"), suggested, tr("PNG 图片 (*.png)"));
    if (path.isEmpty()) return;
    if (!grab().save(path, "PNG")) {
        QMessageBox::warning(this, tr("保存截图"), tr("截图无法写入所选位置。"));
        return;
    }
    statusBar()->showMessage(tr("截图已保存到 %1").arg(QDir::toNativeSeparators(path)), 8000);
}

void MainWindow::openRecordingFolder() {
    QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                       .filePath(QStringLiteral("Captures"));
    if (!QDir(path).exists()) {
        path = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(this, tr("录制文件夹"),
                             tr("无法打开文件夹：%1").arg(QDir::toNativeSeparators(path)));
    }
}

void MainWindow::showRecordingCenter() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("录制中心"));
    dialog.setMinimumSize(720, 520);
    dialog.setObjectName(QStringLiteral("recordingCenter"));
    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(28, 24, 28, 22);
    root->setSpacing(18);

    auto* title = new QLabel(tr("录制与捕获"), &dialog);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 7);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);
    auto* intro = new QLabel(
        tr("录制 PDF 演示或应用窗口、创建持续声音批注，并集中查看输出文件。"), &dialog);
    intro->setStyleSheet(QStringLiteral("color:#667085;"));
    root->addWidget(intro);

    auto* actions = new QGridLayout();
    actions->setHorizontalSpacing(12);
    actions->setVerticalSpacing(12);
    struct ActionCard { const char* title; const char* detail; };
    const ActionCard cards[] = {
        {"PDF 演示录制", "进入放映并打开系统录屏；适合课程、汇报与批注讲解"},
        {"窗口屏幕录制", "录制 Folixa 操作过程；Win+Alt+R 可快速开始或停止"},
        {"声音批注", "持续录制麦克风，并把 WAV 音频嵌入当前 PDF 页面"},
        {"窗口截图", "捕获当前 Folixa 窗口并保存为 PNG 图片"},
    };
    for (int i = 0; i < 4; ++i) {
        auto* button = new QPushButton(&dialog);
        button->setMinimumHeight(82);
        button->setText(tr(cards[i].title) + QStringLiteral("\n") + tr(cards[i].detail));
        button->setStyleSheet(QStringLiteral(
            "QPushButton{text-align:left;padding:14px 16px;border:1px solid #d8deea;"
            "border-radius:10px;background:white;color:#182033;font-weight:600;}"
            "QPushButton:hover{border-color:#7657e8;background:#f7f5ff;}"));
        actions->addWidget(button, i / 2, i % 2);
        if (i == 0) connect(button, &QPushButton::clicked, &dialog, [this, &dialog] {
            dialog.accept(); startScreenRecording(true);
        });
        if (i == 1) connect(button, &QPushButton::clicked, &dialog, [this, &dialog] {
            dialog.accept(); startScreenRecording(false);
        });
        if (i == 2) connect(button, &QPushButton::clicked, &dialog, [this, &dialog] {
            if (PdfView* view = currentView()) { dialog.hide(); view->recordSoundAnnotationDialog(); dialog.show(); }
            else statusBar()->showMessage(tr("请先打开一份 PDF。"), 3000);
        });
        if (i == 3) connect(button, &QPushButton::clicked, &dialog, [this, &dialog] {
            dialog.hide(); captureWindowScreenshot(); dialog.show();
        });
    }
    root->addLayout(actions);

    auto* recentTitle = new QLabel(tr("最近输出"), &dialog);
    recentTitle->setStyleSheet(QStringLiteral("font-weight:700;margin-top:6px;"));
    root->addWidget(recentTitle);
    auto* recent = new QListWidget(&dialog);
    recent->setMinimumHeight(130);
    QStringList roots = {
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).filePath("Captures")
    };
    QFileInfoList files;
    for (const QString& directory : roots) {
        QDir dir(directory);
        files.append(dir.entryInfoList({QStringLiteral("Folixa-*.png"), QStringLiteral("*.mp4")},
                                       QDir::Files, QDir::Time));
    }
    std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.lastModified() > b.lastModified();
    });
    for (int i = 0; i < qMin(8, files.size()); ++i) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1    %2").arg(files[i].fileName(),
                files[i].lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"))), recent);
        item->setData(Qt::UserRole, files[i].absoluteFilePath());
    }
    if (recent->count() == 0) recent->addItem(tr("暂无截图或视频输出"));
    connect(recent, &QListWidget::itemDoubleClicked, &dialog, [](QListWidgetItem* item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    root->addWidget(recent);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* folderButton = buttons->addButton(tr("打开录制文件夹"), QDialogButtonBox::ActionRole);
    connect(folderButton, &QPushButton::clicked, this, &MainWindow::openRecordingFolder);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);
    dialog.exec();
}

void MainWindow::batchRenameDocuments() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("选择要批量重命名的文件"), QString(),
        tr("文档 (*.pdf *.ofd);;PDF 文件 (*.pdf);;所有文件 (*)"));
    if (files.isEmpty()) return;

    bool accepted = false;
    const QString prefix = QInputDialog::getText(
        this, tr("批量重命名"), tr("文件名前缀:"), QLineEdit::Normal,
        tr("文档"), &accepted).trimmed();
    if (!accepted || prefix.isEmpty()) return;

    const BatchRenameResult result = batchRenameFiles(files, prefix);
    if (!result.ok) {
        QMessageBox::warning(this, tr("批量重命名失败"), result.error);
        return;
    }
    statusBar()->showMessage(tr("已重命名 %1 个文件").arg(result.renamedPaths.size()), 5000);
}

void MainWindow::showDocumentOverview() {
    PdfView* view = currentView();
    if (view == nullptr || view->filePath().isEmpty()) {
        statusBar()->showMessage(tr("请先打开并保存一份 PDF。"), 3000);
        return;
    }
    const QFileInfo info(view->filePath());
    QSettings settings;
    const QString key = documentSettingsKey(view->filePath());
    const QString tags = settings.value(key + QStringLiteral("/tags")).toString();
    const QString category = settings.value(key + QStringLiteral("/category")).toString();
    const bool starred = settings.value(key + QStringLiteral("/starred"), false).toBool();
    QMessageBox::information(
        this, tr("文档概述"),
        tr("文件：%1\n页数：%2\n大小：%3 KB\n修改时间：%4\n分类：%5\n标签：%6\n星标：%7")
            .arg(info.fileName()).arg(view->pageCount()).arg((info.size() + 1023) / 1024)
            .arg(QLocale().toString(info.lastModified(), QLocale::ShortFormat))
            .arg(category.isEmpty() ? tr("未分类") : category)
            .arg(tags.isEmpty() ? tr("无") : tags)
            .arg(starred ? tr("是") : tr("否")));
}

void MainWindow::editDocumentTags() {
    PdfView* view = currentView();
    if (view == nullptr || view->filePath().isEmpty()) {
        statusBar()->showMessage(tr("请先打开并保存一份 PDF。"), 3000);
        return;
    }
    QSettings settings;
    const QString setting = documentSettingsKey(view->filePath()) + QStringLiteral("/tags");
    bool accepted = false;
    const QString value = QInputDialog::getText(this, tr("文档标签"), tr("标签（用逗号分隔）："),
                                                QLineEdit::Normal, settings.value(setting).toString(), &accepted);
    if (accepted) {
        settings.setValue(setting, value.trimmed());
        statusBar()->showMessage(tr("文档标签已保存。"), 3000);
    }
}

void MainWindow::editDocumentCategory() {
    PdfView* view = currentView();
    if (view == nullptr || view->filePath().isEmpty()) {
        statusBar()->showMessage(tr("请先打开并保存一份 PDF。"), 3000);
        return;
    }
    QSettings settings;
    const QString setting = documentSettingsKey(view->filePath()) + QStringLiteral("/category");
    bool accepted = false;
    const QString value = QInputDialog::getText(this, tr("文档分类"), tr("分类名称："),
                                                QLineEdit::Normal, settings.value(setting).toString(), &accepted);
    if (accepted) {
        settings.setValue(setting, value.trimmed());
        statusBar()->showMessage(tr("文档分类已保存。"), 3000);
    }
}

void MainWindow::toggleDocumentStar() {
    PdfView* view = currentView();
    if (view == nullptr || view->filePath().isEmpty()) {
        statusBar()->showMessage(tr("请先打开并保存一份 PDF。"), 3000);
        return;
    }
    QSettings settings;
    const QString setting = documentSettingsKey(view->filePath()) + QStringLiteral("/starred");
    const bool starred = !settings.value(setting, false).toBool();
    settings.setValue(setting, starred);
    statusBar()->showMessage(starred ? tr("已添加星标。") : tr("已取消星标。"), 3000);
}

void MainWindow::openDocumentCabinet() {
    PdfView* view = currentView();
    const QString directory = view != nullptr && !view->filePath().isEmpty()
                                  ? QFileInfo(view->filePath()).absolutePath()
                                  : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
        QMessageBox::warning(this, tr("文件柜"), tr("无法打开目录：%1").arg(QDir::toNativeSeparators(directory)));
    }
}

void MainWindow::configureAutoImport() {
    const QString initial = QSettings().value(QStringLiteral("documentManagement/autoImportDirectory")).toString();
    const QString directory = QFileDialog::getExistingDirectory(this, tr("选择自动导入目录"), initial);
    if (directory.isEmpty()) return;
    QSettings().setValue(QStringLiteral("documentManagement/autoImportDirectory"), directory);
    if (autoImportWatcher_ == nullptr) {
        autoImportWatcher_ = new QFileSystemWatcher(this);
        connect(autoImportWatcher_, &QFileSystemWatcher::directoryChanged,
                this, &MainWindow::importWatchedDirectory);
    }
    const QStringList watched = autoImportWatcher_->directories();
    if (!watched.isEmpty()) autoImportWatcher_->removePaths(watched);
    autoImportWatcher_->addPath(directory);
    importWatchedDirectory(directory);
    statusBar()->showMessage(tr("正在监控自动导入目录：%1").arg(QDir::toNativeSeparators(directory)), 5000);
}

void MainWindow::importWatchedDirectory(const QString& directory) {
    QDir dir(directory);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.pdf"), QStringLiteral("*.PDF")},
                                                   QDir::Files, QDir::Time);
    QStringList imported = QSettings().value(QStringLiteral("documentManagement/autoImportedFiles")).toStringList();
    bool hasUnstableFiles = false;
    for (const QFileInfo& info : files) {
        const QString path = info.canonicalFilePath();
        if (path.isEmpty()) continue;
        const QString fingerprint = QStringLiteral("%1|%2|%3")
                                        .arg(path).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
        if (imported.contains(fingerprint, Qt::CaseInsensitive)) continue;
        // 目录变更信号经常在大文件仍在复制时到达。连续两次扫描大小一致后再打开，
        // 避免把半截 PDF 记成“已导入”而永远不再重试。
        if (!autoImportSizes_.contains(path) || autoImportSizes_.value(path) != info.size()) {
            autoImportSizes_.insert(path, info.size());
            hasUnstableFiles = true;
            continue;
        }
        openPdfFile(path);
        imported.prepend(fingerprint);
        autoImportSizes_.remove(path);
    }
    while (imported.size() > 500) imported.removeLast();
    QSettings().setValue(QStringLiteral("documentManagement/autoImportedFiles"), imported);
    if (hasUnstableFiles) {
        QTimer::singleShot(1200, this, [this, directory] { importWatchedDirectory(directory); });
    }
}

void MainWindow::archiveInvoice() {
    PdfView* view = savedShareDocument();
    if (view == nullptr) return;
    const QFileInfo source(view->filePath());
    QString target = QDir(invoiceArchiveDirectory()).filePath(source.fileName());
    if (QFileInfo::exists(target)) {
        target = QDir(invoiceArchiveDirectory()).filePath(
            source.completeBaseName() + QDateTime::currentDateTime().toString(QStringLiteral("-yyyyMMdd-HHmmss.pdf")));
    }
    if (!QFile::copy(source.absoluteFilePath(), target)) {
        QMessageBox::warning(this, tr("发票归档"), tr("无法复制发票到归档目录。"));
        return;
    }
    statusBar()->showMessage(tr("发票已归档：%1").arg(QDir::toNativeSeparators(target)), 6000);
}

void MainWindow::openInvoiceManager() {
    const QString directory = invoiceArchiveDirectory();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
        QMessageBox::warning(this, tr("发票管理"), tr("无法打开发票归档目录。"));
}

void MainWindow::createReimbursementList() {
    const QDir archive(invoiceArchiveDirectory());
    const QFileInfoList invoices = archive.entryInfoList({QStringLiteral("*.pdf")}, QDir::Files, QDir::Name);
    if (invoices.isEmpty()) {
        QMessageBox::information(this, tr("生成报销单"), tr("发票库中还没有已归档的 PDF。"));
        return;
    }
    const QString suggested = archive.filePath(QStringLiteral("报销单-%1.csv").arg(QDate::currentDate().toString("yyyyMMdd")));
    const QString path = QFileDialog::getSaveFileName(this, tr("保存报销单"), suggested, tr("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("生成报销单"), tr("无法写入所选文件。"));
        return;
    }
    output.write("\xEF\xBB\xBF");
    output.write(QStringLiteral("序号,发票文件,归档时间,金额,备注\r\n").toUtf8());
    for (int i = 0; i < invoices.size(); ++i) {
        const QFileInfo& invoice = invoices.at(i);
        output.write(QStringLiteral("%1,%2,%3,,\r\n").arg(i + 1)
                         .arg(csvCell(invoice.fileName()), csvCell(invoice.lastModified().toString(Qt::ISODate))).toUtf8());
    }
    if (!output.commit()) {
        QMessageBox::warning(this, tr("生成报销单"), tr("保存报销单失败。"));
        return;
    }
    statusBar()->showMessage(tr("已生成包含 %1 张发票的报销单。 ").arg(invoices.size()), 5000);
}

void MainWindow::digitallySignDocument() {
    PdfView* view = savedShareDocument();
    if (view == nullptr) return;
    if (DigitalSignatureBackend::findPyHanko().isEmpty()) {
        QMessageBox::information(
            this, tr("缺少数字签名引擎"),
            tr("数字签名需要 pyHanko CLI。请安装 pyhanko-cli，并确保 pyhanko 位于 PATH 中。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("数字证书签名"));
    auto* form = new QFormLayout(&dialog);
    auto* certificate = new QLineEdit(&dialog);
    auto* chooseCertificate = new QPushButton(tr("选择…"), &dialog);
    auto* certificateRow = new QWidget(&dialog);
    auto* certificateLayout = new QHBoxLayout(certificateRow);
    certificateLayout->setContentsMargins(0, 0, 0, 0);
    certificateLayout->addWidget(certificate, 1);
    certificateLayout->addWidget(chooseCertificate);
    auto* password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    auto* timestamp = new QLineEdit(&dialog);
    timestamp->setPlaceholderText(tr("可选，例如 https://tsa.example.com"));
    auto* validation = new QCheckBox(tr("嵌入证书链与吊销信息（PAdES-LT）"), &dialog);
    form->addRow(tr("PKCS#12 证书"), certificateRow);
    form->addRow(tr("证书密码"), password);
    form->addRow(tr("RFC 3161 时间戳"), timestamp);
    form->addRow(QString(), validation);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    form->addRow(buttons);
    connect(chooseCertificate, &QPushButton::clicked, &dialog, [this, certificate] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("选择数字证书"), QString(), tr("PKCS#12 证书 (*.p12 *.pfx)"));
        if (!path.isEmpty()) certificate->setText(path);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || certificate->text().trimmed().isEmpty()) return;

    const QFileInfo input(view->filePath());
    const QString suggested = input.dir().filePath(input.completeBaseName() + QStringLiteral("-signed.pdf"));
    const QString output = QFileDialog::getSaveFileName(this, tr("保存已签名文档"), suggested,
                                                        tr("PDF 文件 (*.pdf)"));
    if (output.isEmpty()) return;
    QTemporaryFile passFile(QDir::temp().filePath(QStringLiteral("folixa-sign-pass-XXXXXX")));
    passFile.setAutoRemove(true);
    if (!passFile.open()) {
        QMessageBox::warning(this, tr("数字签名"), tr("无法创建临时密码文件。"));
        return;
    }
    passFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    passFile.write(password->text().toUtf8());
    passFile.write("\n");
    passFile.flush();

    const SignatureCommand command = DigitalSignatureBackend::signPkcs12(
        view->filePath(), output, certificate->text().trimmed(), passFile.fileName(),
        timestamp->text().trimmed(), validation->isChecked());
    if (!command.isValid()) {
        QMessageBox::warning(this, tr("数字签名"), command.error);
        return;
    }
    QProgressDialog progress(tr("正在创建数字签名…"), tr("取消"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(command.program, command.arguments);
    if (!process.waitForStarted(5000)) {
        QMessageBox::warning(this, tr("数字签名"), tr("无法启动 pyHanko。"));
        return;
    }
    while (process.state() != QProcess::NotRunning && !progress.wasCanceled()) {
        process.waitForFinished(100);
        QApplication::processEvents();
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
    }
    QString report = QString::fromUtf8(process.readAll()).trimmed();
    if (progress.wasCanceled()) {
        QFile::remove(output);
        statusBar()->showMessage(tr("数字签名已取消。"), 4000);
        return;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
        !QFileInfo(output).isFile() || QFileInfo(output).size() == 0) {
        QFile::remove(output);
        QMessageBox::warning(this, tr("数字签名失败"), report.isEmpty() ? tr("pyHanko 未生成签名文件。") : report);
        return;
    }
    statusBar()->showMessage(tr("数字签名完成：%1").arg(QDir::toNativeSeparators(output)), 7000);
}

void MainWindow::verifyDigitalSignatures() {
    QString input;
    if (PdfView* view = currentView()) input = view->filePath();
    if (input.isEmpty())
        input = QFileDialog::getOpenFileName(this, tr("选择待验证 PDF"), QString(), tr("PDF 文件 (*.pdf)"));
    if (input.isEmpty()) return;
    const SignatureCommand command = DigitalSignatureBackend::verify(input);
    if (!command.isValid()) {
        QMessageBox::information(this, tr("无法验证签名"),
                                 command.error + tr("\n请安装 pyhanko-cli，并确保 pyhanko 位于 PATH 中。"));
        return;
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(command.program, command.arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(120000)) {
        process.kill();
        QMessageBox::warning(this, tr("验证签名"), tr("签名验证进程启动失败或超时。"));
        return;
    }
    const QString report = QString::fromUtf8(process.readAll()).trimmed();
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        QMessageBox::information(this, tr("签名验证通过"), report);
    } else {
        QMessageBox::warning(this, tr("签名验证未通过"), report.isEmpty() ? tr("没有发现有效数字签名。") : report);
    }
}

void MainWindow::convertDocument(ConversionKind kind) {
    QString input;
    QString filter;
    QString suffix;
    if (kind == ConversionKind::OfficeToPdf) {
        filter = tr("Office 文档 (*.doc *.docx *.xls *.xlsx *.ppt *.pptx *.odt *.ods *.odp)");
    } else if (kind == ConversionKind::HtmlToPdf) {
        filter = tr("HTML 文档 (*.html *.htm)");
    } else {
        PdfView* view = savedShareDocument();
        if (view == nullptr) return;
        input = view->filePath();
        suffix = kind == ConversionKind::PdfA2b ? QStringLiteral("-pdfa.pdf")
                                                : QStringLiteral("-optimized.pdf");
    }
    if (input.isEmpty()) {
        input = QFileDialog::getOpenFileName(this, tr("选择要转换的文件"), QString(), filter);
        if (input.isEmpty()) return;
        suffix = QStringLiteral(".pdf");
    }

    const QFileInfo inputInfo(input);
    const QString suggested = inputInfo.dir().filePath(inputInfo.completeBaseName() + suffix);
    const QString output = QFileDialog::getSaveFileName(this, tr("保存转换结果"), suggested,
                                                        tr("PDF 文件 (*.pdf)"));
    if (output.isEmpty()) return;
    if (QFileInfo(input).absoluteFilePath().compare(QFileInfo(output).absoluteFilePath(),
                                                    Qt::CaseInsensitive) == 0) {
        QMessageBox::warning(this, tr("转换"), tr("输出文件不能覆盖输入文件，请选择其他名称。"));
        return;
    }

    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        QMessageBox::warning(this, tr("转换"), tr("无法创建安全的临时目录。"));
        return;
    }
    const QString temporaryOutput = QDir(temporary.path()).filePath(QStringLiteral("converted.pdf"));
    ConversionPlan plan = DocumentConverter::plan(kind, input, temporaryOutput);
    if (!plan.isValid()) {
        QMessageBox::information(
            this, tr("缺少转换引擎"),
            tr("%1\n\nOffice/HTML 转换需要 LibreOffice；PDF/A 和优化压缩需要 Ghostscript。"
               "安装后重新执行即可，Folixa 不会上传文档。").arg(plan.error));
        return;
    }

    QProgressDialog progress(tr("正在使用 %1 转换…").arg(plan.engineName), tr("取消"), 0, 0, this);
    progress.setWindowTitle(tr("文档转换"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(plan.program, plan.arguments);
    if (!process.waitForStarted(5000)) {
        QMessageBox::warning(this, tr("转换失败"),
                             tr("无法启动 %1：%2").arg(plan.engineName, process.errorString()));
        return;
    }
    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() != QProcess::NotRunning && elapsed.elapsed() < 10 * 60 * 1000 &&
           !progress.wasCanceled()) {
        process.waitForFinished(100);
        QApplication::processEvents();
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
    }
    if (progress.wasCanceled()) {
        statusBar()->showMessage(tr("转换已取消。"), 4000);
        return;
    }
    const QByteArray diagnostics = process.readAll();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
        !QFileInfo(plan.expectedOutput).isFile() || QFileInfo(plan.expectedOutput).size() == 0) {
        QString detail = QString::fromLocal8Bit(diagnostics).trimmed();
        if (detail.size() > 1200) detail = detail.right(1200);
        QMessageBox::warning(this, tr("转换失败"),
                             tr("%1 返回了错误。\n%2").arg(plan.engineName, detail));
        return;
    }

    QFile source(plan.expectedOutput);
    QSaveFile destination(output);
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("转换失败"), tr("无法写入输出文件。"));
        return;
    }
    while (!source.atEnd()) {
        const QByteArray block = source.read(1024 * 1024);
        if (block.isEmpty() && source.error() != QFileDevice::NoError) break;
        if (destination.write(block) != block.size()) break;
    }
    if (source.error() != QFileDevice::NoError || !destination.commit()) {
        QMessageBox::warning(this, tr("转换失败"), tr("保存转换结果失败，原文件没有改动。"));
        return;
    }
    statusBar()->showMessage(tr("转换完成：%1").arg(QDir::toNativeSeparators(output)), 7000);
}

void MainWindow::batchConvertOfd() {
    QString converter = QStandardPaths::findExecutable(QStringLiteral("ofd2pdf"));
    if (converter.isEmpty()) converter = QSettings().value(QStringLiteral("documentManagement/ofdConverter")).toString();
    if (converter.isEmpty() || !QFileInfo::exists(converter)) {
        converter = QFileDialog::getOpenFileName(this, tr("选择 OFD 转 PDF 工具"), QString(), tr("可执行文件 (*.exe)"));
        if (converter.isEmpty()) return;
        QSettings().setValue(QStringLiteral("documentManagement/ofdConverter"), converter);
    }
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("选择 OFD 文件"), QString(), tr("OFD 文件 (*.ofd *.OFD)"));
    if (files.isEmpty()) return;
    int converted = 0;
    QStringList failures;
    QProgressDialog progress(tr("正在转换 OFD 文件…"), tr("取消"), 0, files.size(), this);
    progress.setWindowTitle(tr("OFD 批量转换"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    for (int index = 0; index < files.size() && !progress.wasCanceled(); ++index) {
        const QString& input = files.at(index);
        progress.setValue(index);
        progress.setLabelText(tr("正在转换：%1").arg(QFileInfo(input).fileName()));
        QApplication::processEvents();
        const QFileInfo inputInfo(input);
        QString output = inputInfo.dir().filePath(inputInfo.completeBaseName() + QStringLiteral(".pdf"));
        if (QFileInfo::exists(output)) {
            output = inputInfo.dir().filePath(inputInfo.completeBaseName() + QStringLiteral("-converted.pdf"));
        }
        QProcess process;
        process.start(converter, {input, output});
        QElapsedTimer elapsed;
        elapsed.start();
        while (process.state() != QProcess::NotRunning && elapsed.elapsed() < 120000 &&
               !progress.wasCanceled()) {
            process.waitForFinished(100);
            QApplication::processEvents();
        }
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(3000);
        }
        if (!progress.wasCanceled() && process.exitStatus() == QProcess::NormalExit &&
            process.exitCode() == 0 && QFileInfo(output).isFile() && QFileInfo(output).size() > 0) {
            ++converted;
        } else {
            failures.append(QFileInfo(input).fileName());
        }
    }
    progress.setValue(files.size());
    if (progress.wasCanceled()) {
        statusBar()->showMessage(tr("OFD 转换已取消；已完成 %1 个文件。").arg(converted), 5000);
        return;
    }
    if (!failures.isEmpty()) {
        QMessageBox::warning(this, tr("OFD 批量转换"),
                             tr("已转换 %1 个，失败 %2 个。转换器应接受“输入文件 输出文件”参数。\n失败：%3")
                                 .arg(converted).arg(failures.size()).arg(failures.join(QStringLiteral("、"))));
    } else {
        statusBar()->showMessage(tr("已转换 %1 个 OFD 文件。 ").arg(converted), 5000);
    }
}

PdfView* MainWindow::savedShareDocument() {
    PdfView* view = shareDocument();
    if (view == nullptr) {
        return nullptr;
    }

    if (view->filePath().isEmpty()) {
        saveDocumentAs();
        view = currentView();
        if (view == nullptr || view->filePath().isEmpty()) {
            return nullptr;  // 用户取消了“另存为”。
        }
    } else if (view->isModified()) {
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this, tr("保存后共享"),
            tr("当前文档有未保存的修改。要先保存最新版本再共享吗？"),
            QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save);
        if (choice != QMessageBox::Save) {
            return nullptr;
        }
        saveDocument();
    }

    view = currentView();
    if (view == nullptr || view->filePath().isEmpty() || view->isModified()) {
        return nullptr;  // 保存失败时 saveDocument() 已显示具体原因。
    }
    return view;
}

// ---------------------------------------------------------- 保存（v0.8.0）----

void MainWindow::saveDocument() {
    auto* view = currentView();
    if (view == nullptr) {
        return;
    }
    if (view->filePath().isEmpty()) {
        saveDocumentAs();
        return;
    }
    QString error;
    QString backupPath;
    if (!view->save(&error, &backupPath)) {
        statusBar()->showMessage(tr("保存失败：%1").arg(error));
        trace(QStringLiteral("save failed"));
        return;
    }
    statusBar()->showMessage(
        backupPath.isEmpty()
            ? tr("已保存 %1").arg(QFileInfo(view->filePath()).fileName())
            : tr("已保存 %1（原文件备份为 %2）")
                  .arg(QFileInfo(view->filePath()).fileName(),
                       QFileInfo(backupPath).fileName()),
        5000);
}

void MainWindow::saveDocumentAs() {
    auto* view = currentView();
    if (view == nullptr) {
        return;
    }
    const QString suggested =
        view->filePath().isEmpty()
            ? QString()
            : QFileInfo(view->filePath()).absolutePath() + QDir::separator() +
                  QFileInfo(view->filePath()).completeBaseName() + QStringLiteral("-copy.pdf");
    const QString target = QFileDialog::getSaveFileName(
        this, tr("另存为"), suggested, tr("PDF 文件 (*.pdf)"));
    if (target.isEmpty()) {
        return;
    }
    QString error;
    QString backupPath;
    if (!view->saveAs(target, &error, &backupPath)) {
        statusBar()->showMessage(tr("保存失败：%1").arg(error));
        trace(QStringLiteral("save as failed"));
        return;
    }
    // 文档换了路径：标签页标题跟着改。
    const int index = tabs_->currentIndex();
    tabs_->setTabText(index, QFileInfo(target).fileName());
    statusBar()->showMessage(tr("已另存为 %1").arg(QFileInfo(target).fileName()), 5000);
}

// 启动时看看有没有上次没保存完的工作。
//
// 恢复日志记的是命令序列，不是文件副本：
// 打开原文档之后按同样的顺序重放一遍，未保存的工作就回来了。
void MainWindow::checkRecovery() {
    const QVector<doc::RecoveryEntry> entries = doc::RecoveryLog::list();
    if (entries.isEmpty()) {
        return;
    }
    const doc::RecoveryEntry entry = entries.first();
    // 原文档已经不在了：这条记录没用了，直接清掉。
    if (!QFileInfo::exists(entry.documentPath)) {
        doc::RecoveryLog::remove(entry.documentPath);
        return;
    }
    trace(QStringLiteral("recovery found path=%1 commands=%2")
              .arg(entry.documentPath)
              .arg(entry.commands.size()));

    // 探针模式：不要问，直接跳过。
    //
    // 恢复问询是模态弹窗，会挡住验收脚本要点的界面；
    // 而且验收每个场景都要重启应用，上一轮留下的记录会让每一轮启动都弹框。
    // 设了 AIPDF_SKIP_RECOVERY_PROMPT 就当用户点了「丢弃」：记录照旧留着，
    // 只是这一轮不问。
    if (qEnvironmentVariableIsSet("AIPDF_SKIP_RECOVERY_PROMPT")) {
        trace(QStringLiteral("recovery prompt skipped path=%1").arg(entry.documentPath));
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("发现未保存的工作"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("上次在 %1 的修改没有保存（%2 条操作）。要恢复吗？")
                    .arg(QFileInfo(entry.documentPath).fileName())
                    .arg(entry.commands.size()));
    QPushButton* recoverButton = box.addButton(tr("恢复"), QMessageBox::AcceptRole);
    box.addButton(tr("丢弃"), QMessageBox::DestructiveRole);
    box.setDefaultButton(recoverButton);
    box.exec();
    if (box.clickedButton() != recoverButton) {
        // 丢弃：把恢复日志删掉，免得每次启动都来问。
        doc::RecoveryLog::remove(entry.documentPath);
        return;
    }

    openPdfFile(entry.documentPath);
    auto* view = currentView();
    if (view == nullptr || view->annotationController() == nullptr) {
        return;
    }
    // 重放命令：恢复出来的修改同样能撤销。
    int replayed = 0;
    for (const QByteArray& command : entry.commands) {
        QString error;
        auto restored = doc::deserializeEditCommand(command, &error);
        if (restored == nullptr) {
            continue;
        }
        if (view->annotationController()->applyCommand(std::move(restored))) {
            ++replayed;
        }
    }
    statusBar()->showMessage(tr("已恢复 %1 条未保存的操作").arg(replayed), 5000);
    trace(QStringLiteral("recovery replayed=%1").arg(replayed));
}

void MainWindow::bindNavigation(PdfView* view) {
    NavigationController* controller =
        view != nullptr ? view->navigationController() : nullptr;
    if (controller == boundNavigation_) {
        return;
    }
    // 换文档时把上一个控制器的连接断掉，否则刷新会打到别的文档上。
    if (navigationConnection_) {
        QObject::disconnect(navigationConnection_);
        navigationConnection_ = QMetaObject::Connection();
    }
    boundNavigation_ = controller;
    if (controller != nullptr) {
        navigationConnection_ = connect(controller, &NavigationController::outlineReady,
                                        this, [this]() {
                                            refreshOutline(currentView());
                                        });
    }
    refreshOutline(view);
}

void MainWindow::refreshOutline(PdfView* view) {
    if (outlineTree_ == nullptr || bookmarkList_ == nullptr) {
        return;
    }
    outlineTree_->clear();
    bookmarkList_->clear();

    NavigationController* controller =
        view != nullptr ? view->navigationController() : nullptr;
    const bool hasOutline = controller != nullptr && controller->outlineLoaded() &&
                            !controller->outline().empty();

    if (outlineStack_ != nullptr) {
        outlineStack_->setCurrentIndex(hasOutline ? 1 : 0);
    }
    if (bookmarkStack_ != nullptr) {
        bookmarkStack_->setCurrentIndex(hasOutline ? 1 : 0);
    }
    if (!hasOutline) {
        // 只有**真的取回过**（且是空的）才算「这个文档没有大纲」。
        // 还没取回来时也写「空」会让断言失去意义：有大纲的文档在取回之前
        // 同样会先显示一次空状态。
        if (controller != nullptr && controller->outlineLoaded()) {
            trace(QStringLiteral("outline empty"));
        } else {
            trace(QStringLiteral("outline pending"));
        }
        return;
    }

    addOutlineItems(outlineTree_->invisibleRootItem(), controller->outline());
    // 顶层条目默认展开：用户一眼能看到层级，而不是一排收起来的箭头。
    // （收起的子条目在无障碍树里根本不出现，验收脚本也点不到。）
    for (int index = 0; index < outlineTree_->topLevelItemCount(); ++index) {
        outlineTree_->expandItem(outlineTree_->topLevelItem(index));
    }

    // 书签页签：同一份大纲的扁平列表，用缩进表示层级。
    const std::vector<pdf::OutlineEntry> entries = controller->flatOutline();
    for (const pdf::OutlineEntry& entry : entries) {
        auto* item = new QListWidgetItem(bookmarkList_);
        item->setText(QString(entry.depth * 2, QLatin1Char(' ')) + entry.title);
        item->setData(Qt::UserRole, entry.pageIndex);
        item->setData(Qt::UserRole + 1, entry.title);
        if (entry.pageIndex < 0) {
            item->setToolTip(tr("该条目没有可跳转的目标"));
        } else {
            item->setToolTip(tr("跳转到第 %1 页").arg(entry.pageIndex + 1));
        }
    }
    trace(QStringLiteral("outline shown items=%1").arg(entries.size()));
}

void MainWindow::addCurrentPageBookmark() {
    PdfView* view = currentView();
    if (view == nullptr) return;
    bool accepted = false;
    const QString title = QInputDialog::getText(
        this, tr("添加书签"), tr("书签标题:"), QLineEdit::Normal,
        tr("第 %1 页").arg(view->currentPage() + 1), &accepted).trimmed();
    if (!accepted || title.isEmpty()) return;
    std::vector<pdf::OutlineItem> outline = view->outline();
    pdf::OutlineItem item;
    item.title = title;
    item.pageIndex = view->currentPage();
    outline.push_back(std::move(item));
    QString error;
    if (!view->replaceOutline(outline, &error)) {
        QMessageBox::warning(this, tr("添加书签"), error);
        return;
    }
    statusBar()->showMessage(tr("已添加书签“%1”").arg(title), 3000);
}

void MainWindow::renameSelectedBookmark() {
    PdfView* view = currentView();
    const int row = bookmarkList_ != nullptr ? bookmarkList_->currentRow() : -1;
    if (view == nullptr || row < 0) {
        QMessageBox::information(this, tr("重命名书签"), tr("请先在书签面板中选择一个书签。"));
        return;
    }
    std::vector<pdf::OutlineItem> outline = view->outline();
    int cursor = 0;
    pdf::OutlineItem* item = outlineItemAt(&outline, row, &cursor);
    if (item == nullptr) return;
    bool accepted = false;
    const QString title = QInputDialog::getText(this, tr("重命名书签"), tr("书签标题:"),
                                                 QLineEdit::Normal, item->title,
                                                 &accepted).trimmed();
    if (!accepted || title.isEmpty() || title == item->title) return;
    item->title = title;
    QString error;
    if (!view->replaceOutline(outline, &error)) {
        QMessageBox::warning(this, tr("重命名书签"), error);
        return;
    }
    statusBar()->showMessage(tr("书签已重命名"), 3000);
}

void MainWindow::deleteSelectedBookmark() {
    PdfView* view = currentView();
    const int row = bookmarkList_ != nullptr ? bookmarkList_->currentRow() : -1;
    if (view == nullptr || row < 0) {
        QMessageBox::information(this, tr("删除书签"), tr("请先在书签面板中选择一个书签。"));
        return;
    }
    std::vector<pdf::OutlineItem> outline = view->outline();
    int cursor = 0;
    if (!removeOutlineItemAt(&outline, row, &cursor)) return;
    QString error;
    if (!view->replaceOutline(outline, &error)) {
        QMessageBox::warning(this, tr("删除书签"), error);
        return;
    }
    statusBar()->showMessage(tr("已删除书签"), 3000);
}

// 切到导航栏的某一页并把它显示出来。
//
// 导航栏的图标按钮是开关：再点一次当前页会把面板收起来。
// 命令栏的「页面」「批注」是导航命令，不是开关——连点两次把面板关掉，
// 会让人以为功能坏了。
void MainWindow::showNavigationPage(int index) {
    if (navigationRail_ != nullptr) {
        if (auto* button = navigationRail_->button(index)) {
            button->setChecked(true);
        }
    }
    if (navigationPages_ == nullptr) {
        return;
    }
    navigationPages_->setCurrentIndex(index);
    setNavigationExpanded(true);
    traceNavigationPanel(index);
}

void MainWindow::traceNavigationPanel(int index) {
    // 页签内容不进 UIA 文本（QStackedWidget 只暴露当前页），
    // 条目数记进轨迹，验收脚本才能断言面板里真的有内容。
    switch (index) {
        case 1:
            trace(QStringLiteral("panel outline items=%1")
                      .arg(outlineTree_ != nullptr ? outlineTree_->topLevelItemCount() : 0));
            break;
        case 2:
            trace(QStringLiteral("panel bookmarks items=%1")
                      .arg(bookmarkList_ != nullptr ? bookmarkList_->count() : 0));
            break;
        case 4:
            trace(QStringLiteral("panel annotations items=%1")
                      .arg(annotationList_ != nullptr ? annotationList_->count() : 0));
            break;
        case 5:
            trace(QStringLiteral("panel index items=%1")
                      .arg(indexPanel_ != nullptr ? indexPanel_->resultCount() : 0));
            break;
        default:
            break;
    }
}

void MainWindow::navigateToOutlineTarget(int pageIndex, const QString& title) {
    if (pageIndex < 0) {
        statusBar()->showMessage(tr("「%1」没有可跳转的目标").arg(title), 3000);
        trace(QStringLiteral("navigate skipped title=%1").arg(title));
        return;
    }
    PdfView* view = currentView();
    if (view == nullptr) {
        return;
    }
    view->goToPage(pageIndex);
    trace(QStringLiteral("navigate page=%1 title=%2").arg(pageIndex).arg(title));
}

void MainWindow::openPdfDialog() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 PDF"), {}, tr("PDF 文档 (*.pdf)"));
    if (!path.isEmpty()) {
        openPdfFile(path);
    }
}

void MainWindow::sendHealthCheck() {
    workerClient_->sendRequest("system.health");
}

void MainWindow::refreshThumbnails(PdfView* view) {
    if (pagesList_ == nullptr) {
        return;
    }
    pagesList_->clear();
    if (view == nullptr) {
        return;
    }

    // 首批按需生成，避免打开 500 页文档时一次性阻塞 UI。
    const int visibleBatch = std::min(view->pageCount(), 24);
    for (int index = 0; index < visibleBatch; ++index) {
        const QImage image = view->renderThumbnail(index, 124);
        auto* item = new QListWidgetItem();
        item->setData(Qt::UserRole, index);
        item->setText(QString::number(index + 1));
        item->setTextAlignment(Qt::AlignHCenter);
        if (!image.isNull()) {
            item->setIcon(QIcon(QPixmap::fromImage(image)));
        }
        pagesList_->addItem(item);
    }
    // 打开文档时可能已经停在某一页（恢复上次位置），先把高亮对上。
    highlightThumbnail(view->currentPage());
}

void MainWindow::highlightThumbnail(int pageIndex) {
    if (pagesList_ == nullptr || pagesList_->count() == 0) {
        return;
    }
    // 缩略图只渲染了前若干页，后面的页没有条目可高亮。
    if (pageIndex < 0 || pageIndex >= pagesList_->count()) {
        return;
    }
    if (pagesList_->currentRow() != pageIndex) {
        pagesList_->setCurrentRow(pageIndex);
        pagesList_->scrollToItem(pagesList_->item(pageIndex));
    }
    trace(QStringLiteral("thumbnail current page=%1").arg(pageIndex));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (droppedPdfPaths(event->mimeData()).isEmpty()) {
        return;
    }
    event->acceptProposedAction();
    // 只在状态栏给一句提示：拖动过程中弹出遮罩或对话框都会打断手势。
    statusBar()->showMessage(tr("松开即可打开 PDF"));
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent* event) {
    QMainWindow::dragLeaveEvent(event);
    // 只在等一次拖放时清提示，别把「第 3 页」这类常驻消息也抹掉。
    if (statusBar()->currentMessage() == tr("松开即可打开 PDF")) {
        statusBar()->clearMessage();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QStringList paths = droppedPdfPaths(event->mimeData());
    if (paths.isEmpty()) {
        return;
    }
    event->acceptProposedAction();
    statusBar()->clearMessage();
    trace(QStringLiteral("drop files=%1").arg(paths.size()));
    for (const QString& path : paths) {
        openPdfFile(path);
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    // 这里布局还没重排完，tabs_->width() 还是旧值：直接摆会把浮层留在旧位置，
    // 窗口缩小时卡片就被挤到窗口外面。挪到下一轮事件循环再摆。
    QTimer::singleShot(0, this, [this] {
        positionFloatingUi();
        positionStatusBarUi();
    });
}

void MainWindow::positionStatusBarUi() {
    if (statusSettingsButton_ == nullptr || statusMessageLabel_ == nullptr) {
        return;
    }
    QStatusBar* bar = statusBar();
    const int buttonSize = 26;
    const int top = std::max(0, (bar->height() - buttonSize) / 2);
    statusSettingsButton_->setGeometry(4, top, buttonSize, buttonSize);
    int right = bar->width() - 8;
    if (statusDocumentControls_ != nullptr && statusDocumentControls_->isVisible()) {
        right = std::min(right, statusDocumentControls_->x() - 10);
    }
    statusMessageLabel_->setGeometry(38, 0, std::max(0, right - 38), bar->height());
}

void MainWindow::updateAiQuickActions() {
    if (aiQuickCard_ == nullptr || aiQuickButtons_.isEmpty()) {
        return;
    }
    PdfView* view = currentView();
    const bool hasDocument = view != nullptr;
    bool hasSelection = false;
    if (hasDocument && view->textController() != nullptr) {
        hasSelection = !view->textController()->selectedText().trimmed().isEmpty();
    }

    if (aiQuickSubtitle_ != nullptr) {
        // 设计规范：上下文不足时要禁用并说明原因，而不是让按钮静默失效。
        if (!hasDocument) {
            aiQuickSubtitle_->setText(tr("打开一份 PDF 后，这些动作才有可用的内容"));
        } else if (!hasSelection) {
            aiQuickSubtitle_->setText(tr("选中文字后可「解释」；「总结」作用于当前页"));
        } else {
            aiQuickSubtitle_->setText(tr("「解释」会用到你选中的这段文字"));
        }
    }

    for (int index = 0; index < aiQuickButtons_.size(); ++index) {
        QPushButton* button = aiQuickButtons_.at(index);
        if (button == nullptr) {
            continue;
        }
        // 下标与构建时的动作表一致：0 开始聊天、1 改写、2 校对、3 解释、4 总结。
        if (index == 3) {
            button->setEnabled(hasSelection);
            button->setToolTip(hasSelection ? tr("解释页面上选中的文字")
                                            : tr("先在页面上选中一段文字"));
        } else if (index == 4) {
            button->setEnabled(hasDocument);
            button->setToolTip(hasDocument ? tr("总结当前页") : tr("先打开一份 PDF"));
        }
    }
}

void MainWindow::setNavigationExpanded(bool expanded) {
    if (navigationDock_ == nullptr || navigationPages_ == nullptr) {
        return;
    }
    navigationPages_->setVisible(expanded);
    if (navigationHeader_ != nullptr) {
        navigationHeader_->setVisible(expanded);
    }
    // 收起时把 dock 连同内容一起钉在 rail 的宽度上。只调 resizeDocks 不够：
    // QMainWindow 在窗口尺寸变化/布局重算时会重新分配，空面板又会被撑开，
    // rail 就飘在一条 300 多像素的空条中间。
    if (expanded) {
        if (QWidget* content = navigationDock_->widget()) {
            content->setMinimumWidth(0);
            content->setMaximumWidth(QWIDGETSIZE_MAX);
        }
        resizeDocks({navigationDock_}, {224}, Qt::Horizontal);
        // 宽度钉死：只给 resizeDocks 的话，QMainWindow 会把「页签内容的最小宽度」
        // 一起算进去（实测留下 400px 的框、却只用了 320px），页面整体被推右。
        navigationDock_->setFixedWidth(280);
    } else {
        resizeDocks({navigationDock_}, {52}, Qt::Horizontal);
        navigationDock_->setFixedWidth(52);
        if (QWidget* content = navigationDock_->widget()) {
            content->setFixedWidth(52);
        }
    }
}

void MainWindow::positionFloatingUi() {
    if (tabs_ == nullptr || aiFloatingButton_ == nullptr || aiQuickCard_ == nullptr) {
        return;
    }
    constexpr int margin = 18;
    // 兜底钳制：窗口被拖得很小时，卡片不能跑到窗口外面去。
    const int buttonX = std::max(margin, tabs_->width() - aiFloatingButton_->width() - margin);
    const int buttonY = std::max(margin, tabs_->height() - aiFloatingButton_->height() - margin);
    aiFloatingButton_->move(buttonX, buttonY);
    aiFloatingButton_->raise();
    aiQuickCard_->adjustSize();
    const int cardX = std::max(margin, tabs_->width() - aiQuickCard_->width() - margin);
    const int cardY =
        std::max(margin, aiFloatingButton_->y() - aiQuickCard_->height() - margin);
    aiQuickCard_->move(cardX, cardY);
    if (aiQuickCard_->isVisible()) {
        aiQuickCard_->raise();
    }
}

void MainWindow::openPdfFile(const QString& filePath) {
    pdf::PdfOpenResult result = pdf::openDocument(filePath);
    trace(QStringLiteral("open file=%1 required=%2 msg=%3")
              .arg(QFileInfo(filePath).fileName())
              .arg(result.passwordRequired ? 1 : 0)
              .arg(result.errorMessage));
    if (result.passwordRequired) {
        bool accepted = false;
        const QString password = QInputDialog::getText(
            this,
            tr("需要密码"),
            tr("请输入 PDF 密码："),
            QLineEdit::Password,
            QString(),
            &accepted);
        trace(QStringLiteral("dialog returned accepted=%1 passwordLen=%2")
                  .arg(accepted ? 1 : 0)
                  .arg(password.size()));
        if (!accepted) {
            statusBar()->showMessage(tr("已取消打开 %1").arg(QFileInfo(filePath).fileName()), 5000);
            return;
        }
        result = pdf::openDocument(filePath, password);
        trace(QStringLiteral("reopen valid=%1 required=%2 msg=%3")
                  .arg(result.document.isValid() ? 1 : 0)
                  .arg(result.passwordRequired ? 1 : 0)
                  .arg(result.errorMessage));
    }

    if (!result.document.isValid()) {
        // 错误提示常驻，直到下一次操作覆盖它。
        statusBar()->showMessage(
            tr("无法打开 %1：%2").arg(QFileInfo(filePath).fileName(), result.errorMessage));
        return;
    }

    auto* view = new PdfView(this);
    view->setDocument(result.document, filePath);
    // 文档/批注/图形工具条已经收进 Ribbon，释放垂直空间给画布与状态栏。
    view->setToolBarsVisible(false);
    // 打开成功才记：密码错、文件损坏的不该出现在「最近文档」里。
    rememberRecentFile(filePath);
    const QString title = QFileInfo(filePath).fileName();
    if (tabs_->count() == 1 && tabs_->widget(0)->property("welcomePage").toBool()) {
        QWidget* welcome = tabs_->widget(0);
        tabs_->removeTab(0);
        welcome->deleteLater();
    }
    tabs_->addTab(view, title);
    tabs_->setCurrentWidget(view);
    syncTabBar();
    refreshThumbnails(view);
    bindNavigation(view);
    bindAnnotations(view);
    connect(view, &PdfView::metricsChanged, metricsLabel_, &QLabel::setText);
    connect(view, &PdfView::currentPageChanged, this, &MainWindow::updateStatusPage);
    connect(view, &PdfView::zoomChanged, this, &MainWindow::updateStatusZoom);
    connect(view, &PdfView::permissionsChanged, this, &MainWindow::updatePermissionActionState);
    connect(view, &PdfView::pageStructureChanged, this, [this] {
        if (auto* current = currentView()) {
            updateStatusPage(current->currentPage());
        }
    });
    updateStatusPage(view->currentPage());
    updateStatusZoom(view->zoom());
    updatePermissionActionState();
    connect(view, &PdfView::saveRequested, this, &MainWindow::saveDocument);
    connect(view, &PdfView::statusMessage, this, [this](const QString& text) {
        statusBar()->showMessage(text, 3000);
    });
    connect(view, &PdfView::annotationsChanged, this, [this](int count) {
        trace(QStringLiteral("annotations total=%1").arg(count));
    });
    connect(view, &PdfView::textCopied, this, [this](int characters) {
        statusBar()->showMessage(tr("已复制 %1 个字符到剪贴板").arg(characters), 3000);
    });
    connect(view, &PdfView::currentPageChanged, this, [this, view](int index) {
        highlightThumbnail(index);
        updatePageActionState();
        // 翻到没有文本层的页时自动识别一次（每页每次会话只试一次）。
        if (ocrController_ != nullptr) {
            ocrController_->handlePageShown(view, index);
        }
        // AI 面板要知道当前页：摘要与提问都只送当前页。
        if (aiPanel_ != nullptr) {
            aiPanel_->setCurrentPage(index);
        }
    });
    // 打开文档后也自动看一眼当前页：扫描件不必先翻页才识别。
    // 等一会儿是为了让文本层先落地——否则会给正常的文字页也做一遍 OCR。
    QTimer::singleShot(1200, this, [this, view] {
        if (ocrController_ == nullptr) {
            return;
        }
        ocrController_->resetAttempts();
        ocrController_->handlePageShown(view, view->currentPage());
    });

    // 页面结构变了：缩略图列表与页面按钮都要跟着更新。
    connect(view, &PdfView::pageStructureChanged, this, [this]() {
        refreshThumbnails(currentView());
        updatePageActionState();
    });
    // 文本与导航控制器被换掉了：搜索面板与目录面板要重新指过去。
    connect(view, &PdfView::controllersRefreshed, this, [this, view]() {
        if (searchPanel_ != nullptr) {
            searchPanel_->setController(view->textController());
        }
        bindNavigation(view);
        // 索引控制器也换了：索引面板要重新指过去。
        bindIndex(view);
        // 问答控制器同样换了：AI 面板要重新指过去，
        // 否则它握着的是上一个已经销毁的控制器。
        if (aiPanel_ != nullptr) {
            aiPanel_->setQaController(view->qaController());
        }
    });
    connect(view, &PdfView::filePathChanged, this, [this, view](const QString&) {
        const int index = tabs_->indexOf(view);
        if (index >= 0) {
            tabs_->setTabText(index, QFileInfo(view->filePath()).fileName());
        }
    });
    connect(view, &PdfView::linkFollowed, this, [this](int targetPage, const QString& uri) {
        if (targetPage >= 0) {
            statusBar()->showMessage(tr("已跳转到第 %1 页").arg(targetPage + 1), 3000);
        } else if (!uri.isEmpty()) {
            statusBar()->showMessage(tr("已打开外部链接：%1").arg(uri), 3000);
        }
    });
    if (searchPanel_ != nullptr) {
        searchPanel_->setController(view->textController());
    }
    bindIndex(view);
    // AI 面板的整篇问答也要指到这个文档的问答控制器。
    //
    // 换标签页时也会指一次（currentChanged），但启动时直接打开文档走的是这条路径，
    // 这里不指的话，整篇问答会找不到索引。
    if (aiPanel_ != nullptr) {
        aiPanel_->setQaController(view->qaController());
    }
    statusBar()->showMessage(tr("已打开 %1（%2 页）").arg(title).arg(view->pageCount()));
}

// ---------------------------------------------------------------- 语言 ----

void MainWindow::applyLanguage(const QString& preference) {
    if (!i18n::LanguageManager::instance().setPreference(preference)) {
        return;
    }
    // 重译由 languageChanged 信号驱动（本窗口与各面板各自登记），
    // 这里只补一条状态栏反馈，用的是切换之后的新语言。
    const QString code = i18n::LanguageManager::instance().code();
    statusBar()->showMessage(tr("界面语言已切换为 %1").arg(i18n::nativeNameOf(code)), 5000);
}


void MainWindow::updateNavigationHeader(int index) {
    if (navigationHeader_ == nullptr) {
        return;
    }
    switch (index) {
        case 0: navigationHeader_->setText(tr("页面")); break;
        case 1: navigationHeader_->setText(tr("目录")); break;
        case 2: navigationHeader_->setText(tr("书签")); break;
        case 3: navigationHeader_->setText(tr("搜索")); break;
        case 4: navigationHeader_->setText(tr("批注")); break;
        case 5: navigationHeader_->setText(tr("表单")); break;
        case 6: navigationHeader_->setText(tr("索引")); break;
        default: navigationHeader_->setText(tr("导航")); break;
    }
}


void MainWindow::updateStatusPage(int pageIndex) {
    if (statusPageEdit_ == nullptr || statusPageTotal_ == nullptr) {
        return;
    }
    PdfView* view = currentView();
    const int total = (view != nullptr) ? view->pageCount() : 0;
    if (total <= 0) {
        statusPageEdit_->clear();
        statusPageTotal_->setText(tr("/ 0 页"));
        return;
    }
    statusPageEdit_->setText(QString::number(qBound(1, pageIndex + 1, total)));
    statusPageTotal_->setText(tr("/ %1 页").arg(total));
}

void MainWindow::updateStatusZoom(double factor) {
    if (statusZoomLabel_ == nullptr || statusZoomSlider_ == nullptr) {
        return;
    }
    const int percent = qBound(25, qRound(factor * 100.0), 400);
    updatingStatusZoom_ = true;
    statusZoomLabel_->setText(QStringLiteral("%1%").arg(percent));
    statusZoomSlider_->setValue(percent);
    updatingStatusZoom_ = false;
}

void MainWindow::retranslateUi() {
    texts_.retranslate();
    syncLanguageMenu();
    // 随状态变化的文案由各自的 update 函数按当前语言重算。
    updateAiQuickActions();
    updateNavigationHeader(navigationPages_ != nullptr ? navigationPages_->currentIndex() : 0);
}

void MainWindow::syncLanguageMenu() {
    if (languageActions_ == nullptr) {
        return;
    }
    const QString preference = i18n::LanguageManager::instance().preference();
    for (QAction* action : languageActions_->actions()) {
        action->setChecked(action->data().toString() == preference);
    }
    if (followSystemAction_ != nullptr) {
        // 跟随系统时把实际生效的语言写进括号：不用去翻系统设置猜「现在是哪种」。
        followSystemAction_->setText(tr("跟随系统（%1）")
                                         .arg(i18n::nativeNameOf(
                                             i18n::LanguageManager::instance().code())));
    }
}

void MainWindow::bindWelcomeTab(QWidget* page) {
    // 用 QPointer 兜底：标签页随时可能被关掉并销毁，重译时不能再碰野指针。
    QPointer<QWidget> guard(page);
    texts_.bind([this, guard] {
        if (guard.isNull()) {
            return;
        }
        const int index = tabs_->indexOf(guard);
        if (index >= 0) {
            tabs_->setTabText(index, tr("欢迎页"));
        }
    });
}

}  // namespace aipdf::app

