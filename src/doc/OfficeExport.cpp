#include "doc/OfficeExport.h"

#include <QStringList>
#include <QtMath>

#include <algorithm>

#include "doc/ZipWriter.h"
#include "doc/XmlText.h"

namespace aipdf::doc {

namespace {

// Word 的页宽页高用缇（twip）表示：1 点 = 20 缇。
constexpr double kPointsToTwips = 20.0;

// 默认页面（A4），调用方给的尺寸非法时用它兜底。
constexpr double kDefaultPageWidthPoints = 595.0;
constexpr double kDefaultPageHeightPoints = 842.0;

// XML 文本转义统一走 doc/XmlText.h：本文件以前自己写了一份，
// 不过滤 XML 1.0 非法控制字符（PDF 文本层里一个 0x01 就能让 Word 报文件损坏），
// 也不保留合法代理对。统一实现只留一处，四个导出器共用。

// RTF 文本转义。
//
// 非 ASCII 字符写成 \uN?：N 是 UTF-16 码元（超过 0x7FFF 时按有符号 16 位
// 写负数，这是 RTF 规范的要求），? 是"退化成单字节时的替代字符"。
// 代理对按两个码元分别输出，Word / WordPad 读出来仍然是原字符。
QString escapeRtf(const QString& text) {
    QString out;
    out.reserve(text.size() + 8);
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\\')) {
            out.append(QStringLiteral("\\\\"));
        } else if (ch == QLatin1Char('{')) {
            out.append(QStringLiteral("\\{"));
        } else if (ch == QLatin1Char('}')) {
            out.append(QStringLiteral("\\}"));
        } else if (ch.unicode() < 128) {
            out.append(ch);
        } else {
            out.append(QStringLiteral("\\u"));
            out.append(QString::number(static_cast<int>(static_cast<qint16>(ch.unicode()))));
            out.append(QLatin1Char('?'));
        }
    }
    return out;
}

// 行内文字：按字符间隙补空格。
QString buildLineText(const QVector<pdf::TextChar>& chars) {
    QString out;
    bool hasPrevious = false;
    double previousRight = 0.0;
    double previousHeight = 0.0;
    for (const pdf::TextChar& character : chars) {
        const double height = qMax(character.box.height(), 1.0);
        if (hasPrevious) {
            const double gap = character.box.left() - previousRight;
            // 阈值取字高的三成：正常字距（约 0.1 倍字高）远小于它，
            // 词距（约 0.5 倍字高）大于它。
            //
            // 为什么不是两成：两成时小写字母之间的字距（x 高度 × 0.23）
            // 会越线，"Quarterly" 会被拆成 "Qu arterly"（实测过）。
            const double threshold = 0.3 * qMax(previousHeight, 1.0);
            const bool previousIsSpace = out.endsWith(QLatin1Char(' '));
            const bool currentIsSpace = character.text.startsWith(QLatin1Char(' '));
            if (gap > threshold && !previousIsSpace && !currentIsSpace) {
                out.append(QLatin1Char(' '));
            }
        }
        out.append(character.text);
        previousRight = character.box.right();
        previousHeight = height;
        hasPrevious = true;
    }
    return out;
}

// ---- 纯文本 -------------------------------------------------------------

QByteArray exportPlainText(const QVector<TextExportLine>& lines) {
    QString out;
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            out.append(QLatin1Char('\n'));
        }
        out.append(lines.at(i).text);
    }
    return out.toUtf8();
}

// ---- RTF ----------------------------------------------------------------

QByteArray exportRtf(const QVector<TextExportLine>& lines) {
    QString out;
    out.append(QStringLiteral("{\\rtf1\\ansi\\ansicpg936\\deff0"));
    // 字体表：\fcharset134 是 GB2312，中文能正常显示。
    out.append(QStringLiteral("{\\fonttbl{\\f0\\fnil\\fcharset134 SimSun;}}"));
    out.append(QStringLiteral("{\\colortbl;}"));
    for (const TextExportLine& line : lines) {
        if (line.raw) {
            // 原样片段（分页符）：不能再走 escapeRtf。
            out.append(line.text);
            out.append(QLatin1Char('\n'));
            continue;
        }
        // \fs24 = 12 磅；\par 结束这一段。
        out.append(QStringLiteral("\\pard\\plain\\f0\\fs24 "));
        out.append(escapeRtf(line.text));
        out.append(QStringLiteral("\\par\n"));
    }
    out.append(QLatin1Char('}'));
    return out.toUtf8();
}

// ---- Word Open XML（.docx） ---------------------------------------------

QByteArray contentTypesXml() {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" "
        "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>"));
    return out.toUtf8();
}

QByteArray packageRelsXml() {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"word/document.xml\"/>"
        "</Relationships>"));
    return out.toUtf8();
}

QByteArray documentXml(const QVector<QVector<TextExportLine>>& pageLines, const QSizeF& pageSizePt) {
    double widthPoints = pageSizePt.width();
    double heightPoints = pageSizePt.height();
    if (!(widthPoints > 0.0) || !(heightPoints > 0.0)) {
        widthPoints = kDefaultPageWidthPoints;
        heightPoints = kDefaultPageHeightPoints;
    }

    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:body>"));
    for (int pageIndex = 0; pageIndex < pageLines.size(); ++pageIndex) {
        // 页与页之间插分页符：不插的话 30 页会连成一大段，Word 里看不出页的边界。
        if (pageIndex > 0) {
            out.append(QStringLiteral("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>"));
        }
        for (const TextExportLine& line : pageLines.at(pageIndex)) {
            // xml:space="preserve"：不加它，行首行尾的空格会被 Word 吃掉。
            out.append(QStringLiteral("<w:p><w:r><w:t xml:space=\"preserve\">%1</w:t></w:r></w:p>")
                           .arg(escapeOoxmlText(line.text)));
        }
    }
    out.append(QStringLiteral("<w:sectPr><w:pgSz w:w=\"%1\" w:h=\"%2\"/></w:sectPr>")
                   .arg(qRound(widthPoints * kPointsToTwips))
                   .arg(qRound(heightPoints * kPointsToTwips)));
    out.append(QStringLiteral("</w:body></w:document>"));
    return out.toUtf8();
}

QByteArray exportWordOpenXml(const QVector<QVector<TextExportLine>>& pageLines,
                             const QSizeF& pageSizePt) {
    // 说明：产出的是**zip 打包的 .docx**（[Content_Types].xml + _rels/.rels +
    // word/document.xml），不是 Flat OPC 单文件。Word / WPS / LibreOffice
    // 都能直接打开这种包。
    zip::Writer writer;
    writer.addFile(QStringLiteral("[Content_Types].xml"), contentTypesXml());
    writer.addFile(QStringLiteral("_rels/.rels"), packageRelsXml());
    writer.addFile(QStringLiteral("word/document.xml"), documentXml(pageLines, pageSizePt));
    return writer.toByteArray();
}

}  // namespace

QVector<TextExportLine> buildTextLines(const pdf::TextPage& text) {
    QVector<pdf::TextChar> chars;
    chars.reserve(text.chars.size());
    for (const pdf::TextChar& character : text.chars) {
        if (character.text.isEmpty()) {
            continue;
        }
        chars.append(character);
    }

    // 按页面上从上到下排；同一行内按 x 从小到大。
    //
    // 注意 Qt 的 QRectF 约定：top() 是**较小的** y，bottom() 是较大的 y；
    // 而 PDF 的 y 向上，页面上方是较大的 y。所以这里按 box.bottom() 排
    // （那才是 PDF 的上边）。弄反了整页的上下都会颠倒，
    // 同一个坑在 pdf/kernel/TextPage.h 开头也写着。
    std::sort(chars.begin(), chars.end(),
              [](const pdf::TextChar& left, const pdf::TextChar& right) {
                  if (left.box.bottom() != right.box.bottom()) {
                      return left.box.bottom() > right.box.bottom();
                  }
                  return left.box.left() < right.box.left();
              });

    QVector<TextExportLine> lines;
    for (const pdf::TextChar& character : chars) {
        const double height = qMax(character.box.height(), 1.0);
        // 归行用**基线**（字符盒的下边，即较小的 y），不用盒子的中心。
        //
        // 为什么：句点、逗号这类字符的盒子只有基线以上那一丁点高
        // （12 号字里只有 1.2 pt），中心明显偏低。按中心归组的话，
        // "3.50" 会被拆成 "350" 与 "." 两行——导出到 Word / Excel 里
        // 就会看到数字被拆开（实测过）。
        const double baseline = character.box.top();

        bool appended = false;
        if (!lines.isEmpty()) {
            TextExportLine& last = lines.last();
            // 阈值用这一行字高的一半，且取 **max** 而不是 min：
            // 矮字符（句点）不能把阈值一起拉小，否则永远归不进行里。
            const double tolerance = 0.6 * qMax(qMax(last.fontSize, height), 1.0);
            if (qAbs(baseline - last.baseline) <= tolerance) {
                last.chars.append(character);
                last.baseline = (last.baseline * (last.chars.size() - 1) + baseline) /
                                last.chars.size();
                // PDF 的下边 = Qt 的 box.top()（较小的 y），
                // PDF 的上边 = Qt 的 box.bottom()（较大的 y）。
                last.bottom = qMin(last.bottom, character.box.top());
                last.top = qMax(last.top, character.box.bottom());
                last.left = qMin(last.left, character.box.left());
                last.right = qMax(last.right, character.box.right());
                last.fontSize = qMax(last.fontSize, height);
                appended = true;
            }
        }
        if (appended) {
            continue;
        }

        TextExportLine line;
        line.chars.append(character);
        line.baseline = baseline;
        line.bottom = character.box.top();
        line.top = character.box.bottom();
        line.left = character.box.left();
        line.right = character.box.right();
        line.fontSize = height;
        lines.append(line);
    }

    for (TextExportLine& line : lines) {
        std::sort(line.chars.begin(), line.chars.end(),
                  [](const pdf::TextChar& left, const pdf::TextChar& right) {
                      return left.box.left() < right.box.left();
                  });
        // 行首行尾的空白要去掉：PDFium 的文本流里夹着**排版产生的空白**
        // （两个文本对象之间的换行会被当成一个字符抽出来）。
        // 不去掉的话，导出的 Excel 单元格里会是 "Item " 这种带尾巴的值，
        // Word 里也会多出一堆空段落（实测过：openpyxl 读到 "Price\r\n"）。
        line.text = buildLineText(line.chars).trimmed();
    }
    return lines;
}

ExportCapability exportCapability(ExportFormat format) {
    ExportCapability capability;
    switch (format) {
    case ExportFormat::WordOpenXml:
        capability.keepsText = true;
        capability.keepsLayout = false;
        capability.keepsImages = false;
        capability.keepsTables = false;
        capability.limitation = QStringLiteral(
            "只保留文字内容与段落划分，图片、表格、批注和精确版式都会丢失。");
        break;
    case ExportFormat::Rtf:
        capability.keepsText = true;
        capability.keepsLayout = false;
        capability.keepsImages = false;
        capability.keepsTables = false;
        capability.limitation = QStringLiteral(
            "只保留文字与换行，图片、表格、颜色和字体样式都会丢失。");
        break;
    case ExportFormat::PlainText:
        capability.keepsText = true;
        capability.keepsLayout = false;
        capability.keepsImages = false;
        capability.keepsTables = false;
        capability.limitation = QStringLiteral(
            "只保留纯文字，表格会退化成空格，页眉页脚会混进正文。");
        break;
    default:
        capability.limitation = QStringLiteral("未知格式，无法导出。");
        break;
    }
    return capability;
}

ExportResult exportPageTo(ExportFormat format, const pdf::TextPage& text,
                           const QSizeF& pageSizePt) {
    ExportResult result;
    switch (format) {
    case ExportFormat::WordOpenXml: {
        result.suggestedSuffix = QStringLiteral("docx");
        const QVector<TextExportLine> lines = buildTextLines(text);
        result.bytes = exportWordOpenXml(QVector<QVector<TextExportLine>>{lines}, pageSizePt);
        result.ok = !result.bytes.isEmpty();
        if (!result.ok) {
            result.bytes.clear();
            result.error = QStringLiteral("导出 Word 文档失败：生成的压缩包为空。");
        }
        break;
    }
    case ExportFormat::Rtf: {
        result.suggestedSuffix = QStringLiteral("rtf");
        const QVector<TextExportLine> lines = buildTextLines(text);
        result.bytes = exportRtf(lines);
        result.ok = true;
        break;
    }
    case ExportFormat::PlainText: {
        result.suggestedSuffix = QStringLiteral("txt");
        const QVector<TextExportLine> lines = buildTextLines(text);
        result.bytes = exportPlainText(lines);
        result.ok = true;
        break;
    }
    default:
        result.ok = false;
        result.bytes.clear();
        result.suggestedSuffix.clear();
        result.error = QStringLiteral("导出失败：不支持的导出格式。");
        break;
    }
    return result;
}

ExportResult exportPagesTo(ExportFormat format, const QVector<pdf::TextPage>& pages,
                            const QSizeF& pageSizePt) {
    ExportResult result;
    if (pages.isEmpty()) {
        result.error = QStringLiteral("导出失败：文档里没有可导出的文字层。");
        return result;
    }
    QVector<QVector<TextExportLine>> pageLines;
    pageLines.reserve(pages.size());
    for (const pdf::TextPage& page : pages) {
        pageLines.append(buildTextLines(page));
    }
    switch (format) {
    case ExportFormat::WordOpenXml: {
        result.suggestedSuffix = QStringLiteral("docx");
        result.bytes = exportWordOpenXml(pageLines, pageSizePt);
        result.ok = !result.bytes.isEmpty();
        if (!result.ok) {
            result.bytes.clear();
            result.error = QStringLiteral("导出 Word 文档失败：生成的压缩包为空。");
        }
        break;
    }
    case ExportFormat::Rtf: {
        result.suggestedSuffix = QStringLiteral("rtf");
        // RTF 没有分页符的说法，用换页（\page）隔开。
        QVector<TextExportLine> merged;
        for (int pageIndex = 0; pageIndex < pageLines.size(); ++pageIndex) {
            if (pageIndex > 0) {
                TextExportLine separator;
                separator.text = QStringLiteral("\\page");
                // 原样输出：走 escapeRtf 会变成字面 "\page"，分页失效。
                separator.raw = true;
                merged.append(separator);
            }
            merged.append(pageLines.at(pageIndex));
        }
        result.bytes = exportRtf(merged);
        result.ok = true;
        break;
    }
    case ExportFormat::PlainText: {
        result.suggestedSuffix = QStringLiteral("txt");
        QVector<TextExportLine> merged;
        for (int pageIndex = 0; pageIndex < pageLines.size(); ++pageIndex) {
            if (pageIndex > 0) {
                TextExportLine separator;
                separator.text = QStringLiteral("\f");
                merged.append(separator);
            }
            merged.append(pageLines.at(pageIndex));
        }
        result.bytes = exportPlainText(merged);
        result.ok = true;
        break;
    }
    default:
        result.error = QStringLiteral("导出失败：不支持的导出格式。");
        break;
    }
    return result;
}

}  // namespace aipdf::doc
