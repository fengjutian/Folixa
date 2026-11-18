#include "doc/export/OfdExport.h"

#include <QStringList>
#include <QtMath>

#include "doc/export/OfficeExport.h"
#include "doc/export/XmlText.h"
#include "doc/export/ZipWriter.h"

namespace aipdf::doc {

namespace {

// 一英寸 = 72 点 = 25.4 毫米。
constexpr double kPointsToMillimeters = 25.4 / 72.0;

// OFD 的命名空间。
const QString kOfdNamespace = QStringLiteral("http://www.ofdspec.org/2016");

// 数字转字符串：最多 3 位小数，去掉多余的 0。
//
// 为什么去掉 0：OFD 里的坐标是毫米，25.4 写成 "25.4" 比 "25.400000"
// 更像人写的，也方便人核对（测试里要断言 "25.4"）。
QString formatMillimeters(double value) {
    QString text = QString::number(value, 'f', 3);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    return text;
}

// XML 文本转义统一走 doc/export/XmlText.h：不要在本文件里再写一份。

// 文本对象的坐标换算。
//
// PDF 的坐标系是左下原点、y 向上；OFD 是左上原点、y 向下。
// 换算只有两处：x 直接乘比例，y 用「页高 - y」翻转。
struct PageGeometry {
    double widthMillimeters = 0.0;
    double heightMillimeters = 0.0;
    // 原页面盒子的左下角（PDF 点）。字符框是相对这个盒子的，
    // 不减掉它的话，页面原点不是 0 的文档会整体偏移。
    double originLeftPoints = 0.0;
    double originBottomPoints = 0.0;

    double toMillimetersX(double points) const {
        return (points - originLeftPoints) * kPointsToMillimeters;
    }

    // PDF 的 y（自下而上）→ OFD 的 y（自上而下）。
    double toMillimetersYFromTop(double points) const {
        return heightMillimeters - (points - originBottomPoints) * kPointsToMillimeters;
    }
};

PageGeometry makeGeometry(const pdf::TextPage& text, const QSizeF& pageSizePt) {
    PageGeometry geometry;
    const double widthPoints = pageSizePt.width() > 0.0 ? pageSizePt.width() : 595.0;
    const double heightPoints = pageSizePt.height() > 0.0 ? pageSizePt.height() : 842.0;
    geometry.widthMillimeters = widthPoints * kPointsToMillimeters;
    geometry.heightMillimeters = heightPoints * kPointsToMillimeters;
    geometry.originLeftPoints = text.contentBox.left;
    geometry.originBottomPoints = text.contentBox.bottom;
    return geometry;
}

// 一行的文本对象（XML 片段）。
QString buildTextObject(int objectId, const TextExportLine& line, const PageGeometry& geometry) {
    const double leftMm = geometry.toMillimetersX(line.left);
    const double rightMm = geometry.toMillimetersX(line.right);
    const double topMm = geometry.toMillimetersYFromTop(line.top);
    const double bottomMm = geometry.toMillimetersYFromTop(line.bottom);
    const double widthMm = qMax(rightMm - leftMm, 0.0);
    const double heightMm = qMax(bottomMm - topMm, 0.0);
    const double fontSizeMm = qMax(line.fontSize * kPointsToMillimeters, 0.1);

    // 每个字符前进多少（毫米）：用字符自身的宽度，
    // 这样出来的效果和原页面上的字距一致。
    QStringList deltas;
    deltas.reserve(line.chars.size());
    for (const pdf::TextChar& character : line.chars) {
        deltas.append(formatMillimeters(character.box.width() * kPointsToMillimeters));
    }

    QString out;
    out.append(QStringLiteral("<ofd:TextObject ID=\"%1\" Boundary=\"%2 %3 %4 %5\" Font=\"1\" Size=\"%6\">")
                   .arg(objectId)
                   .arg(formatMillimeters(leftMm))
                   .arg(formatMillimeters(topMm))
                   .arg(formatMillimeters(widthMm))
                   .arg(formatMillimeters(heightMm))
                   .arg(formatMillimeters(fontSizeMm)));
    // TextCode 的 X/Y 相对文本对象自身的原点（左上角），
    // Y 是基线位置：近似取行底（PDFium 给的是字形包围盒，不是基线）。
    out.append(QStringLiteral("<ofd:TextCode X=\"0\" Y=\"%1\" DeltaX=\"%2\">%3</ofd:TextCode>")
                   .arg(formatMillimeters(heightMm))
                   .arg(deltas.join(QLatin1Char(' ')))
                   .arg(escapeOoxmlText(line.text)));
    out.append(QStringLiteral("</ofd:TextObject>"));
    return out;
}

// 一页的 Content.xml。
QByteArray buildContentXml(const QVector<TextExportLine>& lines, const PageGeometry& geometry) {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    out.append(QStringLiteral("<ofd:Page xmlns:ofd=\"%1\">").arg(kOfdNamespace));
    out.append(QStringLiteral("<ofd:Area><ofd:PhysicalBox>0 0 %1 %2</ofd:PhysicalBox></ofd:Area>")
                   .arg(formatMillimeters(geometry.widthMillimeters))
                   .arg(formatMillimeters(geometry.heightMillimeters)));
    out.append(QStringLiteral("<ofd:Content><ofd:Layer ID=\"1\">"));
    int objectId = 1;
    for (const TextExportLine& line : lines) {
        if (line.text.isEmpty()) {
            continue;
        }
        out.append(buildTextObject(objectId, line, geometry));
        ++objectId;
    }
    out.append(QStringLiteral("</ofd:Layer></ofd:Content></ofd:Page>"));
    return out.toUtf8();
}

// 文档级描述 OFD.xml。
QByteArray buildOfdXml(const OfdExportOptions& options, const QString& documentId) {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    out.append(QStringLiteral("<ofd:OFD xmlns:ofd=\"%1\" DocType=\"OFD\" Version=\"1.1\">").arg(kOfdNamespace));
    out.append(QStringLiteral("<ofd:DocBody>"));
    out.append(QStringLiteral("<ofd:DocInfo>"));
    out.append(QStringLiteral("<ofd:DocID>%1</ofd:DocID>").arg(escapeOoxmlText(documentId)));
    out.append(QStringLiteral("<ofd:Title>%1</ofd:Title>").arg(escapeOoxmlText(options.title)));
    out.append(QStringLiteral("<ofd:Author>%1</ofd:Author>").arg(escapeOoxmlText(options.author)));
    out.append(QStringLiteral("<ofd:Creator>AI PDF Editor</ofd:Creator>"));
    out.append(QStringLiteral("</ofd:DocInfo>"));
    out.append(QStringLiteral("<ofd:DocRoot>Doc_0/Document.xml</ofd:DocRoot>"));
    out.append(QStringLiteral("</ofd:DocBody></ofd:OFD>"));
    return out.toUtf8();
}

// 文档描述 Document.xml。
QByteArray buildDocumentXml(int pageCount, const PageGeometry& geometry) {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    out.append(QStringLiteral("<ofd:Document xmlns:ofd=\"%1\">").arg(kOfdNamespace));
    out.append(QStringLiteral("<ofd:CommonData>"));
    out.append(QStringLiteral("<ofd:MaxUnitID>999</ofd:MaxUnitID>"));
    out.append(QStringLiteral("<ofd:PageArea><ofd:PhysicalBox>0 0 %1 %2</ofd:PhysicalBox></ofd:PageArea>")
                   .arg(formatMillimeters(geometry.widthMillimeters))
                   .arg(formatMillimeters(geometry.heightMillimeters)));
    out.append(QStringLiteral("<ofd:PublicRes>Doc_0/Res/PublicRes.xml</ofd:PublicRes>"));
    out.append(QStringLiteral("</ofd:CommonData>"));
    out.append(QStringLiteral("<ofd:Pages>"));
    for (int i = 0; i < pageCount; ++i) {
        // 页 ID 从 1 开始，路径里的页目录从 0 开始：两个数不是一回事，
        // 写错了页面内容就找不到（Word 里表现为「页是空的」）。
        out.append(QStringLiteral("<ofd:Page ID=\"%1\" BaseLoc=\"Pages/Page_%2/Content.xml\"/>")
                       .arg(i + 1)
                       .arg(i));
    }
    out.append(QStringLiteral("</ofd:Pages></ofd:Document>"));
    return out.toUtf8();
}

// 公共资源：只声明一个字体（文本对象的 Font 属性引用的就是它）。
QByteArray buildPublicResXml() {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    out.append(QStringLiteral("<ofd:Res xmlns:ofd=\"%1\" BaseLoc=\"\">").arg(kOfdNamespace));
    out.append(QStringLiteral("<ofd:Fonts>"));
    out.append(QStringLiteral("<ofd:Font ID=\"1\" FontName=\"SimSun\" FamilyName=\"SimSun\" "
                              "Bold=\"false\" Italic=\"false\" FixedWidth=\"false\"/>"));
    out.append(QStringLiteral("</ofd:Fonts></ofd:Res>"));
    return out.toUtf8();
}

// 文档 ID：用页面文字的 CRC-32 拼一个固定的 UUID 形状。
//
// 为什么不用随机数：OFD 的 DocID 应当能标识"这份文档"，同一份内容
// 每次导出必须得到同一个 ID，否则没法判断两份包是不是同一份文档。
QString makeDocumentId(const QVector<pdf::TextPage>& pages) {
    QByteArray material;
    for (const pdf::TextPage& page : pages) {
        material.append(page.plainText().toUtf8());
        material.append('\n');
    }
    const quint32 checksum = zip::crc32(material);
    const QString hex = QStringLiteral("%1").arg(checksum, 8, 16, QLatin1Char('0'));
    // 补成 32 位十六进制，再按 UUID 的形状分组。
    const QString padded = hex + QString(24, QLatin1Char('0'));
    return QStringLiteral("%1-%2-%3-%4-%5")
        .arg(padded.left(8), padded.mid(8, 4), padded.mid(12, 4), padded.mid(16, 4),
             padded.mid(20, 12));
}

}  // namespace

OfdExportResult exportPagesToOfd(const QVector<pdf::TextPage>& pages, const QSizeF& pageSizePt,
                                 const OfdExportOptions& options) {
    OfdExportResult result;
    if (pages.isEmpty()) {
        result.error = QStringLiteral("导出 OFD 失败：没有可导出的页面。");
        return result;
    }

    zip::Writer writer;
    writer.addFile(QStringLiteral("OFD.xml"),
                   buildOfdXml(options, makeDocumentId(pages)));
    writer.addFile(QStringLiteral("Doc_0/Document.xml"),
                   buildDocumentXml(pages.size(), makeGeometry(pages.first(), pageSizePt)));
    writer.addFile(QStringLiteral("Doc_0/Res/PublicRes.xml"), buildPublicResXml());

    for (int i = 0; i < pages.size(); ++i) {
        const QVector<TextExportLine> lines = buildTextLines(pages.at(i));
        const PageGeometry geometry = makeGeometry(pages.at(i), pageSizePt);
        writer.addFile(QStringLiteral("Doc_0/Pages/Page_%1/Content.xml").arg(i),
                       buildContentXml(lines, geometry));
    }

    result.bytes = writer.toByteArray();
    if (result.bytes.isEmpty()) {
        result.error = QStringLiteral("导出 OFD 失败：生成的压缩包为空。");
        return result;
    }
    result.ok = true;
    return result;
}

OfdExportResult exportPageToOfd(const pdf::TextPage& text, const QSizeF& pageSizePt,
                                const OfdExportOptions& options) {
    return exportPagesToOfd(QVector<pdf::TextPage>{text}, pageSizePt, options);
}

}  // namespace aipdf::doc
