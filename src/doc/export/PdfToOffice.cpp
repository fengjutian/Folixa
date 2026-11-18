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

// 表格 → 导出用的网格。
//
// 列宽取每一列里**最宽**的那个单元：列宽要装得下最宽的内容，
// 取平均会让长单元被压扁、字被折行。
office::TableGrid tableGridOf(const pdf::TableStructure& table) {
    office::TableGrid grid;
    grid.rows = tableRows(table);
    const int columns = table.columnCount;
    grid.columnWidthsPoints.resize(columns);
    for (int column = 0; column < columns; ++column) {
        double width = 0.0;
        for (const pdf::TableCell& cell : table.cells) {
            if (cell.column == column) {
                width = qMax(width, cell.bounds.width());
            }
        }
        grid.columnWidthsPoints[column] = width;
    }
    const QRectF bounds = tableBounds(table);
    grid.leftPoints = bounds.left();
    // QRectF 的 top() 是较小的 y，也就是 PDF 的**下边**。
    grid.bottomPoints = bounds.top();
    grid.widthPoints = bounds.width();
    grid.heightPoints = bounds.height();
    return grid;
}

// 一页里认出的表格。
struct DetectedTable {
    // 识别出来的结构（行列、单元格）。
    pdf::TableStructure structure;
    // 导出用的网格（单元格文字 + 列宽 + 位置）。
    office::TableGrid grid;
    // 这张表属于哪条带。
    int band = 0;
};

// 一页里认出的表格（每条带最多一张）。
//
// 为什么按带认表：整页一起认表的话，双栏正文会被误判成一张两列表
// （两栏同基线的字在几何上与两列表格几乎一样）。分带之后，
// 每一带单独认表，正文与表格各归各位。
QVector<DetectedTable> tablesOfBands(const QVector<TextBand>& bands) {
    QVector<DetectedTable> detected;
    for (int band = 0; band < bands.size(); ++band) {
        const pdf::TableStructure table =
            pdf::TableStructure::detect(tableObjectsFromLines(bands.at(band).lines));
        if (!table.isValid()) {
            continue;
        }
        DetectedTable entry;
        entry.structure = table;
        entry.grid = tableGridOf(table);
        entry.band = band;
        detected.append(entry);
    }
    return detected;
}

// 各带的行拼在一起。
QVector<TextExportLine> concatLines(const QVector<TextBand>& bands) {
    QVector<TextExportLine> lines;
    for (const TextBand& band : bands) {
        lines.append(band.lines);
    }
    return lines;
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

// 表格之外的文字行（跨所有带）。
QVector<QVector<QString>> linesOutsideTables(const QVector<TextBand>& bands,
                                              const QVector<DetectedTable>& tables) {
    QVector<QVector<QString>> rows;
    for (int band = 0; band < bands.size(); ++band) {
        const pdf::TableStructure* structure = nullptr;
        for (const DetectedTable& table : tables) {
            if (table.band == band) {
                structure = &table.structure;
                break;
            }
        }
        const QVector<TextExportLine> lines = bands.at(band).lines;
        for (const TextExportLine& line : lines) {
            if (line.text.trimmed().isEmpty()) {
                continue;
            }
            if (structure != nullptr && lineInsideTable(line, *structure)) {
                continue;
            }
            rows.append(QVector<QString>{line.text});
        }
    }
    return rows;
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

// 打开 PDF 并把每一页的文字层与图片取出来。
//
// 为什么在这里统一做：三种导出都要「逐页取文字层」，各写一遍就会出现
// 三种略有差异的取法（比如某一版漏了空页处理）。
struct PageTexts {
    bool ok = false;
    QVector<pdf::TextPage> pages;
    // 每一页的图片（与 pages 同下标；没有图片的页是空列表）。
    QVector<QVector<pdf::PdfImage>> images;
    QSizeF pageSizePt;
    QString error;
};

// 导出时图片的重采样上限（DPI）。
//
// 为什么要上限：600 DPI 的扫描图按原样塞进包里，一个包就能到几百 MB
// （ZIP 又是 store 不压缩）。150 DPI 屏幕上看已经够清楚。
constexpr double kExportImageMaxDpi = 150.0;

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
        collected.images.append(page->extractImages(kExportImageMaxDpi));
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

// 一页的图片 → 导出用的落位。
//
// media 不为空时，顺手把图片的字节登记进媒体集合（编号全局连续）。
QVector<office::ImagePlacement> placementsOfImages(const QVector<pdf::PdfImage>& images,
                                                   const QVector<TextBand>& bands,
                                                   office::MediaSet* media) {
    QVector<office::ImagePlacement> placements;
    for (const pdf::PdfImage& image : images) {
        if (image.bytes.isEmpty()) {
            continue;
        }
        office::ImagePlacement placement;
        placement.mediaIndex = media != nullptr ? media->add(image.bytes, image.mimeType) : -1;
        placement.leftPoints = image.bounds.left();
        // QRectF 的 top() 是较小的 y，也就是 PDF 的**下边**。
        placement.bottomPoints = image.bounds.top();
        placement.widthPoints = image.bounds.width();
        placement.heightPoints = image.bounds.height();
        placement.bandIndex = qMax(0, bandIndexForPoint(bands, image.bounds.center().x(),
                                                        image.bounds.center().y()));
        placements.append(placement);
    }
    return placements;
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
        // 分带：每一带单独认表（双栏正文不会被误判成表格）。
        const QVector<TextBand> bands = buildTextBands(collected.pages.at(index));
        if (bands.isEmpty()) {
            continue;
        }
        const QVector<DetectedTable> tables = tablesOfBands(bands);
        if (tables.isEmpty()) {
            SheetGrid grid;
            grid.name = QStringLiteral("第 %1 页").arg(index + 1);
            grid.rows = linesAsColumn(concatLines(bands));
            if (!grid.rows.isEmpty()) {
                sheets.append(grid);
            }
            continue;
        }
        // 每张表一张工作表。
        for (int table = 0; table < tables.size(); ++table) {
            SheetGrid grid;
            grid.name = table == 0 ? QStringLiteral("第 %1 页 表").arg(index + 1)
                                   : QStringLiteral("第 %1 页 表 %2").arg(index + 1).arg(table + 1);
            grid.rows = tableRows(tables.at(table).structure);
            sheets.append(grid);
            ++result.tableCount;
        }
        // 表以外的文字单独一张表：混在表格工作表里会破坏行列结构。
        SheetGrid rest;
        rest.name = QStringLiteral("第 %1 页 文字").arg(index + 1);
        rest.rows = linesOutsideTables(bands, tables);
        if (!rest.rows.isEmpty()) {
            sheets.append(rest);
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

    // 媒体集合：整个包共用一份，编号全局连续。
    office::MediaSet media(QStringLiteral("ppt/media"));

    pptx::Deck deck;
    deck.title = QStringLiteral("PDF 导出");
    for (int index = 0; index < collected.pages.size(); ++index) {
        const QVector<TextBand> bands = buildTextBands(collected.pages.at(index));
        const QVector<TextExportLine> lines = concatLines(bands);
        const QStringList texts = textsOf(lines);
        const QVector<DetectedTable> tables = tablesOfBands(bands);

        // 文字太多就拆成「（续）」：宁可多几页，也不让文字溢出到框外。
        // 拆完之后，图片与表格按**纵向位置**落到对应的那一张上，
        // 否则一页多图时会全挤在第一张上。
        struct SlideChunk {
            int offset = 0;
            int count = 0;
            double top = 0.0;
            double bottom = 0.0;
        };
        QVector<SlideChunk> chunks;
        if (texts.isEmpty()) {
            SlideChunk chunk;
            chunk.count = 0;
            chunks.append(chunk);
        } else {
            int offset = 0;
            while (offset < texts.size()) {
                SlideChunk chunk;
                chunk.offset = offset;
                chunk.count = qMin(kLinesPerSlide, texts.size() - offset);
                chunk.top = -std::numeric_limits<double>::max();
                chunk.bottom = std::numeric_limits<double>::max();
                for (int line = 0; line < chunk.count; ++line) {
                    const TextExportLine& source = lines.at(offset + line);
                    chunk.top = qMax(chunk.top, source.top);
                    chunk.bottom = qMin(chunk.bottom, source.bottom);
                }
                chunks.append(chunk);
                offset += chunk.count;
            }
        }

        // 找一个落位（按中心 y）该去的那一张。
        const auto chunkOf = [&chunks](double y) {
            for (int chunk = 0; chunk < chunks.size(); ++chunk) {
                if (y >= chunks.at(chunk).bottom && y <= chunks.at(chunk).top) {
                    return chunk;
                }
            }
            int nearest = 0;
            double best = std::numeric_limits<double>::max();
            for (int chunk = 0; chunk < chunks.size(); ++chunk) {
                const double distance = y > chunks.at(chunk).top ? y - chunks.at(chunk).top
                                                               : chunks.at(chunk).bottom - y;
                if (distance < best) {
                    best = distance;
                    nearest = chunk;
                }
            }
            return nearest;
        };

        // 图片先登记进媒体集合（编号全局连续），再落到对应的幻灯片上。
        const QVector<pdf::PdfImage> pageImages =
            index < collected.images.size() ? collected.images.at(index) : QVector<pdf::PdfImage>();
        const QVector<office::ImagePlacement> placements =
            placementsOfImages(pageImages, bands, &media);
        QVector<QVector<office::ImagePlacement>> imagesPerChunk(chunks.size());
        for (const office::ImagePlacement& placement : placements) {
            const double centerY = placement.bottomPoints + placement.heightPoints * 0.5;
            imagesPerChunk[chunkOf(centerY)].append(placement);
        }

        QVector<QVector<office::TableGrid>> tablesPerChunk(chunks.size());
        for (const DetectedTable& table : tables) {
            const double centerY = table.grid.bottomPoints + table.grid.heightPoints * 0.5;
            tablesPerChunk[chunkOf(centerY)].append(table.grid);
        }

        for (int chunk = 0; chunk < chunks.size(); ++chunk) {
            pptx::Slide slide;
            slide.index = deck.slides.size() + 1;
            slide.title = chunk == 0 ? QStringLiteral("第 %1 页").arg(index + 1)
                                     : QStringLiteral("第 %1 页（续）").arg(index + 1);
            slide.pageSizePoints = collected.pageSizePt;
            const SlideChunk& source = chunks.at(chunk);
            for (int line = 0; line < source.count; ++line) {
                slide.bullets.append(texts.at(source.offset + line));
            }
            slide.images = imagesPerChunk.at(chunk);
            slide.tables = tablesPerChunk.at(chunk);
            deck.slides.append(slide);
        }
    }

    result.bytes = pptx::deckToPptx(deck, media);
    result.ok = !result.bytes.isEmpty();
    result.slideCount = deck.slides.size();
    result.imageCount = media.count();
    for (const pptx::Slide& slide : deck.slides) {
        result.tableCount += slide.tables.size();
    }
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

    // 媒体集合：整个包共用一份，编号全局连续（word/media/ 是包级命名空间）。
    office::MediaSet media(QStringLiteral("word/media"));

    QVector<OfficePageContent> contents;
    contents.reserve(collected.pages.size());
    for (int index = 0; index < collected.pages.size(); ++index) {
        OfficePageContent content;
        content.bands = buildTextBands(collected.pages.at(index));
        const QVector<DetectedTable> tables = tablesOfBands(content.bands);
        for (const DetectedTable& table : tables) {
            office::TableGrid grid = table.grid;
            grid.bandIndex = table.band;
            content.tables.append(grid);
        }
        const QVector<pdf::PdfImage> pageImages =
            index < collected.images.size() ? collected.images.at(index) : QVector<pdf::PdfImage>();
        content.images = placementsOfImages(pageImages, content.bands, &media);
        contents.append(content);
        result.tableCount += content.tables.size();
    }
    result.imageCount = media.count();

    const ExportResult exported = exportPagesTo(ExportFormat::WordOpenXml, contents,
                                                collected.pageSizePt, media);
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
