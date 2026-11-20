#include "pdf/ops/DocumentReflow.h"

#include "pdf/compliance/StructTreeReader.h"
#include "pdf/compliance/TagTreeWriter.h"

#include "i18n/Translate.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <vector>

#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdfview.h>

namespace aipdf::pdf {
namespace {

// ---------------------------------------------------------------------------
// 标签树的判定
// ---------------------------------------------------------------------------

// 块级标签：这些元素整体算一段。
//
// 为什么按标签而不是按「有没有文字」一刀切：P 里套 Span 是最常见的形状，
// 只看 P 自己的文字会把正文读成空的；按标签挑块、再把块里的文字收齐，
// 才既不漏也不重。
bool isBlockTag(const QString& tag) {
    static const QSet<QString> tags = {
        QStringLiteral("P"),       QStringLiteral("H1"),    QStringLiteral("H2"),
        QStringLiteral("H3"),      QStringLiteral("H4"),    QStringLiteral("H5"),
        QStringLiteral("H6"),      QStringLiteral("LI"),    QStringLiteral("Lbl"),
        QStringLiteral("LBody"),   QStringLiteral("TD"),    QStringLiteral("TH"),
        QStringLiteral("BlockQuote"), QStringLiteral("Caption"), QStringLiteral("Title"),
        QStringLiteral("Subtitle"), QStringLiteral("Pre"),  QStringLiteral("Quote"),
        QStringLiteral("Note")
    };
    return tags.contains(tag);
}

// 页眉页脚：内容块，但角色是页眉／页脚。
bool isRunningHeadTag(const QString& tag) {
    return tag == QLatin1String("Head") || tag == QLatin1String("Foot");
}

// 内容块：块级标签 + 页眉页脚。
//
// 「内容块」是**计数的单位**：同一个内容块里的文字算一块。
// 页眉页脚也算内容块（它们有文字、也要计数），只是角色不同。
bool isContentBlockTag(const QString& tag) {
    return isBlockTag(tag) || isRunningHeadTag(tag);
}

// 标题类标签：排版时用粗体，和正文区分开。
bool isHeadingTag(const QString& tag) {
    return tag.size() == 2 && tag.at(0) == QLatin1Char('H') && tag.at(1) >= QLatin1Char('1') &&
           tag.at(1) <= QLatin1Char('6');
}

// 粗体字体族名。
QString boldFamily(const QString& family) {
    const QString base = family.trimmed().isEmpty() ? QStringLiteral("Helvetica") : family.trimmed();
    if (base.contains(QStringLiteral("Bold"))) return base;
    return base + QStringLiteral("-Bold");
}

// 文字里有没有标准 14 号字体（Latin-1）表示不了的字符。
bool hasUnrepresentableChars(const QString& text) {
    for (const QChar ch : text) {
        if (ch.unicode() > 0xFF) return true;
    }
    return false;
}

// 把标准 14 号字体表示不了的字符换成 '?'（与内容流那条路的口径一致）。
QString toLatin1Placeholder(const QString& text) {
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text) {
        result.append(ch.unicode() > 0xFF ? QLatin1Char('?') : ch);
    }
    return result;
}

// 一行的大致高度。
double estimatedLineHeight(const ParagraphFormat& format) {
    const double fontHeight = format.fontSize * 1.25;
    if (format.lineSpacingMode == LineSpacingMode::Fixed) {
        return qMax(fontHeight, format.lineSpacing);
    }
    return qMax(fontHeight, format.fontSize * format.lineSpacing);
}

// ---------------------------------------------------------------------------
// 文字块的归属索引
// ---------------------------------------------------------------------------

// 从标签树整理「文字块归属索引」。
//
// 口径：一个 MCID 属于**最近的内容块祖先**（P、H1、TD…，以及 Head/Foot）。
// 为什么要找祖先而不是直接用 MCID 所在的那个元素：标签树里段落下面常挂 Span，
// 一个段落会被拆成好几个 Span。按最细的元素算，一段正文会被数成好几块，
// 而重排结果里它仍然是一段（一个块），两边对不上，门禁就会误报。
PageTextBlockIndex buildBlockIndex(const StructTreeReport& tree) {
    PageTextBlockIndex index;
    const int count = tree.elements.size();
    if (count == 0) return index;

    // 子元素表（标签树按先序排列，parentIndex 指回父元素）。
    QVector<QVector<int>> children(count);
    for (int i = 0; i < count; ++i) {
        const int parent = tree.elements.at(i).parentIndex;
        if (parent >= 0 && parent < count) children[parent].append(i);
    }

    // 每个元素的内容块祖先（自己就是内容块时，祖先就是自己）。
    QVector<int> owner(count, -1);
    for (int i = 0; i < count; ++i) {
        if (isContentBlockTag(tree.elements.at(i).tag)) {
            owner[i] = i;
            continue;
        }
        int parent = tree.elements.at(i).parentIndex;
        while (parent >= 0 && parent < count) {
            if (isContentBlockTag(tree.elements.at(parent).tag)) {
                owner[i] = parent;
                break;
            }
            parent = tree.elements.at(parent).parentIndex;
        }
    }

    // 收集一个内容块里的 MCID 与文字（遇到嵌套的内容块就停手）。
    std::function<void(int, QVector<int>*, QString*)> gather = [&](int elementIndex,
                                                                    QVector<int>* mcids,
                                                                    QString* text) {
        const StructElement& element = tree.elements.at(elementIndex);
        mcids->append(element.mcids);
        if (!element.text.isEmpty()) *text += element.text;
        for (const int child : children.at(elementIndex)) {
            if (isContentBlockTag(tree.elements.at(child).tag)) continue;
            gather(child, mcids, text);
        }
    };

    QSet<int> done;
    for (int i = 0; i < count; ++i) {
        if (!isContentBlockTag(tree.elements.at(i).tag)) continue;
        if (done.contains(i)) continue;
        done.insert(i);

        QVector<int> mcids;
        QString text;
        gather(i, &mcids, &text);

        const QString tag = tree.elements.at(i).tag;
        PageTextRole role = PageTextRole::Body;
        if (tag == QLatin1String("Head")) {
            role = PageTextRole::Header;
        } else if (tag == QLatin1String("Foot")) {
            role = PageTextRole::Footer;
        }

        const int blockKey = i;
        while (index.blockRoles.size() <= blockKey) index.blockRoles.append(PageTextRole::Body);
        while (index.blockTexts.size() <= blockKey) index.blockTexts.append(QString());
        while (index.blockTags.size() <= blockKey) index.blockTags.append(QString());
        index.blockRoles[blockKey] = role;
        index.blockTexts[blockKey] = text;
        index.blockTags[blockKey] = tag;

        for (const int mcid : mcids) {
            index.mcidToBlock.insert(
                PageTextBlockIndex::mcidKey(tree.elements.at(i).pageIndex, mcid), blockKey);
        }
    }
    return index;
}

// ---------------------------------------------------------------------------
// 排版
// ---------------------------------------------------------------------------

// 排好的一段（一个内容块在某一页上的部分）。
struct LayoutChunk {
    int pageIndex = -1;
    QRectF bounds;
    QVector<ParagraphLine> lines;
    // 表格专用：这一块覆盖原表的哪几行（左闭右开）。0 行表示整块。
    int firstRow = 0;
    int rowCount = 0;
};

// 排好版的一个条目。
struct LayoutItem {
    PageObjectKind kind = PageObjectKind::Unknown;
    QString objectId;
    int sourcePage = -1;
    QRectF sourceBounds;
    QString text;
    bool heading = false;
    bool hasUnrepresentableChars = false;
    QString fontFamily;
    // 段落 / 表格 / 图形各自的细节。
    QVector<LayoutChunk> chunks;
    QVector<PageTableCell> cells;
    QVector<double> columnWidths;
    int rowCount = 0;
    int columnCount = 0;
    QSizeF targetSize;
    // 图片按「原尺寸与 DPI 换算」得到的自然尺寸（点）。
    QSizeF naturalSize;
    int sourceObjectIndex = -1;
    // 结果。
    int targetPage = -1;
    QRectF targetBounds;
    // 标记里的块号（写回时写进页面对象）。
    int blockKey = -1;
};

// 表格重建后每行的高度。
double tableRowHeight(const ParagraphFormat& format) {
    return qMax(format.fontSize * 1.8, 14.0);
}

// 新页面的版面几何。
struct LayoutGeometry {
    double pageWidth = 0.0;
    double pageHeight = 0.0;
    double marginTop = 0.0;
    double marginBottom = 0.0;
    double contentLeft = 0.0;
    double contentRight = 0.0;
    double contentBottom = 0.0;
    double contentTop = 0.0;
    ParagraphFormat paragraph;
};

// ---------------------------------------------------------------------------
// PDFium 写回
// ---------------------------------------------------------------------------

// 内存输出缓冲：FPDF_FILEWRITE 的写入回调往这里追加。
//
// FPDF_FILEWRITE 没有用户数据字段（只有 version 与 WriteBlock），
// 所以缓冲指针只能走文件级静态变量（与 pdf::PdfiumAdapter 同样的做法）。
struct MemoryWriter {
    QByteArray data;
};

MemoryWriter* g_reflowMemoryTarget = nullptr;

int writeReflowMemory(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
    if (g_reflowMemoryTarget == nullptr || data == nullptr) {
        return 0;
    }
    g_reflowMemoryTarget->data.append(static_cast<const char*>(data), static_cast<qsizetype>(size));
    return 1;
}

// 写回时的上下文。
struct WriteContext {
    FPDF_DOCUMENT document = nullptr;
    LayoutGeometry geometry;
    // 标记里的块号（一个内容块一个号）。
    int blockCounter = 0;
    QString error;
};

// 往一页里放一段文字，并打上「这一段属于第几块」的标记。
bool insertTextRun(WriteContext& context, FPDF_PAGE page, const QString& text, double x,
                    double baseline, double fontSize, const QString& fontFamily, int blockKey,
                    PageTextRole role) {
    if (text.isEmpty()) return true;
    FPDF_PAGEOBJECT object = FPDFPageObj_NewTextObj(
        context.document, fontFamily.toLatin1().constData(), static_cast<float>(fontSize));
    if (object == nullptr) {
        context.error = i18n::tr("aipdf::pdf::DocumentReflow", "PDFium 建不出文字对象");
        return false;
    }
    const QString latin1 = toLatin1Placeholder(text);
    FPDFText_SetText(object,
                     reinterpret_cast<FPDF_WIDESTRING>(const_cast<ushort*>(latin1.utf16())));
    FPDFPageObj_Transform(object, 1.0, 0.0, 0.0, 1.0, x, baseline);
    if (!FPDFPage_InsertObject(page, object)) {
        FPDFPageObj_Destroy(object);
        context.error = i18n::tr("aipdf::pdf::DocumentReflow", "文字对象放不进页面");
        return false;
    }
    // 标记：写进文件里的块身份。重新打开还在——门禁靠它把重排结果的
    // 文字块和源文档的文字块对上（实测：标记随保存重开存活，且不带 /MCID，
    // 所以后面打标签那一步不会把它摘掉）。
    FPDF_PAGEOBJECTMARK mark = FPDFPageObj_AddMark(object, kReflowBlockMark);
    if (mark != nullptr) {
        FPDFPageObjMark_SetIntParam(context.document, object, mark, kReflowBlockParam, blockKey);
        FPDFPageObjMark_SetIntParam(context.document, object, mark, kReflowRoleParam,
                                    static_cast<int>(role));
    }
    return true;
}

// 往一页里画一个矩形边框（表格线）。
bool insertBorderRect(WriteContext& context, FPDF_PAGE page, const QRectF& rect, double lineWidth) {
    FPDF_PAGEOBJECT border = FPDFPageObj_CreateNewRect(
        static_cast<float>(rect.left()), static_cast<float>(rect.bottom()),
        static_cast<float>(rect.width()), static_cast<float>(rect.height()));
    if (border == nullptr) {
        context.error = i18n::tr("aipdf::pdf::DocumentReflow", "PDFium 建不出表格边框");
        return false;
    }
    FPDFPageObj_SetStrokeColor(border, 0, 0, 0, 255);
    FPDFPageObj_SetStrokeWidth(border, static_cast<float>(lineWidth));
    // 只描边、不填充：不填充才不会把单元格文字盖住。
    FPDFPath_SetDrawMode(border, FPDF_FILLMODE_NONE, 1);
    if (!FPDFPage_InsertObject(page, border)) {
        FPDFPageObj_Destroy(border);
        context.error = i18n::tr("aipdf::pdf::DocumentReflow", "表格边框放不进页面");
        return false;
    }
    return true;
}

// 页面对象的定位键：「页号|页内下标」。
QString objectKey(int pageIndex, int objectIndex) {
    return QStringLiteral("%1|%2").arg(pageIndex).arg(objectIndex);
}

// 把排好的版面写进文档（就地改页），产出新的 PDF 字节。
//
// 为什么用 PDFium 就地改页，而不是 qpdf 另建一份：
//   * 矢量图形（路径对象）在 PDFium 里是**对象**，能直接搬到另一页；
//     qpdf 那边只有内容流里的画线指令，搬不过去。
//   * 图片对象自带图像数据，PDFium 搬对象时图像跟着走，不用重新编码。
//   * 不用 IncrementalWriter：它只认经典 xref 表与直接对象，
//     遇对象流 / xref 流 / 加密文件直接失败，不能拿来做整篇重排。
QByteArray writeReflowedDocument(const QByteArray& sourceBytes, QVector<LayoutItem>& items,
                                  const QVector<LayoutItem>& headers,
                                  const QVector<LayoutItem>& footers, int pageCount,
                                  const LayoutGeometry& geometry,
                                  QVector<QVector<TaggedRegion>>* regions, QString* error) {
    detail::ensurePageObjectModelPdfiumReady();
    std::lock_guard<std::recursive_mutex> lock(detail::pageObjectModelPdfiumMutex());

    FPDF_DOCUMENT document = FPDF_LoadMemDocument(sourceBytes.constData(),
                                                  static_cast<int>(sourceBytes.size()), nullptr);
    if (document == nullptr) {
        *error = i18n::tr("aipdf::pdf::DocumentReflow", "PDFium 打不开这份 PDF，重排失败");
        return {};
    }

    WriteContext context;
    context.document = document;
    context.geometry = geometry;

    // ---- 第一步：把页面上的对象取下来 ----
    // 文字对象直接销毁（正文要重新排版）；图片与矢量先拿着，等下搬到新页上。
    //
    // **关键**：PDFium 的改页状态挂在页面句柄上——句柄一关（FPDF_ClosePage），
    // 这一页的改动就没了（内容流不会重建）。所以这里把所有页面句柄都留着，
    // 直到内容流生成完再关。
    QHash<QString, FPDF_PAGEOBJECT> movable;
    const int sourcePageCount = FPDF_GetPageCount(document);
    QVector<FPDF_PAGE> pageHandles;
    for (int pageIndex = 0; pageIndex < sourcePageCount; ++pageIndex) {
        FPDF_PAGE page = FPDF_LoadPage(document, pageIndex);
        if (page == nullptr) continue;
        pageHandles.append(page);
        const int objectCount = FPDFPage_CountObjects(page);
        QVector<FPDF_PAGEOBJECT> handles;
        for (int index = 0; index < objectCount; ++index) {
            FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, index);
            if (object != nullptr) handles.append(object);
        }
        for (int index = 0; index < handles.size(); ++index) {
            FPDF_PAGEOBJECT object = handles.at(index);
            if (!FPDFPage_RemoveObject(page, object)) continue;
            const int type = FPDFPageObj_GetType(object);
            if (type == FPDF_PAGEOBJ_TEXT) {
                FPDFPageObj_Destroy(object);
                continue;
            }
            movable.insert(objectKey(pageIndex, index), object);
        }
    }

    // ---- 第二步：把页数调成重排后的页数 ----
    // 多余的页删掉（它们上面的对象已经在上一步取下来了）；
    // 不够的页新建。
    for (int pageIndex = sourcePageCount - 1; pageIndex >= pageCount; --pageIndex) {
        FPDFPage_Delete(document, pageIndex);
        if (pageIndex < pageHandles.size() && pageHandles.at(pageIndex) != nullptr) {
            // 页面已被删掉，句柄不能再用，也不能关（关它等于把状态还原）。
            pageHandles[pageIndex] = nullptr;
        }
    }
    for (int pageIndex = sourcePageCount; pageIndex < pageCount; ++pageIndex) {
        FPDF_PAGE added = FPDFPage_New(document, pageIndex, geometry.pageWidth, geometry.pageHeight);
        if (added == nullptr) {
            *error = i18n::tr("aipdf::pdf::DocumentReflow", "新页面建不出来");
            FPDF_CloseDocument(document);
            return {};
        }
        pageHandles.append(added);
    }

    // ---- 第三步：把每页的尺寸改成新页宽 ----
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        if (pageIndex >= pageHandles.size() || pageHandles.at(pageIndex) == nullptr) continue;
        FPDF_PAGE page = pageHandles.at(pageIndex);
        FPDFPage_SetMediaBox(page, 0.0F, 0.0F, static_cast<float>(geometry.pageWidth),
                              static_cast<float>(geometry.pageHeight));
        FPDFPage_SetCropBox(page, 0.0F, 0.0F, static_cast<float>(geometry.pageWidth),
                            static_cast<float>(geometry.pageHeight));
    }

    // ---- 第四步：把内容摆到新页上 ----
    regions->resize(pageCount);
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        (*regions)[pageIndex].clear();
    }
    int regionOrder = 0;

    // 页眉页脚：每页一份。
    QVector<int> headerBlockKeys;
    for (const LayoutItem& header : headers) {
        headerBlockKeys.append(context.blockCounter++);
    }
    QVector<int> footerBlockKeys;
    for (const LayoutItem& footer : footers) {
        footerBlockKeys.append(context.blockCounter++);
    }

    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        if (pageIndex >= pageHandles.size() || pageHandles.at(pageIndex) == nullptr) continue;
        FPDF_PAGE page = pageHandles.at(pageIndex);

        // 页眉在上、页脚在下：放在页边距里，不占正文的位置。
        for (int index = 0; index < headers.size(); ++index) {
            const LayoutItem& header = headers.at(index);
            const double baseline =
                geometry.pageHeight - qMax(4.0, geometry.marginTop * 0.45);
            if (!insertTextRun(context, page, header.text, geometry.contentLeft, baseline,
                               geometry.paragraph.fontSize, geometry.paragraph.fontFamily,
                               headerBlockKeys.at(index), PageTextRole::Header)) {
                *error = context.error;
                FPDF_CloseDocument(document);
                return {};
            }
            TaggedRegion region;
            region.rect = QRectF(geometry.contentLeft, baseline - geometry.paragraph.fontSize,
                                  geometry.contentRight - geometry.contentLeft,
                                  geometry.paragraph.fontSize * 1.2);
            region.order = regionOrder++;
            (*regions)[pageIndex].append(region);
        }
        for (int index = 0; index < footers.size(); ++index) {
            const LayoutItem& footer = footers.at(index);
            const double baseline = qMax(4.0, geometry.marginBottom * 0.45);
            if (!insertTextRun(context, page, footer.text, geometry.contentLeft, baseline,
                               geometry.paragraph.fontSize, geometry.paragraph.fontFamily,
                               footerBlockKeys.at(index), PageTextRole::Footer)) {
                *error = context.error;
                FPDF_CloseDocument(document);
                return {};
            }
            TaggedRegion region;
            region.rect = QRectF(geometry.contentLeft, baseline - geometry.paragraph.fontSize,
                                  geometry.contentRight - geometry.contentLeft,
                                  geometry.paragraph.fontSize * 1.2);
            region.order = regionOrder++;
            (*regions)[pageIndex].append(region);
        }
    }

    for (LayoutItem& item : items) {
        if (item.chunks.isEmpty()) continue;
        item.blockKey = context.blockCounter++;
        for (const LayoutChunk& chunk : item.chunks) {
            if (chunk.pageIndex >= pageHandles.size() ||
                pageHandles.at(chunk.pageIndex) == nullptr) {
                continue;
            }
            FPDF_PAGE page = pageHandles.at(chunk.pageIndex);
            bool ok = true;
            if (item.kind == PageObjectKind::Text) {
                for (const ParagraphLine& line : chunk.lines) {
                    if (!insertTextRun(context, page, line.text, line.x, line.baseline.y(),
                                       line.fontSize, item.fontFamily, item.blockKey,
                                       PageTextRole::Body)) {
                        ok = false;
                        break;
                    }
                }
            } else if (item.kind == PageObjectKind::Table) {
                // 表格：先画格子，再把单元格文字写进去。
                // 块里记的是这张表的一行区间（跨页时会被拆成多块）。
                const int totalRows = qMax(1, item.rowCount);
                const int columns = qMax(1, item.columnCount);
                const double rowHeight = tableRowHeight(geometry.paragraph);
                const int firstRow = chunk.firstRow;
                const int rowCount = chunk.rowCount > 0 ? chunk.rowCount : totalRows;
                QVector<double> columnLeft(columns, 0.0);
                double offset = chunk.bounds.left();
                for (int column = 0; column < columns; ++column) {
                    columnLeft[column] = offset;
                    offset += item.columnWidths.value(column, 0.0);
                }
                for (int localRow = 0; localRow < rowCount; ++localRow) {
                    for (int column = 0; column < columns; ++column) {
                        const double left = columnLeft.value(column, chunk.bounds.left());
                        const double width = item.columnWidths.value(column, 0.0);
                        const double top = chunk.bounds.top() - localRow * rowHeight;
                        const QRectF cell(left, top - rowHeight, width, rowHeight);
                        if (!insertBorderRect(context, page, cell, 0.5)) {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) break;
                }
                if (ok) {
                    for (const PageTableCell& cell : item.cells) {
                        if (cell.row < firstRow || cell.row >= firstRow + rowCount) continue;
                        const double left = columnLeft.value(cell.column, chunk.bounds.left());
                        const int localRow = cell.row - firstRow;
                        const double baseline = chunk.bounds.top() -
                                                (localRow + 1) * rowHeight + rowHeight * 0.3;
                        const QString text = cell.text;
                        if (text.isEmpty()) continue;
                        if (!insertTextRun(context, page, text, left + 2.0, baseline,
                                           geometry.paragraph.fontSize,
                                           geometry.paragraph.fontFamily,
                                           context.blockCounter++, PageTextRole::Body)) {
                            ok = false;
                            break;
                        }
                    }
                }
            } else {
                // 图片与矢量：把原对象搬到新位置。
                FPDF_PAGEOBJECT handle =
                    movable.take(objectKey(item.sourcePage, item.sourceObjectIndex));
                if (handle == nullptr) {
                    *error = i18n::tr("aipdf::pdf::DocumentReflow",
                                       "第 %1 页上的图形对象找不到了，搬不过去")
                                 .arg(item.sourcePage + 1);
                    FPDF_CloseDocument(document);
                    return {};
                }
                const QRectF source = item.sourceBounds;
                const QRectF target = chunk.bounds;
                const double scaleX =
                    source.width() > 0.0 ? target.width() / source.width() : 1.0;
                const double scaleY =
                    source.height() > 0.0 ? target.height() / source.height() : 1.0;
                FPDFPageObj_Transform(handle, scaleX, 0.0, 0.0, scaleY,
                                      target.left() - source.left() * scaleX,
                                      target.bottom() - source.bottom() * scaleY);
                if (!FPDFPage_InsertObject(page, handle)) {
                    FPDFPageObj_Destroy(handle);
                    *error = i18n::tr("aipdf::pdf::DocumentReflow", "图形对象放不进新页面");
                    FPDF_CloseDocument(document);
                    return {};
                }
            }
            if (!ok) {
                *error = context.error;
                FPDF_CloseDocument(document);
                return {};
            }
            if (item.kind != PageObjectKind::Image && item.kind != PageObjectKind::Vector) {
                TaggedRegion region;
                region.rect = chunk.bounds;
                region.order = regionOrder++;
                (*regions)[chunk.pageIndex].append(region);
            }
        }
        // 目标位置：第一个块所在的页与所有块的外框。
        item.targetPage = item.chunks.first().pageIndex;
        QRectF target;
        for (const LayoutChunk& chunk : item.chunks) {
            target = target.isNull() ? chunk.bounds : target.united(chunk.bounds);
        }
        item.targetBounds = target;
    }

    // 没搬上的图形对象：销毁（门禁会因此发现「少了对象」并拦住写回）。
    for (auto it = movable.begin(); it != movable.end(); ++it) {
        FPDFPageObj_Destroy(it.value());
    }

    // ---- 第五步：生成内容流并序列化 ----
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        if (pageIndex >= pageHandles.size() || pageHandles.at(pageIndex) == nullptr) continue;
        FPDFPage_GenerateContent(pageHandles.at(pageIndex));
    }
    for (int pageIndex = 0; pageIndex < pageHandles.size(); ++pageIndex) {
        if (pageHandles.at(pageIndex) == nullptr) continue;
        FPDF_ClosePage(pageHandles.at(pageIndex));
    }

    MemoryWriter writer;
    g_reflowMemoryTarget = &writer;
    FPDF_FILEWRITE fileWrite = {};
    fileWrite.version = 1;
    fileWrite.WriteBlock = &writeReflowMemory;
    const FPDF_BOOL saved = FPDF_SaveAsCopy(document, &fileWrite, 0);
    g_reflowMemoryTarget = nullptr;
    FPDF_CloseDocument(document);

    if (!saved || writer.data.isEmpty()) {
        *error = i18n::tr("aipdf::pdf::DocumentReflow", "PDFium 没有写出重排后的 PDF");
        return {};
    }
    return writer.data;
}

// 门禁用的说明：少了哪一类、少了几个。
QString describeMissing(PageObjectKind kind, int count) {
    switch (kind) {
    case PageObjectKind::Text:
        return i18n::tr("aipdf::pdf::DocumentReflow", "%1 个文字块").arg(count);
    case PageObjectKind::Image:
        return i18n::tr("aipdf::pdf::DocumentReflow", "%1 个图片对象").arg(count);
    case PageObjectKind::Vector:
        return i18n::tr("aipdf::pdf::DocumentReflow", "%1 个矢量图形").arg(count);
    case PageObjectKind::Table:
        return i18n::tr("aipdf::pdf::DocumentReflow", "%1 个表格").arg(count);
    default:
        return i18n::tr("aipdf::pdf::DocumentReflow", "%1 个对象").arg(count);
    }
}

// 正文的**非空白字符总数**：块数门禁看不出「一段文字被截短了」，
// 字符总数能看出来。重排会重新断行、合并块，空白数量会变，所以只数非空白字符；
// 标准 14 号字体表示不了的字符在写回时被换成 '?'，长度不变，不影响计数。
int bodyCharacterCount(const DocumentObjectModel& model) {
    int total = 0;
    for (const PageObject& object : model.objects) {
        if (object.kind != PageObjectKind::Text) continue;
        for (const QChar c : toLatin1Placeholder(object.text)) {
            if (!c.isSpace()) ++total;
        }
    }
    return total;
}

QString missingBodyTextReason(const DocumentObjectModel& source,
                              const DocumentObjectModel& output) {
    const int sourceChars = bodyCharacterCount(source);
    const int outputChars = bodyCharacterCount(output);
    if (outputChars >= sourceChars) return QString();
    return i18n::tr("aipdf::pdf::DocumentReflow",
                    "重排后正文少了 %1 个字符（%2 → %3），已阻止写回")
        .arg(sourceChars - outputChars)
        .arg(sourceChars)
        .arg(outputChars);
}

// 页眉／页脚按「每页重复」写出去，个数本来就对不上，
// 所以门禁比的是**文字还在不在**。
//
// 比之前先做同样的「标准 14 号字体替换」：页眉里若有 Latin-1 之外的字符，
// 写出去时会被换成 '?'，不统一口径就会误报「页眉丢了」。
QStringList missingRunningHeadTexts(const DocumentObjectModel& source,
                                     const DocumentObjectModel& output, PageObjectKind kind) {
    QStringList missing;
    const QStringList candidates = distinctTextsOfKind(output, kind);
    for (const QString& text : distinctTextsOfKind(source, kind)) {
        const QString wanted = toLatin1Placeholder(text).trimmed();
        bool found = false;
        for (const QString& candidate : candidates) {
            if (toLatin1Placeholder(candidate).trimmed() == wanted) {
                found = true;
                break;
            }
        }
        if (found) continue;
        missing.append(kind == PageObjectKind::Header
                           ? i18n::tr("aipdf::pdf::DocumentReflow", "页眉「%1」").arg(wanted)
                           : i18n::tr("aipdf::pdf::DocumentReflow", "页脚「%1」").arg(wanted));
    }
    return missing;
}

// 条目里是不是已经有同样的文字（页眉去重用）。
bool containsSameText(const QVector<LayoutItem>& items, const QString& text) {
    const QString wanted = toLatin1Placeholder(text).trimmed();
    for (const LayoutItem& item : items) {
        if (toLatin1Placeholder(item.text).trimmed() == wanted) return true;
    }
    return false;
}

}  // namespace

DocumentReflowResult reflowTaggedDocument(const QByteArray& pdfBytes, const ReflowOptions& options) {
    DocumentReflowResult result;
    if (pdfBytes.isEmpty()) {
        result.report.error = QStringLiteral("没有可重排的 PDF 内容");
        return result;
    }
    if (options.pageWidth <= 0.0) {
        result.report.error = QStringLiteral("新页宽必须大于 0");
        return result;
    }
    if (options.paragraph.fontSize <= 0.0) {
        result.report.error = QStringLiteral("字号必须大于 0");
        return result;
    }

    // ---- 第一步：读既有的标签树 ----
    // 必须用仓库现成的 pdf::readStructTree：各家的标签树形状差别很大，
    // 自己再写一套解析器只会得到一个更差的版本。
    const StructTreeReport tree = readStructTree(pdfBytes);
    if (!tree.ok()) {
        result.report.error = QStringLiteral("读不出标签树：%1").arg(tree.error);
        return result;
    }
    if (!tree.hasStructTree) {
        // 明确拒绝，不用启发式硬凑。
        result.report.error =
            QStringLiteral("这份文档没有结构标签（/StructTreeRoot），没有可靠依据判断正文、"
                           "页眉页脚与段落边界，拒绝重排。"
                           "请先把文档标成带结构标签的文档，再重排。");
        return result;
    }

    // ---- 第二步：读页面对象模型 ----
    // 这一步给出「原文档里有什么、各多少」，后面写回时的门禁就是拿它对比的。
    const PageTextBlockIndex blockIndex = buildBlockIndex(tree);
    QString modelError;
    const DocumentObjectModel sourceModel = readPageObjectModel(pdfBytes, blockIndex, &modelError);
    if (!sourceModel.ok()) {
        result.report.error = modelError;
        return result;
    }
    result.report.sourcePageCount = sourceModel.pageCount;
    result.report.sourceCounts = sourceModel.counts;

    // ---- 第三步：页面几何 ----
    LayoutGeometry geometry;
    geometry.pageWidth = options.pageWidth;
    geometry.pageHeight =
        options.pageHeight > 0.0 ? options.pageHeight : options.pageWidth * 1.41421356;
    geometry.marginTop = options.marginTop;
    geometry.marginBottom = options.marginBottom;
    geometry.contentLeft = options.marginLeft;
    geometry.contentRight = options.pageWidth - options.marginRight;
    const double contentWidth = geometry.contentRight - geometry.contentLeft;
    if (contentWidth <= 0.0) {
        result.report.error = QStringLiteral("左右页边距之和已经不小于新页宽，没有可用的排版宽度");
        return result;
    }
    geometry.contentBottom = options.marginBottom;
    geometry.contentTop = geometry.pageHeight - options.marginTop;
    const double contentHeight = geometry.contentTop - geometry.contentBottom;
    if (contentHeight <= 0.0) {
        result.report.error = QStringLiteral("上下页边距之和已经不小于新页高，没有可用的排版高度");
        return result;
    }
    geometry.paragraph = options.paragraph;

    // ---- 第四步：把条目按阅读顺序排到新页上 ----
    // 条目来自页面对象模型：正文块、表格、图片与矢量。
    // 页眉页脚不在正文流里（它们每页重复）。
    QVector<LayoutItem> items;
    QVector<LayoutItem> headers;
    QVector<LayoutItem> footers;
    for (const PageObject& object : sourceModel.objects) {
        LayoutItem item;
        item.kind = object.kind;
        item.objectId = object.id;
        item.sourcePage = object.pageIndex;
        item.sourceBounds = object.bounds;
        item.text = object.text;
        item.sourceObjectIndex = object.sourceObjectIndex;
        item.cells = object.cells;
        item.rowCount = object.rowCount;
        item.columnCount = object.columnCount;
        item.naturalSize = object.naturalSize;
        item.fontFamily = options.paragraph.fontFamily;
        if (object.kind == PageObjectKind::Text) {
            // 标题用粗体：靠标签树里这个块的标签判断。
            const int key = object.sourceBlockKey;
            if (key >= 0 && key < blockIndex.blockTags.size()) {
                item.heading = isHeadingTag(blockIndex.blockTags.at(key));
            }
            if (item.heading) item.fontFamily = boldFamily(options.paragraph.fontFamily);
            item.hasUnrepresentableChars = hasUnrepresentableChars(item.text);
            items.append(item);
        } else if (object.kind == PageObjectKind::Table ||
                   object.kind == PageObjectKind::Image ||
                   object.kind == PageObjectKind::Vector) {
            items.append(item);
        } else if (object.kind == PageObjectKind::Header) {
            // 页眉去重：源文档每页都有一份同样的页眉，
            // 收集时只留一份（按文字去重），写回时每页放一份。
            // 不去重的话，两页的文档会在每一页上叠两份同样的页眉。
            if (!containsSameText(headers, object.text)) headers.append(item);
        } else if (object.kind == PageObjectKind::Footer) {
            if (!containsSameText(footers, object.text)) footers.append(item);
        }
    }
    // 阅读顺序：先按页，再按位置（从上往下）。
    //
    // 注意用 **bottom()** 排序而不是 top()：本仓库的 PDF user space 是
    // 左下原点、y 向上为正，QRectF 的 top() 是**小**的 y（页面更下面），
    // bottom() 才是大的 y（页面更上面）。用 top() 排会把上下关系排反，
    // 重叠的对象（比如表格边框与表格本身）就会摆错位置。
    std::sort(items.begin(), items.end(), [](const LayoutItem& left, const LayoutItem& right) {
        if (left.sourcePage != right.sourcePage) return left.sourcePage < right.sourcePage;
        if (!qFuzzyCompare(left.sourceBounds.bottom() + 1.0, right.sourceBounds.bottom() + 1.0)) {
            return left.sourceBounds.bottom() > right.sourceBounds.bottom();
        }
        return left.sourceBounds.left() < right.sourceBounds.left();
    });

    result.report.blockCount = items.size();
    if (items.isEmpty()) {
        result.report.error = QStringLiteral("标签树里没有可重排的正文（可能只有图片或表格外壳）");
        return result;
    }

    // 排版：逐个条目往上摆。
    double cursor = geometry.contentBottom;
    int pageIndex = 0;
    int reflowed = 0;
    int degraded = 0;
    QVector<ReflowDropped> dropped;

    auto startNewPage = [&]() {
        ++pageIndex;
        cursor = geometry.contentBottom;
    };

    for (LayoutItem& item : items) {
        if (item.kind == PageObjectKind::Text) {
            // 段落：重新断行。排不下的部分挪到下一页。
            ParagraphFormat format = options.paragraph;
            format.fontFamily = item.fontFamily;
            const double lineHeight = estimatedLineHeight(format);
            QString remaining = item.text;
            bool failed = false;
            while (!remaining.isEmpty()) {
                const double available = geometry.contentTop - cursor;
                if (available < lineHeight) {
                    startNewPage();
                    continue;
                }
                const QRectF box(geometry.contentLeft, cursor, contentWidth, available);
                const ParagraphPlan plan = layoutParagraph(remaining, box, format);
                if (!plan.ok() || plan.remainingText == remaining) {
                    ReflowDropped itemDropped;
                    itemDropped.tag = QStringLiteral("P");
                    itemDropped.reason = i18n::tr("aipdf::pdf::DocumentReflow",
                                                   "这一段在新页宽下排不进去：%1")
                                              .arg(plan.ok() ? QStringLiteral("一行都排不下")
                                                             : plan.error);
                    itemDropped.pageIndex = item.sourcePage;
                    dropped.append(itemDropped);
                    failed = true;
                    break;
                }
                LayoutChunk chunk;
                chunk.pageIndex = pageIndex;
                chunk.bounds = plan.bounds;
                chunk.lines = plan.lines;
                item.chunks.append(chunk);
                cursor += plan.bounds.height();
                remaining = plan.remainingText;
                if (remaining.isEmpty()) {
                    cursor += options.paragraphSpacing;
                } else {
                    startNewPage();
                }
            }
            if (failed) continue;
            ++reflowed;
            if (item.hasUnrepresentableChars) ++degraded;
        } else if (item.kind == PageObjectKind::Table) {
            // 表格：按行列重建，画边框，再把单元格文字写进去。
            const int rows = qMax(1, item.rowCount);
            const int columns = qMax(1, item.columnCount);
            const double rowHeight = tableRowHeight(options.paragraph);
            const double tableHeight = rows * rowHeight;
            if (geometry.contentTop - cursor < tableHeight) {
                startNewPage();
            }
            // 列宽：按原表格各列的宽度比例分摊新页宽。
            QVector<double> sourceWidths(columns, 0.0);
            for (const PageTableCell& cell : item.cells) {
                if (cell.column < 0 || cell.column >= columns) continue;
                sourceWidths[cell.column] += cell.bounds.width();
            }
            double sourceTotal = 0.0;
            for (const double width : sourceWidths) sourceTotal += width;
            item.columnWidths = QVector<double>(columns, contentWidth / columns);
            if (sourceTotal > 0.0) {
                for (int column = 0; column < columns; ++column) {
                    item.columnWidths[column] =
                        contentWidth * sourceWidths.at(column) / sourceTotal;
                }
            }
            if (geometry.contentTop - cursor >= tableHeight) {
                LayoutChunk chunk;
                chunk.pageIndex = pageIndex;
                chunk.bounds = QRectF(geometry.contentLeft, cursor, contentWidth, tableHeight);
                chunk.firstRow = 0;
                chunk.rowCount = rows;
                item.chunks.append(chunk);
                cursor += tableHeight + options.paragraphSpacing;
            } else {
                // 一整张放不下一页：按行拆到多页，行不会被裁掉。
                if (rowHeight <= 0.0) {
                    ReflowDropped itemDropped;
                    itemDropped.tag = QStringLiteral("Table");
                    itemDropped.reason = i18n::tr("aipdf::pdf::DocumentReflow",
                                                   "这张表算不出行高，没能搬过去");
                    itemDropped.pageIndex = item.sourcePage;
                    dropped.append(itemDropped);
                    continue;
                }
                int row = 0;
                bool tableFailed = false;
                while (row < rows) {
                    if (geometry.contentTop - cursor < rowHeight) startNewPage();
                    const int fit = static_cast<int>(
                        std::floor((geometry.contentTop - cursor) / rowHeight));
                    if (fit <= 0) {
                        ReflowDropped itemDropped;
                        itemDropped.tag = QStringLiteral("Table");
                        itemDropped.reason = i18n::tr(
                            "aipdf::pdf::DocumentReflow",
                            "这张表的一行都放不进新页，没能搬过去");
                        itemDropped.pageIndex = item.sourcePage;
                        dropped.append(itemDropped);
                        tableFailed = true;
                        break;
                    }
                    const int take = qMin(fit, rows - row);
                    LayoutChunk chunk;
                    chunk.pageIndex = pageIndex;
                    chunk.bounds = QRectF(geometry.contentLeft, cursor, contentWidth,
                                          take * rowHeight);
                    chunk.firstRow = row;
                    chunk.rowCount = take;
                    item.chunks.append(chunk);
                    cursor += take * rowHeight + options.paragraphSpacing;
                    row += take;
                    if (row < rows) startNewPage();
                }
                if (tableFailed) continue;
            }
        } else {
            // 图片与矢量：按原尺寸搬过去；放不下就等比缩小到内容宽度。
            QSizeF size = item.sourceBounds.size();
            if (item.kind == PageObjectKind::Image && !item.naturalSize.isEmpty()) {
                // 图片按「原尺寸与 DPI 换算」：像素数除以 DPI 得到物理尺寸。
                size = item.naturalSize;
            }
            if (size.width() > contentWidth && size.width() > 0.0) {
                size *= contentWidth / size.width();
            }
            if (geometry.contentTop - cursor < size.height()) {
                startNewPage();
            }
            if (size.height() > contentHeight && contentHeight > 0.0) {
                size *= contentHeight / size.height();
            }
            if (size.isEmpty()) {
                ReflowDropped itemDropped;
                itemDropped.tag = item.kind == PageObjectKind::Image ? QStringLiteral("Figure")
                                                                      : QStringLiteral("Path");
                itemDropped.reason =
                    i18n::tr("aipdf::pdf::DocumentReflow", "这个对象没有尺寸，搬不过去");
                itemDropped.pageIndex = item.sourcePage;
                dropped.append(itemDropped);
                continue;
            }
            LayoutChunk chunk;
            chunk.pageIndex = pageIndex;
            chunk.bounds = QRectF(geometry.contentLeft, cursor, size.width(), size.height());
            item.chunks.append(chunk);
            item.targetSize = size;
            cursor += size.height() + options.paragraphSpacing;
        }
    }

    const int totalPages = pageIndex + 1;
    result.report.outputPageCount = totalPages;
    result.report.reflowedBlockCount = reflowed;
    result.report.degradedBlockCount = degraded;
    // 丢弃清单在这里就落进报告：门禁拦下时下面会提前返回，
    // 清单要是等到最后才赋值，那份「到底什么没搬过去」的明细就丢了。
    result.report.dropped = dropped;
    result.report.droppedCount = dropped.size();
    if (degraded > 0) {
        result.report.notes.append(
            i18n::tr("aipdf::pdf::DocumentReflow",
                     "有 %1 段包含标准 14 号字体表示不了的字符，排版时按仓库既有做法写成 '?' 占位符")
                .arg(degraded));
    }

    // ---- 第五步：写回（PDFium 就地改页） ----
    QVector<QVector<TaggedRegion>> regions;
    QString writeError;
    QByteArray written = writeReflowedDocument(pdfBytes, items, headers, footers, totalPages,
                                               geometry, &regions, &writeError);
    if (written.isEmpty()) {
        result.report.error = writeError.isEmpty()
                                  ? i18n::tr("aipdf::pdf::DocumentReflow", "重排结果写不出来")
                                  : writeError;
        result.bytes.clear();
        return result;
    }

    // ---- 第六步：给新文档重新打标签 ----
    // 源文档是带标签的，输出也应该是：否则「重排」把可访问性弄丢了。
    // 打标签失败不致命（结构还在），但要如实写进报告。
#ifdef AIPDF_WITH_QPDF
    QString tagError;
    const QByteArray tagged = writeTaggedStructure(written, regions, &tagError);
    if (!tagError.isEmpty() || tagged.isEmpty()) {
        result.report.notes.append(i18n::tr("aipdf::pdf::DocumentReflow", "新文档没能重新打上标签：%1")
                                       .arg(tagError.isEmpty() ? QStringLiteral("未知原因") : tagError));
    } else {
        written = tagged;
    }
#else
    result.report.notes.append(
        i18n::tr("aipdf::pdf::DocumentReflow", "本次构建没有启用 qpdf，新文档没能重新打上标签"));
#endif

    result.bytes = written;

    // ---- 第七步：门禁 ----
    // 把重排结果重新打开、重新统计一遍，和原文档的统计逐类对比。
    // 少了一类对象就是「有内容没搬过去」——这时**不写回**。
    QString outputModelError;
    const DocumentObjectModel outputModel =
        readPageObjectModel(written, PageTextBlockIndex(), &outputModelError);
    if (!outputModel.ok()) {
        result.report.error = i18n::tr("aipdf::pdf::DocumentReflow",
                                        "重排结果打不开，统计不了页面对象：%1")
                                  .arg(outputModelError);
        result.bytes.clear();
        return result;
    }
    result.report.outputCounts = outputModel.counts;

    const QVector<QPair<PageObjectKind, int>> missing =
        result.report.sourceCounts.missingComparedWith(result.report.outputCounts);
    QStringList missingParts;
    for (const auto& entry : missing) {
        missingParts.append(describeMissing(entry.first, entry.second));
    }
    // 页眉页脚不比个数（每页重复会让个数变），比文字。
    missingParts.append(missingRunningHeadTexts(sourceModel, outputModel, PageObjectKind::Header));
    missingParts.append(missingRunningHeadTexts(sourceModel, outputModel, PageObjectKind::Footer));
    if (!missingParts.isEmpty()) {
        result.report.error = i18n::tr("aipdf::pdf::DocumentReflow", "重排后少了 %1，已阻止写回")
                                  .arg(missingParts.join(QStringLiteral("、")));
        result.bytes.clear();
        return result;
    }

    // 块数没少、但某一段被截短了：用非空白字符总数兜底。
    const QString bodyLoss = missingBodyTextReason(sourceModel, outputModel);
    if (!bodyLoss.isEmpty()) {
        result.report.error = bodyLoss;
        result.bytes.clear();
        return result;
    }

    // ---- 第八步：锚点计划 ----
    ReflowPlan& plan = result.report.plan;
    plan.sourcePageCount = result.report.sourcePageCount;
    plan.targetPageCount = totalPages;
    plan.sourceCounts = result.report.sourceCounts;
    plan.targetCounts = result.report.outputCounts;
    for (const LayoutItem& item : items) {
        ReflowAnchor anchor;
        anchor.objectId = item.objectId;
        anchor.kind = item.kind;
        anchor.sourcePage = item.sourcePage;
        anchor.targetPage = item.targetPage;
        anchor.sourceBounds = item.sourceBounds;
        anchor.targetBounds = item.targetBounds;
        anchor.summary = item.text.left(24);
        plan.anchors.append(anchor);
    }
    for (int index = 0; index < headers.size(); ++index) {
        const LayoutItem& header = headers.at(index);
        ReflowAnchor anchor;
        anchor.objectId = header.objectId;
        anchor.kind = PageObjectKind::Header;
        anchor.sourcePage = header.sourcePage;
        anchor.targetPage = 0;
        anchor.sourceBounds = header.sourceBounds;
        anchor.targetBounds = QRectF(geometry.contentLeft,
                                     geometry.pageHeight - qMax(4.0, geometry.marginTop * 0.45),
                                     geometry.contentRight - geometry.contentLeft,
                                     geometry.paragraph.fontSize * 1.2);
        anchor.summary = header.text.left(24);
        anchor.repeated = true;
        plan.anchors.append(anchor);
    }
    for (int index = 0; index < footers.size(); ++index) {
        const LayoutItem& footer = footers.at(index);
        ReflowAnchor anchor;
        anchor.objectId = footer.objectId;
        anchor.kind = PageObjectKind::Footer;
        anchor.sourcePage = footer.sourcePage;
        anchor.targetPage = 0;
        anchor.sourceBounds = footer.sourceBounds;
        anchor.targetBounds = QRectF(geometry.contentLeft,
                                     qMax(4.0, geometry.marginBottom * 0.45),
                                     geometry.contentRight - geometry.contentLeft,
                                     geometry.paragraph.fontSize * 1.2);
        anchor.summary = footer.text.left(24);
        anchor.repeated = true;
        plan.anchors.append(anchor);
    }

    // ---- 第九步：报告里如实说明做了什么、有什么限制 ----
    result.report.notes.append(
        i18n::tr("aipdf::pdf::DocumentReflow",
                 "重排后的版面与原文档不同：页数从 %1 页变成 %2 页")
            .arg(result.report.sourcePageCount)
            .arg(totalPages));
    result.report.notes.append(
        i18n::tr("aipdf::pdf::DocumentReflow",
                 "图片 %1 个、矢量图形 %2 个、表格 %3 个已按新版面搬过去")
            .arg(result.report.sourceCounts.images)
            .arg(result.report.sourceCounts.vectors)
            .arg(result.report.sourceCounts.tables));
    if (!headers.isEmpty() || !footers.isEmpty()) {
        result.report.notes.append(
            i18n::tr("aipdf::pdf::DocumentReflow", "页眉 %1 处、页脚 %2 处已按每页重复放到每一页")
                .arg(headers.size())
                .arg(footers.size()));
    }
    result.report.notes.append(
        i18n::tr("aipdf::pdf::DocumentReflow",
                 "表格边框是新画的线，矢量图形的计数因此比原文档多"));
    result.report.notes.append(
        i18n::tr("aipdf::pdf::DocumentReflow",
                 "重排不保留书签与内部链接的目标：页码变了，那些指向已经旧了"));

    // 丢弃清单在排版结束时就已经落进报告（见上文），
    // 这里不用再赋一次：门禁拦下时是提前返回的。
    return result;
}

}  // namespace aipdf::pdf
