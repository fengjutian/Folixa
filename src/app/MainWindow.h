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
class IndexPanel;
class NavigationController;
class AnnotationController;

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
    QWidget* createPagesTab();
    QStackedWidget* createOutlineTab();
    QStackedWidget* createBookmarkTab();
    QStackedWidget* createAnnotationTab();
    void openPdfDialog();
    void sendHealthCheck();
    void sendEcho();
    void refreshThumbnails(PdfView* view);
    // 在缩略图列表里高亮当前页。
    void highlightThumbnail(int pageIndex);
    // 把导航面板绑到当前文档的导航控制器上，并刷新目录与书签。
    void bindNavigation(PdfView* view);
    void refreshOutline(PdfView* view);
    // 把批注面板绑到当前文档的批注控制器上，并刷新批注列表。
    void bindAnnotations(PdfView* view);
    void refreshAnnotations(PdfView* view);
    // 把索引面板绑到当前文档的索引控制器上。
    void bindIndex(PdfView* view);
    // 删除批注面板里选中的批注。
    void removeSelectedAnnotation();
    // ------------------------------------------------------- 页面管理 ----
    // 下面几个都作用在当前页上（页面页的按钮）。
    void insertBlankPage();
    void deleteCurrentPage();
    void rotateCurrentPageRight();
    void rotateCurrentPageLeft();
    void moveCurrentPageUp();
    void moveCurrentPageDown();
    // 更新页面管理按钮的可用状态。
    void updatePageActionState();
    // ---------------------------------------------------------- 保存 ----
    void saveDocument();
    void saveDocumentAs();
    // 启动时看看有没有上次没保存完的工作。
    void checkRecovery();
    // 当前标签页的文档视图；不是文档页时返回 nullptr。
    PdfView* currentView() const;
    // 目录树里点到某个条目：跳到它的目标页。
    void navigateToOutlineTarget(int pageIndex, const QString& title);
    // 切换导航页签时把该页签的条目数记进轨迹（验收脚本据此断言面板真的有内容）。
    void traceNavigationPanel(int index);
    // 切到导航栏的某一页并把它显示出来。
    //
    // 导航栏自己的图标是开关：再点一次当前页会把面板收起来；
    // 命令栏的「页面」「批注」是导航命令，不该做开关——连点两次把面板关掉
    // 会让人以为功能坏了。
    void showNavigationPage(int index);
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
    // 批注页：批注列表（v0.7.0）。
    QStackedWidget* annotationStack_ = nullptr;
    QListWidget* annotationList_ = nullptr;
    QPushButton* annotationRemoveButton_ = nullptr;
    // 页面页的页面管理按钮（v0.8.0）。
    QPushButton* pageInsertButton_ = nullptr;
    QPushButton* pageDeleteButton_ = nullptr;
    QPushButton* pageRotateRightButton_ = nullptr;
    QPushButton* pageRotateLeftButton_ = nullptr;
    QPushButton* pageMoveUpButton_ = nullptr;
    QPushButton* pageMoveDownButton_ = nullptr;
    // 当前绑定的批注控制器；换文档时要把上一个的连接断掉。
    AnnotationController* boundAnnotations_ = nullptr;
    QMetaObject::Connection annotationConnection_;
    // 当前绑定的导航控制器；换文档时要把上一个的连接断掉。
    NavigationController* boundNavigation_ = nullptr;
    QMetaObject::Connection navigationConnection_;
    QButtonGroup* navigationRail_ = nullptr;
    SearchPanel* searchPanel_ = nullptr;
    // 索引页：本地索引状态、检索与结果（v0.9.0）。
    IndexPanel* indexPanel_ = nullptr;
    QFrame* aiQuickCard_ = nullptr;
    QPushButton* aiFloatingButton_ = nullptr;
    bool navigationWasVisible_ = true;
    bool aiWasVisible_ = false;
};

}  // namespace aipdf::app

