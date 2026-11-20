#pragma once

#include <QMainWindow>
#include <QHash>
#include <QPointer>

#include "i18n/Translate.h"
#include "app/controllers/DocumentConverter.h"
#include "app/recording/RecordingLibrary.h"
#include "pdf/kernel/Navigation.h"

class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QDoubleSpinBox;
class QSlider;
class QTableWidget;
class QLineEdit;
#include <QColor>
#include <QToolButton>
class QTabWidget;
class QTimer;
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
class QToolButton;
class QFileSystemWatcher;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

class PdfView;
class SearchPanel;
class IndexPanel;
class AttachmentPanel;
class AiPanel;
class RibbonBar;
class NavigationController;
class AnnotationController;
class WiringActions;
class RecordingLibrary;
class ScreenRecorder;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // 打开 PDF 文件；失败时在状态栏显示原因，不弹模态框。
    void openPdfFile(const QString& filePath);



protected:
    void resizeEvent(QResizeEvent* event) override;
    // 拖放打开：窗口任意位置松手都能收下 PDF，不必先找准落点。
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    // 全局鼠标事件过滤：沉浸阅读时光标自动隐藏靠它。
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    // 只有工作台时把标签条整条收起来：一个标签的标签条只是浪费一行高度。
    void syncTabBar();
    // 工作台是真正的页签：点它回到最近文档页，没有就按需重建。
    void showWorkspace();
    // 当前停在工作台还是某份文档上：决定顶部按钮的选中态与 Ribbon 的显隐。
    bool isWorkspaceCurrent() const;
    QWidget* createWelcomePage();
    // 把刚打开的文件记到最近文档的最前面（去重、限量）。
    void rememberRecentFile(const QString& filePath);
    // 让工作台里的「最近打开」跟着设置里的那份走：刚打开、被删都要就地更新。
    void refreshWelcomeRecentFiles();
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
    // 「表单到工作表」：导出 Excel 工作簿（.xlsx），也保留 CSV。
    void exportFormWorksheet();
    void importFormCsv();
    // 表单数据的 XFDF 导入导出（与批注的 XFDF 分开，避免对方软件认不得）。
    void exportFormXfdf();
    void importFormXfdf();
    // 导入/导出前先选格式（CSV / FDF / XFDF）。
    void exportFormDataDialog();
    void importFormDataDialog();
    // 表单脚本：跑一段脚本并把结果写回字段；按次序执行各字段的计算脚本。
    void runFormScript();
    void showFormCalculationOrder();
    // 重置表单：有默认值的恢复默认值，没有的清空。
    void resetForms();
    // 表单模板：把当前页的域存成模板 / 按模板在当前页建域。
    void saveFormTemplate();
    void applyFormTemplate();
    // 表单工具设置：新建域时默认的字体、字号与颜色。
    void configureFormTools();
    // 设计助手：用一句描述让模型给出字段设计，人确认后才建域。
    void designFormWithAssistant();
    // 表单自动识别：从没有 AcroForm 域的页面上认出线框字段，人确认后才建域。
    void detectFieldlessForm();
    static QString formFieldTypeName(int type);
    void openPdfDialog();
    void sendHealthCheck();
    void refreshThumbnails(PdfView* view);
    // 缩略图分批补图标：列表先建全（可跳转、可高亮），图标在事件循环里一批批
    // 渲染，避免打开几百页的文档时一次性卡住界面。
    void renderNextThumbnailBatch();
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
    // 把附件面板绑到当前文档上，并刷新附件列表。
    void bindAttachments(PdfView* view);
    // 附件面板的四个动作：添加、打开、保存副本、删除。
    void addDocumentAttachment();
    void openDocumentAttachment(const QString& name);
    void saveDocumentAttachment(const QString& name);
    void removeDocumentAttachment(const QString& name);
    // 删除批注面板里选中的批注（多选时一次删一批）。
    void removeSelectedAnnotation();
    // 保持工具选择：把当前选中的批注工具标记为「放完保持激活」。
    void applyKeepToolSelection(bool retained);
    // 工具换了以后刷新「保持工具选择」按钮的勾选状态。
    void refreshKeepToolButton();
    // 当前选中的批注工具 id（没选工具时返回空串）。
    QString currentAnnotationToolId(PdfView* view) const;
    // 工具 id → 界面上的中文名。
    static QString annotationToolDisplayName(const QString& toolId);
    void replyToSelectedAnnotation();
    void applyAnnotationAppearance();
    void pickAnnotationColor();
    void setSelectedAnnotationState();
    // 给批注面板里选中的批注写替代文本（/Alt）。
    void setSelectedAnnotationAltText();
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
    void updatePermissionActionState();
    // ---------------------------------------------------------- 保存 ----
    void saveDocument();
    void saveDocumentAs();
    // ---------------------------------------------------------- 共享 ----
    // 共享功能只使用本机能力：默认邮件客户端、文件链接与 XFDF 审阅交换。
    void shareByEmail();
    void copyDocumentLink();
    void showReviewTracker();
    void batchRenameDocuments();
    void convertDocument(ConversionKind kind);
    void digitallySignDocument();
    void verifyDigitalSignatures();
    void showDocumentOverview();
    void editDocumentTags();
    void editDocumentCategory();
    void toggleDocumentStar();
    void openDocumentCabinet();
    void configureAutoImport();
    void importWatchedDirectory(const QString& directory);
    void archiveInvoice();
    void openInvoiceManager();
    void createReimbursementList();
    void batchConvertOfd();
    // 安全文档：在隔离进程里只读打开一份 PDF（主进程不打开它）。
    void openSafeDocument();
    void showRecordingCenter();
    void startScreenRecording(bool presentPdf);
    // 声音录制：录一段麦克风，产出 WAV 文件并登记进录制列表。
    void startAudioRecording();
    // 把音视频挂成页面附件：来源可以是录制中心的产物，也可以是本机文件。
    //
    // 边界：挂上去的是**附件**，页面里不播放 —— 用户点开附件时由阅读器
    // 交给系统上注册的外部程序去放（PDF 页面内嵌播放是另一件事）。
    void attachMediaToCurrentPage();
    // 结束当前录制（用户点了「停止录制」，或 PDF 放映结束）。
    void finishScreenRecording();
    // 把一次录制登记进录制列表。
    void registerRecording(RecordingKind kind, const QString& path, qint64 durationMs,
                           int width, int height);
    // 录制中的状态条：红点 + 计时 + 停止按钮。
    void showRecordingIndicator();
    void hideRecordingIndicator();
    void updateRecordingIndicator();
    // 录制列表（惰性创建，全应用共用一份索引）。
    RecordingLibrary* recordingLibrary();
    void captureWindowScreenshot();
    // 沉浸阅读：藏掉界面外壳、全屏、光标自动隐藏。Esc 退出。
    void enterImmersiveReading();
    void leaveImmersiveReading();
    bool immersiveReadingActive() const;
    // 无障碍配置向导：一步步把界面字号、阅读配色、导航面板与朗读配好。
    void showAccessibilityWizard();
    // 手型工具：按住左键拖动平移视图。Ribbon 上的开关走这里。
    void setPanModeEnabled(bool enabled);
    bool panModeEnabled() const;
    void openRecordingFolder();
    PdfView* shareDocument();
    PdfView* savedShareDocument();
    // 启动时看看有没有上次没保存完的工作。
    void checkRecovery();
    // 当前标签页的文档视图；不是文档页时返回 nullptr。
    PdfView* currentView() const;
    // 切换阅读配色（夜间 / 护眼 / 高对比）。
    void changeReadingScheme();
    // 视图设置面板：集中设置缩放、页面布局、侧栏显示与视图旋转。
    void showViewSettingsDialog();
    // 目录树里点到某个条目：跳到它的目标页。
    void navigateToOutlineTarget(int pageIndex, const QString& title);
    void addCurrentPageBookmark();
    void renameSelectedBookmark();
    void deleteSelectedBookmark();
    // 书签的结构编辑：同级换位（排序）与层级调整（升级/降级）。
    void moveBookmarkUp();
    void moveBookmarkDown();
    void indentBookmark();
    void outdentBookmark();
    // 用树视图改书签层级：拖拽改层级，改完整体写回文档。
    void editBookmarkHierarchy();
    // 书签结构编辑的公共准备：取选中书签的行号、整棵大纲与它在大纲树里的路径。
    // 失败时已经给过用户提示，这里返回 false。
    bool currentBookmarkContext(std::vector<pdf::OutlineItem>* outline, int* row,
                                std::vector<int>* path);
    // 切换导航页签时把该页签的条目数记进轨迹（验收脚本据此断言面板真的有内容）。
    void traceNavigationPanel(int index);
    // 切到导航栏的某一页并把它显示出来。
    //
    // 导航栏自己的图标是开关：再点一次当前页会把面板收起来；
    // 命令栏的「页面」「批注」是导航命令，不该做开关——连点两次把面板关掉
    // 会让人以为功能坏了。
    void showNavigationPage(int index);
    // 命令入口（命令栏 / 菜单 / 快捷键）打开导航栏的统一入口。
    //
    // 工作台上没有文档：七个页签都没有对象可指。命令在这里被拦掉，
    // 既不展开面板，也不把 dock 重新 show 出来——否则会露出一张空缩略图
    // 和六个点不出结果的按钮。返回 false 表示当前没有文档、命令没生效。
    bool openNavigationPanel(int index);
    void positionFloatingUi();
    void positionStatusBarUi();
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
    // 文档带大纲时自动把左侧面板展开到「目录」：一打开就看得见章节，
    // 不用先猜导航栏那个图标是干什么的。同一个文档只自动展开一次。
    void revealOutlineForDocument(PdfView* view);
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
    QWidget* statusDocumentControls_ = nullptr;
    QToolButton* statusSettingsButton_ = nullptr;
    QLabel* statusMessageLabel_ = nullptr;
    bool updatingStatusZoom_ = false;
    // AI 面板（v0.10.0）：三个受限功能 + 流式显示。
    AiPanel* aiPanel_ = nullptr;
    // 第二批接线（E/F/G/H）的动作集合：新建/打印/AI 五项/转换导出/注释三件套。
    WiringActions* wiringActions_ = nullptr;
    // 顶部 Ribbon（文件 / 主页 / 注释 / 页面管理 / 视图 / 保护 / AI）。
    RibbonBar* ribbonBar_ = nullptr;
    // Ribbon 所在的顶部 Dock：欢迎页整条收起来（没有文档时标签条上的每一页都是空命令组）。
    QDockWidget* ribbonDock_ = nullptr;
    // 命令栏里的全局搜索框：欢迎页没有搜索对象，跟着 Ribbon 一起收。
    QLineEdit* globalSearch_ = nullptr;
    // 它在工具条里的动作：收起靠这个，靠控件自身的 setVisible 会被重排顶掉。
    QAction* globalSearchAction_ = nullptr;
    // 命令栏左端的「工作台」页签：选中态表示当前停在工作台页。
    QToolButton* workspaceButton_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QDockWidget* navigationDock_ = nullptr;
    // 主菜单里的「显示导航面板」开关：工作台上没有文档，这个开关置灰。
    QAction* navigationPanelAction_ = nullptr;
    // 视图菜单里的同名开关（QDockWidget::toggleViewAction），同样要跟着置灰。
    QAction* navigationViewToggleAction_ = nullptr;
    QDockWidget* aiDock_ = nullptr;
    QListWidget* pagesList_ = nullptr;
    QTimer* thumbnailTimer_ = nullptr;
    QPointer<PdfView> thumbnailView_;
    int thumbnailNextIndex_ = 0;
    // 导航栏的四个视图（页面 / 目录 / 书签 / 搜索）。
    QStackedWidget* navigationPages_ = nullptr;
    // 目录页：大纲树（v0.6.0）。索引 0 是空状态，索引 1 是树。
    QStackedWidget* outlineStack_ = nullptr;
    QTreeWidget* outlineTree_ = nullptr;
    // 自动展开过目录面板的文档视图。用 QPointer 是为了视图被关掉时自动置空，
    // 不会拿一个已经销毁的指针去做比较。
    QPointer<QObject> outlineAutoRevealedFor_;
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
    // 批注筛选栏：按类型与关键字筛，再一次选中筛出来的全部。
    QComboBox* annotationFilterTypeBox_ = nullptr;
    QLineEdit* annotationFilterKeywordEdit_ = nullptr;
    QPushButton* annotationFilterApplyButton_ = nullptr;
    QPushButton* annotationFilterSelectAllButton_ = nullptr;
    QStackedWidget* formStack_ = nullptr;
    QTableWidget* formTable_ = nullptr;
    QLineEdit* formValueEdit_ = nullptr;
    QToolButton* formBoldButton_ = nullptr;
    QToolButton* formItalicButton_ = nullptr;
    QToolButton* formColorButton_ = nullptr;
    QColor formValueColor_;
    // 输入框里「上一次被接受的值」：击键被字段脚本拒绝时回退到它。
    QString formValueText_;
    QPushButton* formApplyButton_ = nullptr;
    QPushButton* formFlattenButton_ = nullptr;
    QPushButton* formPropertiesButton_ = nullptr;
    QPushButton* formExportCsvButton_ = nullptr;
    QPushButton* formImportCsvButton_ = nullptr;
    QPushButton* formExportFdfButton_ = nullptr;
    QPushButton* formImportFdfButton_ = nullptr;
    QPushButton* formExportXfdfButton_ = nullptr;
    QPushButton* formImportXfdfButton_ = nullptr;
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
    AnnotationController* boundAnnotations_ = nullptr;    QMetaObject::Connection annotationConnection_;
    // 工具切换的连接（换文档时也要断，否则旧控制器的信号还会来）。
    QMetaObject::Connection annotationToolConnection_;
    // 「保持工具选择」按钮：勾选状态跟着当前选中的工具走。
    QToolButton* keepToolButton_ = nullptr;
    // 沉浸阅读：进入前记下界面原来的样子，退出时照原样恢复。
    bool immersiveReading_ = false;
    // 专注模式（藏面板）是否开着：沉浸阅读要看它决定要不要再藏一次。
    bool focusModeEnabled_ = false;
    bool immersiveRibbonWasVisible_ = true;
    bool immersiveTabsWereVisible_ = true;
    bool immersiveStatusWasVisible_ = true;
    bool immersiveNavigationWasVisible_ = true;
    bool immersiveAiWasVisible_ = true;
    bool immersiveWindowWasFullScreen_ = false;
    // 光标自动隐藏的计时器：鼠标静止多久之后把光标藏起来。
    QTimer* immersiveCursorTimer_ = nullptr;
    // 手型工具的开关按钮（主页与视图各一个，状态同步）。
    QVector<QToolButton*> panToolButtons_;
    // 用户是否要开手型工具：换文档时要保持（换视图不能把这个状态弄丢）。
    bool panModeEnabled_ = false;
    // 正在把状态同步到按钮上：这时按钮的 toggled 不该再反过来改状态。
    bool panModeSyncing_ = false;
    // 当前绑定的导航控制器；换文档时要把上一个的连接断掉。
    NavigationController* boundNavigation_ = nullptr;
    QMetaObject::Connection navigationConnection_;
    QButtonGroup* navigationRail_ = nullptr;
    SearchPanel* searchPanel_ = nullptr;
    // 索引页：本地索引状态、检索与结果（v0.9.0）。
    IndexPanel* indexPanel_ = nullptr;
    // 附件页：文档级嵌入文件（/Names → /EmbeddedFiles）。
    AttachmentPanel* attachmentPanel_ = nullptr;
    // 当前绑定的附件变更连接；换文档时要把上一个的断掉。
    QMetaObject::Connection attachmentsConnection_;
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
    // 语言菜单本体与它末尾的分隔线：新装的语言要插在分隔线**之前**，
    // 否则会排到「安装语言包…」后面，看着不像语言项。
    QMenu* languageMenu_ = nullptr;
    QAction* languageSeparator_ = nullptr;
    QFileSystemWatcher* autoImportWatcher_ = nullptr;
    QHash<QString, qint64> autoImportSizes_;

    // 录制中心：录制列表索引、当前的录制会话、状态条上的录制指示器。
    //
    // 为什么录制会话挂在主窗口上：停止录制的入口（状态条按钮）必须在主窗口上，
    // 而录像是从主窗口这一侧发起的，两边要在同一个对象上对齐。
    RecordingLibrary* recordingLibrary_ = nullptr;
    ScreenRecorder* screenRecorder_ = nullptr;
    // 本次录制属于哪一类（屏幕 / PDF 放映）。
    //
    // 为什么单独记着：录完之后要把它登记进录制列表，而登记发生在
    // ScreenRecorder::finished 信号里——那时候只有文件路径，
    // 靠文件名猜种类是猜不准的（用户可以改文件名）。
    RecordingKind pendingRecordingKind_ = RecordingKind::Screen;
    // 录制中的状态条部件：红点 + 计时 + 「停止录制」。
    QLabel* recordingIndicator_ = nullptr;
    QPushButton* stopRecordingButton_ = nullptr;
    // 计时刷新（每 500ms 一次，只在录制期间存在）。
    QTimer* recordingTimer_ = nullptr;
};

}  // namespace aipdf::app

