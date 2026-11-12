#pragma once

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTextBrowser;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // 打开 PDF 文件；失败时在状态栏显示原因，不弹模态框。
    void openPdfFile(const QString& filePath);

private:
    void buildUi();
    void sendHealthCheck();
    void sendEcho();

    ipc::WorkerClient* workerClient_ = nullptr;
    QLabel* workerStatus_ = nullptr;
    // 状态栏常驻的性能指标（缓存占用、待渲染任务数）。
    QLabel* metricsLabel_ = nullptr;
    QTextBrowser* conversation_ = nullptr;
    QLineEdit* prompt_ = nullptr;
    QPushButton* sendButton_ = nullptr;
    QTabWidget* tabs_ = nullptr;
};

}  // namespace aipdf::app

