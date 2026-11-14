#pragma once

#include <QWidget>
#include <QImage>

#include "app/PageLayout.h"
#include "app/TileCache.h"
#include "pdf/PdfiumAdapter.h"

#include <memory>

class QLabel;
class QTimer;
class QToolButton;

namespace aipdf::app {

class PageCanvas;
class RenderThread;
class TextController;
class NavigationController;
class AnnotationController;
class IndexController;
class DocumentQaController;
class PageTransform;

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

    void clear();

    int pageCount() const;
    int currentPage() const;
    double zoom() const;
    QString filePath() const;
    // 为导航面板生成真实页面缩略图；仅按需渲染请求页。
    QImage renderThumbnail(int pageIndex, int targetWidth) const;
    // 页面尺寸（PDF 点）。OCR 要把像素框换回页面坐标，必须知道它。
    QSizeF pageSizePoints(int pageIndex) const;

    // 缓存当前占用的字节数，用于观测内存上限是否生效。
    qint64 cacheBytes() const;
    // 渲染线程里尚未完成的请求数。
    int pendingRenders() const;

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

public slots:
    void zoomIn();
    void zoomOut();
    void zoomToFitWidth();
    void zoomToFitPage();
    void rotateRight();
    void rotateLeft();
    void setZoom(double factor);
    void goToPage(int index);
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

    // ---------------------------------------------------------- 安全保存 ----
    //
    // 写临时文件 → 重新打开校验 → 原子替换，失败时原文件不动。
    bool save(QString* errorMessage, QString* backupPath = nullptr);
    bool saveAs(const QString& targetPath, QString* errorMessage, QString* backupPath = nullptr);
    // 文档有没有未保存的改动。
    bool isModified() const;

    // -------------------------------------------------------- 索引定位 ----
    //
    // 跳到某页的某个块，并把那块在页面上高亮出来。
    // 索引里的坐标是 PDF user space，这里负责转成显示空间。
    void showIndexedChunk(int pageIndex, const QString& chunkId);

signals:
    void zoomChanged(double factor);
    void pageCountChanged(int pages);
    void currentPageChanged(int index);
    // 性能指标文本（缓存占用与待渲染任务数），交给主窗口显示在状态栏。
    void metricsChanged(const QString& text);
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
    // 刷新批注工具栏的可用状态（撤销、重做、选区类按钮）。
    void updateAnnotationToolBar();

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

    QLabel* zoomLabel_ = nullptr;
    QLabel* pageInfoLabel_ = nullptr;
    // 批注工具栏上的按钮。
    QToolButton* highlightButton_ = nullptr;
    QToolButton* underlineButton_ = nullptr;
    QToolButton* strikeOutButton_ = nullptr;
    QToolButton* noteButton_ = nullptr;
    QToolButton* squareButton_ = nullptr;
    QToolButton* inkButton_ = nullptr;
    QToolButton* undoButton_ = nullptr;
    QToolButton* redoButton_ = nullptr;
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
};

}  // namespace aipdf::app
