#pragma once

#include <QWidget>

#include "pdf/PdfiumAdapter.h"

class QLabel;
class QScrollArea;

namespace aipdf::app {

// PDF 页面视图：显示渲染后的页面，支持缩放与高 DPI。
class PdfView final : public QWidget {
    Q_OBJECT

public:
    explicit PdfView(QWidget* parent = nullptr);

    // 载入文档并渲染首页。失败时返回 false，并通过 errorMessage 说明原因。
    bool load(const QString& filePath, QString* errorMessage);

    // 使用已打开的文档（例如由调用方完成密码校验后传入）。
    void setDocument(pdf::PdfDocument document, const QString& filePath);

    void clear();

    int pageCount() const;
    int currentPage() const;
    double zoom() const;
    QString filePath() const;

public slots:
    void zoomIn();
    void zoomOut();
    void zoomToFitWidth();
    void setZoom(double factor);

signals:
    void zoomChanged(double factor);
    void pageCountChanged(int pages);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuild();
    void updateZoomLabel();

    pdf::PdfDocument document_;
    QLabel* pageLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QLabel* pageInfoLabel_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QString filePath_;
    int pageCount_ = 0;
    int currentPage_ = 0;
    double zoom_ = 1.0;
    bool fitToWidth_ = false;
};

}  // namespace aipdf::app
