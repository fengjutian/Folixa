#include "doc/PdfToOffice.h"

#include "doc/OfficeExport.h"
#include "doc/PptxWriter.h"
#include "doc/SpreadsheetExport.h"
#include "pdf/PdfiumAdapter.h"
#include "pdf/TableStructure.h"
#include "pdf/TextPage.h"

#include <QRectF>
#include <QSizeF>
#include <QVector>

#include <optional>

namespace aipdf::doc {

namespace {

// 一页幻灯片最多放多少行文字。
//
// 为什么要拆：PowerPoint 的正文框放不下几十行，硬塞进去的话文字会溢出到框外
// （看得见，但版式乱了）。拆成「第 N 页（续）」既不丢字，也不溢出。
constexpr int kLinesPerSlide = 15;

// 把一页的文本行变成表格检测用的对象。
//
// 为什么用「行」而不是「字符」：PDF 的字符流里没有「单元格」的概念，
// 一个单元格里的文字通常就是一行（也可能折成几行）。用行做检测的粒度
// 与 pdf::TableStructure 的设计一致（它按行的 y 聚类、按 x 分列）。
QVector<pdf::TableTextObject> tableObjectsFromLines(const QVector<TextExportLine>& lines) {
    QVector<pdf::TableTextObject> objects;
    objects.reserve(lines.size());
    for (int index = 0; index < lines.size(); ++index) {
        const TextExportLine& line = lines.at(index);
        if (line.text.trimmed().isEmpty()) {
            continue;
        }
        pdf::TableTextObject object;
        object.objectId = index;
        // TextExportLine 的 bottom 是较小的 y、top 是较大的 y（PDF 坐标），
        // 高度取两者之差即可。
        object.bounds = QRectF(line.left, line.bottom, line.right - line.left,
                               line.top - line.bottom);
        object.text = line.text;
        objects.append(object);
    }
    return objects;
}

// 表格 → 二维单元格。
QVector<QVector<QString>> tableRows(const pdf::TableStructure& table) {
    QVector<QVector<QString>> rows;
    if (!table.isValid()) {
        return rows;
    }
    rows.resize(table.rowCount);
    for (int row = 0; row < table.rowCount; ++row) {
        rows[row].resize(table.columnCount);
    }
    for (const pdf::TableCell& cell : table.cells) {
        if (cell.row < 0 || cell.row >= rows.size()) {
            continue;
        }
        if (cell.column < 0 || cell.column >= rows.at(cell.row).size()) {
            continue;
        }
        rows[cell.row][cell.column] = cell.text;
    }
    return rows;
}

// 这一行是不是落在表格里（落在任何一个单元格里就算在）。
bool lineInsideTable(const TextExportLine& line, const pdf::TableStructure& table) {
    const QPointF center(line.left, (line.bottom + line.top) * 0.5);
    for (const pdf::TableCell& cell : table.cells) {
        if (cell.bounds.contains(center)) {
            return true;
        }
    }
    return false;
}

// 表格之外的文字行 → 单列的工作表。
QVector<QVector<QString>> linesOutsideTable(const QVector<TextExportLine>& lines,
                                            const pdf::TableStructure& table) {
    QVector<QVector<QString>> rows;
    for (const TextExportLine& line : lines) {
        if (line.text.trimmed().isEmpty()) {
            continue;
        }
        if (lineInsideTable(line, table)) {
            continue;
        }
        rows.append(QVector<QString>{line.text});
    }
    return rows;
}

// 全部文字行 → 单列的工作表。
QVector<QVector<QString>> linesAsColumn(const QVector<TextExportLine>& lines) {
    QVector<QVector<QString>> rows;
    for (const TextExportLine& line : lines) {
        if (line.text.trimmed().isEmpty()) {
            continue;
        }
        rows.append(QVector<QString>{line.text});
    }
    return rows;
}

// 一页的文字（去掉空行）。
QStringList textsOf(const QVector<TextExportLine>& lines) {
    QStringList texts;
    for (const TextExportLine& line : lines) {
        if (line.text.trimmed().isEmpty()) {
            continue;
        }
        texts.append(line.text);
    }
    return texts;
}

// 打开 PDF 并把每一页的文字层取出来。
//
// 为什么在这里统一做：三种导出都要「逐页取文字层」，各写一遍就会出现
// 三种略有差异的取法（比如某一版漏了空页处理）。
struct PageTexts {
    bool ok = false;
    QVector<pdf::TextPage> pages;
    QSizeF pageSizePt;
    QString error;
};

PageTexts collectPageTexts(const QByteArray& pdfBytes) {
    PageTexts collected;
    const pdf::PdfOpenResult opened =
        pdf::openDocumentFromBytes(pdfBytes, QStringLiteral("export.pdf"), QString());
    if (!opened.document.isValid()) {
        collected.error = opened.errorMessage.isEmpty()
                              ? QStringLiteral("打不开这个 PDF。")
                              : opened.errorMessage;
        return collected;
    }
    const int pageCount = opened.document.pageCount();
    for (int index = 0; index < pageCount; ++index) {
        const std::optional<pdf::PdfPage> page = opened.document.page(index);
        if (!page.has_value() || !page->isValid()) {
            continue;
        }
        if (collected.pageSizePt.isEmpty()) {
            collected.pageSizePt = QSizeF(page->contentBox().width(), page->contentBox().height());
        }
        const std::optional<pdf::TextPage> text = page->extractText();
        if (!text.has_value()) {
            // 没有文本层的页（纯图片页）也要占一页的位置：
            // 整篇导出的页数要与原文对得上。
            pdf::TextPage empty;
            empty.pageIndex = index;
            empty.contentBox = page->contentBox();
            collected.pages.append(empty);
            continue;
        }
        collected.pages.append(*text);
    }
    collected.ok = true;
    return collected;
}

}  // namespace

ExcelExportResult exportPdfToExcel(const QByteArray& pdfBytes) {
    ExcelExportResult result;
    const PageTexts collected = collectPageTexts(pdfBytes);
    if (!collected.ok) {
        result.error = collected.error;
        return result;
    }
    if (collected.pages.isEmpty()) {
        result.error = QStringLiteral("导出失败：这个 PDF 里没有页面。");
        return result;
    }

    QVector<SheetGrid> sheets;
    for (int index = 0; index < collected.pages.size(); ++index) {
        ++result.pageCount;
        const QVector<TextExportLine> lines = buildTextLines(collected.pages.at(index));
        if (lines.isEmpty()) {
            continue;
        }
        const pdf::TableStructure table =
            pdf::TableStructure::detect(tableObjectsFromLines(lines));
        if (table.isValid()) {
            SheetGrid grid;
            grid.name = QStringLiteral("第 %1 页 表").arg(index + 1);
            grid.rows = tableRows(table);
            sheets.append(grid);
            ++result.tableCount;

            // 表以外的文字单独一张表：混在表格工作表里会破坏行列结构。
            SheetGrid rest;
            rest.name = QStringLiteral("第 %1 页 文字").arg(index + 1);
            rest.rows = linesOutsideTable(lines, table);
            if (!rest.rows.isEmpty()) {
                sheets.append(rest);
            }
        } else {
            SheetGrid grid;
            grid.name = QStringLiteral("第 %1 页").arg(index + 1);
            grid.rows = linesAsColumn(lines);
            sheets.append(grid);
        }
    }

    if (sheets.isEmpty()) {
        result.error = QStringLiteral("导出失败：文档里没有可导出的文字层。");
        return result;
    }
    result.bytes = workbookToXlsx(sheets);
    result.ok = !result.bytes.isEmpty();
    result.sheetCount = sheets.size();
    if (!result.ok) {
        result.bytes.clear();
        result.error = QStringLiteral("导出 Excel 失败：生成的工作簿是空的。");
    }
    return result;
}

PptxExportResult exportPdfToPptx(const QByteArray& pdfBytes) {
    PptxExportResult result;
    const PageTexts collected = collectPageTexts(pdfBytes);
    if (!collected.ok) {
        result.error = collected.error;
        return result;
    }
    if (collected.pages.isEmpty()) {
        result.error = QStringLiteral("导出失败：这个 PDF 里没有页面。");
        return result;
    }

    pptx::Deck deck;
    deck.title = QStringLiteral("PDF 导出");
    for (int index = 0; index < collected.pages.size(); ++index) {
        const QStringList texts = textsOf(buildTextLines(collected.pages.at(index)));
        if (texts.isEmpty()) {
            // 没有文字的页也要有一张幻灯片：页数要与原文对得上。
            pptx::Slide slide;
            slide.index = deck.slides.size() + 1;
            slide.title = QStringLiteral("第 %1 页").arg(index + 1);
            deck.slides.append(slide);
            continue;
        }
        // 文字太多就拆成「（续）」：宁可多几页，也不让文字溢出到框外。
        int offset = 0;
        int part = 0;
        while (offset < texts.size()) {
            pptx::Slide slide;
            slide.index = deck.slides.size() + 1;
            slide.title = part == 0 ? QStringLiteral("第 %1 页").arg(index + 1)
                                    : QStringLiteral("第 %1 页（续）").arg(index + 1);
            const int count = qMin(kLinesPerSlide, texts.size() - offset);
            for (int line = 0; line < count; ++line) {
                slide.bullets.append(texts.at(offset + line));
            }
            offset += count;
            ++part;
            deck.slides.append(slide);
        }
    }

    result.bytes = pptx::deckToPptx(deck);
    result.ok = !result.bytes.isEmpty();
    result.slideCount = deck.slides.size();
    if (!result.ok) {
        result.bytes.clear();
        result.error = QStringLiteral("导出 PowerPoint 失败：生成的演示文稿是空的。");
    }
    return result;
}

WordExportResult exportPdfToWord(const QByteArray& pdfBytes) {
    WordExportResult result;
    const PageTexts collected = collectPageTexts(pdfBytes);
    if (!collected.ok) {
        result.error = collected.error;
        return result;
    }
    if (collected.pages.isEmpty()) {
        result.error = QStringLiteral("导出失败：这个 PDF 里没有页面。");
        return result;
    }
    const ExportResult exported =
        exportPagesTo(ExportFormat::WordOpenXml, collected.pages, collected.pageSizePt);
    result.ok = exported.ok;
    result.bytes = exported.bytes;
    result.pageCount = collected.pages.size();
    if (!result.ok) {
        result.bytes.clear();
        result.error = exported.error.isEmpty()
                           ? QStringLiteral("导出 Word 失败：生成的文档是空的。")
                           : exported.error;
    }
    return result;
}

}  // namespace aipdf::doc
