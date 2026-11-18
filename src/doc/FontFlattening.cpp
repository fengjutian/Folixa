#include "doc/FontFlattening.h"

#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QTransform>
#include <QtMath>

#include <optional>
#include <vector>

#include "doc/BlankDocument.h"
#include "pdf/kernel/PdfiumAdapter.h"

namespace aipdf::doc {

namespace {

// 一英寸 72 点。
constexpr double kPointsPerInch = 72.0;

// 分辨率上限。
//
// 为什么有上限：位图面积按 dpi 的平方增长，6000 DPI 的 A4 页是
// 5 万 × 7 万像素（约 140 亿像素），一个 QImage 就要 560 GB，
// 还没写进 PDF 就先把内存吃光了。上限之外按上限处理。
constexpr double kMaxDpi = 1200.0;

// 把「显示方向」的位图转回「页面坐标方向」。
//
// PDF 的 /Rotate 是顺时针的：显示方向 = 页面坐标方向顺时针转 N 度。
// 要回到页面坐标方向，就得把显示图逆时针转 N 度。
QImage rotateBackToPageSpace(const QImage& displayImage, int rotationDegrees) {
    const int normalized = ((rotationDegrees % 360) + 360) % 360;
    if (normalized == 0) {
        return displayImage;
    }
    return displayImage.transformed(QTransform().rotate(-normalized));
}

// 一页渲染出来的结果。
struct RenderedPage {
    // 显示方向的位图（PDFium 已经应用了页面自带的 /Rotate）。
    QImage displayImage;
    // 页面坐标方向的位图（用来盖回原页面）。
    QImage pageSpaceImage;
    // 显示尺寸（点，已经包含 /Rotate）。
    QSizeF displaySize;
    // 页面有效盒（点，页面坐标方向）。
    QRectF contentRect;
};

}  // namespace

QByteArray flattenFontsToOutlines(const QByteArray& pdfBytes, const FlattenOptions& options,
                                  QString* error) {
    const auto fail = [error](const QString& reason) -> QByteArray {
        if (error != nullptr) {
            *error = reason;
        }
        return QByteArray();
    };

    if (pdfBytes.isEmpty()) {
        return fail(QStringLiteral("字体转曲失败：输入字节为空。"));
    }
    if (!(options.dpi > 0.0)) {
        return fail(QStringLiteral("字体转曲失败：dpi 必须大于 0。"));
    }
    const double dpi = qMin(options.dpi, kMaxDpi);
    const double zoom = dpi / kPointsPerInch;

    // 只通过 pdf 适配层调用 PDFium：适配层里那把进程级锁是全局的，
    // doc 层自己 include PDFium 就等于绕开它。
    pdf::PdfOpenResult opened = pdf::openDocumentFromBytes(pdfBytes);
    if (!opened.document.isValid()) {
        return fail(QStringLiteral("字体转曲失败：输入不是有效的 PDF（%1）。")
                        .arg(opened.errorMessage));
    }
    const int pageCount = opened.document.pageCount();
    if (pageCount <= 0) {
        return fail(QStringLiteral("字体转曲失败：文档里没有页面。"));
    }

    // 先把所有页面渲染出来：渲染失败要在写任何东西之前就知道。
    std::vector<RenderedPage> rendered;
    rendered.reserve(static_cast<std::size_t>(pageCount));
    for (int i = 0; i < pageCount; ++i) {
        const std::optional<pdf::PdfPage> page = opened.document.page(i);
        if (!page.has_value() || !page->isValid()) {
            return fail(QStringLiteral("字体转曲失败：第 %1 页无效。").arg(i + 1));
        }
        RenderedPage item;
        item.displayImage = page->render(zoom);
        if (item.displayImage.isNull()) {
            return fail(QStringLiteral("字体转曲失败：第 %1 页渲染失败（分辨率过高或页面损坏）。")
                            .arg(i + 1));
        }
        item.pageSpaceImage = rotateBackToPageSpace(item.displayImage, page->rotationDegrees());
        item.displaySize = QSizeF(page->widthPoints(), page->heightPoints());
        const pdf::MediaBox box = page->contentBox();
        item.contentRect = QRectF(box.left, box.bottom, box.width(), box.height());
        rendered.push_back(item);
    }

    if (options.removeTextLayer) {
        // 目标文档：新建一份只有位图的 PDF。
        // 页面尺寸用**显示尺寸**（已经含 /Rotate），位图也用显示方向，
        // 这样旋转就烘进像素里了，不必再往新页上写 /Rotate。
        BlankDocumentOptions blankOptions;
        blankOptions.size = PageSize::Custom;
        blankOptions.widthPt = rendered.front().displaySize.width();
        blankOptions.heightPt = rendered.front().displaySize.height();
        blankOptions.pageCount = 1;

        QString blankError;
        const QByteArray blankBytes = createBlankDocument(blankOptions, &blankError);
        if (blankBytes.isEmpty()) {
            return fail(QStringLiteral("字体转曲失败：%1").arg(blankError));
        }
        pdf::PdfOpenResult target = pdf::openDocumentFromBytes(blankBytes);
        if (!target.document.isValid()) {
            return fail(QStringLiteral("字体转曲失败：新建的目标文档打不开。"));
        }
        // 页面尺寸不一致时，只能一页一页插。
        for (std::size_t i = 1; i < rendered.size(); ++i) {
            if (!target.document.insertBlankPage(static_cast<int>(i),
                                                 rendered[i].displaySize.width(),
                                                 rendered[i].displaySize.height())) {
                return fail(QStringLiteral("字体转曲失败：新建第 %1 页失败。")
                                .arg(static_cast<int>(i) + 1));
            }
        }
        for (int i = 0; i < pageCount; ++i) {
            std::optional<pdf::PdfPage> page = target.document.page(i);
            if (!page.has_value() || !page->isValid()) {
                return fail(QStringLiteral("字体转曲失败：第 %1 页无效。").arg(i + 1));
            }
            const QSizeF size = rendered[static_cast<std::size_t>(i)].displaySize;
            if (!page->addImageLayer(QRectF(0.0, 0.0, size.width(), size.height()),
                                     rendered[static_cast<std::size_t>(i)].displayImage)) {
                return fail(QStringLiteral("字体转曲失败：第 %1 页写入位图失败。").arg(i + 1));
            }
        }
        QString serializeError;
        const QByteArray output = target.document.serialize(&serializeError);
        if (output.isEmpty()) {
            return fail(QStringLiteral("字体转曲失败：写出 PDF 失败（%1）。").arg(serializeError));
        }
        return output;
    }

    // 保留文本层：直接在原文档上盖一层位图。
    // 位图必须用**页面坐标方向**，否则旋转过的页面会盖歪。
    for (int i = 0; i < pageCount; ++i) {
        std::optional<pdf::PdfPage> page = opened.document.page(i);
        if (!page.has_value() || !page->isValid()) {
            return fail(QStringLiteral("字体转曲失败：第 %1 页无效。").arg(i + 1));
        }
        if (!page->addImageLayer(rendered[static_cast<std::size_t>(i)].contentRect,
                                 rendered[static_cast<std::size_t>(i)].pageSpaceImage)) {
            return fail(QStringLiteral("字体转曲失败：第 %1 页写入位图失败。").arg(i + 1));
        }
    }
    QString serializeError;
    const QByteArray output = opened.document.serialize(&serializeError);
    if (output.isEmpty()) {
        return fail(QStringLiteral("字体转曲失败：写出 PDF 失败（%1）。").arg(serializeError));
    }
    return output;
}

bool hasExtractableText(const QByteArray& pdfBytes) {
    if (pdfBytes.isEmpty()) {
        return false;
    }
    pdf::PdfOpenResult opened = pdf::openDocumentFromBytes(pdfBytes);
    if (!opened.document.isValid()) {
        return false;
    }
    const int pageCount = opened.document.pageCount();
    for (int i = 0; i < pageCount; ++i) {
        const std::optional<pdf::PdfPage> page = opened.document.page(i);
        if (!page.has_value() || !page->isValid()) {
            continue;
        }
        const std::optional<pdf::TextPage> text = page->extractText();
        if (text.has_value() && !text->isEmpty()) {
            return true;
        }
    }
    return false;
}

}  // namespace aipdf::doc
