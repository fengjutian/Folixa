#pragma once

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTextBrowser;
class QDockWidget;
class QListWidget;
class QStackedWidget;
class QButtonGroup;
class QFrame;
class QResizeEvent;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

class PdfView;
class SearchPanel;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // 打开 PDF 文件；失败时在状态栏显示原因，不弹模态框。
    void openPdfFile(const QString& filePath);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    QWidget* createWelcomePage();
    QWidget* createNavigationEmptyState(const QString& title, const QString& description);
    void openPdfDialog();
    void sendHealthCheck();
    void sendEcho();
    void refreshThumbnails(PdfView* view);
    void positionFloatingUi();

    ipc::WorkerClient* workerClient_ = nullptr;
    QLabel* workerStatus_ = nullptr;
    // 状态栏常驻的性能指标（缓存占用、待渲染任务数）。
    QLabel* metricsLabel_ = nullptr;
    QTextBrowser* conversation_ = nullptr;
    QLineEdit* prompt_ = nullptr;
    QPushButton* sendButton_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QDockWidget* navigationDock_ = nullptr;
    QDockWidget* aiDock_ = nullptr;
    QListWidget* pagesList_ = nullptr;
    // 导航栏的四个视图（页面 / 目录 / 书签 / 搜索）。
    QStackedWidget* navigationPages_ = nullptr;
    QButtonGroup* navigationRail_ = nullptr;
    SearchPanel* searchPanel_ = nullptr;
    QFrame* aiQuickCard_ = nullptr;
    QPushButton* aiFloatingButton_ = nullptr;
    bool navigationWasVisible_ = true;
    bool aiWasVisible_ = false;
};

}  // namespace aipdf::app

