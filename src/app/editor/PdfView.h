#pragma once

#include <QWidget>
#include <QImage>

#include "app/editor/NavigationHistory.h"
#include "app/layout/PageLayout.h"
#include "app/ui/ReadingScheme.h"
#include "app/editor/TileCache.h"
#include "app/controllers/AnnotationController.h"
#include "app/controllers/DocumentOutput.h"
#include "ai/content/AiNarration.h"
#include "doc/batch/ActionWizard.h"
#include "doc/model/DocumentChunk.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/security/PdfSecurity.h"
#include "pdf/annot/AnnotationToolkit.h"
#include "pdf/forms/FormFieldWriter.h"
#include "pdf/security/SignatureWorkflow.h"
#include "pdf/compliance/TagTreeWriter.h"
#include "pdf/forms/FormRuntime.h"
#include "pdf/forms/FormScriptStore.h"
#include "i18n/Translate.h"
#include "speech/SpeechSettings.h"
#include "speech/SpeechText.h"
#include "pdf/ops/PageSplitPlanner.h"

#include <memory>

class QLabel;
class QTimer;
class QToolButton;

namespace aipdf::speech {
class SpeechEngine;
}

namespace aipdf::app {

class PageCanvas;
class RenderThread;
class TextController;
class NavigationController;
class AnnotationController;
class IndexController;
class DocumentQaController;
class PageTransform;
class PresentationWindow;

// PDF 页面视图：连续显示多页，按视口渲染，支持缩放与旋转。
class PdfView final : public QWidget {
    Q_OBJECT

public:
    explicit PdfView(QWidget* parent = nullptr);
    ~PdfView() override;

    // 载入文档并渲染首页。失败时返回 false，并通过 errorMessage 说明原因。
    bool load(const QString& filePath, QString* errorMessage);

    // 使用已打开的文档（例如由调用方完成密码校验后传入）。
    void setDocument(pdf::PdfDocument document, const QString& filePath);

    // 记下打开这份文档用的密码。
    //
    // 为什么视图要记：加密文档的字节一直是加密的（PDFium 序列化会保留加密），
    // 于是「清理隐藏信息」与「保存」这两件事都需要同一个密码——qpdf 解不开就
    // 枚举不了，校验也会被自己写的加密挡住。调用方在打开成功后设一次即可。
    void setDocumentPassword(const QString& password);

    void clear();

    int pageCount() const;
    int currentPage() const;
    double zoom() const;
    // 缩放范围（10%–800%）。界面上的滑块与百分比显示必须用同一份常量，
    // 否则会出现“600% 显示成 400%、滑块到不了 400% 以上”。
    static double minZoomFactor();
    static double maxZoomFactor();
    QString filePath() const;
    // 为导航面板生成真实页面缩略图；仅按需渲染请求页。
    QImage renderThumbnail(int pageIndex, int targetWidth) const;
    // 页面尺寸（PDF 点）。OCR 要把像素框换回页面坐标，必须知道它。
    QSizeF pageSizePoints(int pageIndex) const;

    // 缓存当前占用的字节数，用于观测内存上限是否生效。
    qint64 cacheBytes() const;
    // 渲染线程里尚未完成的请求数。
    int pendingRenders() const;

    // 按当前语言重新报一次性能指标。
    // updateMetrics() 平时按「值有没有变」去抖，切换语言时值没变、文案却要变，
    // 所以给一个强制重报的入口。
    void refreshMetrics();

    // 当前文档的文本控制器（文本提取、选择与搜索）。
    // 没有打开文档时返回 nullptr。
    TextController* textController() const;

    // 当前文档的导航控制器（大纲与链接注释）。
    // 没有打开文档时返回 nullptr。
    NavigationController* navigationController() const;

    // 当前文档的批注控制器（批注模型、撤销栈与批注工具）。
    // 没有打开文档时返回 nullptr。
    AnnotationController* annotationController() const;

    // 当前文档的索引控制器（分块、章节归属与本地检索）。
    // 没有打开文档时返回 nullptr。
    IndexController* indexController() const;

    // 整篇问答控制器（混合检索 + 引用核验）。
    // 没有打开文档时返回 nullptr。
    DocumentQaController* qaController() const;

    // 当前 PDF 大纲（书签）及整体替换入口。替换会重开内存文档并刷新控制器。
    std::vector<pdf::OutlineItem> outline() const;
    bool replaceOutline(const std::vector<pdf::OutlineItem>& outline, QString* error = nullptr);

public slots:
    void zoomIn();
    void zoomOut();
    void zoomToFitWidth();
    void zoomToFitPage();
    void rotateRight();
    void rotateLeft();
    void setZoom(double factor);
    void goToPage(int index);
    // 文档比较的「引用定位」：翻到那一页，并选中原型里的对象。
    //
    // userRect 是 PDF user space 里的矩形（左下原点、未应用 /Rotate）。
    void goToBlock(int pageIndex, const QRectF& userRect);

    // 排布方式：单页长卷 / 双页对开 / 双页(封面单独一行)。
    void setLayoutMode(PageLayout::LayoutMode mode);
    PageLayout::LayoutMode layoutMode() const;

    // 前进 / 后退：回到上一次跳转或缩放的位置。
    void navigateBack();
    void navigateForward();

    // 放映：全屏一页一张，方向键翻页。
    void startPresentation();

    // 放映是否循环：放映到最后一页时绕回第一页。
    //
    // 循环是**放映的选项**，不是文档的属性，所以只存在内存里，不随文件保存。
    void setPresentationLoop(bool enabled);
    bool presentationLoop() const { return presentationLoop_; }

    // 批注交换：XFDF 导入导出。
    void exportAnnotationsXfdf();
    void importAnnotationsXfdf();
    // 批注摘要：导出 HTML / 直接打印。
    void exportAnnotationSummary();
    void printAnnotationSummary();

    // 页面装饰（v1.5）.0：水印、页眉页脚、页码。
    void applyWatermarkDialog();
    void applyHeaderFooterDialog();
    void applyPageNumbersDialog();
    // 页面组装（对齐福昕页面管理）：裁剪与贝茨编号。
    void cropCurrentPageDialog();
    void applyBatesNumberingDialog();
    void duplicateCurrentPage();
    void reversePages();
    void swapCurrentWithNext();
    void splitDocumentDialog();
    void replaceCurrentPageDialog();
    void crossMergeDialog();
    void extractPagesDialog();
    void insertPagesFromFileDialog();
    void resizeCurrentPageDialog();
    void setPageBackgroundDialog();
    // 表格检测与单元格改写：先检测成网格，再按格改写文字。
    void detectTableDialog();

    // 安全（v1.6.0）：保存时做 AES-256 加密并写权限位。
    bool encryptionEnabled() const;
    pdf::DocumentPermissions effectivePermissions() const;
    bool unlockPermissionsWithOwnerPassword();
    void configureEncryptionDialog();
    void sanitizeHiddenData();
    void redactSelection();
    // Ribbon 与验收脚本用的批注工具入口：id 与按钮对象名一致（highlight/square/ink…）。
    void activateAnnotationTool(const QString& id);
    // 把文档/批注/图形三条工具条收起来（工具已进 Ribbon）。
    void setToolBarsVisible(bool visible);

    // 表单（v1.5.0）：字段列表、写值、扁平化。
    QVector<pdf::FormField> formFields() const;
    bool setFormFieldValue(int pageIndex, const QString& name, const QString& value);
    // 交互填写：走 FormRuntime（Keystroke/Validate/Calculate/Format 事件顺序），
    // 值写回 /V 并重生成 /AP。计算域跟着变、校验失败会回滚并给出原因。
    // 批量导入仍用上面的轻量接口，避免每个字段都重写一遍文档。
    bool setFormFieldValueWithScripts(int pageIndex, const QString& name, const QString& value,
                                      QString* reason = nullptr);
    // 批量写值（CSV / FDF / XFDF 导入、重置表单）：整篇只载入一次、计算一次、
    // 写回一次、重开一次。字段匹配不到、只读、校验失败都会被跳过并计入失败，
    // 返回真正写成功的字段数。values 是「字段名 → 值」。
    int applyFormFieldValues(const QVector<QPair<QString, QString>>& values,
                             QString* reason = nullptr);
    // 逐字符校验：candidate 是输入框里的完整新值。字段脚本用 event.rc=false
    // 拒绝某次击键时返回 false 并给出原因，调用方应把输入框回退到上一个值。
    // 只校验「在现有值后面追加」的情况；删除/整段粘贴交给提交时的校验。
    bool validateFormKeystroke(int pageIndex, const QString& name, const QString& candidate,
                              QString* reason = nullptr);
    // 富文本写回（/RV + 按段 /AP）。这条路径不跑字段脚本，只做值与外观。
    bool applyFormFieldRichValue(int pageIndex, const QString& name,
                                 const QVector<pdf::FormTextRun>& runs,
                                 QString* reason = nullptr);
    bool flattenFormsOnPage(int pageIndex);
    // 在当前页创建真正的 AcroForm Widget。kind 为 text/checkbox/radio/combo/list/button/barcode。
    void createFormFieldDialog(const QString& kind);
    bool editFormFieldProperties(int pageIndex, const QString& name);
    // 按给定的参数建一个表单域（套用表单模板、批量建域都走这里）。
    // 失败时 reason 里是中文原因。
    bool createFormFieldWith(const pdf::FormFieldInsert& insert, QString* reason);
    // 字段上挂的脚本与默认值（对象层的 /AA、/A、/DV）。
    QVector<pdf::FormFieldScript> formFieldScripts() const;
    // AcroForm /CO 里的计算次序（字段名）。
    QStringList formCalculationOrder() const;
    // 把脚本与计算次序写回文档；失败时 reason 里是中文原因。
    bool saveFormScripts(const QVector<pdf::FormFieldScript>& scripts, const QStringList& order,
                         QString* reason);
    // 重置表单：有默认值的恢复默认值，没有默认值的清空。返回被重置的字段数。
    int resetFormFields();

    // 文档编辑（v1.11.0）：合并、抽取、图片替换、文字替换。
    void mergeDocumentDialog();
    // 页码标签：把页码显示规则写进 PDF 的 /PageLabels。
    void pageLabelsDialog();
    // 命名位置：给当前页起名，之后可按名字跳回。
    void namedDestinationDialog();
    // 通用批处理：对一个目录里的 PDF 依次处理，产出 JSON 报告。
    void batchProcessDialog();
    // 阅读配色（夜间 / 护眼 / 高对比）：转发给画布。
    void setReadingScheme(theme::ReadingScheme scheme);

    // --------------------------------------------------- 视图模式（v1.16）----
    //
    // 这一组全是「怎么显示」：标尺、边缘自动滚动、翻页过渡、纯文本查看器、
    // 逆序镜像。它们只改画面，不改文档，也不进撤销栈。

    // 标尺开关与单位。单位用整数值（0=点 1=毫米 2=英寸），
    // 这样 Ribbon 上的按钮可以直接把序号传进来。
    void setRulerVisible(bool visible);
    void setRulerUnit(int unitIndex);
    // 拖动批注或选区时把指针拖到视口边缘就自动滚动。
    void setAutoScrollEnabled(bool enabled);
    // 翻页过渡：0=无 1=淡入淡出 2=左滑 3=右滑。
    void setPageTransition(int transitionIndex);
    // 逆序镜像（日漫式从右往左读）：只改显示空间，文件里不会被改。
    void setMirrorEnabled(bool enabled);
    // 手型工具：按住左键拖动平移视图。
    void setPanModeEnabled(bool enabled);
    bool panModeEnabled() const;

    // 纯文本查看器：把当前页（或整篇）按阅读顺序显示成纯文本。
    void showPlainTextViewer();
    void showWholeDocumentPlainText();

    // 电子签章：多方签署流程面板（签署流程 / 签署报告 / 签署记录）。
    void showSignatureWorkflowDialog();

    // --------------------------------------------------- 第二批接线（E/F/G/H）----
    //
    // 这一组是给新面板用的入口：面板只负责收集输入与显示结果，
    // 真正碰文档的活都在这里，判定与边界一律交给领域层。

    // 当前文档的字节（印前检查、活页夹、图像化 PDF、转曲都要它）。
    // 取不到时返回空字节，并把原因写进 error。
    QByteArray documentBytes(QString* error = nullptr) const;
    // 当前文档的文本层（每页一个 TextPage）。
    QVector<pdf::TextPage> documentTextPages() const;
    // 当前页按阅读顺序拼出来的纯文本（没有文本层时返回空串）。
    QString currentPagePlainText() const;
    // 当前选区的文本（没有选区时返回空串）。
    QString currentSelectionText() const;
    // 整篇按阅读顺序拼出来的纯文本（没有文本层时返回空串）。
    QString wholeDocumentPlainText() const;

    // 智能朗读：清洗文本、排朗读分段，然后按段喂给现有的语音引擎。
    void speakNarration();

    // 把一段文字作为文本批注插到当前页（计算器、符号面板用）。
    bool insertTextAnnotation(const QString& text, double fontSize = 12.0);

    // 把一张图片（PNG/JPEG 字节）作为图片批注插到当前页正中（AI 生成图用）。
    bool insertGeneratedImage(const QByteArray& imageBytes, const QString& label);

    // 连续批注模式：打开后放完批注不切回选择工具。
    // 状态存在领域层的 pdf::ContinuousAnnotationMode 里，界面不另记一套。
    void setContinuousAnnotationMode(bool enabled);
    bool continuousAnnotationMode() const;
    // 放完一个批注后调用：返回 true 表示"保持当前工具"。
    bool afterAnnotationPlacement();
    // 某个工具是否支持连续（界面决定：墨迹、橡皮这类连着放会越用越歪）。
    static bool toolSupportsContinuous(AnnotationController::Tool tool);
    // 工具 → 领域层用的工具 id。
    static QString continuousToolId(AnnotationController::Tool tool);

    // 保持工具选择：逐工具记住"放完别退回选择态"。
    // 状态存在领域层的 pdf::ToolRetentionSet 里。
    void setToolRetained(const QString& toolId, bool retained);
    bool toolRetained(const QString& toolId) const;
    // 被标记为保持的工具（按标记顺序）。
    QVector<QString> retainedAnnotationTools() const;
    void clearRetainedAnnotationTools();
    // 编号图章：先问编号规则，再切到编号图章工具。
    void activateNumberStampTool();

    // 按指定的打印设置打印当前文档（批量打印与打印模板都用它）。
    //
    // 返回 false 时 error 里是中文原因；打印对话框由调用方弹。
    bool printWithOptions(const PrintOptions& options, QString* error);

    // ------------------------------------------------- 第三批接线（任务 J~U）----
    //
    // 这一组是给第三批新面板用的入口：面板只负责收集输入与显示结果，
    // 真正碰文档的活都在这里，判定与边界一律交给领域层。

    // 用新的字节替换当前文档的内容（写回用：隐藏文字层、图层、重排都走它）。
    //
    // 失败时返回 false，error 里是中文原因，**文档保持原样**。
    bool replaceDocumentBytes(const QByteArray& bytes, QString* error = nullptr);

    // 读取文档里既有的标签树（/StructTreeRoot），逐个结构元素显示出来。
    void showStructTreeDialog();
    // 段落排版：把选中的文字按段落参数重新排一次。
    void paragraphFormatDialog();

    // 某页的有效盒子（PDF user space，左下原点；优先 CropBox）。
    // 写不可见文字层时字符框要相对同一个盒子才对得上。
    pdf::MediaBox pageContentBox(int pageIndex) const;

    // 执行一步文档级动作（动作向导用）。
    //
    // 动作类型、参数与失败原因都由领域层的类型给出；这里只做「把参数
    // 翻译成领域层的调用」。返回 false 时 reason 里是中文原因。
    bool applyDocumentAction(doc::WizardActionType type,
                             const QVector<doc::WizardParam>& params,
                             QString* reason);
    void extractCurrentPageDialog();
    void replaceSelectionWithImageDialog();
    void deleteSelectionImagesDialog();
    void replaceSelectionTextDialog();
    void editSelectedObjectsDialog();
    void reflowSelectionDialog(bool paragraph);
    void searchReplaceDialog();
    void addLinkToSelectionDialog();
    void addInternalLinkToSelectionDialog();
    void detectWebLinks();
    // 交叉引用：插入一段「见第 N 页」式的引用文字，并让它点得动（跳到对应书签）。
    void insertCrossReferenceDialog();
    // 添加文章框：在当前页放一个多行文本框（正文框，与批注文本框同一套外观）。
    void addArticleFrameDialog();
    // 字数统计：总字数、字符数、页数与每页分布。
    void showDocumentStatisticsDialog();
    // 文档属性：读 PDF 的 /Info 并可写回。
    void showDocumentPropertiesDialog();
    // 自动创建书签：按标题样式（字高）生成大纲。
    void createOutlineFromHeadingsDialog();
    // 朗读：把当前页按视觉阅读顺序逐词读出来，并在页面上高亮正在读的那个词。
    void speakCurrentPage();
    void stopSpeaking();
    // 阅读选项：阅读顺序预览 + 朗读设置（音色、语速、音量）。
    void showReadingOptions();
    // 当前页的逻辑阅读顺序（按视觉顺序重排过的纯文本）。
    QString currentPageReadingOrder() const;
    // 朗读设置（音色、语速、音量）。设置存在语音引擎上，下一次朗读生效。
    speech::SpeechSettings speechSettings() const;
    void setSpeechSettings(const speech::SpeechSettings& settings);
    // 系统里可用的音色列表（探测不到就返回空列表）。
    QVector<speech::SpeechVoice> availableSpeechVoices() const;
    // 批量替换：一张规则表，规则按顺序依次生效。
    void batchReplaceDialog();
    // 拼写检查：列出可疑的词并给出建议。
    void spellCheckDialog();
    // 密文四件套：标记 / 马赛克 / 擦除 / 应用密文。
    void redactionModeDialog();
    // 验签：签名列表 + 证书链面板。
    void showSignatureListDialog();
    // 区域阅读顺序：看已写入的顺序 / 调整当前页顺序 / 整篇按默认顺序写入。
    void showRegionReadingOrderDialog();
    void changeRegionReadingOrderDialog();
    void applyRegionReadingOrder();
    // 录音声音批注（v1.4.0 尾巴）。
    void recordSoundAnnotationDialog();
    // 本地电子签章：选中的签章在下一次点击页面时落下；骑缝章会拆分到全部页面。
    void activateSignaturePlacement(const QByteArray& image, const QString& name);
    bool applyRidingSeamSignature(const QByteArray& image, const QString& name);
    // 表单数据：FDF 导入导出（CSV 在面板里）。
    void importFormFdfDialog();
    void exportFormFdfDialog();

private:
    struct ContentEditRecord {
        int pageIndex = -1;
        pdf::PdfDocument before;
        pdf::PdfDocument after;
        QString name;
    };

    bool requirePermission(bool allowed, const QString& action);
    // 页面内容变了之后刷新渲染缓存与布局。
    void refreshAfterDecoration();
    void createFormFieldAt(const QString& kind, int pageIndex, const QRectF& rect);
    void editObjectsInRect(int pageIndex, const QRectF& userRect, bool useCurrentSelection = false);
    void selectObjectAtPoint(int pageIndex, const QPointF& userPoint, bool additive);
    void transformSelectedObjects(int pageIndex, const QRectF& oldRect, const QRectF& newRect);
    void rotateSelectedObjects(int pageIndex, double degrees);
    void beginInlineTextEdit(int pageIndex, const QPointF& userPoint);
    void commitInlineTextEdit(int pageIndex, int objectIndex, const QString& text);
    void recordContentEdit(int pageIndex, const pdf::PdfDocument& before, const QString& name);
    bool restoreContentSnapshot(const pdf::PdfDocument& snapshot, int pageIndex);
    // 把 [start, end) 这一段页写成一个独立文件。三个拆分入口共用它。
    bool writePageRange(int start, int end, const QString& path, QString* error);
    // 取当前页的整页文本，并建立「文本码元 → 阅读顺序下标」的映射。
    // 朗读与拼写检查共用。
    bool collectPageText(int pageIndex);
    // 区域阅读顺序：整理整篇的行与顺序，写回标签树。
    QVector<QVector<pdf::TaggedRegion>> buildTaggedRegions(int pageIndex,
                                                            const QVector<int>& currentPageOrder) const;
    bool writeTaggedRegions(const QVector<QVector<pdf::TaggedRegion>>& regions, QString* error);
    // 朗读真正开始：切词、送引擎、接逐词高亮。
    void startSpeechOnPage(int pageIndex);
    // 显示坐标 → PDF 用户空间（y 反向）。
    QPointF displayToUser(int pageIndex, const QPointF& displayPoint) const;
    // 工具切换后同步连续批注模式（当前挂着哪个工具由领域层记着）。
    void syncContinuousModeWithTool();
    // 连续批注：放完一个批注后的收尾（保持工具或回到选择态）。
    void finishAnnotationPlacement();
    // 朗读：按分段逐段送进语音引擎。
    void speakNextNarrationSegment();
    // 段落排版：把选中的文字对象按段落参数重排一次并写回页面。
    // 排版本身交给 pdf::layoutParagraph，界面不算位置也不断行。
    void applyParagraphFormat(int pageIndex, const QVector<pdf::PageObjectInfo>& objects);

public:
    // 把当前选中的文字复制到剪贴板。
    void copySelection();
    // 撤销 / 重做批注操作。
    void undoAnnotation();
    void redoAnnotation();

    // ------------------------------------------------- 页面管理（v0.8.0）----
    //
    // 页面操作都作用在「当前页」上：
    // 插入是在当前页之后插一张空白页；旋转改的是页面自身的 /Rotate
    // （与工具栏上那个只转视图的「旋转」不是一回事）。
    void insertBlankPageAfterCurrent();
    void deleteCurrentPage();
    void rotateCurrentPageRight();
    void rotateCurrentPageLeft();
    void moveCurrentPageUp();
    void moveCurrentPageDown();
    // 把当前页移动到指定位置（引擎早就支持任意位置，只是界面只给了上移/下移）。
    void moveCurrentPageToDialog();
    // 拆分的三个入口：按页数、按书签、按体积。差别只在"从哪里断"。
    void splitDocumentByOutlineDialog();
    void splitDocumentBySizeDialog();

    // ---------------------------------------------------------- 安全保存 ----
    //
    // 写临时文件 → 重新打开校验 → 原子替换，失败时原文件不动。
    bool save(QString* errorMessage, QString* backupPath = nullptr);
    bool saveAs(const QString& targetPath, QString* errorMessage, QString* backupPath = nullptr);
    // 文档有没有未保存的改动。
    bool isModified() const;

    // ------------------------------------------------------ 打印与导出 ----
    //
    // 三个入口都在这里：打印走"渲染成图"，导出走同一套渲染，保证"屏幕、纸面、
    // 导出文件"三处看到的是同一份内容（包括批注）。
    void printDocument();
    void exportPageImages();
    void exportPlainText();

    // 无障碍工具：检查文本层、Unicode 映射和表单说明，并生成可保存的报告。
    void showAccessibilityCheck();
    void exportAccessibilityReport();
    // 显示 PDF 当前可提取的逻辑文字顺序，便于人工核对阅读顺序。
    void showReadingOrder();
    // 论文工具：只做当前文档内部的重复段落自查，不冒充在线论文数据库查重。
    void showThesisCheck();
    void showThesisCheckHistory();

    // -------------------------------------------------------- 索引定位 ----
    //
    // 跳到某页的某个块，并把那块在页面上高亮出来。
    // 索引里的坐标是 PDF user space，这里负责转成显示空间。
    void showIndexedChunk(int pageIndex, const QString& chunkId);

signals:
    void permissionsChanged();
    void zoomChanged(double factor);
    void pageCountChanged(int pages);
    void currentPageChanged(int index);
    // 性能指标文本（缓存占用与待渲染任务数），交给主窗口显示在状态栏。
    void metricsChanged(const QString& text);
    // 历史可用性变化（供菜单项置灰）。
    void historyChanged(bool canGoBack, bool canGoForward);
    // 表单字段值或扁平化状态变了。
    void formFieldsChanged();
    // 选中的文字已复制（字符数）。
    void textCopied(int characters);
    // 链接被激活：内部跳转给出目标页（从 0 开始），外部链接给出地址。
    void linkFollowed(int targetPage, const QString& uri);
    // 批注状态变化（新增、删除、撤销、重做）。
    void annotationsChanged(int count);
    // 需要在状态栏提示一句话。
    void statusMessage(const QString& text);
    // 点了「保存」按钮：真正的保存由主窗口做（它管着文件与提示）。
    void saveRequested();
    // 页面结构变了：页数、顺序或某页的旋转改了。
    void pageStructureChanged();
    // 文本控制器与导航控制器被换掉了（页面结构变化之后）。
    // 外面拿着旧指针的地方要在这个信号里重新指一遍。
    void controllersRefreshed();
    // 文档路径变了（另存为之后）。
    void filePathChanged(const QString& path);
    // 文档的未保存状态变了。
    void modifiedChanged(bool modified);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void buildAnnotationToolBar();
    // 整篇文档切块（与建索引同一套排版规则）。
    // 字数统计与自动创建书签都要用，所以放在一处，避免两处判断打架。
    QVector<doc::DocumentChunk> buildDocumentChunks() const;
    // 切换语言后就地重译常驻文案，并让动态标签按新语言重算。
    void retranslateUi();
    void rebuildLayout();
    void updateZoomLabel();
    void updatePageInfo();
    void updateMetrics();
    void requestVisiblePages();
    void stopRenderThread();
    // 起一个渲染线程（换文档或页面结构变化后重新起）。
    void startRenderThread();
    // 建文本与导航控制器，并把画布指过去。
    void setupControllers();
    // 取某页的显示变换：优先用批注控制器（它知道用户额外旋转），
    // 没有就按文档里的页面盒子自己算一份。
    bool transformForIndexPage(int pageIndex, PageTransform* transform) const;
    // 页面结构变了：重排布局、换掉渲染线程与后台控制器的文档副本。
    void handlePageStructureChanged();
    // 记一次恢复日志（改过文档就要能恢复）。
    void writeRecoveryLog();
    // 更新「有没有未保存的改动」并通知界面。
    void setModified(bool modified);
    double fitZoomForWidth() const;
    double fitZoomForPage() const;
    // 给当前选区加一个标记类批注（高亮 / 下划线 / 删除线）。
    void applyMarkAnnotation(pdf::AnnotationType type);
    // 区域高亮：顶点（用户空间）→ 领域层生成外观流 → 进模型与撤销栈。
    void createRegionHighlight(int pageIndex, const QVector<QPointF>& userPoints);
    // callout 箭头框：注释框 + 箭头尖 → 领域层生成外观流 → 进模型与撤销栈。
    void createCallout(int pageIndex, const QRectF& userBox, const QPointF& userTip);
    // 刷新批注工具栏的可用状态（撤销、重做、选区类按钮）。
    void updateAnnotationToolBar();
    // 缩放的唯一入口：recordHistory=false 用于连续滚轮缩放，
    // 避免一个手势往历史里塞几十条记录。
    void applyZoom(double factor, bool recordHistory);
    // Ctrl+滚轮：缩放后把指针下的页面坐标放回原处。
    void zoomAtPoint(double factor, int pageIndex, const QPointF& pagePoint,
                     const QPoint& viewportPoint);
    // 画布右键菜单（复制 / 全选当前页 / 缩放 / 适应 / 前进后退）。
    void showCanvasContextMenu(const QPoint& viewportPoint);
    // 选中当前页的全部文字。
    void selectAllOnCurrentPage();

    // 视图历史：只记用户主动换位置的动作（跳页、缩放），滚轮不记。
    NavigationHistory history_;
    bool restoringHistory_ = false;
    QVector<ContentEditRecord> contentUndo_;
    QVector<ContentEditRecord> contentRedo_;
    // 表单逐字符输入的常驻会话：文档字节没变就复用，避免每敲一个键都重新解析。
    std::unique_ptr<pdf::FormRuntime> formTypingSession_;
    QByteArray formTypingBytes_;
    enum class EditHistoryKind { Content, Annotation };
    QVector<EditHistoryKind> editUndoTimeline_;
    QVector<EditHistoryKind> editRedoTimeline_;
    bool replayingEditHistory_ = false;
    int knownAnnotationUndoCount_ = 0;
    int selectedObjectPage_ = -1;
    QVector<int> selectedObjectIndexes_;
    int objectCyclePage_ = -1;
    QPointF objectCyclePoint_;
    int objectCyclePosition_ = -1;

    ViewState captureViewState() const;
    void applyViewState(const ViewState& state);

    pdf::PdfDocument document_;
    PageLayout layout_;
    TileCache cache_;
    PageCanvas* canvas_ = nullptr;
    RenderThread* renderThread_ = nullptr;
    // 文本控制器：提取文本层、选区与全文搜索。
    // 声明在 document_ 之后，析构时先于文档句柄释放。
    std::unique_ptr<TextController> textController_;
    // 导航控制器：取文档大纲与页面链接。
    // 同样声明在 document_ 之后：它内部也持有一份文档句柄。
    std::unique_ptr<NavigationController> navigationController_;
    // 批注控制器：批注模型、撤销栈与批注工具。
    // 同样声明在 document_ 之后：它内部也持有一份文档句柄。
    std::unique_ptr<AnnotationController> annotationController_;
    // 索引控制器：后台分块、章节归属与本地检索。
    // 同样声明在 document_ 之后：它内部也持有一份文档句柄。
    std::unique_ptr<IndexController> indexController_;
    // 整篇问答控制器：混合检索与引用核验。
    // 它跟着索引控制器走（要块与文本），也必须在文档句柄之前释放。
    std::unique_ptr<DocumentQaController> qaController_;
    // 电子签章：多方签署流程的状态机。
    // 流程状态与签署人状态只由领域层管，界面不另算一套 ——
    // 签署记录是拿来追责的，不能有第二种说法。
    pdf::SignatureWorkflow signatureWorkflow_;

    QLabel* zoomLabel_ = nullptr;
    QLabel* pageInfoLabel_ = nullptr;
    // 批注工具栏上的按钮。
    QToolButton* highlightButton_ = nullptr;
    QToolButton* underlineButton_ = nullptr;
    QToolButton* squigglyButton_ = nullptr;
    QToolButton* strikeOutButton_ = nullptr;
    QToolButton* noteButton_ = nullptr;
    QToolButton* squareButton_ = nullptr;
    QToolButton* ellipseButton_ = nullptr;
    QToolButton* lineButton_ = nullptr;
    QToolButton* arrowButton_ = nullptr;
    QToolButton* polygonButton_ = nullptr;
    QToolButton* cloudButton_ = nullptr;
    QToolButton* freeTextButton_ = nullptr;
    QToolButton* stampButton_ = nullptr;
    QToolButton* measureButton_ = nullptr;
    QToolButton* attachmentButton_ = nullptr;
    QToolButton* imageStampButton_ = nullptr;
    QToolButton* soundButton_ = nullptr;
    QToolButton* perimeterButton_ = nullptr;
    QToolButton* areaButton_ = nullptr;
    QToolButton* inkButton_ = nullptr;
    QToolButton* eraserButton_ = nullptr;
    QToolButton* undoButton_ = nullptr;
    QToolButton* redoButton_ = nullptr;
    // 画布工具按钮与工具枚举的对应关系。
    //
    // 只在这张表里维护“哪个按钮对应哪个工具”：可勾选状态、互斥、点击分发
    // 都由表驱动。以前三者各写一遍（checkable 清单、autoExclusive 清单、
    // 17 段 connect），漏一处就表现为“点了没反应”。
    QVector<QPair<QToolButton*, AnnotationController::Tool>> annotationToolButtons_;
    // 指标不放进工具栏：标签宽度一变，工具栏的溢出状态就变，
    // 画布高度跟着变，滚动范围又跟着变，滚动位置会被钳制——
    // 一个文字宽度引发的反馈环。指标交给主窗口放状态栏。
    QTimer* metricsTimer_ = nullptr;
    qint64 lastCacheBytes_ = -1;
    int lastPending_ = -1;

    QString filePath_;
    int pageCount_ = 0;
    double zoom_ = 1.0;
    int userRotation_ = 0;
    bool fitToWidth_ = false;
    bool fitToPage_ = false;
    // 文档有没有未保存的改动。
    bool modified_ = false;
    // 放映是否循环（只存在内存里）。
    bool presentationLoop_ = false;
    // 正在放映的窗口：放映期间改循环开关要能立刻作用到它身上。
    // 关掉之后由 destroyed 信号清空，不能留野指针。
    PresentationWindow* presentationWindow_ = nullptr;
    // 保存时的加密设置（v1.6.0）。encryptionEnabled_ 为假时按明文保存。
    pdf::PdfEncryptionOptions encryptionOptions_;
    bool encryptionEnabled_ = false;
    // 打开这份文档用的密码（加密文档才有）。清理隐藏信息与保存都要用它。
    QString documentPassword_;
    bool ownerPermissionsUnlocked_ = false;
    QByteArray pendingSignatureImage_;
    QString pendingSignatureName_;
    // 朗读引擎：系统语音合成（Windows PowerShell + System.Speech）。
    // 声明在 document_ 之后，析构时先于文档句柄释放。
    std::unique_ptr<speech::SpeechEngine> speechEngine_;
    // 正在朗读的页（-1 表示没在朗读）。
    int speechPage_ = -1;
    // 整页文本 + 每个 UTF-16 码元对应的阅读顺序下标。
    // 词的偏移是文本下标，要换算成阅读顺序下标才能拿到高亮矩形。
    // 朗读与拼写检查共用这一份。
    QString pageText_;
    QVector<int> pageOrderPerUnit_;
    // 整页文本切出来的词（与引擎回报的下标一一对应）。
    QVector<speech::SpeechWord> speechWords_;
    // 排队等文本层的朗读请求：文本层是后台提取的，提取完再开读。
    int pendingSpeechPage_ = -1;
    // 连续批注模式：状态由领域层的 pdf::ContinuousAnnotationMode 记着，
    // 界面只存「用户开没开这个开关」这一位。
    pdf::ContinuousAnnotationMode continuousMode_;
    bool continuousModeEnabled_ = false;
    // 保持工具选择：逐工具记住"放完别退回选择态"。
    pdf::ToolRetentionSet retainedTools_;
    // 编号图章的计数器：每放一个图章自动取下一个编号。
    pdf::StampNumberSequence stampSequence_;
    // 智能朗读的分段队列：按段念，一段念完接下一段。
    QVector<ai::NarrationSegment> narrationSegments_;
    int narrationSegmentIndex_ = -1;
    // 一次设定、长期显示的界面文案：切换语言时就地重译。
    i18n::TextBindings texts_;
};

// =============================================================================
//  动作向导「加书签」「拆分文档」两步的纯计算
// =============================================================================
//
// 为什么做成头文件里的自由函数：这两步里能单测的部分全是「参数怎么解释、
// 页区间怎么换算、文件名怎么拼、页码越不越界」，跟窗口、跟 PDF 引擎都没有
// 关系。写成 PdfView 的成员函数就只能靠真开一个窗口来测（要 PDFium、要文档、
// 要显示器），慢又脆；做成自由函数之后，测试直接调它们即可
// （tests/WizardDocumentActionsTests.cpp）。
//
// 领域层已有的部分不在这里重写：页区间的切法归 pdf::PageSplitPlanner
// （byCount / byOutline / extract），这里只负责把向导参数翻译成它的入参。

// 拆分口径。
enum class WizardSplitMode {
    Range,    // 按 ranges 参数给的页区间拆
    Count,    // 按每 N 页一份
    Outline,  // 按顶层书签拆
};

// 解析 mode 参数。空字符串按默认的 range 处理。
//
// 支持的取值（其余一律报错，并把支持的取值写进错误信息里）：
//   - "range"    按 ranges 参数给的页区间拆（默认口径）。
//   - "count"    每 1 页一份（等价于 "count:1"）。
//   - "count:N"  每 N 页一份，N ≥ 1。
//   - "outline"  按顶层书签拆（走 pdf::PageSplitPlanner::byOutline）。
//   - "each"     每 1 页一份（领域层 doc/batch/WizardActions 里的同义写法）。
//
// 为什么 N 只能写在 mode 里：动作向导给「拆分文档」准备的参数只有
// outputDirectory / mode / ranges 三个（见 app/panels/LastWavePanels.cpp 的
// wizardParamFields），没有 pagesPerFile；再加一个参数就得改界面层的参数表，
// 那不在本轮范围内。mode 是这一轮唯一能带值的参数，所以 N 跟它走。
inline bool parseWizardSplitMode(const QString& mode, WizardSplitMode* out, int* pagesPerFile,
                                 QString* error) {
    if (out == nullptr || pagesPerFile == nullptr) {
        return false;
    }
    const QString trimmed = mode.trimmed();
    if (trimmed.isEmpty()) {
        *out = WizardSplitMode::Range;
        *pagesPerFile = 1;
        return true;
    }
    const QString lower = trimmed.toLower();
    if (lower == QStringLiteral("range")) {
        *out = WizardSplitMode::Range;
        *pagesPerFile = 1;
        return true;
    }
    if (lower == QStringLiteral("count") || lower == QStringLiteral("each")) {
        *out = WizardSplitMode::Count;
        *pagesPerFile = 1;
        return true;
    }
    if (lower.startsWith(QStringLiteral("count:"))) {
        bool ok = false;
        const int pages = trimmed.mid(6).trimmed().toInt(&ok);
        if (!ok || pages < 1) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::app::PdfView",
                                  "count 后面要写每份文件的页数（至少 1），这一步写的是：%1")
                             .arg(trimmed);
            }
            return false;
        }
        *out = WizardSplitMode::Count;
        *pagesPerFile = pages;
        return true;
    }
    if (lower == QStringLiteral("outline")) {
        *out = WizardSplitMode::Outline;
        *pagesPerFile = 1;
        return true;
    }
    if (error != nullptr) {
        *error = i18n::tr("aipdf::app::PdfView",
                          "拆分方式只支持 range（按 ranges 给的页区间）、count 或 count:N"
                          "（每 N 页一份）、outline（按顶层书签），这一步写的是：%1")
                     .arg(trimmed);
    }
    return false;
}

// 解析 ranges 里的一项："1-3" 或者 "1"（页码 1 基、含两端）。
//
// 失败时（空项、不是数字、起止写反、页码小于 1）error 里写明是哪一项错了。
inline bool parseWizardSplitRangeItem(const QString& item, int* firstPage, int* lastPage,
                                      QString* error) {
    if (firstPage == nullptr || lastPage == nullptr) {
        return false;
    }
    const QString trimmed = item.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::PdfView",
                              "页区间是空的：每一项要写成 1-3 或者 1 这种形式。");
        }
        return false;
    }
    const int dash = trimmed.indexOf(QLatin1Char('-'));
    int first = 0;
    int last = 0;
    bool ok = false;
    if (dash < 0) {
        first = trimmed.toInt(&ok);
        if (!ok) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::app::PdfView",
                                  "页区间写错了：%1（要写成 1-3 或者 1 这种形式）")
                             .arg(trimmed);
            }
            return false;
        }
        last = first;
    } else {
        first = trimmed.left(dash).trimmed().toInt(&ok);
        if (!ok) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::app::PdfView",
                                  "页区间写错了：%1（要写成 1-3 或者 1 这种形式）")
                             .arg(trimmed);
            }
            return false;
        }
        last = trimmed.mid(dash + 1).trimmed().toInt(&ok);
        if (!ok) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::app::PdfView",
                                  "页区间写错了：%1（要写成 1-3 或者 1 这种形式）")
                             .arg(trimmed);
            }
            return false;
        }
    }
    if (first < 1 || last < 1) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::PdfView", "页码从 1 开始，这一步写的是：%1").arg(trimmed);
        }
        return false;
    }
    if (first > last) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::PdfView", "页区间的起止写反了：%1").arg(trimmed);
        }
        return false;
    }
    *firstPage = first;
    *lastPage = last;
    return true;
}

// 拆分出来的文件名（与 PdfView::splitDocumentDialog 的命名保持一致：
// "%1_%2-%3.pdf"，页码 1 基、含两端）。
inline QString wizardSplitPartFileName(const QString& baseName, const pdf::PageRange& range) {
    return QStringLiteral("%1_%2-%3.pdf")
        .arg(baseName.isEmpty() ? QStringLiteral("part") : baseName)
        .arg(range.first + 1)
        .arg(range.last);
}

// 书签要指向的页：1 基 → 0 基，并检查越界。
//
// 为什么要查越界：pdf::OutlineItem::pageIndex 越界的语义是「目标损坏」，
// 写进去的书签点开会跳到文档外面去——看着存上了，其实是个坏书签。
inline bool resolveWizardBookmarkPage(qint64 pageIndex, int pageCount, int* resolved,
                                      QString* error) {
    if (resolved == nullptr) {
        return false;
    }
    if (pageCount <= 0) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::PdfView", "这份文档里没有页，没法加书签。");
        }
        return false;
    }
    // 书签指向的页必须落在文档里：页码从 1 起算，超出页数就是「目标损坏」，
    // 不能写一条指向文档外的书签（那样看起来存进去了，其实点不动）。
    if (pageIndex < 1) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::PdfView",
                              "书签要指向第 1 页或更后面的页，这一步写的是第 %1 页。")
                         .arg(pageIndex);
        }
        return false;
    }
    if (pageIndex > pageCount) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::PdfView",
                              "书签要指向的页码超出范围：第 %1 页，这份文档一共 %2 页。")
                         .arg(pageIndex)
                         .arg(pageCount);
        }
        return false;
    }
    *resolved = static_cast<int>(pageIndex - 1);
    return true;
}

}  // namespace aipdf::app
