#include "app/MainWindow.h"

#include "app/PdfView.h"
#include "app/SearchPanel.h"
#include "app/NavigationController.h"
#include "app/AnnotationController.h"
#include "ipc/WorkerClient.h"
#include "doc/EditCommand.h"
#include "doc/RecoveryLog.h"
#include "pdf/PdfiumAdapter.h"
#include "pdf/Annotation.h"
#include "pdf/Navigation.h"

#include <QAction>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QDir>
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
#include <QMessageBox>
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
#include <QTreeWidget>
#include <QTreeWidgetItem>
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
        file.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " + message + "\n").toUtf8());
    }
}

// 把大纲树灌进目录树控件。目标页解析不出来的条目照样显示，只是不能点。
void addOutlineItems(QTreeWidgetItem* parent, const std::vector<pdf::OutlineItem>& items) {
    for (const pdf::OutlineItem& item : items) {
        auto* node = new QTreeWidgetItem(parent);
        node->setText(0, item.title);
        node->setData(0, Qt::UserRole, item.pageIndex);
        if (item.pageIndex < 0) {
            node->setToolTip(0, QCoreApplication::translate("MainWindow", "该条目没有可跳转的目标"));
        } else {
            node->setToolTip(0, QCoreApplication::translate("MainWindow", "跳转到第 %1 页")
                                       .arg(item.pageIndex + 1));
        }
        addOutlineItems(node, item.children);
    }
}
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), workerClient_(new ipc::WorkerClient(this)) {
    buildUi();

    connect(workerClient_, &ipc::WorkerClient::stateChanged,
            this, [this](const QString& state) {
                // Worker 状态记进轨迹：验收脚本靠它断言「Worker 就绪」。
                // 界面上的状态标签在默认收起的 AI 面板里，UI Automation 读不到，
                // 而面板文案还会随改版变化——断言绑在轨迹上才稳。
                trace(QStringLiteral("worker state=%1").arg(state));
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
                    trace(QStringLiteral("worker health protocol=%1 version=%2")
                              .arg(payload.value("protocol_version").toInt())
                              .arg(payload.value("worker_version").toString()));
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

    // 启动后看看有没有上次没保存完的工作。
    // 延后到事件循环起来之后再问：这时主窗口已经显示出来了。
    QTimer::singleShot(0, this, &MainWindow::checkRecovery);
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
    // 保存与另存为（v0.8.0）：写临时文件、校验、原子替换，失败不动原文件。
    auto* saveAction = fileMenu->addAction(tr("保存"));
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setObjectName(QStringLiteral("fileSave"));
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveDocument);
    auto* saveAsAction = fileMenu->addAction(tr("另存为…"));
    saveAsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    saveAsAction->setObjectName(QStringLiteral("fileSaveAs"));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveDocumentAs);
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
    appMenu->addAction(saveAction);
    appMenu->addAction(saveAsAction);
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
    commandBar->addSeparator();
    auto* homeAction = commandBar->addAction(tr("阅读"));
    homeAction->setCheckable(true);
    homeAction->setChecked(true);
    // 批注已交付（v0.7.0）：点开就是批注面板。
    auto* annotationAction = commandBar->addAction(tr("批注"));
    annotationAction->setObjectName(QStringLiteral("commandAnnotation"));
    annotationAction->setToolTip(tr("查看与删除批注"));
    connect(annotationAction, &QAction::triggered, this, [this]() {
        navigationDock_->setVisible(true);
        // 切到导航栏的批注页（第五个按钮）。
        showNavigationPage(4);
        trace(QStringLiteral("open annotation panel"));
    });
    // 页面管理已交付（v0.8.0）：点开就是页面页（缩略图 + 页面操作按钮）。
    auto* pageAction = commandBar->addAction(tr("页面"));
    pageAction->setObjectName(QStringLiteral("commandPages"));
    pageAction->setToolTip(tr("整理页面：插入、删除、旋转与移动"));
    connect(pageAction, &QAction::triggered, this, [this]() {
        navigationDock_->setVisible(true);
        // 切到导航栏的页面页（第一个按钮）。
        showNavigationPage(0);
        trace(QStringLiteral("open page panel"));
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
    const QStringList futureCommands = {tr("编辑"), tr("转换"),
                                        tr("保护"), tr("表单"), tr("工具")};
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
    // 导航栏按钮与页签一一对应。
    //
    // 两者必须写在同一张表里：v0.7.0 真的踩过一次——
    // 批注页插在搜索页之前，两处列表的下标就错开了，
    // 点「搜索」打开的是批注页，Ctrl+F 也跟着失灵。
    struct RailEntry {
        QString icon;
        QString tip;
        // 每个按钮给一个独立的对象名：验收脚本靠它定位「目录」「书签」页签，
        // 靠按钮文字（图标按钮没有文字）会随主题改版一起失效。
        QString id;
        QWidget* page = nullptr;
    };
    QVector<RailEntry> railEntries;
    railEntries.append({QStringLiteral(":/icons/pages.svg"), tr("页面"),
                         QStringLiteral("railButton.pages"), createPagesTab()});
    // 目录页与书签页：同一份大纲的两种看法（树 / 扁平列表）。
    railEntries.append({QStringLiteral(":/icons/outline.svg"), tr("目录"),
                         QStringLiteral("railButton.outline"), createOutlineTab()});
    railEntries.append({QStringLiteral(":/icons/bookmark.svg"), tr("书签"),
                         QStringLiteral("railButton.bookmarks"), createBookmarkTab()});
    // 搜索页：全文搜索（v0.5.0）。
    searchPanel_ = new SearchPanel(navigationPages);
    railEntries.append({QStringLiteral(":/icons/search.svg"), tr("搜索"),
                         QStringLiteral("railButton.search"), searchPanel_});
    // 批注页：批注列表（v0.7.0）。
    railEntries.append({QStringLiteral(":/icons/annotation.svg"), tr("批注"),
                         QStringLiteral("railButton.annotations"), createAnnotationTab()});

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
        button->setIcon(QIcon(entry.icon));
        button->setIconSize(QSize(20, 20));
        button->setToolTip(entry.tip);
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
    connect(annotationRemoveButton_, &QPushButton::clicked, this,
            &MainWindow::removeSelectedAnnotation);
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
        navigationDock_->show();
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
        // 目录与书签跟着当前文档走。
        bindNavigation(view);
        // 批注列表也跟着当前文档走。
        bindAnnotations(view);
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
    auto makeButton = [page](const QString& text,
                              const QString& tip,
                              const QString& id) {
        auto* button = new QPushButton(text, page);
        button->setToolTip(tip);
        button->setObjectName(id);
        return button;
    };

    pageInsertButton_ = makeButton(tr("插入"), tr("在当前页后插入一张空白页"),
                                    QStringLiteral("page.insert"));
    pageDeleteButton_ = makeButton(tr("删除"), tr("删除当前页"),
                                    QStringLiteral("page.delete"));
    pageRotateRightButton_ = makeButton(tr("右转"), tr("把当前页顺时针转 90 度"),
                                         QStringLiteral("page.rotateRight"));
    pageRotateLeftButton_ = makeButton(tr("左转"), tr("把当前页逆时针转 90 度"),
                                        QStringLiteral("page.rotateLeft"));
    pageMoveUpButton_ = makeButton(tr("上移"), tr("把当前页往前挪一位"),
                                    QStringLiteral("page.moveUp"));
    pageMoveDownButton_ = makeButton(tr("下移"), tr("把当前页往后挪一位"),
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
        tr("暂无文档目录"), tr("当前文档没有大纲（书签）。")));
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
        tr("暂无书签"), tr("当前文档没有可显示的书签。")));
    bookmarkList_ = new QListWidget(stack);
    bookmarkList_->setObjectName(QStringLiteral("bookmarkList"));
    stack->addWidget(bookmarkList_);
    bookmarkStack_ = stack;
    return stack;
}

// 批注页：批注列表 + 删除按钮（v0.7.0）。
QStackedWidget* MainWindow::createAnnotationTab() {
    auto* stack = new QStackedWidget(navigationPages_);
    stack->addWidget(createNavigationEmptyState(
        tr("暂无批注"), tr("选中文字后点「高亮」，或用便签、矩形、墨迹工具添加批注。")));

    auto* content = new QWidget(stack);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    annotationList_ = new QListWidget(content);
    annotationList_->setObjectName(QStringLiteral("annotationList"));
    layout->addWidget(annotationList_, 1);
    annotationRemoveButton_ = new QPushButton(tr("删除所选批注"), content);
    annotationRemoveButton_->setObjectName(QStringLiteral("annotationRemove"));
    layout->addWidget(annotationRemoveButton_);

    stack->addWidget(content);
    annotationStack_ = stack;
    return stack;
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

    pageInsertButton_->setEnabled(hasDocument);
    pageDeleteButton_->setEnabled(hasDocument && count > 1);
    pageRotateRightButton_->setEnabled(hasDocument);
    pageRotateLeftButton_->setEnabled(hasDocument);
    pageMoveUpButton_->setEnabled(hasDocument && current > 0);
    pageMoveDownButton_->setEnabled(hasDocument && current >= 0 && current < count - 1);
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
    navigationPages_->setVisible(true);
    resizeDocks({navigationDock_}, {224}, Qt::Horizontal);
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
    bindNavigation(view);
    bindAnnotations(view);
    connect(view, &PdfView::metricsChanged, metricsLabel_, &QLabel::setText);
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
    connect(view, &PdfView::currentPageChanged, this, [this](int index) {
        highlightThumbnail(index);
        updatePageActionState();
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
    statusBar()->showMessage(tr("已打开 %1（%2 页）").arg(title).arg(view->pageCount()));
}

}  // namespace aipdf::app

