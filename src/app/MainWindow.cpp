#include "app/MainWindow.h"

#include "app/PdfView.h"
#include "ipc/WorkerClient.h"
#include "pdf/PdfiumAdapter.h"

#include <QAction>
#include <QDockWidget>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolBar>
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
    setWindowTitle(QString("AI PDF Editor %1").arg(QStringLiteral(AIPDF_VERSION)));
    resize(1280, 820);

    auto* tabs = new QTabWidget(this);
    tabs->setTabsClosable(true);
    tabs->setDocumentMode(true);
    auto* emptyDocument = new QLabel(
        "Open a PDF to begin\n\nPDFium integration is planned for M1.", tabs);
    emptyDocument->setAlignment(Qt::AlignCenter);
    tabs->addTab(emptyDocument, "Welcome");
    setCentralWidget(tabs);
    tabs_ = tabs;

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openAction = fileMenu->addAction("&Open PDF...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, "Open PDF", {}, "PDF documents (*.pdf)");
        if (!path.isEmpty()) {
            openPdfFile(path);
        }
    });
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", QKeySequence::Quit, this, &QWidget::close);

    auto* toolbar = addToolBar("Document");
    toolbar->setMovable(false);
    toolbar->addAction(openAction);

    auto* navigationDock = new QDockWidget("Navigation", this);
    auto* navigation = new QTabWidget(navigationDock);
    navigation->addTab(new QListWidget(navigation), "Pages");
    navigation->addTab(new QListWidget(navigation), "Bookmarks");
    navigationDock->setWidget(navigation);
    addDockWidget(Qt::LeftDockWidgetArea, navigationDock);

    auto* aiDock = new QDockWidget("AI Assistant", this);
    aiDock->setMinimumWidth(340);
    auto* aiPanel = new QWidget(aiDock);
    auto* aiLayout = new QVBoxLayout(aiPanel);
    workerStatus_ = new QLabel("AI Worker: stopped", aiPanel);
    conversation_ = new QTextBrowser(aiPanel);
    conversation_->setPlaceholderText("Document answers and citations will appear here.");
    prompt_ = new QLineEdit(aiPanel);
    prompt_->setPlaceholderText("Ask about the current document...");
    sendButton_ = new QPushButton("Send", aiPanel);
    sendButton_->setEnabled(false);
    aiLayout->addWidget(workerStatus_);
    aiLayout->addWidget(conversation_, 1);
    aiLayout->addWidget(prompt_);
    aiLayout->addWidget(sendButton_);
    aiDock->setWidget(aiPanel);
    addDockWidget(Qt::RightDockWidgetArea, aiDock);

    connect(sendButton_, &QPushButton::clicked, this, &MainWindow::sendEcho);
    connect(prompt_, &QLineEdit::returnPressed, this, &MainWindow::sendEcho);

    statusBar()->showMessage(QString("AI PDF Editor %1 · M0 development shell")
                                 .arg(QStringLiteral(AIPDF_VERSION)));
    // 性能指标放在状态栏的常驻区：放工具栏会因为文字宽度变化
    // 触发工具栏溢出，导致画布高度变化、滚动范围漂移。
    metricsLabel_ = new QLabel(QStringLiteral("缓存 0 MiB · 待渲染 0"), this);
    metricsLabel_->setToolTip(QStringLiteral("页面缓存占用与待完成渲染任务数"));
    statusBar()->addPermanentWidget(metricsLabel_);
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

