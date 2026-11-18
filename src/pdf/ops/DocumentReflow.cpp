#include "pdf/ops/DocumentReflow.h"

#include "pdf/compliance/StructTreeReader.h"
#include "pdf/compliance/TagTreeWriter.h"

#include <QSet>

#include <functional>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <exception>
#include <memory>
#include <vector>
#endif

namespace aipdf::pdf {
namespace {

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

// 标题类标签：排版时用粗体，和正文区分开。
bool isHeadingTag(const QString& tag) {
    return tag.size() == 2 && tag.at(0) == QLatin1Char('H') && tag.at(1) >= QLatin1Char('1') &&
           tag.at(1) <= QLatin1Char('6');
}

// 纯结构容器：它们本身不是内容，只是把内容组织起来。
//
// 这类元素不进「丢弃清单」——把 Document 报成「丢了内容」是荒唐的。
bool isPureStructureTag(const QString& tag) {
    static const QSet<QString> tags = {
        QStringLiteral("Document"),   QStringLiteral("DocumentFragment"),
        QStringLiteral("Part"),       QStringLiteral("Sect"),
        QStringLiteral("Div"),        QStringLiteral("Art"),
        QStringLiteral("UL"),         QStringLiteral("OL"),
        QStringLiteral("DL"),         QStringLiteral("TOC"),
        QStringLiteral("TOCI"),       QStringLiteral("Index"),
        QStringLiteral("RB"),         QStringLiteral("RT"),
        QStringLiteral("RP"),         QStringLiteral("WT"),
        QStringLiteral("WP"),         QStringLiteral("Form"),
        QStringLiteral("Link"),       QStringLiteral("Reference"),
        QStringLiteral("Annot"),      QStringLiteral("Artifact"),
        QStringLiteral("TR"),         QStringLiteral("THead"),
        QStringLiteral("TBody"),      QStringLiteral("TFoot")
    };
    return tags.contains(tag);
}

// 图形/表格容器：里面装的是搬不走的内容，必须如实报出来。
bool isGraphicContainerTag(const QString& tag) {
    return tag == QLatin1String("Figure") || tag == QLatin1String("Table");
}

// 页眉页脚：按约定不搬。
bool isRunningHeadTag(const QString& tag) {
    return tag == QLatin1String("Head") || tag == QLatin1String("Foot");
}

// 一个正文块（段落）。
struct Block {
    QString tag;
    QString text;
    int pageIndex = -1;
    bool heading = false;
    // 文字里有标准 14 号字体表示不了的字符（会被写成 '?'）。
    bool hasUnrepresentableChars = false;
};

// 粗体字体族名。
//
// 为什么直接拼 "-Bold"：标准 14 号字体里粗体就叫 "Helvetica-Bold"，
// 这是 PDF 里现成的字体，拼出来的名字在别的阅读器上也认得。
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

// 一行的大致高度。
//
// 为什么要估：换页之前得知道「这一页还放不放得下一行」。
// 真实行高由 pdf::layoutParagraph 用字体度量算，这里估得略保守一点
// （宁可早换一页，也不要让排版函数在最后一厘米上失败）。
double estimatedLineHeight(const ParagraphFormat& format) {
    const double fontHeight = format.fontSize * 1.25;
    if (format.lineSpacingMode == LineSpacingMode::Fixed) {
        return qMax(fontHeight, format.lineSpacing);
    }
    return qMax(fontHeight, format.fontSize * format.lineSpacing);
}

// 从标签树里挑出正文块。
//
// 规则（报告里也会体现）：
//   * 块级标签（P、H1-H6、LI、TD…）整体算一段；文字从它自己与子孙里收，
//     遇到嵌套的块级元素就停手（那一段自己算）。
//   * Figure、Table 这类装着搬不走的内容的元素，记进「丢弃清单」。
//   * Head、Foot（页眉页脚）不搬，记进「丢弃清单」。
//   * 纯结构容器（Document、Div、Sect…）不算内容，也不记丢弃。
//   * 其它标签：有文字就算一段；没文字就不算内容，也不记丢弃。
QVector<Block> collectBlocks(const StructTreeReport& tree, QVector<ReflowDropped>* dropped) {
    const QVector<StructElement>& elements = tree.elements;
    const int count = elements.size();
    if (count == 0) return {};

    // 子元素表（标签树是按先序排列的，parentIndex 指回父元素）。
    QVector<QVector<int>> children(count);
    for (int i = 0; i < count; ++i) {
        const int parent = elements.at(i).parentIndex;
        if (parent >= 0 && parent < count) children[parent].append(i);
    }

    // 某个元素上面有没有块级祖先。
    //
    // 用 fill 而不是「QVector<char>(count, 0)」：后者在 GCC 13 上会触发一条
    // -Wstringop-overflow 误报（Qt 的批量填充实现在展开后边界算不出来）。
    QVector<char> hasBlockAncestor;
    hasBlockAncestor.fill(0, count);
    for (int i = 0; i < count; ++i) {
        int parent = elements.at(i).parentIndex;
        while (parent >= 0 && parent < count) {
            if (isBlockTag(elements.at(parent).tag)) {
                hasBlockAncestor[i] = 1;
                break;
            }
            parent = elements.at(parent).parentIndex;
        }
    }

    QVector<char> consumed;
    consumed.fill(0, count);

    // 收集一个块里的文字（自己的 + 后代的，遇嵌套块级元素停手）。
    std::function<void(int, QString*)> gather = [&](int index, QString* out) {
        *out += elements.at(index).text;
        for (const int child : children.at(index)) {
            if (isBlockTag(elements.at(child).tag)) continue;
            gather(child, out);
        }
    };

    // 标记整棵子树已经处理过。
    std::function<void(int)> markConsumed = [&](int index) {
        consumed[index] = 1;
        for (const int child : children.at(index)) {
            markConsumed(child);
        }
    };

    QVector<Block> blocks;
    for (int i = 0; i < count; ++i) {
        if (consumed[i]) continue;
        const StructElement& element = elements.at(i);
        const QString tag = element.tag;

        if (isBlockTag(tag)) {
            QString text;
            gather(i, &text);
            markConsumed(i);
            text = text.trimmed();
            if (text.isEmpty()) {
                ReflowDropped item;
                item.tag = tag;
                item.reason = QStringLiteral("这个元素里没有可重排的文字");
                item.pageIndex = element.pageIndex;
                item.detail = element.alt;
                dropped->append(item);
                continue;
            }
            Block block;
            block.tag = tag;
            block.text = text;
            block.pageIndex = element.pageIndex;
            block.heading = isHeadingTag(tag);
            block.hasUnrepresentableChars = hasUnrepresentableChars(text);
            blocks.append(block);
            continue;
        }

        if (isGraphicContainerTag(tag)) {
            ReflowDropped item;
            item.tag = tag;
            item.reason = (tag == QLatin1String("Figure"))
                              ? QStringLiteral("图片元素没有重排，图形搬不到新页面上")
                              : QStringLiteral("表格的表格结构没有保留，单元格文字按独立段落重排");
            item.pageIndex = element.pageIndex;
            item.detail = element.alt;
            dropped->append(item);
            continue;
        }

        if (isRunningHeadTag(tag)) {
            ReflowDropped item;
            item.tag = tag;
            item.reason = QStringLiteral("页眉页脚没有搬过来");
            item.pageIndex = element.pageIndex;
            item.detail = element.alt;
            dropped->append(item);
            continue;
        }

        if (isPureStructureTag(tag)) continue;

        // 其它标签：有文字才算一段。
        // 已经在某个块里被收走的（Span 之类）不再单独算，否则会重复。
        const QString text = element.text.trimmed();
        if (text.isEmpty() || hasBlockAncestor[i]) continue;
        markConsumed(i);
        Block block;
        block.tag = tag;
        block.text = text;
        block.pageIndex = element.pageIndex;
        block.heading = isHeadingTag(tag);
        block.hasUnrepresentableChars = hasUnrepresentableChars(text);
        blocks.append(block);
    }
    return blocks;
}

// 新文档的一页：内容流 + 这一页上各段落的矩形（打标签时用）。
struct OutputPage {
    QByteArray content;
    QVector<QRectF> regions;
};

#ifdef AIPDF_WITH_QPDF

// 源文档的页数。
int countPages(const QByteArray& pdfBytes) {
    QPDF source;
    source.setSuppressWarnings(true);
    source.processMemoryFile("reflow-source.pdf", pdfBytes.constData(),
                             static_cast<size_t>(pdfBytes.size()));
    return static_cast<int>(source.getAllPages().size());
}

// 读源文档的文档属性（/Info），抄到新文档上。
//
// 为什么抄：重排出来的还是同一份文档，标题与作者不该丢。
QPDFObjectHandle readInfo(const QByteArray& pdfBytes) {
    QPDF source;
    source.setSuppressWarnings(true);
    source.processMemoryFile("reflow-source.pdf", pdfBytes.constData(),
                             static_cast<size_t>(pdfBytes.size()));
    return source.getTrailer().getKey("/Info");
}

// 文本串的值（UTF-8）。
QString textOf(const QPDFObjectHandle& handle) {
    if (!handle.isString()) return QString();
    return QString::fromUtf8(handle.getUTF8Value().c_str());
}

#endif  // AIPDF_WITH_QPDF

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

    // 源文档页数：能用 qpdf 就数真的，否则退回标签树里出现过的最大页码。
    int sourcePages = 0;
#ifdef AIPDF_WITH_QPDF
    try {
        sourcePages = countPages(pdfBytes);
    } catch (const std::exception&) {
        sourcePages = 0;
    }
#endif
    if (sourcePages <= 0) {
        for (const StructElement& element : tree.elements) {
            sourcePages = qMax(sourcePages, element.pageIndex + 1);
        }
    }
    result.report.sourcePageCount = sourcePages;

    // ---- 第二步：挑出正文块 ----
    QVector<ReflowDropped> dropped;
    const QVector<Block> blocks = collectBlocks(tree, &dropped);
    result.report.blockCount = blocks.size();
    if (blocks.isEmpty()) {
        result.report.dropped = dropped;
        result.report.droppedCount = dropped.size();
        result.report.error = QStringLiteral("标签树里没有可重排的正文（可能只有图片或表格外壳）");
        return result;
    }

    // ---- 页面几何 ----
    const double pageWidth = options.pageWidth;
    const double pageHeight =
        options.pageHeight > 0.0 ? options.pageHeight : options.pageWidth * 1.41421356;
    const double contentLeft = options.marginLeft;
    const double contentRight = pageWidth - options.marginRight;
    const double contentWidth = contentRight - contentLeft;
    if (contentWidth <= 0.0) {
        result.report.error = QStringLiteral("左右页边距之和已经不小于新页宽，没有可用的排版宽度");
        return result;
    }
    const double contentBottom = options.marginBottom;
    const double contentTop = pageHeight - options.marginTop;
    const double contentHeight = contentTop - contentBottom;
    if (contentHeight <= 0.0) {
        result.report.error = QStringLiteral("上下页边距之和已经不小于新页高，没有可用的排版高度");
        return result;
    }

    // ---- 第三步：逐段排到新页宽上 ----
    QVector<OutputPage> pages;
    OutputPage current;
    double cursor = contentBottom;  // 还能放文字的最低 y（PDF 的 y 向上为正）
    int reflowed = 0;

    for (const Block& block : blocks) {
        ParagraphFormat format = options.paragraph;
        if (block.heading) format.fontFamily = boldFamily(options.paragraph.fontFamily);
        const double lineHeight = estimatedLineHeight(format);
        QString remaining = block.text;

        while (!remaining.isEmpty()) {
            const double available = contentTop - cursor;
            if (available < lineHeight) {
                // 这一页放不下了 → 换页。
                pages.append(current);
                current = OutputPage();
                cursor = contentBottom;
                continue;
            }
            const QRectF box(contentLeft, cursor, contentWidth, available);
            const ParagraphPlan plan = layoutParagraph(remaining, box, format);
            if (!plan.ok()) {
                // 排不进去：如果这一页还很空，是参数问题（宽度不够放一个字）；
                // 否则是这一页只剩一点空间，换一页就好。
                if (available >= lineHeight * 2.0) {
                    ReflowDropped item;
                    item.tag = block.tag;
                    item.reason = QStringLiteral("这一段在新页宽下排不进去：%1").arg(plan.error);
                    item.pageIndex = block.pageIndex;
                    dropped.append(item);
                    break;
                }
                pages.append(current);
                current = OutputPage();
                cursor = contentBottom;
                continue;
            }
            if (plan.remainingText == remaining) {
                // 一行都没排进去：再循环下去会死循环，如实记成丢弃。
                ReflowDropped item;
                item.tag = block.tag;
                item.reason = QStringLiteral("这一段在新页宽下排不进任何一行");
                item.pageIndex = block.pageIndex;
                dropped.append(item);
                break;
            }
            current.content += buildParagraphContentStream(plan, QStringLiteral("F1"));
            current.regions.append(plan.bounds);
            cursor += plan.bounds.height();
            remaining = plan.remainingText;
            if (remaining.isEmpty()) {
                cursor += options.paragraphSpacing;
            } else {
                pages.append(current);
                current = OutputPage();
                cursor = contentBottom;
            }
        }
        ++reflowed;
    }
    if (!current.content.isEmpty() || pages.isEmpty()) {
        pages.append(current);
    }

    result.report.reflowedBlockCount = reflowed;
    result.report.outputPageCount = pages.size();
    result.report.dropped = dropped;
    result.report.droppedCount = dropped.size();
    for (const Block& block : blocks) {
        if (block.hasUnrepresentableChars) ++result.report.degradedBlockCount;
    }
    if (result.report.degradedBlockCount > 0) {
        result.report.notes.append(
            QStringLiteral("有 %1 段包含标准 14 号字体表示不了的字符，排版时按仓库既有做法写成 '?' 占位符")
                .arg(result.report.degradedBlockCount));
    }

#ifndef AIPDF_WITH_QPDF
    result.report.error = QStringLiteral("本次构建没有启用 qpdf，写不了重排后的 PDF");
    return result;
#else
    // ---- 第四步：写成新 PDF ----
    try {
        QPDF pdf;
        pdf.emptyPDF();
        QPDFObjectHandle root = pdf.getRoot();
        QPDFObjectHandle pagesDict = root.getKey("/Pages");
        if (!pagesDict.isDictionary()) {
            result.report.error = QStringLiteral("qpdf 建出来的空 PDF 没有页树");
            return result;
        }
        if (!pagesDict.isIndirect()) {
            // 页树必须是间接对象：每个页的 /Parent 都要指回它。
            pagesDict = pdf.makeIndirectObject(pagesDict);
            root.replaceKey("/Pages", pagesDict);
        }

        QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
        font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
        font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
        font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
        const QPDFObjectHandle fontRef = pdf.makeIndirectObject(font);

        std::vector<QPDFObjectHandle> pageRefs;
        for (const OutputPage& outputPage : pages) {
            QPDFObjectHandle contentStream = QPDFObjectHandle::newStream(
                &pdf, std::string(outputPage.content.constData(),
                                  static_cast<size_t>(outputPage.content.size())));
            const QPDFObjectHandle contentRef = pdf.makeIndirectObject(contentStream);

            QPDFObjectHandle fonts = QPDFObjectHandle::newDictionary();
            fonts.replaceKey("/F1", fontRef);
            QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
            resources.replaceKey("/Font", fonts);

            QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
            page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
            page.replaceKey("/Parent", pagesDict);
            page.replaceKey("/MediaBox",
                            QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{
                                QPDFObjectHandle::newInteger(0),
                                QPDFObjectHandle::newInteger(0),
                                QPDFObjectHandle::newInteger(static_cast<long long>(pageWidth)),
                                QPDFObjectHandle::newInteger(static_cast<long long>(pageHeight)),
                            }));
            page.replaceKey("/Resources", resources);
            page.replaceKey("/Contents", contentRef);
            pageRefs.push_back(pdf.makeIndirectObject(page));
        }
        pagesDict.replaceKey("/Kids", QPDFObjectHandle::newArray(pageRefs));
        pagesDict.replaceKey("/Count",
                             QPDFObjectHandle::newInteger(static_cast<long long>(pageRefs.size())));

        // 文档属性：把源文档的标题与作者抄过来。
        const QPDFObjectHandle sourceInfo = readInfo(pdfBytes);
        if (sourceInfo.isDictionary()) {
            QPDFObjectHandle info = QPDFObjectHandle::newDictionary();
            const QString title = textOf(sourceInfo.getKey("/Title"));
            const QString author = textOf(sourceInfo.getKey("/Author"));
            const QString subject = textOf(sourceInfo.getKey("/Subject"));
            if (!title.isEmpty()) {
                info.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(title.toUtf8().constData()));
            }
            if (!author.isEmpty()) {
                info.replaceKey("/Author", QPDFObjectHandle::newUnicodeString(author.toUtf8().constData()));
            }
            if (!subject.isEmpty()) {
                info.replaceKey("/Subject", QPDFObjectHandle::newUnicodeString(subject.toUtf8().constData()));
            }
            if (!info.getKeys().empty()) {
                pdf.getTrailer().replaceKey("/Info", pdf.makeIndirectObject(info));
            }
        }

        QPDFWriter writer(pdf);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setOutputMemory();
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            result.report.error = QStringLiteral("qpdf 没有写出重排后的 PDF");
            return result;
        }
        QByteArray written(reinterpret_cast<const char*>(buffer->getBuffer()),
                           static_cast<qsizetype>(buffer->getSize()));

        // ---- 第五步：给新文档重新打标签 ----
        // 源文档是带标签的，输出也应该是：否则「重排」把可访问性弄丢了。
        // 打标签失败不致命（结构还在），但要如实写进报告。
        QVector<QVector<TaggedRegion>> regions;
        for (const OutputPage& outputPage : pages) {
            QVector<TaggedRegion> pageRegions;
            for (int i = 0; i < outputPage.regions.size(); ++i) {
                TaggedRegion region;
                region.rect = outputPage.regions.at(i);
                region.order = i;
                pageRegions.append(region);
            }
            regions.append(pageRegions);
        }
        QString tagError;
        const QByteArray tagged = writeTaggedStructure(written, regions, &tagError);
        if (!tagError.isEmpty() || tagged.isEmpty()) {
            result.report.notes.append(QStringLiteral("新文档没能重新打上标签：%1")
                                           .arg(tagError.isEmpty() ? QStringLiteral("未知原因") : tagError));
        } else {
            written = tagged;
        }

        result.bytes = written;
        result.report.notes.append(
            QStringLiteral("重排后的版面与原文档不同：页数从 %1 页变成 %2 页，图形与表格结构没有保留")
                .arg(result.report.sourcePageCount)
                .arg(result.report.outputPageCount));
        return result;
    } catch (const std::exception& failure) {
        result.report.error = QStringLiteral("写出重排后的 PDF 失败：%1")
                                  .arg(QString::fromUtf8(failure.what()));
        result.bytes.clear();
        return result;
    }
#endif
}

}  // namespace aipdf::pdf
