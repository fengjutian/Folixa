#include "app/MainWindow.h"

#include "app/PdfView.h"
#include "app/SearchPanel.h"
#include "ipc/WorkerClient.h"
#include "pdf/PdfiumAdapter.h"

#include <QAction>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDateTime>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMenu>
#include <QSettings>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace aipdf::app {

namespace {
void trace(const QString& message) {
    const QString path = qEnvironmentVariable("AIPDF_TRACE_FILE");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write((QDateTime::currentDateTime().toString(Qt::ISODate) + " " + message + "\n").toUtf8());
    }
}
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), workerClient_(new ipc::WorkerClient(this)) {
    buildUi();

    connect(workerClient_, &ipc::WorkerClient::stateChanged,
            this, [this](const QString& state) {
                const QString label = state == QStringLiteral("ready") ? tr("已就绪")
                    : state == QStringLiteral("starting") ? tr("正在启动")
                    : state == QStringLiteral("stopped") ? tr("已停止")
                    : state;
                workerStatus_->setText(tr("AI 服务：%1").arg(label));
                sendButton_->setEnabled(state == "ready");
                if (state == "ready") {
                    sendHealthCheck();
                }
            });
    connect(workerClient_, &ipc::WorkerClient::responseReceived,
            this, [this](const QString&, const QJsonObject& response) {
                const QJsonObject payload = response.value("payload").toObject();
                if (payload.contains("status")) {
                    conversation_->append(
                        tr("<b>系统：</b>本地 AI 服务已就绪（协议 %1，版本 %2）。")
                            .arg(payload.value("protocol_version").toInt())
                            .arg(payload.value("worker_version").toString().toHtmlEscaped()));
                } else if (payload.contains("text")) {
                    conversation_->append(tr("<b>服务：</b>") +
                                          payload.value("text").toString().toHtmlEscaped());
                } else {
                    conversation_->append(tr("<b>服务：</b>") +
                        QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)).toHtmlEscaped());
                }
            });
    connect(workerClient_, &ipc::WorkerClient::requestFailed,
            this, [this](const QString&, const QString& message) {
                conversation_->append(tr("<b>错误：</b>") + message.toHtmlEscaped());
            });

    workerClient_->start();
}

void MainWindow::buildUi() {
    setWindowTitle(QString("Folixa · AI PDF Editor %1").arg(QStringLiteral(AIPDF_VERSION)));
    resize(1440, 900);

    auto* tabs = new QTabWidget(this);
    tabs->setTabsClosable(true);
    tabs->setDocumentMode(true);
    auto* welcome = createWelcomePage();
    welcome->setProperty("welcomePage", true);
    tabs->addTab(welcome, tr("欢迎"));
    tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
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
            tabs_->addTab(welcomePage, tr("欢迎"));
            tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
        }
    });
    setCentralWidget(tabs);
    tabs_ = tabs;

    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    auto* openAction = fileMenu->addAction(tr("打开 PDF…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openPdfDialog);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出"), QKeySequence::Quit, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    menuBar()->hide();

    // 轻量功能导航：仅当前已交付的阅读功能可用，其余明确标记为后续能力。
    auto* commandBar = addToolBar(tr("功能导航"));
    commandBar->setObjectName(QStringLiteral("commandBar"));
    commandBar->setMovable(false);
    commandBar->setFloatable(false);
    auto* brand = new QLabel(QStringLiteral("  F  "), commandBar);
    brand->setObjectName(QStringLiteral("brandMark"));
    brand->setToolTip(tr("Folixa 文档工作区"));
    commandBar->addWidget(brand);
    auto* appMenuButton = new QToolButton(commandBar);
    appMenuButton->setObjectName(QStringLiteral("appMenuButton"));
    appMenuButton->setText(QStringLiteral("☰"));
    appMenuButton->setToolTip(tr("主菜单"));
    appMenuButton->setPopupMode(QToolButton::InstantPopup);
    auto* appMenu = new QMenu(appMenuButton);
    appMenu->addAction(openAction);
    appMenu->addSeparator();
    auto* findAction = appMenu->addAction(tr("查找…"));
    findAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    appMenu->addSeparator();
    appMenu->addAction(navigationDock_ == nullptr ? tr("导航面板") : QString());
    appMenu->addSeparator();
    appMenu->addAction(tr("退出"), this, &QWidget::close);
    appMenuButton->setMenu(appMenu);
    commandBar->addWidget(appMenuButton);
    connect(findAction, &QAction::triggered, this, [this]() {
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
    commandBar->addSeparator();
    auto* homeAction = commandBar->addAction(tr("阅读"));
    homeAction->setCheckable(true);
    homeAction->setChecked(true);
    const QStringList futureCommands = {tr("批注"), tr("编辑"), tr("转换"),
                                        tr("页面"), tr("保护"), tr("表单"), tr("工具")};
    for (const QString& label : futureCommands) {
        QAction* action = commandBar->addAction(label);
        action->setEnabled(false);
        action->setToolTip(tr("将在后续版本中提供"));
    }
    auto* commandSpacer = new QWidget(commandBar);
    commandSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    commandBar->addWidget(commandSpacer);
    auto* aiAction = commandBar->addAction(tr("✦ AI 助手"));
    aiAction->setObjectName(QStringLiteral("aiCommand"));

    navigationDock_ = new QDockWidget(tr("导航"), this);
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
    pagesList_ = new QListWidget(navigationPages);
    pagesList_->setObjectName(QStringLiteral("thumbnailList"));
    pagesList_->setViewMode(QListView::IconMode);
    pagesList_->setResizeMode(QListView::Adjust);
    pagesList_->setMovement(QListView::Static);
    pagesList_->setIconSize(QSize(132, 172));
    pagesList_->setGridSize(QSize(166, 206));
    pagesList_->setSpacing(6);
    navigationPages->addWidget(pagesList_);
    navigationPages->addWidget(createNavigationEmptyState(
        tr("暂无文档目录"), tr("文档目录将在这里显示。")));
    navigationPages->addWidget(createNavigationEmptyState(
        tr("暂无书签"), tr("当前文档没有可显示的书签。")));
    // 搜索页：全文搜索（v0.5.0）。
    searchPanel_ = new SearchPanel(navigationPages);
    navigationPages->addWidget(searchPanel_);
    auto* railGroup = new QButtonGroup(rail);
    navigationRail_ = railGroup;
    railGroup->setExclusive(true);
    const QStringList railIcons = {QStringLiteral(":/icons/pages.svg"),
                                   QStringLiteral(":/icons/outline.svg"),
                                   QStringLiteral(":/icons/bookmark.svg"),
                                   QStringLiteral(":/icons/search.svg")};
    const QStringList railTips = {tr("页面"), tr("目录"), tr("书签"), tr("搜索")};
    for (int index = 0; index < railIcons.size(); ++index) {
        auto* button = new QToolButton(rail);
        button->setObjectName(QStringLiteral("railButton"));
        button->setIcon(QIcon(railIcons.at(index)));
        button->setIconSize(QSize(20, 20));
        button->setToolTip(railTips.at(index));
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
        navigationPages->setVisible(shouldShow);
        resizeDocks({navigationDock_}, {shouldShow ? 224 : 52}, Qt::Horizontal);
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
    navigationLayout->addWidget(rail);
    navigationLayout->addWidget(navigationPages, 1);
    navigationDock_->setWidget(navigation);
    addDockWidget(Qt::LeftDockWidgetArea, navigationDock_);
    // 菜单在导航面板创建后补齐切换动作。
    // 按文本找而不是按下标：菜单里增删条目会挪动下标。
    const QList<QAction*> appMenuActions = appMenu->actions();
    for (QAction* action : appMenuActions) {
        if (action->text() != tr("导航面板")) {
            continue;
        }
        action->setText(tr("显示导航面板"));
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::toggled, navigationDock_, &QDockWidget::setVisible);
        connect(navigationDock_, &QDockWidget::visibilityChanged, action, &QAction::setChecked);
        break;
    }
    viewMenu->addAction(navigationDock_->toggleViewAction());

    aiDock_ = new QDockWidget(tr("AI 助手"), this);
    aiDock_->setObjectName(QStringLiteral("aiDock"));
    aiDock_->setMinimumWidth(280);
    aiDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    auto* aiPanel = new QFrame(aiDock_);
    aiPanel->setObjectName(QStringLiteral("aiPanel"));
    auto* aiLayout = new QVBoxLayout(aiPanel);
    aiLayout->setContentsMargins(16, 16, 16, 16);
    aiLayout->setSpacing(12);
    auto* aiTitle = new QLabel(tr("✦  Folixa AI"), aiPanel);
    aiTitle->setObjectName(QStringLiteral("aiTitle"));
    auto* aiSubtitle = new QLabel(tr("阅读、理解并定位当前文档内容"), aiPanel);
    aiSubtitle->setObjectName(QStringLiteral("aiSubtitle"));
    workerStatus_ = new QLabel(tr("AI 服务：正在启动"), aiPanel);
    workerStatus_->setObjectName(QStringLiteral("secondaryText"));
    auto* quickActions = new QHBoxLayout();
    quickActions->setSpacing(6);
    for (const QString& text : {tr("总结"), tr("解释"), tr("提取要点")}) {
        auto* chip = new QPushButton(text, aiPanel);
        chip->setObjectName(QStringLiteral("aiChip"));
        chip->setEnabled(false);
        chip->setToolTip(tr("接入模型后可用"));
        quickActions->addWidget(chip);
    }
    conversation_ = new QTextBrowser(aiPanel);
    conversation_->document()->setDefaultStyleSheet(QStringLiteral(
        "body{font-family:'Segoe UI','Microsoft YaHei UI';color:#202A3A;}"
        ".welcome{background:#FFFFFF;border:1px solid #E2DDF2;padding:14px;margin:4px;}"
        ".eyebrow{color:#765ECC;font-size:11px;font-weight:600;}"
        ".message{background:#FFFFFF;padding:10px;margin:8px 2px;}"
        ".user{background:#EAF0FF;padding:10px;margin:8px 2px;}"
        ".muted{color:#8792A3;font-size:11px;}"));
    conversation_->setHtml(tr(
        "<div class='welcome'><div class='eyebrow'>FOLIXA AI</div>"
        "<h3>从当前文档开始</h3>"
        "<p>我可以帮你总结内容、解释段落，并在接入引用索引后定位原文。</p>"
        "<p class='muted'>选择上方建议，或在下方输入问题。</p></div>"));
    prompt_ = new QLineEdit(aiPanel);
    prompt_->setPlaceholderText(tr("询问当前文档…"));
    sendButton_ = new QPushButton(aiPanel);
    sendButton_->setObjectName(QStringLiteral("aiButton"));
    sendButton_->setIcon(QIcon(QStringLiteral(":/icons/send.svg")));
    sendButton_->setIconSize(QSize(18, 18));
    sendButton_->setToolTip(tr("发送"));
    sendButton_->setFixedSize(38, 38);
    sendButton_->setEnabled(false);
    aiLayout->addWidget(aiTitle);
    aiLayout->addWidget(aiSubtitle);
    aiLayout->addLayout(quickActions);
    aiLayout->addWidget(workerStatus_);
    aiLayout->addWidget(conversation_, 1);
    auto* composer = new QFrame(aiPanel);
    composer->setObjectName(QStringLiteral("aiComposer"));
    auto* composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(8, 6, 6, 6);
    composerLayout->setSpacing(6);
    composerLayout->addWidget(prompt_, 1);
    composerLayout->addWidget(sendButton_);
    aiLayout->addWidget(composer);
    aiDock_->setWidget(aiPanel);
    addDockWidget(Qt::RightDockWidgetArea, aiDock_);
    resizeDocks({navigationDock_, aiDock_}, {52, 340}, Qt::Horizontal);
    viewMenu->addAction(aiDock_->toggleViewAction());

    aiQuickCard_ = new QFrame(tabs_);
    aiQuickCard_->setObjectName(QStringLiteral("aiQuickCard"));
    aiQuickCard_->setFixedWidth(230);
    auto* quickCardLayout = new QVBoxLayout(aiQuickCard_);
    quickCardLayout->setContentsMargins(14, 14, 14, 14);
    quickCardLayout->setSpacing(8);
    auto* quickCardTitle = new QLabel(tr("✦ Folixa AI"), aiQuickCard_);
    quickCardTitle->setObjectName(QStringLiteral("aiQuickTitle"));
    quickCardLayout->addWidget(quickCardTitle);
    const QStringList quickLabels = {tr("开始聊天"), tr("改写"), tr("校对"), tr("解释"), tr("总结")};
    for (int index = 0; index < quickLabels.size(); ++index) {
        auto* button = new QPushButton(quickLabels.at(index), aiQuickCard_);
        button->setObjectName(QStringLiteral("aiQuickAction"));
        if (index == 0) {
            connect(button, &QPushButton::clicked, this, [this] {
                aiQuickCard_->hide();
                aiDock_->show();
                aiDock_->raise();
                prompt_->setFocus();
            });
        } else {
            button->setEnabled(false);
            button->setToolTip(tr("接入模型后可用"));
        }
        quickCardLayout->addWidget(button);
    }
    aiQuickCard_->hide();

    aiFloatingButton_ = new QPushButton(QStringLiteral("✦"), tabs_);
    aiFloatingButton_->setObjectName(QStringLiteral("aiFloatingButton"));
    aiFloatingButton_->setFixedSize(44, 44);
    aiFloatingButton_->setToolTip(tr("AI 快捷操作"));
    connect(aiFloatingButton_, &QPushButton::clicked, this, [this] {
        aiQuickCard_->setVisible(!aiQuickCard_->isVisible());
        positionFloatingUi();
    });
    connect(aiAction, &QAction::triggered, aiFloatingButton_, &QPushButton::click);

    auto* focusAction = viewMenu->addAction(tr("专注模式"));
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
    constexpr int kWorkspaceLayoutVersion = 4;
    const bool isNewWorkspaceLayout =
        settings.value(QStringLiteral("workspace/layoutVersion"), 0).toInt() < kWorkspaceLayoutVersion;
    if (isNewWorkspaceLayout) {
        settings.setValue(QStringLiteral("workspace/layoutVersion"), kWorkspaceLayoutVersion);
        settings.setValue(QStringLiteral("workspace/navigationVisible"), true);
        settings.setValue(QStringLiteral("workspace/aiVisible"), false);
    }
    navigationDock_->setVisible(settings.value(QStringLiteral("workspace/navigationVisible"), true).toBool());
    aiDock_->setVisible(settings.value(QStringLiteral("workspace/aiVisible"), false).toBool());
    navigationPages_->hide();
    QTimer::singleShot(0, this, [this] {
        resizeDocks({navigationDock_}, {52}, Qt::Horizontal);
        positionFloatingUi();
    });
    connect(navigationDock_, &QDockWidget::visibilityChanged, this, [](bool visible) {
        QSettings().setValue(QStringLiteral("workspace/navigationVisible"), visible);
    });
    connect(aiDock_, &QDockWidget::visibilityChanged, this, [](bool visible) {
        QSettings().setValue(QStringLiteral("workspace/aiVisible"), visible);
    });

    connect(sendButton_, &QPushButton::clicked, this, &MainWindow::sendEcho);
    connect(prompt_, &QLineEdit::returnPressed, this, &MainWindow::sendEcho);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
        auto* view = qobject_cast<PdfView*>(tabs_->currentWidget());
        refreshThumbnails(view);
        // 搜索面板跟着当前文档走：换标签页就换控制器。
        if (searchPanel_ != nullptr) {
            searchPanel_->setController(view != nullptr ? view->textController() : nullptr);
        }
    });

    statusBar()->showMessage(tr("就绪"));
    // 性能指标放在状态栏的常驻区：放工具栏会因为文字宽度变化
    // 触发工具栏溢出，导致画布高度变化、滚动范围漂移。
    metricsLabel_ = new QLabel(QStringLiteral("缓存 0 MiB · 待渲染 0"), this);
    metricsLabel_->setToolTip(QStringLiteral("页面缓存占用与待完成渲染任务数"));
    statusBar()->addPermanentWidget(metricsLabel_);
    // 验收探针通过 trace 文件运行；正式用户只有显式开启诊断时才看到指标。
    metricsLabel_->setVisible(qEnvironmentVariableIsSet("AIPDF_DIAGNOSTICS") ||
                              qEnvironmentVariableIsSet("AIPDF_TRACE_FILE") ||
                              QCoreApplication::arguments().contains(QStringLiteral("--diagnostics")));
}

QWidget* MainWindow::createWelcomePage() {
    auto* page = new QWidget(this);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(48, 48, 48, 48);
    outer->addStretch();

    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("emptyCard"));
    card->setMaximumWidth(560);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(48, 44, 48, 44);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Folixa"), card);
    title->setObjectName(QStringLiteral("emptyTitle"));
    title->setAlignment(Qt::AlignCenter);
    auto* description = new QLabel(
        tr("专注阅读，也为文档智能留出恰到好处的空间。\n打开 PDF 开始浏览。"), card);
    description->setObjectName(QStringLiteral("emptyDescription"));
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    auto* openButton = new QPushButton(tr("打开 PDF"), card);
    openButton->setObjectName(QStringLiteral("primaryButton"));
    openButton->setMinimumHeight(40);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openPdfDialog);

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(8);
    layout->addWidget(openButton, 0, Qt::AlignCenter);
    outer->addWidget(card, 0, Qt::AlignCenter);
    outer->addStretch();
    return page;
}

QWidget* MainWindow::createNavigationEmptyState(const QString& titleText,
                                                const QString& descriptionText) {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 28, 18, 28);
    auto* title = new QLabel(titleText, page);
    title->setAlignment(Qt::AlignCenter);
    title->setWordWrap(true);
    auto* description = new QLabel(descriptionText, page);
    description->setObjectName(QStringLiteral("emptyDescription"));
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    layout->addStretch();
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addStretch();
    return page;
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

void MainWindow::sendEcho() {
    const QString text = prompt_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    conversation_->append(tr("<div class='user'><b>你</b><br>%1</div>").arg(text.toHtmlEscaped()));
    prompt_->clear();
    workerClient_->sendRequest("ai.echo", {{"text", text}});
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
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    positionFloatingUi();
}

void MainWindow::positionFloatingUi() {
    if (tabs_ == nullptr || aiFloatingButton_ == nullptr || aiQuickCard_ == nullptr) {
        return;
    }
    constexpr int margin = 18;
    aiFloatingButton_->move(tabs_->width() - aiFloatingButton_->width() - margin,
                            tabs_->height() - aiFloatingButton_->height() - margin);
    aiFloatingButton_->raise();
    aiQuickCard_->adjustSize();
    aiQuickCard_->move(tabs_->width() - aiQuickCard_->width() - margin,
                       std::max(margin, tabs_->height() - aiQuickCard_->height() -
                                           aiFloatingButton_->height() - margin * 2));
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
    const QString title = QFileInfo(filePath).fileName();
    if (tabs_->count() == 1 && tabs_->widget(0)->property("welcomePage").toBool()) {
        QWidget* welcome = tabs_->widget(0);
        tabs_->removeTab(0);
        welcome->deleteLater();
    }
    tabs_->addTab(view, title);
    tabs_->setCurrentWidget(view);
    refreshThumbnails(view);
    connect(view, &PdfView::metricsChanged, metricsLabel_, &QLabel::setText);
    connect(view, &PdfView::textCopied, this, [this](int characters) {
        statusBar()->showMessage(tr("已复制 %1 个字符到剪贴板").arg(characters), 3000);
    });
    if (searchPanel_ != nullptr) {
        searchPanel_->setController(view->textController());
    }
    statusBar()->showMessage(tr("已打开 %1（%2 页）").arg(title).arg(view->pageCount()));
}

}  // namespace aipdf::app

