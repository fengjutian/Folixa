#include "app/PdfView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace aipdf::app {

namespace {
constexpr double kMinZoom = 0.25;
constexpr double kMaxZoom = 4.0;
constexpr double kZoomStep = 1.25;
}  // namespace

PdfView::PdfView(QWidget* parent) : QWidget(parent) {
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

    auto* fitButton = new QPushButton(QStringLiteral("适应宽度"), this);
    fitButton->setToolTip(QStringLiteral("缩放到适应窗口宽度"));
    connect(fitButton, &QPushButton::clicked, this, &PdfView::zoomToFitWidth);

    zoomLabel_ = new QLabel(QStringLiteral("100%"), this);
    zoomLabel_->setMinimumWidth(56);
    zoomLabel_->setAlignment(Qt::AlignCenter);

    pageInfoLabel_ = new QLabel(QStringLiteral("未打开文档"), this);
    pageInfoLabel_->setAlignment(Qt::AlignCenter);

    toolBar->addWidget(zoomOut);
    toolBar->addWidget(zoomIn);
    toolBar->addWidget(fitButton);
    toolBar->addSeparator();
    toolBar->addWidget(zoomLabel_);
    toolBar->addSeparator();
    toolBar->addWidget(pageInfoLabel_);
    layout->addWidget(toolBar);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setAlignment(Qt::AlignCenter);
    scrollArea_->setBackgroundRole(QPalette::Mid);

    pageLabel_ = new QLabel(scrollArea_);
    pageLabel_->setAlignment(Qt::AlignCenter);
    pageLabel_->setText(QStringLiteral("未打开文档"));
    scrollArea_->setWidget(pageLabel_);
    layout->addWidget(scrollArea_, 1);

    updateZoomLabel();
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
    document_ = std::move(document);
    filePath_ = path;
    pageCount_ = document_.isValid() ? document_.pageCount() : 0;
    currentPage_ = 0;
    zoom_ = 1.0;
    fitToWidth_ = false;

    emit pageCountChanged(pageCount_);
    rebuild();
}

void PdfView::clear() {
    document_ = pdf::PdfDocument();
    filePath_.clear();
    pageCount_ = 0;
    currentPage_ = 0;
    pageLabel_->clear();
    pageLabel_->setText(QStringLiteral("未打开文档"));
    emit pageCountChanged(0);
    updateZoomLabel();
}

int PdfView::pageCount() const {
    return pageCount_;
}

int PdfView::currentPage() const {
    return currentPage_;
}

double PdfView::zoom() const {
    return zoom_;
}

QString PdfView::filePath() const {
    return filePath_;
}

void PdfView::setZoom(double factor) {
    const double clamped = qBound(kMinZoom, factor, kMaxZoom);
    if (qFuzzyCompare(clamped, zoom_)) {
        return;
    }
    zoom_ = clamped;
    fitToWidth_ = false;
    rebuild();
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
    rebuild();
    emit zoomChanged(zoom_);
}

void PdfView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fitToWidth_) {
        rebuild();
    }
}

void PdfView::rebuild() {
    if (!document_.isValid()) {
        pageLabel_->setText(QStringLiteral("未打开文档"));
        return;
    }

    const auto page = document_.page(currentPage_);
    if (!page.has_value()) {
        pageLabel_->setText(QStringLiteral("无法读取页面"));
        return;
    }

    const double devicePixelRatio = this->devicePixelRatioF();
    double effectiveZoom = zoom_;
    if (fitToWidth_) {
        const double available =
            std::max(1.0, static_cast<double>(scrollArea_->viewport()->width()));
        effectiveZoom = available / std::max(1.0, page->widthPoints());
    }

    const QImage image = page->render(effectiveZoom * devicePixelRatio);
    if (image.isNull()) {
        pageLabel_->setText(QStringLiteral("页面渲染失败"));
        return;
    }

    QImage scaled = image;
    scaled.setDevicePixelRatio(devicePixelRatio);
    pageLabel_->setPixmap(QPixmap::fromImage(scaled));
    pageLabel_->setText(QString());
    updateZoomLabel();
}

void PdfView::updateZoomLabel() {
    zoomLabel_->setText(QStringLiteral("%1%").arg(qRound(zoom_ * 100.0)));
    if (pageCount_ > 0) {
        pageInfoLabel_->setText(
            QStringLiteral("第 %1 / %2 页").arg(currentPage_ + 1).arg(pageCount_));
    } else {
        pageInfoLabel_->setText(QStringLiteral("未打开文档"));
    }
}

}  // namespace aipdf::app
