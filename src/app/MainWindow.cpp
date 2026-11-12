#include "app/MainWindow.h"

#include "app/PdfView.h"
#include "ipc/WorkerClient.h"
#include "pdf/PdfiumAdapter.h"

#include <QAction>
#include <QDockWidget>
#include <QDateTime>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QSettings>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

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
                workerStatus_->setText("AI Worker: " + state);
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
                        QString("<b>System:</b> Worker is ready (protocol %1, worker %2).")
                            .arg(payload.value("protocol_version").toInt())
                            .arg(payload.value("worker_version").toString().toHtmlEscaped()));
                } else if (payload.contains("text")) {
                    conversation_->append("<b>Worker:</b> " +
                                          payload.value("text").toString().toHtmlEscaped());
                } else {
                    conversation_->append("<b>Worker:</b> " +
                        QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)).toHtmlEscaped());
                }
            });
    connect(workerClient_, &ipc::WorkerClient::requestFailed,
            this, [this](const QString&, const QString& message) {
                conversation_->append("<b>Error:</b> " + message.toHtmlEscaped());
            });

    workerClient_->start();
}

void MainWindow::buildUi() {
    setWindowTitle(QString("Folixa %1").arg(QStringLiteral(AIPDF_VERSION)));
    resize(1440, 900);

    auto* tabs = new QTabWidget(this);
    tabs->setTabsClosable(true);
    tabs->setDocumentMode(true);
    tabs->addTab(createWelcomePage(), tr("欢迎"));
    setCentralWidget(tabs);
    tabs_ = tabs;

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openAction = fileMenu->addAction("&Open PDF...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openPdfDialog);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", QKeySequence::Quit, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(tr("视图(&V)"));

    navigationDock_ = new QDockWidget(tr("导航"), this);
    navigationDock_->setObjectName(QStringLiteral("navigationDock"));
    navigationDock_->setMinimumWidth(180);
    navigationDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    auto* navigation = new QTabWidget(navigationDock_);
    navigation->addTab(createNavigationEmptyState(
                           tr("暂无页面缩略图"), tr("缩略图与页面跳转将在导航功能中提供。")),
                       tr("页面"));
    navigation->addTab(createNavigationEmptyState(
                           tr("暂无书签"), tr("当前文档没有可显示的书签。")),
                       tr("书签"));
    navigationDock_->setWidget(navigation);
    addDockWidget(Qt::LeftDockWidgetArea, navigationDock_);
    viewMenu->addAction(navigationDock_->toggleViewAction());

    aiDock_ = new QDockWidget(tr("AI 助手"), this);
    aiDock_->setObjectName(QStringLiteral("aiDock"));
    aiDock_->setMinimumWidth(280);
    aiDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    auto* aiPanel = new QWidget(aiDock_);
    auto* aiLayout = new QVBoxLayout(aiPanel);
    workerStatus_ = new QLabel(tr("AI 服务：正在启动"), aiPanel);
    workerStatus_->setObjectName(QStringLiteral("secondaryText"));
    conversation_ = new QTextBrowser(aiPanel);
    conversation_->setPlaceholderText(tr("打开文档后，可以在这里提问。\n\n真实模型与引用功能将在后续版本接入。"));
    prompt_ = new QLineEdit(aiPanel);
    prompt_->setPlaceholderText(tr("询问当前文档…"));
    sendButton_ = new QPushButton(tr("发送"), aiPanel);
    sendButton_->setObjectName(QStringLiteral("aiButton"));
    sendButton_->setEnabled(false);
    aiLayout->addWidget(workerStatus_);
    aiLayout->addWidget(conversation_, 1);
    aiLayout->addWidget(prompt_);
    aiLayout->addWidget(sendButton_);
    aiDock_->setWidget(aiPanel);
    addDockWidget(Qt::RightDockWidgetArea, aiDock_);
    viewMenu->addAction(aiDock_->toggleViewAction());

    auto* focusAction = viewMenu->addAction(tr("专注模式"));
    focusAction->setCheckable(true);
    focusAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    connect(focusAction, &QAction::toggled, this, [this](bool enabled) {
        navigationDock_->setVisible(!enabled);
        aiDock_->setVisible(!enabled);
    });

    QSettings settings;
    navigationDock_->setVisible(settings.value(QStringLiteral("workspace/navigationVisible"), true).toBool());
    aiDock_->setVisible(settings.value(QStringLiteral("workspace/aiVisible"), false).toBool());
    connect(navigationDock_, &QDockWidget::visibilityChanged, this, [](bool visible) {
        QSettings().setValue(QStringLiteral("workspace/navigationVisible"), visible);
    });
    connect(aiDock_, &QDockWidget::visibilityChanged, this, [](bool visible) {
        QSettings().setValue(QStringLiteral("workspace/aiVisible"), visible);
    });

    connect(sendButton_, &QPushButton::clicked, this, &MainWindow::sendEcho);
    connect(prompt_, &QLineEdit::returnPressed, this, &MainWindow::sendEcho);

    statusBar()->showMessage(tr("就绪"));
    // 性能指标放在状态栏的常驻区：放工具栏会因为文字宽度变化
    // 触发工具栏溢出，导致画布高度变化、滚动范围漂移。
    metricsLabel_ = new QLabel(QStringLiteral("缓存 0 MiB · 待渲染 0"), this);
    metricsLabel_->setToolTip(QStringLiteral("页面缓存占用与待完成渲染任务数"));
    statusBar()->addPermanentWidget(metricsLabel_);
    // 验收探针通过 trace 文件运行；正式用户只有显式开启诊断时才看到指标。
    metricsLabel_->setVisible(qEnvironmentVariableIsSet("AIPDF_DIAGNOSTICS") ||
                              qEnvironmentVariableIsSet("AIPDF_TRACE_FILE"));
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
    conversation_->append("<b>You:</b> " + text.toHtmlEscaped());
    prompt_->clear();
    workerClient_->sendRequest("ai.echo", {{"text", text}});
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
    tabs_->addTab(view, title);
    tabs_->setCurrentWidget(view);
    connect(view, &PdfView::metricsChanged, metricsLabel_, &QLabel::setText);
    statusBar()->showMessage(tr("已打开 %1（%2 页）").arg(title).arg(view->pageCount()));
}

}  // namespace aipdf::app

