#include "doc/convert/FontFlattening.h"

#include "doc/convert/GlyphOutline.h"
#include "doc/convert/OutlinePdfWriter.h"
#include "doc/model/BlankDocument.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QImage>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QTransform>
#include <QtMath>

#include <optional>
#include <vector>

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

// 栅格化转曲（旧行为）：整页渲染成位图再写回一份新 PDF。
//
// 这一条保留下来是因为它是**唯一**能处理任何文档的兜底：
// 轮廓化遇到取不到字形轮廓的字体时做不到「整页视觉等价」。
QByteArray flattenByRasterizing(const QByteArray& pdfBytes, const FlattenOptions& options,
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

// 轮廓化转曲：逐字取字形轮廓，把文字换成路径，产物里不留字体。
QByteArray flattenByOutlining(const QByteArray& pdfBytes, const FlattenOptions& options,
                               FlattenReport* report, QString* error) {
    const auto fail = [error, report](const QString& reason) -> QByteArray {
        if (error != nullptr) {
            *error = reason;
        }
        if (report != nullptr) {
            report->ok = false;
            report->error = reason;
        }
        return QByteArray();
    };

    if (pdfBytes.isEmpty()) {
        return fail(QStringLiteral("字体转曲失败：输入字节为空。"));
    }

#ifdef AIPDF_WITH_QPDF
    // 先用适配层打开一次：既验证输入，又让 PDFium 完成初始化
    // （初始化函数一个进程只该调一次，适配层已经负责了）。
    pdf::PdfOpenResult opened = pdf::openDocumentFromBytes(pdfBytes);
    if (!opened.document.isValid()) {
        return fail(QStringLiteral("字体转曲失败：输入不是有效的 PDF（%1）。")
                        .arg(opened.errorMessage));
    }
    if (opened.document.pageCount() <= 0) {
        return fail(QStringLiteral("字体转曲失败：文档里没有页面。"));
    }

    QVector<PageOutlineResult> outlines;
    QString extractError;
    if (!extractOutlines(pdfBytes, &outlines, &extractError)) {
        return fail(extractError);
    }

    // 哪些页走「保留原文字」的回退。
    QSet<int> keepTextPages;
    for (const PageOutlineResult& page : outlines) {
        const bool keepText = !page.failures.isEmpty() && options.keepTextWhenUnconvertible;
        if (report != nullptr) {
            report->pageCount = qMax(report->pageCount, page.pageIndex + 1);
            report->outlinedCharCount += page.outlined.size();
            report->skipped.append(page.failures);
            if (!page.substitutedFonts.isEmpty()) {
                report->notes.append(
                    QStringLiteral("第 %1 页：字体 %2 没有内嵌字体程序，轮廓取自替代字体"
                                   "（形状可能与原文件显示的略有差异）。")
                        .arg(page.pageIndex + 1)
                        .arg(page.substitutedFonts.join(QStringLiteral("、"))));
            }
            if (page.nonFillRenderModeChars > 0) {
                report->notes.append(
                    QStringLiteral("第 %1 页：有 %2 个字符不是填充渲染（描边／不可见／裁剪），"
                                   "轮廓化按填充还原，视觉可能有差别。")
                        .arg(page.pageIndex + 1)
                        .arg(page.nonFillRenderModeChars));
            }
        }
        if (keepText) {
            keepTextPages.insert(page.pageIndex);
            if (report != nullptr) {
                report->keptCharCount += page.totalChars;
                report->notes.append(
                    QStringLiteral("第 %1 页：有 %2 个字符取不到字形轮廓，该页按回退策略"
                                   "**保留原文字与字体**（产物里这一页仍然依赖字体）。")
                        .arg(page.pageIndex + 1)
                        .arg(page.failures.size()));
            }
        } else if (report != nullptr) {
            report->droppedCharCount += page.failures.size();
        }
    }

    const detail::OutlineWriteResult written =
        detail::writeFlattenedPdf(pdfBytes, outlines, keepTextPages);
    if (!written.ok) {
        return fail(QStringLiteral("字体转曲失败：%1").arg(written.error));
    }
    if (report != nullptr) {
        report->ok = true;
        if (report->droppedCharCount > 0) {
            report->notes.append(
                QStringLiteral("有 %1 个字符取不到字形轮廓，已按「丢弃」策略处理"
                               "（产物里一定没有字体，但这些字的内容没有了）。")
                    .arg(report->droppedCharCount));
        }
        // 产物里若仍有字体程序（例如来自批注），必须说清楚，
        // 不许让用户以为「已经没有任何字体依赖」。
        if (written.bytes.contains("/FontFile")) {
            report->notes.append(
                QStringLiteral("产物里仍然存在字体程序（可能来自批注或表单域），"
                               "这部分没有转曲。"));
        }
    }
    return written.bytes;
#else
    Q_UNUSED(options)
    return fail(QStringLiteral("字体转曲失败：这份构建没有 qpdf，做不了轮廓化转曲。"));
#endif
}

}  // namespace

QByteArray flattenFontsToOutlines(const QByteArray& pdfBytes, const FlattenOptions& options,
                                  QString* error) {
    FlattenReport report;
    return flattenFontsToOutlines(pdfBytes, options, &report, error);
}

QByteArray flattenFontsToOutlines(const QByteArray& pdfBytes, const FlattenOptions& options,
                                  FlattenReport* report, QString* error) {
    if (report != nullptr) {
        *report = FlattenReport();
    }
    if (error != nullptr) {
        error->clear();
    }
    if (options.mode == FlattenMode::Raster) {
        const QByteArray bytes = flattenByRasterizing(pdfBytes, options, error);
        if (report != nullptr) {
            report->ok = !bytes.isEmpty();
            report->notes.append(
                QStringLiteral("本次用的是栅格化模式：整页渲染成位图，文字变成像素，"
                               "产物里没有矢量轮廓。"));
        }
        return bytes;
    }
    return flattenByOutlining(pdfBytes, options, report, error);
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
