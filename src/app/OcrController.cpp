#include "app/OcrController.h"

#include "app/IndexController.h"
#include "app/PdfView.h"
#include "app/TextController.h"
#include "app/TextLayout.h"
#include "ocr/OcrChunkBridge.h"
#include "ocr/ScannedPageDetector.h"
#include "ocr/OcrEngineFactory.h"

#include <QDateTime>
#include <QFile>
#include <QImage>

namespace aipdf::app {

OcrController::OcrController(QObject* parent) : QObject(parent) {
    auto engine = ocr::createDefaultEngine();
    engineAvailable_ = engine->isAvailable();
    service_.setEngine(std::move(engine));

    connect(&service_, &ocr::OcrService::pageReady, this, &OcrController::handlePageReady);
    connect(&service_, &ocr::OcrService::pageFailed, this,
            [this](int pageIndex, const QString& reason) { emit failed(pageIndex, reason); });
}

OcrController::~OcrController() = default;
namespace {

// 与 IndexController 同一套 trace：验收脚本靠它判断「这一步发生了什么」。
void trace(const QString& message) {
    const QString path = qEnvironmentVariable("AIPDF_TRACE_FILE");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " +
                    message + "\n")
                       .toUtf8());
    }
}

}  // namespace
namespace {

// 这一页是否值得 OCR：文本层够用就跳过。
// 面积比在显示空间里算，与页面尺寸同一量纲即可（比值不受缩放影响）。
bool pageNeedsOcr(PdfView* view, int pageIndex, ocr::PageTextStats* stats, bool requireLayout = false) {
    if (view == nullptr) {
        return false;
    }
    ocr::PageTextStats local;
    TextController* text = view->textController();
    if (text == nullptr) {
        return true;  // 文本控制器还没有：当成扫描页
    }
    const TextLayout* layout = text->layout(pageIndex);
    if (layout == nullptr) {
        // 文本层还没提取出来：自动模式下先不动（否则会给正常的文字页也做一遍 OCR，
        // 还会把文本块整个换掉）；手动模式照做，用户明确要求了。
        return !requireLayout;
    }
    int chars = 0;
    QRectF unionBox;
    for (const TextLayout::Line& line : layout->lines()) {
        chars += line.end - line.start;
        unionBox = unionBox.united(line.box);
    }
    local.charCount = chars;
    const QSizeF pageSize = view->pageSizePoints(pageIndex);
    if (pageSize.width() > 0.0 && pageSize.height() > 0.0) {
        const double zoom = view->zoom() > 0.0 ? view->zoom() : 1.0;
        const double areaInPoints = (unionBox.width() / zoom) * (unionBox.height() / zoom);
        local.textAreaRatio = areaInPoints / (pageSize.width() * pageSize.height());
    }
    if (stats != nullptr) {
        *stats = local;
    }
    return ocr::needsOcr(local);
}

}  // namespace

QString OcrController::unavailableReason() const {
    return QStringLiteral("本机没有可用的 OCR：可以装 tesseract（PATH 或 AIPDF_TESSERACT_PATH），或使用 Windows 自带的 OCR");
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

    if (!pageNeedsOcr(view, pageIndex, nullptr)) {
        return fail(QStringLiteral("这一页已有可用的文本层，不需要 OCR"));
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

void OcrController::handlePageShown(PdfView* view, int pageIndex) {
    if (!autoEnabled_ || !engineAvailable_ || view == nullptr) {
        return;
    }
    if (pageIndex < 0 || pageIndex >= view->pageCount()) {
        return;
    }
    // 每页每次会话只试一次：识别失败的页不该在翻来翻去时反复重试。
    if (attemptedPages_.contains(pageIndex)) {
        return;
    }
    if (!pageNeedsOcr(view, pageIndex, nullptr, true)) {
        return;
    }
    attemptedPages_.insert(pageIndex);
    QString reason;
    if (!requestPage(view, pageIndex, &reason)) {
        trace(QStringLiteral("ocr auto skipped page=%1 reason=%2").arg(pageIndex + 1).arg(reason));
    } else {
        trace(QStringLiteral("ocr auto request page=%1").arg(pageIndex + 1));
    }
}

}  // namespace aipdf::app
