#pragma once

#include <QMainWindow>

#include "i18n/Translate.h"

class QAction;
class QActionGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QDoubleSpinBox;
class QSlider;
class QTableWidget;
class QLineEdit;
#include <QColor>
class QTabWidget;
class QTextBrowser;
class QDockWidget;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QButtonGroup;
class QFrame;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QResizeEvent;
namespace aipdf::app { class OcrController; }
class QTreeWidget;
class QTreeWidgetItem;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

class PdfView;
class SearchPanel;
class IndexPanel;
class AiPanel;
class RibbonBar;
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
    // 拖放打开：窗口任意位置松手都能收下 PDF，不必先找准落点。
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildUi();
    // 只有欢迎页时把标签条整条收起来：一个标签的标签条只是浪费一行高度。
    void syncTabBar();
    QWidget* createWelcomePage();
    // 把刚打开的文件记到最近文档的最前面（去重、限量）。
    void rememberRecentFile(const QString& filePath);
    // 空状态页：标题与说明的源文案传进来，切换语言时按同一份文案重译。
    QWidget* createNavigationEmptyState(const char* titleSource,
                                        const char* descriptionSource);
    QWidget* createPagesTab();
    QStackedWidget* createOutlineTab();
    QStackedWidget* createBookmarkTab();
    QWidget* createAnnotationTab();
    QWidget* createFormTab();
    void refreshForms(PdfView* view);
    void applyFormValue();
    void flattenCurrentPageForms();
    void exportFormCsv();
    void importFormCsv();
    static QString formFieldTypeName(int type);
    void openPdfDialog();
    void sendHealthCheck();
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
    void replyToSelectedAnnotation();
    void applyAnnotationAppearance();
    void pickAnnotationColor();
    void setSelectedAnnotationState();
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
    // 展开/收起导航面板：除页面对应关系外，还要把 dock 的宽度一起管住。
    void setNavigationExpanded(bool expanded);
    // 按当前文档与选区状态刷新 AI 快捷卡的动作与说明。
    void updateAiQuickActions();
    // ---------------------------------------------------------- 语言 ----
    // 切换界面语言：交给 LanguageManager，重译由 languageChanged 信号驱动。
    void applyLanguage(const QString& preference);
    // 就地重译：重跑登记的文案，并把随状态变化的标签也刷一遍。
    void retranslateUi();
    // 左侧面板顶部的标题（页面/目录/书签/搜索/批注/表单/索引）。
    void updateNavigationHeader(int index);
    void updateStatusPage(int pageIndex);
    void updateStatusZoom(double factor);
    // 语言菜单的勾选状态跟当前选择同步。
    void syncLanguageMenu();
    // 欢迎页标签的文案跟着语言走（标签页是动态建的，单独登记）。
    void bindWelcomeTab(QWidget* page);

    ipc::WorkerClient* workerClient_ = nullptr;
    // 状态栏常驻的性能指标（缓存占用、待渲染任务数）。
    QLabel* metricsLabel_ = nullptr;
    QLabel* navigationHeader_ = nullptr;
    // 底部页码/缩放控件（对齐福昕）。
    QLineEdit* statusPageEdit_ = nullptr;
    QLabel* statusPageTotal_ = nullptr;
    QLabel* statusZoomLabel_ = nullptr;
    QSlider* statusZoomSlider_ = nullptr;
    bool updatingStatusZoom_ = false;
    // AI 面板（v0.10.0）：三个受限功能 + 流式显示。
    AiPanel* aiPanel_ = nullptr;
    // 顶部 Ribbon（文件 / 主页 / 注释 / 页面管理 / 视图 / 保护 / AI）。
    RibbonBar* ribbonBar_ = nullptr;
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
    QPushButton* annotationReplyButton_ = nullptr;
    QPushButton* annotationColorButton_ = nullptr;
    QPushButton* annotationApplyAppearanceButton_ = nullptr;
    QDoubleSpinBox* annotationWidthSpin_ = nullptr;
    QSlider* annotationOpacitySlider_ = nullptr;
    QStackedWidget* formStack_ = nullptr;
    QTableWidget* formTable_ = nullptr;
    QLineEdit* formValueEdit_ = nullptr;
    QPushButton* formApplyButton_ = nullptr;
    QPushButton* formFlattenButton_ = nullptr;
    QPushButton* formExportCsvButton_ = nullptr;
    QPushButton* formImportCsvButton_ = nullptr;
    QPushButton* formExportFdfButton_ = nullptr;
    QPushButton* formImportFdfButton_ = nullptr;
    QColor annotationPendingColor_ = QColor(255, 214, 0);
    QPushButton* annotationStateButton_ = nullptr;
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
    // OCR 编排：判定 → 渲染 → 识别 → 入索引（v0.13.0）。
    OcrController* ocrController_ = nullptr;
    QFrame* aiQuickCard_ = nullptr;
    QPushButton* aiFloatingButton_ = nullptr;
    // 快捷卡的上下文说明（没有文档/选区时这里写明原因）。
    QLabel* aiQuickSubtitle_ = nullptr;
    // 快捷卡上的动作按钮，顺序与 quickActions 一致；按上下文启用/禁用。
    QVector<QPushButton*> aiQuickButtons_;
    bool navigationWasVisible_ = true;
    bool aiWasVisible_ = false;
    // 用户是否希望左侧导航栏可见（与「欢迎页临时收起」区分开）。
    bool navigationUserVisible_ = true;
    // 欢迎页临时收起了导航栏；打开文档时要把它放回来。
    bool navHiddenForWelcome_ = false;

    // 一次设定、长期显示的界面文案：切换语言时按登记顺序重跑。
    i18n::TextBindings texts_;
    QActionGroup* languageActions_ = nullptr;
    QAction* followSystemAction_ = nullptr;
};

}  // namespace aipdf::app

