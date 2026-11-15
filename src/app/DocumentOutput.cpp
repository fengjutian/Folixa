#include "app/DocumentOutput.h"

#include "pdf/DocumentWriter.h"

#include "i18n/Translate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPageRanges>
#include <QPainter>
#include <QPrinter>

#include <algorithm>
#include <cmath>

namespace aipdf::app {

namespace {

int lastPageIndex(const pdf::PdfDocument& document) {
    return std::max(0, document.pageCount() - 1);
}

OutputRange normalized(const OutputRange& range, const pdf::PdfDocument& document) {
    OutputRange out;
    out.firstPage = std::clamp(range.firstPage, 0, lastPageIndex(document));
    out.lastPage = range.lastPage < 0 ? lastPageIndex(document)
                                      : std::clamp(range.lastPage, out.firstPage, lastPageIndex(document));
    return out;
}

QString paddedPage(int pageIndex) {
    return QStringLiteral("%1").arg(pageIndex + 1, 3, 10, QLatin1Char('0'));
}

}  // namespace

pdf::PdfDocument documentForOutput(const pdf::PdfDocument& document, QString* warning) {
    const pdf::SaveBytes prepared = pdf::DocumentWriter::serializeForSave(document);
    if (prepared.ok && !prepared.bytes.isEmpty()) {
        const pdf::PdfOpenResult reopened = pdf::openDocumentFromBytes(prepared.bytes);
        if (reopened.document.isValid()) {
            return reopened.document;
        }
        if (warning != nullptr) {
            *warning = i18n::tr("aipdf::app::DocumentOutput", "输出副本打不开：%1")
                           .arg(reopened.errorMessage);
        }
        return document;
    }

    // 退回原文档：正文还在，只是本程序写的批注可能不出现。调用方要提示用户。
    if (warning != nullptr) {
        *warning = prepared.error.isEmpty()
                      ? i18n::tr("aipdf::app::DocumentOutput", "输出副本创建失败，批注可能不可见")
                      : i18n::tr("aipdf::app::DocumentOutput", "输出副本创建失败：%1")
                            .arg(prepared.error);
    }
    return document;
}

OutputResult exportPageImages(const pdf::PdfDocument& document,
                              const QString& directory,
                              const QString& baseName,
                              const ImageExportOptions& options) {
    OutputResult result;
    if (!document.isValid()) {
        result.error = i18n::tr("aipdf::app::DocumentOutput", "没有可导出的文档");
        return result;
    }
    const QDir dir(directory);
    if (!dir.exists()) {
        result.error = i18n::tr("aipdf::app::DocumentOutput", "目录不存在：%1").arg(directory);
        return result;
    }
    const int dpi = std::clamp(options.dpi, 72, 600);
    const double zoom = dpi / 72.0;
    const QString format = options.format.toLower();

    QString warning;
    const pdf::PdfDocument source = documentForOutput(document, &warning);

    const OutputRange range = normalized(options.range, source);
    for (int pageIndex = range.firstPage; pageIndex <= range.lastPage; ++pageIndex) {
        const auto page = source.page(pageIndex);
        if (!page.has_value()) {
            result.error = i18n::tr("aipdf::app::DocumentOutput", "第 %1 页打不开").arg(pageIndex + 1);
            return result;
        }
        const QImage image = page->render(zoom);
        if (image.isNull()) {
            result.error = i18n::tr("aipdf::app::DocumentOutput", "第 %1 页渲染失败").arg(pageIndex + 1);
            return result;
        }
        const QString fileName =
            QStringLiteral("%1-p%2.%3").arg(baseName, paddedPage(pageIndex), format);
        const QString path = dir.filePath(fileName);
        if (!image.save(path, nullptr, format == QStringLiteral("jpg") ? options.jpegQuality : -1)) {
            result.error = i18n::tr("aipdf::app::DocumentOutput", "写入失败：%1").arg(path);
            return result;
        }
        result.files.append(path);
    }

    result.ok = true;
    return result;
}

OutputResult exportPlainText(const pdf::PdfDocument& document,
                             const QString& filePath,
                             const TextExportOptions& options) {
    OutputResult result;
    if (!document.isValid()) {
        result.error = i18n::tr("aipdf::app::DocumentOutput", "没有可导出的文档");
        return result;
    }
    if (filePath.isEmpty()) {
        result.error = i18n::tr("aipdf::app::DocumentOutput", "没有目标路径");
        return result;
    }

    QString warning;
    const pdf::PdfDocument source = documentForOutput(document, &warning);
    const OutputRange range = normalized(options.range, source);

    QString text;
    for (int pageIndex = range.firstPage; pageIndex <= range.lastPage; ++pageIndex) {
        const auto page = source.page(pageIndex);
        if (!page.has_value()) {
            result.error = i18n::tr("aipdf::app::DocumentOutput", "第 %1 页打不开").arg(pageIndex + 1);
            return result;
        }
        const auto textPage = page->extractText();
        if (textPage.has_value()) {
            text += textPage->plainText();
        }
        // 页与页之间用换页符：粘进别的工具时仍能看出分页。
        if (pageIndex < range.lastPage) {
            text += QLatin1Char('\f');
        }
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = i18n::tr("aipdf::app::DocumentOutput", "无法写入：%1").arg(file.errorString());
        return result;
    }
    // UTF-8 BOM：Windows 记事本靠它认编码，否则中文会变乱码。
    file.write("\xEF\xBB\xBF");
    file.write(text.toUtf8());
    file.close();

    result.ok = true;
    result.files.append(filePath);
    return result;
}

bool printDocument(const pdf::PdfDocument& document,
                   QPrinter* printer,
                   const PrintOptions& options,
                   QString* error) {
    if (printer == nullptr) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::DocumentOutput", "没有可用的打印机");
        }
        return false;
    }
    if (!document.isValid()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::DocumentOutput", "没有可打印的文档");
        }
        return false;
    }

    QString warning;
    const pdf::PdfDocument source =
        options.includeAnnotations ? documentForOutput(document, &warning) : document;

    // 打印对话框里选了"页面范围"就以它为准——用户的这次选择比调用方的默认值更新。
    OutputRange requested = options.range;
    const QPageRanges printerRanges = printer->pageRanges();
    if (!printerRanges.isEmpty()) {
        const QList<QPageRanges::Range> ranges = printerRanges.toRangeList();
        if (!ranges.isEmpty()) {
            requested.firstPage = ranges.first().from - 1;
            requested.lastPage = ranges.last().to - 1;
        }
    }
    const OutputRange range = normalized(requested, source);

    if (options.copies > 1) {
        printer->setCopyCount(options.copies);
    }

    QPainter painter;
    if (!painter.begin(printer)) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::DocumentOutput", "无法开始打印：%1")
                         .arg(printer->printerState() == QPrinter::Error
                                  ? i18n::tr("aipdf::app::DocumentOutput", "打印机错误")
                                  : i18n::tr("aipdf::app::DocumentOutput", "设备忙"));
        }
        return false;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // DevicePixel 下的可打印区域：所有换算都在这套坐标里做，最后一次性画上去。
    const QRectF paper = printer->pageRect(QPrinter::DevicePixel);

    // 每张纸排几页。2 = 竖排两页，4 = 2×2，6 = 3×2，9 = 3×3。
    int perSheet = 1;
    int columns = 1;
    int rows = 1;
    switch (options.pagesPerSheet) {
        case 2: perSheet = 2; rows = 2; break;
        case 4: perSheet = 4; rows = 2; columns = 2; break;
        case 6: perSheet = 6; rows = 3; columns = 2; break;
        case 9: perSheet = 9; rows = 3; columns = 3; break;
        default: break;
    }
    const double cellWidth = paper.width() / columns;
    const double cellHeight = paper.height() / rows;
    // 多页合一时留一点缝，否则相邻两页的边框会糊成一条线。
    const double gap = perSheet == 1 ? 0.0 : std::min(cellWidth, cellHeight) * 0.02;

    bool firstSheet = true;
    for (int sheetStart = range.firstPage; sheetStart <= range.lastPage; sheetStart += perSheet) {
        if (!firstSheet && !printer->newPage()) {
            painter.end();
            if (error != nullptr) {
                *error = i18n::tr("aipdf::app::DocumentOutput", "换页失败");
            }
            return false;
        }
        firstSheet = false;

        for (int slot = 0; slot < perSheet && sheetStart + slot <= range.lastPage; ++slot) {
            const int pageIndex = sheetStart + slot;
            const auto page = source.page(pageIndex);
            if (!page.has_value()) {
                painter.end();
                if (error != nullptr) {
                    *error = i18n::tr("aipdf::app::DocumentOutput", "第 %1 页打不开").arg(pageIndex + 1);
                }
                return false;
            }

            const double pageWidth = page->displayWidthPoints();
            const double pageHeight = page->displayHeightPoints();
            if (pageWidth <= 0.0 || pageHeight <= 0.0) {
                painter.end();
                if (error != nullptr) {
                    *error = i18n::tr("aipdf::app::DocumentOutput", "第 %1 页尺寸异常").arg(pageIndex + 1);
                }
                return false;
            }

            const int column = slot % columns;
            const int row = slot / columns;
            const QRectF cell(paper.x() + column * cellWidth + gap,
                              paper.y() + row * cellHeight + gap,
                              cellWidth - gap * 2, cellHeight - gap * 2);

            // render() 的输出像素 = 页面点数 × zoom。zoom 必须夹住：打印机 DevicePixel
            // 区域动辄上万像素（1200 DPI），按纸面比例渲染会撞上适配层的渲染上限
            // （单边 8192 / 总量 64M），拿回空图，表现就是"打印失败"。
            // 夹到 8 倍（约 576 DPI）既够清晰，也把最后一段放大交给 QPainter。
            const double fitScale = std::min(cell.width() / pageWidth, cell.height() / pageHeight);
            const double renderZoom = std::min(fitScale, 8.0);

            const QImage image = page->render(renderZoom);
            if (image.isNull()) {
                painter.end();
                if (error != nullptr) {
                    *error = i18n::tr("aipdf::app::DocumentOutput", "第 %1 页渲染失败").arg(pageIndex + 1);
                }
                return false;
            }

            // 按格子摆放（而不是按渲染像素），夹过 zoom 也不会让页面变小。
            const QRectF target(
                cell.x() + (cell.width() - pageWidth * fitScale) / 2.0,
                cell.y() + (cell.height() - pageHeight * fitScale) / 2.0,
                pageWidth * fitScale, pageHeight * fitScale);
            painter.drawImage(target, image);
        }
    }

    painter.end();
    return true;
}

}  // namespace aipdf::app