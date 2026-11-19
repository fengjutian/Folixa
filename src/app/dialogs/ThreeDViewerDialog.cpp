#include "app/dialogs/ThreeDViewerDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace aipdf::app {
namespace {

// 渲染目标尺寸。固定尺寸而不是跟着控件走：渲染器自己算投影，
// 尺寸跟着控件变会让「转了 90° 画面变了没有」这件事更难比。
constexpr int kRenderWidth = 520;
constexpr int kRenderHeight = 380;

}  // namespace

// ---------------------------------------------------------------- 画布 ----

ThreeDViewerCanvas::ThreeDViewerCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 240);
    setCursor(Qt::OpenHandCursor);
}

void ThreeDViewerCanvas::setImage(const QImage& image) {
    image_ = image;
    update();
}

QSize ThreeDViewerCanvas::sizeHint() const {
    return QSize(kRenderWidth, kRenderHeight);
}

void ThreeDViewerCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(238, 238, 242));
    if (image_.isNull()) {
        return;
    }
    const QSize scaled = image_.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
    painter.drawImage(target, image_);
}

void ThreeDViewerCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    lastPos_ = event->pos();
    setCursor(Qt::ClosedHandCursor);
}

void ThreeDViewerCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint delta = event->pos() - lastPos_;
    lastPos_ = event->pos();
    if (delta.isNull()) {
        return;
    }
    // 每像素 0.5 度：拖过一整屏大约转 180 度，手感接近常见的 3D 查看器。
    // 俯仰取负：屏幕 y 向下为正，往下拖应该让模型抬头。
    emit orbitChanged(delta.x() * 0.5, -delta.y() * 0.5);
}

void ThreeDViewerCanvas::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }
    emit zoomChanged(delta > 0 ? 1.1 : (1.0 / 1.1));
    event->accept();
}

// ------------------------------------------------------------ 对话框 ----

ThreeDViewerDialog::ThreeDViewerDialog(const pdf::ThreeDAnnotationInfo& info,
                                       const pdf::ThreeDPosterImage& poster, QWidget* parent)
    : QDialog(parent), info_(info), poster_(poster) {
    options_.width = kRenderWidth;
    options_.height = kRenderHeight;
    buildUi();
    rerender();
}

void ThreeDViewerDialog::buildUi() {
    setWindowTitle(tr("3D 模型查看"));
    resize(kRenderWidth + 40, kRenderHeight + 190);

    auto* layout = new QVBoxLayout(this);

    sourceLabel_ = new QLabel(this);
    sourceLabel_->setWordWrap(true);
    layout->addWidget(sourceLabel_);

    canvas_ = new ThreeDViewerCanvas(this);
    layout->addWidget(canvas_, 1);

    detailLabel_ = new QLabel(this);
    detailLabel_->setWordWrap(true);
    layout->addWidget(detailLabel_);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    resetButton_ = new QPushButton(tr("重置视角"), this);
    buttons->addWidget(resetButton_);
    layout->addLayout(buttons);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    box->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    layout->addWidget(box);

    connect(canvas_, &ThreeDViewerCanvas::orbitChanged, this, [this](double dAzimuth, double dElevation) {
        camera_.azimuthDeg += dAzimuth;
        camera_.elevationDeg = qBound(-89.0, camera_.elevationDeg + dElevation, 89.0);
        rerender();
    });
    connect(canvas_, &ThreeDViewerCanvas::zoomChanged, this, [this](double factor) {
        camera_.zoom = pdf::threeDClampZoom(camera_.zoom * factor);
        rerender();
    });
    connect(resetButton_, &QPushButton::clicked, this, [this] {
        camera_ = pdf::ThreeDOrbitCamera();
        rerender();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ThreeDViewerDialog::rerender() {
    const pdf::ThreeDRenderResult result = pdf::threeDRenderModel(info_.modelData, poster_, camera_, options_);
    if (!result.ok) {
        sourceLabel_->setText(tr("渲染失败：%1").arg(result.error));
        canvas_->setImage(QImage());
        detailLabel_->clear();
        return;
    }

    // 标题必须说清画出来的是哪一种：模型、海报，还是 SVG 表现层。
    QString sourceText = tr("3D 模型（自研解码，可旋转）");
    if (result.source == pdf::ThreeDRenderSource::Poster) {
        sourceText = tr("降级海报（非 3D 模型）");
    } else if (result.source == pdf::ThreeDRenderSource::Svg) {
        sourceText = tr("SVG 回退表示（非 3D 模型）");
    }
    sourceLabel_->setText(sourceText);

    canvas_->setImage(result.image);

    QString detail = tr("模型类型：%1；网格 %2 个；顶点 %3；三角形 %4；方位角 %5°，俯仰角 %6°，缩放 %7×")
                         .arg(pdf::threeDModelKindName(info_.modelKind))
                         .arg(info_.modelMeshCount)
                         .arg(info_.modelVertexCount)
                         .arg(info_.modelFaceCount)
                         .arg(QString::number(camera_.azimuthDeg, 'f', 1))
                         .arg(QString::number(camera_.elevationDeg, 'f', 1))
                         .arg(QString::number(camera_.zoom, 'f', 2));
    // 模型侧的说明（解到什么、没解什么）是 pdf 层的中文原文，直接摊开给用户看。
    const QStringList notes = info_.modelNotes + result.notes;
    if (!notes.isEmpty()) {
        detail += QStringLiteral("\n") + notes.join(QStringLiteral("\n"));
    }
    detailLabel_->setText(detail);
}

}  // namespace aipdf::app
