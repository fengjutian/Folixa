#pragma once

#include <QWidget>

#include "app/PageLayout.h"
#include "app/TileCache.h"
#include "pdf/PdfiumAdapter.h"

class QLabel;
class QPushButton;
class QTimer;

namespace aipdf::app {

class PageCanvas;
class RenderThread;

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

    // 缓存当前占用的字节数，用于观测内存上限是否生效。
    qint64 cacheBytes() const;
    // 渲染线程里尚未完成的请求数。
    int pendingRenders() const;

public slots:
    void zoomIn();
    void zoomOut();
    void zoomToFitWidth();
    void zoomToFitPage();
    void rotateRight();
    void rotateLeft();
    void setZoom(double factor);
    void goToPage(int index);

signals:
    void zoomChanged(double factor);
    void pageCountChanged(int pages);
    void currentPageChanged(int index);

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

    QLabel* zoomLabel_ = nullptr;
    QLabel* pageInfoLabel_ = nullptr;
    // 性能指标：缓存占用与待渲染任务数。
    // 这两个数字是「缓存遵守内存上限」和「快速滚动不积压任务」的观测点。
    QLabel* metricsLabel_ = nullptr;
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
