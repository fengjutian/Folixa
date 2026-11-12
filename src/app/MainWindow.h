#pragma once

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void sendHealthCheck();
    void sendEcho();

    ipc::WorkerClient* workerClient_ = nullptr;
    QLabel* workerStatus_ = nullptr;
    QTextBrowser* conversation_ = nullptr;
    QLineEdit* prompt_ = nullptr;
    QPushButton* sendButton_ = nullptr;
};

}  // namespace aipdf::app

