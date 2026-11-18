#include "app/controllers/OcrController.h"

#include "app/controllers/IndexController.h"
#include "app/editor/PdfView.h"
#include "app/controllers/TextController.h"
#include "app/editor/TextLayout.h"
#include "ocr/pipeline/OcrChunkBridge.h"
#include "ocr/pipeline/ScannedPageDetector.h"
#include "ocr/engines/OcrEngineFactory.h"

#include "i18n/Translate.h"

#include <QDateTime>
#include <QFile>
#include <QImage>

#include <algorithm>

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
    return tr("本机没有可用的 OCR：可以装 tesseract（PATH 或 AIPDF_TESSERACT_PATH），或使用 Windows 自带的 OCR");
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
        return fail(tr("没有打开的文档"));
    }
    if (pageIndex < 0 || pageIndex >= view->pageCount()) {
        return fail(tr("页码超出范围"));
    }

    if (!pageNeedsOcr(view, pageIndex, nullptr)) {
        return fail(tr("这一页已有可用的文本层，不需要 OCR"));
    }

    const QImage pageImage = view->renderThumbnail(pageIndex, 2000);
    if (pageImage.isNull()) {
        return fail(tr("页面渲染失败"));
    }
    const QSizeF pageSize = view->pageSizePoints(pageIndex);
    if (pageSize.width() <= 0.0 || pageSize.height() <= 0.0) {
        return fail(tr("取不到页面尺寸"));
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
        emit failed(result.pageIndex, tr("识别已取消"));
        return;
    }
    if (!result.error.isEmpty()) {
        emit failed(result.pageIndex, result.error);
        return;
    }

    // 结果先留下来：写不可见文字层要的就是这份词级坐标，
    // 即使索引暂时不可用也不该把识别结果丢掉。
    //
    // 控制器是全局的，换文档时上一份的结果必须清掉：
    // 拿别的文档的坐标去写隐藏文字层，字会落到别的页上。
    if (pendingView_.data() != resultsView_.data()) {
        results_.clear();
        resultsView_ = pendingView_;
    }
    results_.insert(result.pageIndex, result);

    PdfView* view = pendingView_.data();
    if (view == nullptr || view->indexController() == nullptr) {
        emit failed(result.pageIndex, tr("索引不可用，识别结果未保存"));
        return;
    }

    const QVector<doc::DocumentChunk> chunks = ocr::chunksFromOcrPage(result, 0);
    if (chunks.isEmpty()) {
        emit failed(result.pageIndex, tr("这一页没有识别出文字"));
        return;
    }
    view->indexController()->applyOcrPage(result.pageIndex, chunks);
    emit pageApplied(result.pageIndex, chunks.size());
}

bool OcrController::ocrResult(int pageIndex, ocr::OcrPageResult* out) const {
    const auto found = results_.constFind(pageIndex);
    if (found == results_.constEnd()) {
        return false;
    }
    if (out != nullptr) {
        *out = found.value();
    }
    return true;
}

QVector<int> OcrController::ocrResultPages() const {
    QVector<int> pages;
    pages.reserve(results_.size());
    for (auto it = results_.constBegin(); it != results_.constEnd(); ++it) {
        pages.append(it.key());
    }
    std::sort(pages.begin(), pages.end());
    return pages;
}

int OcrController::ocrResultCount() const {
    return static_cast<int>(results_.size());
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
