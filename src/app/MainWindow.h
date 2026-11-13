#pragma once

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTextBrowser;
class QDockWidget;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QButtonGroup;
class QFrame;
class QResizeEvent;
class QTreeWidget;
class QTreeWidgetItem;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

class PdfView;
class SearchPanel;
class NavigationController;

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
    QStackedWidget* createOutlineTab();
    QStackedWidget* createBookmarkTab();
    void openPdfDialog();
    void sendHealthCheck();
    void sendEcho();
    void refreshThumbnails(PdfView* view);
    // 把导航面板绑到当前文档的导航控制器上，并刷新目录与书签。
    void bindNavigation(PdfView* view);
    void refreshOutline(PdfView* view);
    // 当前标签页的文档视图；不是文档页时返回 nullptr。
    PdfView* currentView() const;
    // 目录树里点到某个条目：跳到它的目标页。
    void navigateToOutlineTarget(int pageIndex, const QString& title);
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
    // 目录页：大纲树（v0.6.0）。索引 0 是空状态，索引 1 是树。
    QStackedWidget* outlineStack_ = nullptr;
    QTreeWidget* outlineTree_ = nullptr;
    // 书签页：同一份大纲的扁平列表（v0.6.0）。
    QStackedWidget* bookmarkStack_ = nullptr;
    QListWidget* bookmarkList_ = nullptr;
    // 当前绑定的导航控制器；换文档时要把上一个的连接断掉。
    NavigationController* boundNavigation_ = nullptr;
    QMetaObject::Connection navigationConnection_;
    QButtonGroup* navigationRail_ = nullptr;
    SearchPanel* searchPanel_ = nullptr;
    QFrame* aiQuickCard_ = nullptr;
    QPushButton* aiFloatingButton_ = nullptr;
    bool navigationWasVisible_ = true;
    bool aiWasVisible_ = false;
};

}  // namespace aipdf::app

