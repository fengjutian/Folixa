#include "app/OcrController.h"

#include "app/IndexController.h"
#include "app/PdfView.h"
#include "app/TextController.h"
#include "app/TextLayout.h"
#include "ocr/OcrChunkBridge.h"
#include "ocr/ScannedPageDetector.h"
#include "ocr/TesseractCliEngine.h"

#include <QImage>

namespace aipdf::app {

OcrController::OcrController(QObject* parent) : QObject(parent) {
    auto engine = std::make_unique<ocr::TesseractCliEngine>();
    engineAvailable_ = engine->isAvailable();
    service_.setEngine(std::move(engine));

    connect(&service_, &ocr::OcrService::pageReady, this, &OcrController::handlePageReady);
    connect(&service_, &ocr::OcrService::pageFailed, this,
            [this](int pageIndex, const QString& reason) { emit failed(pageIndex, reason); });
}

OcrController::~OcrController() = default;

QString OcrController::unavailableReason() const {
    return QStringLiteral("没有找到 tesseract：把它加到 PATH，或用 AIPDF_TESSERACT_PATH 指定路径");
}

bool OcrController::requestPage(PdfView* view, int pageIndex, QString* reason) {
    const auto fail = [reason](const QString& text) {
        if (reason != nullptr) {
            *reason = text;
        }
        return false;
    };

    if (!engineAvailable_) {
        return fail(unavailableReason());
    }
    if (view == nullptr) {
        return fail(QStringLiteral("没有打开的文档"));
    }
    if (pageIndex < 0 || pageIndex >= view->pageCount()) {
        return fail(QStringLiteral("页码超出范围"));
    }

    // 判定：文本层够用就不 OCR。这里的面积比在显示空间里算，
    // 与页面尺寸同一量纲即可（比值不受缩放影响）。
    if (TextController* text = view->textController()) {
        if (const TextLayout* layout = text->layout(pageIndex)) {
            int chars = 0;
            QRectF unionBox;
            for (const TextLayout::Line& line : layout->lines()) {
                chars += line.end - line.start;
                unionBox = unionBox.united(line.box);
            }
            ocr::PageTextStats stats;
            stats.charCount = chars;
            const QSizeF pageSize = view->pageSizePoints(pageIndex);
            if (pageSize.width() > 0.0 && pageSize.height() > 0.0) {
                const double zoom = view->zoom() > 0.0 ? view->zoom() : 1.0;
                const double areaInPoints =
                    (unionBox.width() / zoom) * (unionBox.height() / zoom);
                stats.textAreaRatio = areaInPoints / (pageSize.width() * pageSize.height());
            }
            if (!ocr::needsOcr(stats)) {
                return fail(QStringLiteral("这一页已有可用的文本层，不需要 OCR"));
            }
        }
    }

    const QImage pageImage = view->renderThumbnail(pageIndex, 2000);
    if (pageImage.isNull()) {
        return fail(QStringLiteral("页面渲染失败"));
    }
    const QSizeF pageSize = view->pageSizePoints(pageIndex);
    if (pageSize.width() <= 0.0 || pageSize.height() <= 0.0) {
        return fail(QStringLiteral("取不到页面尺寸"));
    }

    pendingView_ = view;
    ocr::OcrJob job;
    job.pageIndex = pageIndex;
    job.image = pageImage;
    job.pageSize = pageSize;
    service_.enqueue(job);
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

void OcrController::handlePageReady(const ocr::OcrPageResult& result) {
    if (result.cancelled) {
        emit failed(result.pageIndex, QStringLiteral("识别已取消"));
        return;
    }
    if (!result.error.isEmpty()) {
        emit failed(result.pageIndex, result.error);
        return;
    }

    PdfView* view = pendingView_.data();
    if (view == nullptr || view->indexController() == nullptr) {
        emit failed(result.pageIndex, QStringLiteral("索引不可用，识别结果未保存"));
        return;
    }

    const QVector<doc::DocumentChunk> chunks = ocr::chunksFromOcrPage(result, 0);
    if (chunks.isEmpty()) {
        emit failed(result.pageIndex, QStringLiteral("这一页没有识别出文字"));
        return;
    }
    view->indexController()->applyOcrPage(result.pageIndex, chunks);
    emit pageApplied(result.pageIndex, chunks.size());
}

}  // namespace aipdf::app
