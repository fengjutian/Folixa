#include "doc/convert/ScannedPdfOptimizer.h"

#include <QBuffer>
#include <QImage>

#include "pdf/convert/ImageToPdf.h"
#include "pdf/kernel/PdfiumAdapter.h"

namespace aipdf::doc {

namespace {

// 把一张图编成 PNG 字节。
//
// 为什么用 PNG 而不是 JPEG：扫描件上的文字与细线对 JPEG 的块效应很敏感，
// 而这一步已经是「像素级重排」了，再叠一层有损压缩没有必要。
// 体积由 ImageToPdf 那边按 FlateDecode 处理。
bool encodePng(const QImage& image, QByteArray* out) {
    if (image.isNull() || out == nullptr) {
        return false;
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (!image.save(&buffer, "PNG")) {
        return false;
    }
    buffer.close();
    *out = bytes;
    return true;
}

}  // namespace

QByteArray optimizeScannedPdf(const QByteArray& pdfBytes, const ScannedOptimizeOptions& options,
                               ScannedOptimizeReport* report, QString* error) {
    const auto fail = [&](const QString& reason) -> QByteArray {
        if (error != nullptr) {
            *error = reason;
        }
        if (report != nullptr) {
            report->ok = false;
            report->error = reason;
        }
        return {};
    };

    if (pdfBytes.isEmpty()) {
        return fail(QStringLiteral("没有文档内容可优化。"));
    }
    if (options.dpi <= 0.0) {
        return fail(QStringLiteral("分辨率必须是正数。"));
    }

    const pdf::PdfOpenResult opened = pdf::openDocumentFromBytes(pdfBytes);
    if (!opened.document.isValid()) {
        return fail(QStringLiteral("打不开这份文档，没法优化。"));
    }

    // 渲染缩放：dpi / 72（PDF 用户空间 1 点 = 1/72 英寸）。
    // 这样渲染出来的像素数 ÷ dpi 正好等于原页面的物理尺寸，
    // 写回去时页面尺寸不会因为取整而漂。
    const double zoom = options.dpi / 72.0;

    QVector<QByteArray> encoded;
    QVector<ScannedPageOptimize> pageResults;

    const int pageCount = opened.document.pageCount();
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        const std::optional<pdf::PdfPage> page = opened.document.page(pageIndex);
        if (!page.has_value()) {
            continue;
        }
        const QImage rendered = page->render(zoom);
        if (rendered.isNull()) {
            // 这一页渲染不出来就跳过，不让整篇失败；
            // 但要在汇报里留痕（用户要能看出少了几页）。
            ScannedPageOptimize skipped;
            skipped.pageIndex = pageIndex;
            skipped.note = QStringLiteral("这一页渲染不出来，已跳过。");
            pageResults.append(skipped);
            continue;
        }

        ocr::ImagePrepOptions prepOptions;
        prepOptions.denoise = options.denoise;
        prepOptions.deskew = options.deskew;
        prepOptions.denoiseOptions = options.denoiseOptions;
        prepOptions.deskewOptions = options.deskewOptions;

        const ocr::ImagePrepResult prep = ocr::preprocessScannedPage(rendered, prepOptions);
        if (!prep.valid) {
            // 预处理失败就用原图，但要说清楚这一页没被优化。
            ScannedPageOptimize fallback;
            fallback.pageIndex = pageIndex;
            fallback.note = QStringLiteral("预处理失败，这一页保持原样。");
            pageResults.append(fallback);
            QByteArray bytes;
            if (!encodePng(rendered, &bytes)) {
                continue;
            }
            encoded.append(bytes);
            continue;
        }

        ScannedPageOptimize entry;
        entry.pageIndex = pageIndex;
        entry.deskewAngleDeg = prep.deskewAngleDeg;
        entry.denoised = options.denoise;
        if (options.deskew && std::abs(prep.deskewAngleDeg) > 0.05) {
            entry.note = QStringLiteral("已纠偏 %1°。").arg(prep.deskewAngleDeg, 0, 'f', 1);
        }
        pageResults.append(entry);

        QByteArray bytes;
        if (!encodePng(prep.image, &bytes)) {
            continue;
        }
        encoded.append(bytes);
    }

    if (encoded.isEmpty()) {
        return fail(QStringLiteral("没有一页能渲染出来，产物为空。"));
    }

    pdf::ImageToPdfOptions pdfOptions;
    pdfOptions.dpi = options.dpi;
    const pdf::ImageToPdfResult result = pdf::imagesToPdf(encoded, pdfOptions);
    if (!result.ok() || result.pdf.isEmpty()) {
        return fail(QStringLiteral("优化后的页面没能写成 PDF。"));
    }

    if (report != nullptr) {
        report->ok = true;
        report->error.clear();
        report->pageCount = encoded.size();
        report->pages = pageResults;
        report->notes.append(QStringLiteral("产物是图像化 PDF：文字层不再保留（扫描件通常没有文字层），"
                                            "纠偏过的页面尺寸可能与原文件有细微差别。"));
    }
    return result.pdf;
}

}  // namespace aipdf::doc
