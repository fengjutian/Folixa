#pragma once

#include <QWidget>
#include <QImage>

#include "app/PageLayout.h"
#include "app/TileCache.h"
#include "pdf/PdfiumAdapter.h"

#include <memory>

class QLabel;
class QTimer;

namespace aipdf::app {

class PageCanvas;
class RenderThread;
class TextController;

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

    // 缓存当前占用的字节数，用于观测内存上限是否生效。
    qint64 cacheBytes() const;
    // 渲染线程里尚未完成的请求数。
    int pendingRenders() const;

    // 当前文档的文本控制器（文本提取、选择与搜索）。
    // 没有打开文档时返回 nullptr。
    TextController* textController() const;

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

signals:
    void zoomChanged(double factor);
    void pageCountChanged(int pages);
    void currentPageChanged(int index);
    // 性能指标文本（缓存占用与待渲染任务数），交给主窗口显示在状态栏。
    void metricsChanged(const QString& text);
    // 选中的文字已复制（字符数）。
    void textCopied(int characters);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void rebuildLayout();
    void updateZoomLabel();
    void updatePageInfo();
    void updateMetrics();
    void requestVisiblePages();
    void stopRenderThread();
    double fitZoomForWidth() const;
    double fitZoomForPage() const;

    pdf::PdfDocument document_;
    PageLayout layout_;
    TileCache cache_;
    PageCanvas* canvas_ = nullptr;
    RenderThread* renderThread_ = nullptr;
    // 文本控制器：提取文本层、选区与全文搜索。
    // 声明在 document_ 之后，析构时先于文档句柄释放。
    std::unique_ptr<TextController> textController_;

    QLabel* zoomLabel_ = nullptr;
    QLabel* pageInfoLabel_ = nullptr;
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
};

}  // namespace aipdf::app
