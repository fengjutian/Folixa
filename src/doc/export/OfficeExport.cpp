#include "doc/export/OfficeExport.h"

#include <QRectF>
#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <limits>

#include "doc/export/ZipWriter.h"
#include "doc/export/XmlText.h"

namespace aipdf::doc {

namespace {

// Word 的页宽页高用缇（twip）表示：1 点 = 20 缇。
constexpr double kPointsToTwips = 20.0;

// 默认页面（A4），调用方给的尺寸非法时用它兜底。
constexpr double kDefaultPageWidthPoints = 595.0;
constexpr double kDefaultPageHeightPoints = 842.0;

// XML 文本转义统一走 doc/export/XmlText.h：本文件以前自己写了一份，
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
    QChar previousChar;
    for (const pdf::TextChar& character : chars) {
        const double height = qMax(character.box.height(), 1.0);
        const QChar currentChar = character.text.isEmpty() ? QChar() : character.text.at(0);
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
            // 两个 CJK 字符之间不补空格。
            const bool cjkPair = !previousChar.isNull() && !currentChar.isNull() &&
                                 isCjkChar(previousChar) && isCjkChar(currentChar);
            if (gap > threshold && !previousIsSpace && !currentIsSpace && !cjkPair) {
                out.append(QLatin1Char(' '));
            }
        }
        out.append(character.text);
        previousRight = character.box.right();
        previousHeight = height;
        previousChar = currentChar;
        hasPrevious = true;
    }
    return out;
}

// ---- 分带：双栏页面的行重建 ---------------------------------------------

// 一行的中间产物（还没拼成文字）。
struct RawRow {
    // 行内字符，按 x 从小到大排。
    QVector<pdf::TextChar> chars;
    // 行的基线（PDF user space，**较小的 y**）。
    double baseline = 0.0;
    // 行内字高。
    double fontSize = 0.0;
    double left = 0.0;
    double right = 0.0;
};

// 中位数。
double medianOf(QVector<double> values) {
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int middle = values.size() / 2;
    return values.size() % 2 ? values.at(middle)
                             : (values.at(middle - 1) + values.at(middle)) * 0.5;
}

// 百分位数（values 会被就地排序）。
double percentileOf(QVector<double>& values, double fraction) {
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int index = qBound(0, static_cast<int>(values.size() * fraction), values.size() - 1);
    return values.at(index);
}

// 一个「可能的竖直空白带」。
struct GapCluster {
    // 空白带右沿（右边那一段的起点，x）。
    double x = 0.0;
    // 跨越它的行（行下标）。
    QVector<int> rows;
    // 每行里，空白带左右两侧那两段的宽度。
    QVector<double> leftWidths;
    QVector<double> rightWidths;
    // 跨越它的那些行的基线。
    QVector<double> baselines;
    // 跨越它的那些行的字高（行内字盒的最大高度）。
    QVector<double> fontSizes;
};

// 判定参数：这些都是启发式阈值，理由写在各自的用处旁边。
namespace {

// 空白带候选的最小宽度（相对字高）。
//
// 词距大约是字高的 0.3 倍，栏间距与表格列间距都在一个字高以上，
// 所以一个字高是「词距」与「版式空白」的分界。
constexpr double kGapWidthFactor = 1.0;

// 「这一段是正文而不是表格单元」的判据：宽度至少是字高的这么多倍。
//
// 表格单元是几个字（窄），正文的一行是一整句（宽）。这条判据是
// 「双栏正文」与「多列表格」的分界：两者在几何上几乎一样，
// 只能靠「每段有多长」区分。
constexpr double kProseWidthFactor = 4.0;

// 竖直空白带至少要贯穿页面文字高度的这么比例，才算「栏」而不是「某几行的巧合」。
constexpr double kColumnSpanFactor = 0.2;

// 一个空白带至少要出现在这么多行里。
constexpr int kMinColumnSupport = 3;

// 一个空白带至少要出现在这么比例的行里。
constexpr double kMinColumnSupportRatio = 0.15;

// 空白带候选的最小宽度（相对该行的字高）。
//
// 词距大约是字高的 0.3 倍，栏间距与表格列间距都在一个字高以上，
// 所以一个字高是「词距」与「版式空白」的分界。
//
// 为什么按**该行自己的字高**算，不按全页中位字高：全页中位字高会被
// 小写字母的字盒拖小（11pt 的小写 e 只有 5.7pt 高），阈值随之偏低，
// 全角标点左右的空白（全角句号「。」的墨迹只占字身框的一小块，
// 左右能空出 7pt 左右）就够着阈值了——于是「。」前面凭空多出一个栏口，
// 把整页的正文切碎。
double gapThresholdFor(double fontSize) {
    return qMax(3.0, fontSize * kGapWidthFactor);
}

}  // namespace

// 归行：把字符按基线聚成行。
//
// 归行用**基线**（字符盒的下边，即较小的 y），不用盒子的中心。
// 为什么：句点、逗号这类字符的盒子只有基线以上那一丁点高
// （12 号字里只有 1.2 pt），中心明显偏低。按中心归组的话，
// "3.50" 会被拆成 "350" 与 "." 两行——导出到 Word / Excel 里
// 就会看到数字被拆开（实测过）。
QVector<RawRow> clusterRows(QVector<pdf::TextChar> chars) {
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

    QVector<RawRow> rows;
    for (const pdf::TextChar& character : chars) {
        const double height = qMax(character.box.height(), 1.0);
        const double baseline = character.box.top();

        bool appended = false;
        if (!rows.isEmpty()) {
            RawRow& last = rows.last();
            // 阈值用这一行字高的一半，且取 **max** 而不是 min：
            // 矮字符（句点）不能把阈值一起拉小，否则永远归不进行里。
            const double tolerance = 0.6 * qMax(qMax(last.fontSize, height), 1.0);
            if (qAbs(baseline - last.baseline) <= tolerance) {
                last.chars.append(character);
                last.baseline = (last.baseline * (last.chars.size() - 1) + baseline) /
                                last.chars.size();
                last.fontSize = qMax(last.fontSize, height);
                last.left = qMin(last.left, character.box.left());
                last.right = qMax(last.right, character.box.right());
                appended = true;
            }
        }
        if (appended) {
            continue;
        }

        RawRow row;
        row.chars.append(character);
        row.baseline = baseline;
        row.fontSize = height;
        row.left = character.box.left();
        row.right = character.box.right();
        rows.append(row);
    }

    for (RawRow& row : rows) {
        std::sort(row.chars.begin(), row.chars.end(),
                  [](const pdf::TextChar& left, const pdf::TextChar& right) {
                      return left.box.left() < right.box.left();
                  });
    }
    return rows;
}

// 一行 → 一条导出行。
TextExportLine toExportLine(const RawRow& row) {
    TextExportLine line;
    line.chars = row.chars;
    line.baseline = row.baseline;
    line.fontSize = row.fontSize;
    line.left = row.left;
    line.right = row.right;
    line.bottom = row.chars.constFirst().box.top();
    line.top = row.chars.constFirst().box.bottom();
    for (const pdf::TextChar& character : row.chars) {
        line.bottom = qMin(line.bottom, character.box.top());
        line.top = qMax(line.top, character.box.bottom());
    }
    // 行首行尾的空白要去掉：PDFium 的文本流里夹着**排版产生的空白**
    // （两个文本对象之间的换行会被当成一个字符抽出来）。
    // 不去掉的话，导出的 Excel 单元格里会是 "Item " 这种带尾巴的值，
    // Word 里也会多出一堆空段落（实测过：openpyxl 读到 "Price\r\n"）。
    line.text = buildLineText(line.chars).trimmed();
    return line;
}

// 找出页面上的竖直空白带候选。
//
// 聚类按空白带的**右沿**（右边那一段的起点），不按空隙的中点。
//
// 为什么：中点会跟着左边那段的字数变（每行字数不同 → 中点差几个点），
// 按中点聚类会把同一个栏口拆成一堆碎簇，一个都聚不起来。
// 右沿是**栏的起始位置**，每一行的同一个栏，右沿都在同一个 x 上——
// 这才是稳定的聚类依据。
QVector<GapCluster> detectGapClusters(const QVector<RawRow>& rows, double medianFontSize,
                                       double pageVerticalExtent) {
    struct Gap {
        double rightEdge = 0.0;
        int row = -1;
    };
    QVector<Gap> gaps;
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const RawRow& row = rows.at(rowIndex);
        // 阈值按这一行自己的字高算（见 gapThresholdFor 的说明）。
        const double gapThreshold = gapThresholdFor(row.fontSize);
        for (int index = 0; index + 1 < row.chars.size(); ++index) {
            const double gap = row.chars.at(index + 1).box.left() - row.chars.at(index).box.right();
            if (gap <= gapThreshold) {
                continue;
            }
            Gap entry;
            entry.rightEdge = row.chars.at(index + 1).box.left();
            entry.row = rowIndex;
            gaps.append(entry);
        }
    }
    if (gaps.isEmpty()) {
        return {};
    }

    std::sort(gaps.begin(), gaps.end(),
              [](const Gap& left, const Gap& right) { return left.rightEdge < right.rightEdge; });
    const double clusterTolerance = qMax(2.0, medianFontSize * 0.5);

    QVector<GapCluster> clusters;
    QVector<double> rightEdges;
    for (const Gap& gap : gaps) {
        if (clusters.isEmpty() || gap.rightEdge - rightEdges.last() > clusterTolerance) {
            GapCluster cluster;
            cluster.x = gap.rightEdge;
            clusters.append(cluster);
            rightEdges.append(gap.rightEdge);
        }
        clusters.last().rows.append(gap.row);
    }
    // 簇的代表位置取右沿的中位数：同一簇里的右沿可能有一点点出入。
    for (int index = 0; index < clusters.size(); ++index) {
        QVector<double> edges;
        for (const Gap& gap : gaps) {
            if (qAbs(gap.rightEdge - rightEdges.at(index)) <= clusterTolerance) {
                edges.append(gap.rightEdge);
            }
        }
        if (!edges.isEmpty()) {
            clusters[index].x = medianOf(edges);
        }
    }

    // 量出每个空白带左右两侧那两段有多宽。
    for (GapCluster& cluster : clusters) {
        for (int rowIndex : cluster.rows) {
            const RawRow& row = rows.at(rowIndex);
            double leftMin = 0.0;
            double leftMax = 0.0;
            double rightMin = 0.0;
            double rightMax = 0.0;
            bool hasLeft = false;
            bool hasRight = false;
            for (const pdf::TextChar& character : row.chars) {
                const double center = (character.box.left() + character.box.right()) * 0.5;
                if (center < cluster.x) {
                    leftMin = hasLeft ? qMin(leftMin, character.box.left()) : character.box.left();
                    leftMax = hasLeft ? qMax(leftMax, character.box.right()) : character.box.right();
                    hasLeft = true;
                } else {
                    rightMin = hasRight ? qMin(rightMin, character.box.left()) : character.box.left();
                    rightMax = hasRight ? qMax(rightMax, character.box.right()) : character.box.right();
                    hasRight = true;
                }
            }
            cluster.leftWidths.append(hasLeft ? leftMax - leftMin : 0.0);
            cluster.rightWidths.append(hasRight ? rightMax - rightMin : 0.0);
            cluster.baselines.append(row.baseline);
            cluster.fontSizes.append(row.fontSize);
        }
    }

    // 筛掉不像「栏」的空白带。
    const int minSupport =
        qMax(kMinColumnSupport, static_cast<int>(rows.size() * kMinColumnSupportRatio));
    QVector<GapCluster> kept;
    for (const GapCluster& cluster : clusters) {
        if (cluster.rows.size() < minSupport) {
            continue;
        }
        // 两侧都得像正文：一整句文字，而不是一个表格单元。
        //
        // 这里的字高取**跨越这个空白带的那些行自己的字高**，
        // 不取全页的中位字高——全页中位字高会被小写字母的字盒拖小
        // （11pt 的小写 e 只有 5.7pt 高），阈值随之从 44 掉到 23，
        // 表格首列（"Item" 23.3pt）就被当成了正文，整张表被当成三栏版式，
        // 表里的字被栏口切开。用行自己的字高，阈值回到 34 以上，
        // 表格列间隙就认不出来了。
        const double rowFontSize = medianOf(cluster.fontSizes);
        if (medianOf(cluster.leftWidths) < kProseWidthFactor * rowFontSize ||
            medianOf(cluster.rightWidths) < kProseWidthFactor * rowFontSize) {
            continue;
        }
        // 还得贯穿足够多的高度：只在一两行出现的空白是巧合，不是栏。
        const double span = *std::max_element(cluster.baselines.begin(), cluster.baselines.end()) -
                            *std::min_element(cluster.baselines.begin(), cluster.baselines.end());
        if (pageVerticalExtent > 0.0 && span < kColumnSpanFactor * pageVerticalExtent) {
            continue;
        }
        kept.append(cluster);
    }
    return kept;
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

// Word 里用到的命名空间。
constexpr auto kNsW = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
constexpr auto kNsR = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr auto kNsWp = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing";
constexpr auto kNsA = "http://schemas.openxmlformats.org/drawingml/2006/main";
constexpr auto kNsPic = "http://schemas.openxmlformats.org/drawingml/2006/picture";

// 图片关系（DOCX 的 document.xml.rels 用）。
constexpr auto kRelImage = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";

// 1 点 = 12700 EMU（English Metric Unit）。
//
// Word 的**页面尺寸**用缇（twip，1 点 = 20 缇），**图片尺寸**用 EMU。
// 同一份 XML 里两种单位并存，写错了图片会大到离谱或者缩成一个点。
constexpr double kEmuPerPoint = 12700.0;

// 段落的字体属性。
//
// 为什么要写死 eastAsia：不写的话 Word 与 LibreOffice 各自挑主题里的
// 中文字体，同一份文档在两边的排版会飘（渲染比对就分不清是内容变了
// 还是字体变了）。写死成宋体，两边的选择一致。
QString runProperties() {
    return QStringLiteral(
        "<w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" "
        "w:eastAsia=\"SimSun\" w:cs=\"SimSun\"/></w:rPr>");
}

QByteArray contentTypesXml(const office::MediaSet& media) {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" "
        "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"));
    // 图片的默认内容类型：少了这一项，Office 不知道 png 是什么，
    // 打开时会直接报「文件已损坏」。
    for (const QPair<QString, QString>& entry : media.contentTypeDefaults()) {
        out.append(QStringLiteral("<Default Extension=\"%1\" ContentType=\"%2\"/>")
                       .arg(entry.first, entry.second));
    }
    out.append(QStringLiteral(
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

// word/_rels/document.xml.rels：正文与图片之间的关系。
//
// 为什么以前没有这个部件：以前的 DOCX 里根本没有图片，正文里也没有任何
// r:embed 引用，所以不需要它。现在正文要引用图片，就必须有——
// 少了它，Word 打开时会报「文件已损坏」（它找不到 rId 对应的部件）。
QByteArray documentRelsXml(const office::MediaSet& media) {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"));
    for (int index = 0; index < media.count(); ++index) {
        // Target 相对于 word/ 目录（rels 文件在 word/_rels/ 下），
        // 所以是 "media/image1.png"，不是包里的完整路径。
        out.append(QStringLiteral(
            "<Relationship Id=\"%1\" Type=\"%2\" Target=\"%3\"/>")
                       .arg(office::relationshipId(index), kRelImage,
                            media.at(index).partName.section(QLatin1Char('/'), 1)));
    }
    out.append(QStringLiteral("</Relationships>"));
    return out.toUtf8();
}

// 一个段落。
QString docxParagraph(const QString& text) {
    // xml:space="preserve"：不加它，行首行尾的空格会被 Word 吃掉。
    return QStringLiteral("<w:p><w:r>%1<w:t xml:space=\"preserve\">%2</w:t></w:r></w:p>")
        .arg(runProperties(), escapeOoxmlText(text));
}

// 一张内嵌图片。
//
// 显示尺寸用 **EMU**（1 点 = 12700 EMU），由落位矩形换算而来；
// 像素尺寸只决定清晰度，绝不能拿来当 EMU 用。
QString docxPicture(int shapeId, const QString& relationship, double widthPoints,
                    double heightPoints) {
    const qint64 cx = qRound(widthPoints * kEmuPerPoint);
    const qint64 cy = qRound(heightPoints * kEmuPerPoint);
    return QStringLiteral(
        "<w:p><w:r>%1<w:drawing>"
        "<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
        "<wp:extent cx=\"%2\" cy=\"%3\"/>"
        "<wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>"
        "<wp:docPr id=\"%4\" name=\"图片 %4\"/>"
        "<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
        "<a:graphic>"
        "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:pic>"
        "<pic:nvPicPr><pic:cNvPr id=\"%4\" name=\"图片 %4\"/><pic:cNvPicPr/></pic:nvPicPr>"
        "<pic:blipFill><a:blip r:embed=\"%5\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%2\" cy=\"%3\"/></a:xfrm>"
        "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
        "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>")
        .arg(runProperties())
        .arg(cx)
        .arg(cy)
        .arg(shapeId)
        .arg(relationship);
}

// 这一行是不是落在某张表里。
//
// 表格的行已经写成 <w:tbl> 了，正文里再写一遍就成了重复：
// Word 里会看到同一张表既是一串段落，又是一个表格对象。
//
// 判据用「行的中心点」：表格的一行横跨好几列，中心点落在表格范围内就算在表里；
// 表格上方或下方的说明文字中心点在范围外，就不算。
bool lineInsideTable(const TextExportLine& line, const office::TableGrid& table) {
    const QRectF bounds(table.leftPoints, table.bottomPoints, table.widthPoints,
                        table.heightPoints);
    if (bounds.isEmpty()) {
        return false;
    }
    const QPointF center((line.left + line.right) * 0.5, (line.bottom + line.top) * 0.5);
    return bounds.contains(center);
}

// 这一行是不是落在「属于这条带」的表里。
bool lineInsideBandTable(const TextExportLine& line, const QVector<office::TableGrid>& tables,
                          int band) {
    for (const office::TableGrid& table : tables) {
        if (table.bandIndex != band) {
            continue;
        }
        if (lineInsideTable(line, table)) {
            return true;
        }
    }
    return false;
}

// 一个表格。
//
// 列宽用缇（twip）；单元格宽度与 tblGrid 一致，Word 才会照着画。
QString docxTable(const office::TableGrid& table) {
    const int columns = table.columnCount();
    if (columns <= 0 || table.rows.isEmpty()) {
        return {};
    }
    QVector<double> widths = table.columnWidthsPoints;
    while (widths.size() < columns) {
        widths.append(0.0);
    }
    double total = 0.0;
    for (int column = 0; column < columns; ++column) {
        total += qMax(0.0, widths.at(column));
    }
    if (total <= 0.0) {
        total = qMax(1.0, table.widthPoints);
    }
    const int totalTwips = qMax(1, qRound(total * kPointsToTwips));

    QString out;
    out.append(QStringLiteral(
        "<w:tbl><w:tblPr>"
        "<w:tblW w:w=\"%1\" w:type=\"dxa\"/>"
        "<w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "<w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
        "</w:tblBorders>"
        "<w:tblLayout w:type=\"fixed\"/>"
        "</w:tblPr><w:tblGrid>")
                   .arg(totalTwips));
    for (int column = 0; column < columns; ++column) {
        const int widthTwips = qMax(1, qRound(widths.at(column) * kPointsToTwips));
        out.append(QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(widthTwips));
    }
    out.append(QStringLiteral("</w:tblGrid>"));
    for (const QVector<QString>& row : table.rows) {
        out.append(QStringLiteral("<w:tr>"));
        for (int column = 0; column < columns; ++column) {
            const int widthTwips = qMax(1, qRound(widths.at(column) * kPointsToTwips));
            const QString text = column < row.size() ? row.at(column) : QString();
            out.append(QStringLiteral(
                "<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/></w:tcPr>%2</w:tc>")
                           .arg(widthTwips)
                           .arg(docxParagraph(text)));
        }
        out.append(QStringLiteral("</w:tr>"));
    }
    out.append(QStringLiteral("</w:tbl>"));
    // 表格后面必须跟一个段落：Word 不允许正文以表格结尾。
    out.append(QStringLiteral("<w:p/>"));
    return out;
}

// 正文里的一个块。
struct DocxBlock {
    enum Kind { Paragraph, Picture, Table } kind = Paragraph;
    // 页面上的纵向位置（PDF user space，越大越靠上）。
    double order = 0.0;
    // 在对应列表里的下标。
    int index = 0;
    // 属于哪条带（决定它在正文里的先后）。
    int band = 0;
};

QByteArray documentXml(const QVector<OfficePageContent>& contents, const QSizeF& pageSizePt,
                        const office::MediaSet& media) {
    double widthPoints = pageSizePt.width();
    double heightPoints = pageSizePt.height();
    if (!(widthPoints > 0.0) || !(heightPoints > 0.0)) {
        widthPoints = kDefaultPageWidthPoints;
        heightPoints = kDefaultPageHeightPoints;
    }

    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<w:document xmlns:w=\"%1\" xmlns:r=\"%2\" xmlns:wp=\"%3\" xmlns:a=\"%4\" xmlns:pic=\"%5\">"
        "<w:body>")
                   .arg(kNsW, kNsR, kNsWp, kNsA, kNsPic));
    int shapeId = 1;
    for (int pageIndex = 0; pageIndex < contents.size(); ++pageIndex) {
        const OfficePageContent& page = contents.at(pageIndex);
        // 页与页之间插分页符：不插的话 30 页会连成一大段，Word 里看不出页的边界。
        if (pageIndex > 0) {
            out.append(QStringLiteral("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>"));
        }

        // 正文里的块：按「带」分组，带内按纵向位置从上到下。
        QVector<DocxBlock> blocks;
        for (int band = 0; band < page.bands.size(); ++band) {
            const TextBand& textBand = page.bands.at(band);
            for (int line = 0; line < textBand.lines.size(); ++line) {
                // 表格里的行不再写成段落：表格对象里已经有它们了，
                // 两边都写就是重复内容。
                if (lineInsideBandTable(textBand.lines.at(line), page.tables, band)) {
                    continue;
                }
                DocxBlock block;
                block.kind = DocxBlock::Paragraph;
                block.order = textBand.lines.at(line).top;
                block.index = line;
                block.band = band;
                blocks.append(block);
            }
        }
        for (int index = 0; index < page.images.size(); ++index) {
            const office::ImagePlacement& image = page.images.at(index);
            DocxBlock block;
            block.kind = DocxBlock::Picture;
            block.order = image.upperEdgePoints();
            block.index = index;
            block.band = qMax(0, image.bandIndex);
            blocks.append(block);
        }
        for (int index = 0; index < page.tables.size(); ++index) {
            const office::TableGrid& table = page.tables.at(index);
            DocxBlock block;
            block.kind = DocxBlock::Table;
            block.order = table.topPoints();
            block.index = index;
            block.band = qMax(0, table.bandIndex);
            blocks.append(block);
        }
        std::stable_sort(blocks.begin(), blocks.end(),
                         [](const DocxBlock& left, const DocxBlock& right) {
                             if (left.band != right.band) {
                                 return left.band < right.band;
                             }
                             if (left.order != right.order) {
                                 return left.order > right.order;
                             }
                             return left.kind < right.kind;
                         });

        for (const DocxBlock& block : blocks) {
            switch (block.kind) {
            case DocxBlock::Paragraph: {
                const TextBand& textBand = page.bands.at(block.band);
                if (block.index < 0 || block.index >= textBand.lines.size()) {
                    break;
                }
                out.append(docxParagraph(textBand.lines.at(block.index).text));
                break;
            }
            case DocxBlock::Picture: {
                const office::ImagePlacement& image = page.images.at(block.index);
                if (image.mediaIndex < 0 || image.mediaIndex >= media.count()) {
                    break;
                }
                out.append(docxPicture(++shapeId, office::relationshipId(image.mediaIndex),
                                       image.widthPoints, image.heightPoints));
                break;
            }
            case DocxBlock::Table:
                out.append(docxTable(page.tables.at(block.index)));
                break;
            }
        }
    }
    out.append(QStringLiteral("<w:sectPr><w:pgSz w:w=\"%1\" w:h=\"%2\"/></w:sectPr>")
                   .arg(qRound(widthPoints * kPointsToTwips))
                   .arg(qRound(heightPoints * kPointsToTwips)));
    out.append(QStringLiteral("</w:body></w:document>"));
    return out.toUtf8();
}

QByteArray exportWordOpenXml(const QVector<OfficePageContent>& contents, const QSizeF& pageSizePt,
                              const office::MediaSet& media) {
    // 说明：产出的是**zip 打包的 .docx**（[Content_Types].xml + _rels/.rels +
    // word/_rels/document.xml.rels + word/document.xml + word/media/*），
    // 不是 Flat OPC 单文件。Word / WPS / LibreOffice 都能直接打开这种包。
    zip::Writer writer;
    writer.addFile(QStringLiteral("[Content_Types].xml"), contentTypesXml(media));
    writer.addFile(QStringLiteral("_rels/.rels"), packageRelsXml());
    writer.addFile(QStringLiteral("word/_rels/document.xml.rels"), documentRelsXml(media));
    writer.addFile(QStringLiteral("word/document.xml"), documentXml(contents, pageSizePt, media));
    for (int index = 0; index < media.count(); ++index) {
        // partName 已经是包内完整路径（"word/media/image1.png"）。
        writer.addFile(media.at(index).partName, media.at(index).bytes);
    }
    return writer.toByteArray();
}

}  // namespace

// CJK 表意文字、假名、谚文与全角标点：这些字符之间不插空格、不切单元格。
//
// 为什么：中文不用空格分词，"第一 行" 是错的；把汉字之间的墨迹空隙
// 当成列间距，一整句话会被切成一堆「单元格」，整页正文会被误判成一张表。
// 仓库里 app/layout/TextLayout.cpp 有同样的约定，导出这一侧照它来。
// （这个函数对外可见：doc/export/PdfToOffice.cpp 切单元格时也要用同一把尺子。）
bool isCjkChar(QChar ch) {
    const ushort code = ch.unicode();
    return (code >= 0x4E00 && code <= 0x9FFF) ||    // 统一表意文字
           (code >= 0x3400 && code <= 0x4DBF) ||    // 扩展 A
           (code >= 0x3040 && code <= 0x30FF) ||    // 假名
           (code >= 0xAC00 && code <= 0xD7AF) ||    // 谚文
           (code >= 0x3000 && code <= 0x303F) ||    // CJK 标点
           (code >= 0xFF00 && code <= 0xFFEF);      // 全角形式
}

QVector<TextBand> buildTextBands(const pdf::TextPage& text) {
    QVector<pdf::TextChar> chars;
    chars.reserve(text.chars.size());
    for (const pdf::TextChar& character : text.chars) {
        if (character.text.isEmpty()) {
            continue;
        }
        chars.append(character);
    }
    if (chars.isEmpty()) {
        return {};
    }

    const QVector<RawRow> rows = clusterRows(chars);
    if (rows.isEmpty()) {
        return {};
    }

    double pageTop = 0.0;
    double pageBottom = 0.0;
    double pageLeft = 0.0;
    double pageRight = 0.0;
    QVector<double> fontSizes;
    bool first = true;
    for (const pdf::TextChar& character : chars) {
        pageTop = first ? character.box.bottom() : qMax(pageTop, character.box.bottom());
        pageBottom = first ? character.box.top() : qMin(pageBottom, character.box.top());
        pageLeft = first ? character.box.left() : qMin(pageLeft, character.box.left());
        pageRight = first ? character.box.right() : qMax(pageRight, character.box.right());
        fontSizes.append(qMax(character.box.height(), 1.0));
        first = false;
    }
    const double medianFontSize = medianOf(fontSizes);

    const QVector<GapCluster> columns =
        detectGapClusters(rows, medianFontSize, pageTop - pageBottom);
    if (columns.isEmpty()) {
        // 单栏：整页就是一条通栏带。
        TextBand band;
        band.fullWidth = true;
        band.left = pageLeft;
        band.right = pageRight;
        band.bottom = pageBottom;
        band.top = pageTop;
        for (const RawRow& row : rows) {
            band.lines.append(toExportLine(row));
        }
        return QVector<TextBand>{band};
    }

    // 栏的纵向区域：���「被拆开的那些正文行」定。
    QVector<double> separatorX;
    for (const GapCluster& cluster : columns) {
        separatorX.append(cluster.x);
    }
    std::sort(separatorX.begin(), separatorX.end());

    // 第一遍：决定哪些行会被拆成多栏。
    //
    // 判据：这一行跨过空白带之后，**每一段都还要像正文**（足够宽）。
    // 表格的行跨过空白带时，有一段是一个窄单元（几个字），不满足；
    // 通栏标题只有一段，也不满足。这样表格与标题就不会被栏切开。
    QVector<QVector<QVector<pdf::TextChar>>> splitSegments(rows.size());
    QVector<bool> splittable(rows.size(), false);
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const RawRow& row = rows.at(rowIndex);
        // 按位置切开：字符的中心越过空白带时另起一段。
        //
        // 注意不能只看「有没有字符横跨空白带」——空白带落在两个词**之间**
        // 时根本没有字符横跨它，按横跨判断会一行都拆不开。
        //
        // 但也不能只看「中心越过就断开」：通栏标题会**横跨**栏口
        // （它的字一直排到右栏那一侧），按中心越过就断开，标题会被切成两段。
        // 真正的分栏行在栏口那里是**断开的**（有空白），
        // 所以断开的依据是「那里有没有空隙」。
        QVector<QVector<pdf::TextChar>> segments;
        QVector<pdf::TextChar> current;
        int separatorIndex = 0;
        const double gapThreshold = gapThresholdFor(row.fontSize);
        for (const pdf::TextChar& character : row.chars) {
            const double center = (character.box.left() + character.box.right()) * 0.5;
            while (separatorIndex < separatorX.size() && center >= separatorX.at(separatorIndex)) {
                if (!current.isEmpty()) {
                    const double gap = character.box.left() - current.constLast().box.right();
                    if (gap > gapThreshold) {
                        segments.append(current);
                        current.clear();
                    }
                }
                ++separatorIndex;
            }
            current.append(character);
        }
        if (!current.isEmpty()) {
            segments.append(current);
        }
        if (segments.size() < 2) {
            continue;
        }
        bool allProse = true;
        for (const QVector<pdf::TextChar>& segment : segments) {
            double left = 0.0;
            double right = 0.0;
            bool segmentFirst = true;
            for (const pdf::TextChar& character : segment) {
                left = segmentFirst ? character.box.left() : qMin(left, character.box.left());
                right = segmentFirst ? character.box.right() : qMax(right, character.box.right());
                segmentFirst = false;
            }
            if (right - left < kProseWidthFactor * row.fontSize) {
                allProse = false;
                break;
            }
        }
        if (!allProse) {
            continue;
        }
        splittable[rowIndex] = true;
        splitSegments[rowIndex] = segments;
    }

    // 栏的纵向区域：由「被拆开的那些正文行」定。
    //
    // 为什么用正文行来定：空白带跨越的行里可能混进一行通栏标题
    // （标题很长，横跨整页时也会在空白带那个 x 上留下空隙）。
    // 标题不会被拆开，所以区域只按正文行来算。
    QVector<double> splitBaselines;
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        if (splittable.at(rowIndex)) {
            splitBaselines.append(rows.at(rowIndex).baseline);
        }
    }
    if (splitBaselines.isEmpty()) {
        // 没有一行能被拆开：这一页按单栏处理。
        TextBand band;
        band.fullWidth = true;
        band.left = pageLeft;
        band.right = pageRight;
        band.bottom = pageBottom;
        band.top = pageTop;
        for (const RawRow& row : rows) {
            band.lines.append(toExportLine(row));
        }
        return QVector<TextBand>{band};
    }

    // 栏的纵向区域（正文行的基线范围）。
    const double regionBottom = *std::min_element(splitBaselines.begin(), splitBaselines.end());
    const double regionTop = *std::max_element(splitBaselines.begin(), splitBaselines.end());

    // 分带：通栏（区域上方）+ 各栏 + 通栏（其余）。
    const int columnCount = separatorX.size() + 1;
    QVector<TextBand> bands;
    TextBand above;
    above.fullWidth = true;
    above.left = pageLeft;
    above.right = pageRight;
    above.bottom = regionTop;
    above.top = pageTop;
    // 注意：上面这条带**不能**在这里就 append 进 bands——
    // 它的行是下面派发时才填进去的，先 append 进去的是一份**空副本**，
    // 页面上方的通栏标题（横跨两栏的那种）会整段丢掉。

    QVector<TextBand> columnBands;
    for (int column = 0; column < columnCount; ++column) {
        TextBand band;
        band.fullWidth = false;
        band.left = column == 0 ? pageLeft : separatorX.at(column - 1);
        band.right = column == columnCount - 1 ? pageRight : separatorX.at(column);
        band.bottom = regionBottom;
        band.top = regionTop;
        columnBands.append(band);
    }

    TextBand below;
    below.fullWidth = true;
    below.left = pageLeft;
    below.right = pageRight;
    below.bottom = pageBottom;
    below.top = regionBottom;

    // 把行分派到各带。
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const RawRow& row = rows.at(rowIndex);
        if (splittable.at(rowIndex)) {
            const QVector<QVector<pdf::TextChar>>& segments = splitSegments.at(rowIndex);
            for (int segment = 0; segment < segments.size(); ++segment) {
                const QVector<pdf::TextChar>& charsInSegment = segments.at(segment);
                double left = 0.0;
                double right = 0.0;
                double center = 0.0;
                bool segmentFirst = true;
                for (const pdf::TextChar& character : charsInSegment) {
                    left = segmentFirst ? character.box.left() : qMin(left, character.box.left());
                    right = segmentFirst ? character.box.right() : qMax(right, character.box.right());
                    center = (character.box.left() + character.box.right()) * 0.5;
                    segmentFirst = false;
                }
                int column = 0;
                for (double x : separatorX) {
                    if (center > x) {
                        ++column;
                    }
                }
                RawRow piece;
                piece.chars = charsInSegment;
                piece.baseline = row.baseline;
                piece.fontSize = row.fontSize;
                piece.left = left;
                piece.right = right;
                columnBands[column].lines.append(toExportLine(piece));
            }
            continue;
        }
        // 没被拆的行：
        //   - 横跨空白带的（表格的行、通栏标题）：算通栏，不能归到某一栏，
        //     否则表格会被栏切开；
        //   - 完全落在某一栏里的（该栏的短行）：归到那一栏；
        //   - 区域之外的：一律算通栏。
        const double center = (row.left + row.right) * 0.5;
        bool crosses = false;
        for (double x : separatorX) {
            if (row.left < x && x < row.right) {
                crosses = true;
                break;
            }
        }
        const bool insideRegion = row.baseline <= regionTop && row.baseline >= regionBottom;
        if (!crosses && insideRegion) {
            int column = 0;
            for (double x : separatorX) {
                if (center > x) {
                    ++column;
                }
            }
            columnBands[column].lines.append(toExportLine(row));
            continue;
        }
        if (row.baseline > regionTop) {
            above.lines.append(toExportLine(row));
        } else {
            below.lines.append(toExportLine(row));
        }
    }

    bands.append(above);
    bands.append(columnBands);
    bands.append(below);

    // 空的带不留（留了会在文档里多出一个空段落）。
    QVector<TextBand> result;
    for (const TextBand& band : bands) {
        if (!band.lines.isEmpty()) {
            result.append(band);
        }
    }
    return result;
}

int bandIndexForPoint(const QVector<TextBand>& bands, double x, double y) {
    if (bands.isEmpty()) {
        return -1;
    }
    int verticalMatch = -1;
    int nearest = 0;
    double nearestDistance = std::numeric_limits<double>::max();
    for (int index = 0; index < bands.size(); ++index) {
        const TextBand& band = bands.at(index);
        const bool insideVertical = y >= band.bottom && y <= band.top;
        if (insideVertical) {
            if (x >= band.left && x <= band.right) {
                return index;
            }
            if (verticalMatch < 0) {
                verticalMatch = index;
            }
        }
        const double distance = y > band.top ? y - band.top : band.bottom - y;
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = index;
        }
    }
    return verticalMatch >= 0 ? verticalMatch : nearest;
}

QVector<TextExportLine> buildTextLines(const pdf::TextPage& text) {
    QVector<TextExportLine> lines;
    for (const TextBand& band : buildTextBands(text)) {
        lines.append(band.lines);
    }
    return lines;
}

// 只有文字的页 → 导出用的页内容。
QVector<OfficePageContent> contentsFromLines(const QVector<QVector<TextExportLine>>& pageLines) {
    QVector<OfficePageContent> contents;
    contents.reserve(pageLines.size());
    for (const QVector<TextExportLine>& lines : pageLines) {
        OfficePageContent content;
        TextBand band;
        band.fullWidth = true;
        band.lines = lines;
        for (const TextExportLine& line : lines) {
            band.left = band.lines.isEmpty() ? line.left : qMin(band.left, line.left);
            band.right = qMax(band.right, line.right);
            band.bottom = band.lines.isEmpty() ? line.bottom : qMin(band.bottom, line.bottom);
            band.top = qMax(band.top, line.top);
        }
        content.bands.append(band);
        contents.append(content);
    }
    return contents;
}

ExportCapability exportCapability(ExportFormat format) {
    ExportCapability capability;
    switch (format) {
    case ExportFormat::WordOpenXml:
        capability.keepsText = true;
        capability.keepsLayout = false;
        // DOCX 能带图片与表格：图片是包里的独立部件（word/media/*），
        // 由正文里的 <w:drawing> 引用；表格是真正的 <w:tbl>。
        capability.keepsImages = true;
        capability.keepsTables = true;
        capability.limitation = QStringLiteral(
            "保留文字、图片与表格结构；精确版式、批注与字体样式会简化。");
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
        const QVector<OfficePageContent> contents =
            contentsFromLines(QVector<QVector<TextExportLine>>{buildTextLines(text)});
        result.bytes = exportWordOpenXml(contents, pageSizePt, office::MediaSet());
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
        result.bytes = exportWordOpenXml(contentsFromLines(pageLines), pageSizePt,
                                         office::MediaSet());
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

ExportResult exportPagesTo(ExportFormat format, const QVector<OfficePageContent>& contents,
                            const QSizeF& pageSizePt, const office::MediaSet& media) {
    ExportResult result;
    if (contents.isEmpty()) {
        result.error = QStringLiteral("导出失败：文档里没有可导出的内容。");
        return result;
    }
    switch (format) {
    case ExportFormat::WordOpenXml: {
        result.suggestedSuffix = QStringLiteral("docx");
        result.bytes = exportWordOpenXml(contents, pageSizePt, media);
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
        for (int pageIndex = 0; pageIndex < contents.size(); ++pageIndex) {
            if (pageIndex > 0) {
                TextExportLine separator;
                separator.text = QStringLiteral("\\page");
                // 原样输出：走 escapeRtf 会变成字面 "\page"，分页失效。
                separator.raw = true;
                merged.append(separator);
            }
            merged.append(contents.at(pageIndex).allLines());
        }
        result.bytes = exportRtf(merged);
        result.ok = true;
        break;
    }
    case ExportFormat::PlainText: {
        result.suggestedSuffix = QStringLiteral("txt");
        QVector<TextExportLine> merged;
        for (int pageIndex = 0; pageIndex < contents.size(); ++pageIndex) {
            if (pageIndex > 0) {
                TextExportLine separator;
                separator.text = QStringLiteral("\f");
                merged.append(separator);
            }
            merged.append(contents.at(pageIndex).allLines());
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

QVector<TextExportLine> OfficePageContent::allLines() const {
    QVector<TextExportLine> lines;
    for (const TextBand& band : bands) {
        lines.append(band.lines);
    }
    return lines;
}

}  // namespace aipdf::doc
