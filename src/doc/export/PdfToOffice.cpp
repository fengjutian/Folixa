#include "doc/export/PdfToOffice.h"

#include "doc/export/OfficeExport.h"
#include "doc/export/PptxWriter.h"
#include "doc/export/SpreadsheetExport.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/kernel/TableStructure.h"
#include "pdf/kernel/TextPage.h"

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

// 一行里的单元格：按横向间隙切开。
//
// 为什么不能直接拿「行」当单元格：表格的一行在文本层里是**一整行文字**
// （"Item Qty Price"），列与列之间只是空得远一点。不切开的话，
// 表格检测会以为这一行只有一个格子，整张表就认不出来（实测过：
// 切之前导出的工作表里是「Apple 1 2 3 50」这种糊在一起的内容）。
//
// 阈值取字高的 0.9 倍：词距（≈0.3 倍字高）远小于它，列距通常远大于它。
QVector<QVector<pdf::TextChar>> splitLineIntoCells(const TextExportLine& line) {
    QVector<QVector<pdf::TextChar>> cells;
    QVector<pdf::TextChar> current;
    double previousRight = 0.0;
    bool hasPrevious = false;
    const double threshold = qMax(3.0, line.fontSize * 0.9);
    for (const pdf::TextChar& character : line.chars) {
        if (hasPrevious) {
            const double gap = character.box.left() - previousRight;
            if (gap > threshold && !current.isEmpty()) {
                cells.append(current);
                current.clear();
            }
        }
        current.append(character);
        previousRight = character.box.right();
        hasPrevious = true;
    }    if (!current.isEmpty()) {
        cells.append(current);
    }
    return cells;
}

// 单元格的文字：直接拼字符，不补空格。
//
// 为什么不用 buildLineText：那个函数按字距补空格（词与词之间要断开），
// 而单元格里的空格是原本就有的，补出来的空格会把 "3.50" 变成 "3 . 50"。
QString cellText(const QVector<pdf::TextChar>& chars) {
    QString out;
    for (const pdf::TextChar& character : chars) {
        out.append(character.text);
    }
    // 两头空白去掉：PDFium 的文本流里夹着排版产生的空白
    // （两个文本对象之间的换行会被当成一个字符抽出来）。
    // 不去掉的话，单元格里会是 "Item " 这种带尾巴的值（openpyxl 读得出来）。
    return out.trimmed();
}

// 把一页的文本行变成表格检测用的对象（每个单元格一个对象）。
QVector<pdf::TableTextObject> tableObjectsFromLines(const QVector<TextExportLine>& lines) {
    QVector<pdf::TableTextObject> objects;
    int objectId = 0;
    for (const TextExportLine& line : lines) {
        for (const QVector<pdf::TextChar>& cell : splitLineIntoCells(line)) {
            const QString text = cellText(cell);
            if (text.trimmed().isEmpty()) {
                continue;
            }
            double left = 0.0;
            double right = 0.0;
            double bottom = 0.0;
            double top = 0.0;
            bool first = true;
            for (const pdf::TextChar& character : cell) {
                if (first) {
                    left = character.box.left();
                    right = character.box.right();
                    bottom = character.box.top();
                    top = character.box.bottom();
                    first = false;
                    continue;
                }
                left = qMin(left, character.box.left());
                right = qMax(right, character.box.right());
                bottom = qMin(bottom, character.box.top());
                top = qMax(top, character.box.bottom());
            }
            if (first) {
                continue;
            }
            pdf::TableTextObject object;
            object.objectId = objectId++;
            object.bounds = QRectF(left, bottom, right - left, top - bottom);
            object.text = text;
            objects.append(object);
        }
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

// 表格的整体包围盒（所有单元格的并集）。
QRectF tableBounds(const pdf::TableStructure& table) {
    QRectF bounds;
    for (const pdf::TableCell& cell : table.cells) {
        bounds = bounds.isNull() ? cell.bounds : bounds.united(cell.bounds);
    }
    return bounds;
}

// 这一行是不是落在表格里。
//
// 用「行的中心点」判断：表格的一行横跨好几列，中心点落在表格范围内就算在表里；
// 表格上方或下方的说明文字中心点在范围外，就不算。
bool lineInsideTable(const TextExportLine& line, const pdf::TableStructure& table) {
    const QRectF bounds = tableBounds(table);
    if (bounds.isEmpty()) {
        return false;
    }
    const QPointF center((line.left + line.right) * 0.5, (line.bottom + line.top) * 0.5);
    return bounds.contains(center);
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
