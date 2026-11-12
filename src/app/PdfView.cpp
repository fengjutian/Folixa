#include "app/PdfView.h"

#include "app/PageCanvas.h"
#include "app/RenderThread.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

namespace aipdf::app {

namespace {
constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 8.0;
constexpr double kZoomStep = 1.25;
// 缓存上限 128 MiB，按像素字节计。
constexpr qint64 kCacheBytes = 128LL * 1024 * 1024;
}  // namespace

PdfView::PdfView(QWidget* parent) : QWidget(parent), cache_(kCacheBytes) {
    buildUi();
}

PdfView::~PdfView() {
    // 必须先停渲染线程再放掉文档：
    // PDFium 的句柄只能在创建它的线程里关闭。
    stopRenderThread();
}

void PdfView::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolBar = new QToolBar(this);
    toolBar->setMovable(false);

    auto* zoomOut = new QPushButton(QStringLiteral("−"), this);
    zoomOut->setToolTip(QStringLiteral("缩小"));
    connect(zoomOut, &QPushButton::clicked, this, &PdfView::zoomOut);

    auto* zoomIn = new QPushButton(QStringLiteral("+"), this);
    zoomIn->setToolTip(QStringLiteral("放大"));
    connect(zoomIn, &QPushButton::clicked, this, &PdfView::zoomIn);

    auto* fitWidthButton = new QPushButton(QStringLiteral("适应宽度"), this);
    fitWidthButton->setToolTip(QStringLiteral("缩放到适应窗口宽度"));
    connect(fitWidthButton, &QPushButton::clicked, this, &PdfView::zoomToFitWidth);

    auto* fitPageButton = new QPushButton(QStringLiteral("适应页面"), this);
    fitPageButton->setToolTip(QStringLiteral("缩放到整页可见"));
    connect(fitPageButton, &QPushButton::clicked, this, &PdfView::zoomToFitPage);

    auto* rotateLeftButton = new QPushButton(QStringLiteral("↺"), this);
    rotateLeftButton->setToolTip(QStringLiteral("向左旋转"));
    connect(rotateLeftButton, &QPushButton::clicked, this, &PdfView::rotateLeft);

    auto* rotateRightButton = new QPushButton(QStringLiteral("↻"), this);
    rotateRightButton->setToolTip(QStringLiteral("向右旋转"));
    connect(rotateRightButton, &QPushButton::clicked, this, &PdfView::rotateRight);

    zoomLabel_ = new QLabel(QStringLiteral("100%"), this);
    zoomLabel_->setMinimumWidth(56);
    zoomLabel_->setAlignment(Qt::AlignCenter);

    pageInfoLabel_ = new QLabel(QStringLiteral("未打开文档"), this);
    pageInfoLabel_->setAlignment(Qt::AlignCenter);

    toolBar->addWidget(zoomOut);
    toolBar->addWidget(zoomIn);
    toolBar->addWidget(fitWidthButton);
    toolBar->addWidget(fitPageButton);
    toolBar->addSeparator();
    toolBar->addWidget(rotateLeftButton);
    toolBar->addWidget(rotateRightButton);
    toolBar->addSeparator();
    toolBar->addWidget(zoomLabel_);
    toolBar->addSeparator();
    toolBar->addWidget(pageInfoLabel_);
    layout->addWidget(toolBar);

    canvas_ = new PageCanvas(this);
    canvas_->setTileCache(&cache_);
    connect(canvas_, &PageCanvas::renderRequested, this, &PdfView::requestVisiblePages);
    connect(canvas_, &PageCanvas::currentPageChanged, this, [this](int index) {
        updatePageInfo();
        emit currentPageChanged(index);
    });
    layout->addWidget(canvas_, 1);

    // 指标定期刷新：渲染是异步的，缓存占用和队列长度都在变化。
    metricsTimer_ = new QTimer(this);
    metricsTimer_->setInterval(400);
    connect(metricsTimer_, &QTimer::timeout, this, &PdfView::updateMetrics);
    metricsTimer_->start();

    updateZoomLabel();
    updateMetrics();
}

bool PdfView::load(const QString& path, QString* errorMessage) {
    const pdf::PdfOpenResult result = pdf::openDocument(path);
    if (!result.document.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorMessage;
        }
        return false;
    }

    setDocument(result.document, path);
    return true;
}

void PdfView::setDocument(pdf::PdfDocument document, const QString& path) {
    stopRenderThread();

    document_ = std::move(document);
    filePath_ = path;
    pageCount_ = document_.isValid() ? document_.pageCount() : 0;
    zoom_ = 1.0;
    userRotation_ = 0;
    fitToWidth_ = false;
    fitToPage_ = false;
    cache_.clear();

    // 采集每页的显示尺寸（页面自带 /Rotate 已经应用），布局服务据此排版。
    QVector<PageLayout::PageInfo> pages;
    pages.reserve(pageCount_);
    for (int index = 0; index < pageCount_; ++index) {
        PageLayout::PageInfo info;
        const auto page = document_.page(index);
        if (page.has_value()) {
            info.widthPoints = page->displayWidthPoints();
            info.heightPoints = page->displayHeightPoints();
        }
        pages.append(info);
    }
    layout_.setPages(pages);
    layout_.setUserRotation(userRotation_);
    layout_.setScale(zoom_);

    // 渲染线程自己开一份独立句柄（PDFium 句柄不能跨线程用）。
    renderThread_ = new RenderThread(document_, this);
    connect(renderThread_, &RenderThread::pageRendered, this,
            [this](int index, qint64 zoomKey, int rotation, const QImage& image) {
                // 只接受当前缩放与旋转的结果，旧的直接丢弃。
                const qint64 currentKey =
                    TileCache::zoomToKey(layout_.zoom() * canvas_->devicePixelRatioF());
                if (zoomKey != currentKey || rotation != layout_.userRotation() / 90) {
                    return;
                }
                cache_.insert(TileCache::Key{index, zoomKey, rotation}, image);
                canvas_->viewport()->update();
            });
    connect(renderThread_, &RenderThread::pageFailed, this, [](int, qint64, int) {});
    renderThread_->start();

    canvas_->setLayout(layout_);
    emit pageCountChanged(pageCount_);
    updateZoomLabel();
    updatePageInfo();
    requestVisiblePages();
}

void PdfView::clear() {
    stopRenderThread();
    document_ = pdf::PdfDocument();
    filePath_.clear();
    pageCount_ = 0;
    layout_.clear();
    cache_.clear();
    canvas_->setLayout(layout_);
    emit pageCountChanged(0);
    updateZoomLabel();
    updatePageInfo();
}

int PdfView::pageCount() const {
    return pageCount_;
}

int PdfView::currentPage() const {
    return canvas_ != nullptr ? canvas_->currentPage() : 0;
}

double PdfView::zoom() const {
    return zoom_;
}

QString PdfView::filePath() const {
    return filePath_;
}

qint64 PdfView::cacheBytes() const {
    return cache_.bytes();
}

int PdfView::pendingRenders() const {
    return renderThread_ != nullptr ? renderThread_->pendingCount() : 0;
}

void PdfView::setZoom(double factor) {
    const double clamped = std::clamp(factor, kMinZoom, kMaxZoom);
    if (qFuzzyCompare(clamped, zoom_)) {
        return;
    }
    zoom_ = clamped;
    fitToWidth_ = false;
    fitToPage_ = false;
    rebuildLayout();
    emit zoomChanged(zoom_);
}

void PdfView::zoomIn() {
    setZoom(zoom_ * kZoomStep);
}

void PdfView::zoomOut() {
    setZoom(zoom_ / kZoomStep);
}

void PdfView::zoomToFitWidth() {
    fitToWidth_ = true;
    fitToPage_ = false;
    zoom_ = fitZoomForWidth();
    rebuildLayout();
    emit zoomChanged(zoom_);
}

void PdfView::zoomToFitPage() {
    fitToPage_ = true;
    fitToWidth_ = false;
    zoom_ = fitZoomForPage();
    rebuildLayout();
    emit zoomChanged(zoom_);
}

void PdfView::rotateRight() {
    userRotation_ = (userRotation_ + 90) % 360;
    layout_.setUserRotation(userRotation_);
    cache_.clear();
    canvas_->setLayout(layout_);
    updateZoomLabel();
    requestVisiblePages();
}

void PdfView::rotateLeft() {
    userRotation_ = (userRotation_ + 270) % 360;
    layout_.setUserRotation(userRotation_);
    cache_.clear();
    canvas_->setLayout(layout_);
    updateZoomLabel();
    requestVisiblePages();
}

void PdfView::goToPage(int index) {
    if (canvas_ != nullptr) {
        canvas_->goToPage(index);
    }
}

void PdfView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fitToWidth_ || fitToPage_) {
        const double target = fitToWidth_ ? fitZoomForWidth() : fitZoomForPage();
        if (!qFuzzyCompare(target, zoom_)) {
            zoom_ = target;
            rebuildLayout();
            emit zoomChanged(zoom_);
        }
    }
}

void PdfView::rebuildLayout() {
    layout_.setScale(zoom_);
    canvas_->setLayout(layout_);
    updateZoomLabel();
    requestVisiblePages();
}

void PdfView::updateZoomLabel() {
    zoomLabel_->setText(QStringLiteral("%1%").arg(qRound(zoom_ * 100.0)));
}

void PdfView::updatePageInfo() {
    if (pageCount_ <= 0) {
        pageInfoLabel_->setText(QStringLiteral("未打开文档"));
        return;
    }
    pageInfoLabel_->setText(QStringLiteral("第 %1 / %2 页")
                                .arg(currentPage() + 1)
                                .arg(pageCount_));
}

void PdfView::updateMetrics() {
    const qint64 bytes = cache_.bytes();
    const int pending = pendingRenders();
    if (bytes == lastCacheBytes_ && pending == lastPending_) {
        return;
    }
    lastCacheBytes_ = bytes;
    lastPending_ = pending;
    emit metricsChanged(QStringLiteral("缓存 %1 MiB · 待渲染 %2")
                            .arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1)
                            .arg(pending));
}

void PdfView::requestVisiblePages() {
    if (renderThread_ == nullptr || canvas_ == nullptr || layout_.isEmpty()) {
        return;
    }
    int first = 0;
    int last = -1;
    canvas_->visiblePageRange(&first, &last);
    if (last < first) {
        return;
    }
    const double devicePixelRatio = canvas_->devicePixelRatioF();
    renderThread_->requestRange(
        first, last, zoom_ * devicePixelRatio, layout_.userRotation() / 90);
}

void PdfView::stopRenderThread() {
    if (renderThread_ == nullptr) {
        return;
    }
    renderThread_->cancel();
    renderThread_->wait();
    delete renderThread_;
    renderThread_ = nullptr;
}

double PdfView::fitZoomForWidth() const {
    if (canvas_ == nullptr || layout_.pageCount() <= 0) {
        return zoom_;
    }
    const QSizeF size = layout_.pageSize(0);
    if (size.width() <= 0.0) {
        return zoom_;
    }
    // layout 里的尺寸是「点 × 当前缩放」，反推回去就是适应宽度需要的缩放。
    const double available = std::max(1.0, static_cast<double>(canvas_->viewport()->width()) - 32.0);
    return std::clamp(available / (size.width() / zoom_), kMinZoom, kMaxZoom);
}

double PdfView::fitZoomForPage() const {
    if (canvas_ == nullptr || layout_.pageCount() <= 0) {
        return zoom_;
    }
    const QSizeF size = layout_.pageSize(0);
    if (size.width() <= 0.0 || size.height() <= 0.0) {
        return zoom_;
    }
    const double availableWidth = std::max(1.0, static_cast<double>(canvas_->viewport()->width()) - 32.0);
    const double availableHeight = std::max(1.0, static_cast<double>(canvas_->viewport()->height()) - 32.0);
    const double byWidth = availableWidth / (size.width() / zoom_);
    const double byHeight = availableHeight / (size.height() / zoom_);
    return std::clamp(std::min(byWidth, byHeight), kMinZoom, kMaxZoom);
}

}  // namespace aipdf::app
