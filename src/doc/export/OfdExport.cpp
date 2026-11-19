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

// 资源 ID 的起点。
//
// 为什么资源 ID 要另起一段：OFD 的资源（图片）与页面图元共用一个 ID 空间
// （Document.xml 里的 MaxUnitID 就是整个文档的最大 ID），撞号会让阅读器
// 把图片认成图元。页面图元的 ID 从 1 开始按页排，资源 ID 从 100 起。
constexpr int kFirstResourceId = 100;

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

// PDF 用户空间的一点 → OFD 页面空间（毫米、左上原点、y 向下）的一点。
//
// 为什么要翻转：OFD 的页面空间原点是**左上角**（GB/T 33190-2016），
// PDF 用户空间的原点在左下角。同一份 OFD 里文字与图形必须用同一套坐标，
// 否则图形会与文字上下颠倒——本文件的文字对象早就是按 OFD 口径翻的
// （见 PageGeometry::toMillimetersYFromTop），图形必须跟它一致。
QPointF toPagePoint(const QPointF& point, const PageGeometry& geometry) {
    return QPointF(geometry.toMillimetersX(point.x()),
                   geometry.toMillimetersYFromTop(point.y()));
}

// OFD 的颜色是 0..255 的整数三元组（RGB）。
QString formatColor(const pdf::GraphicsRgb& color) {
    const int red = qBound(0, qRound(color.r * 255.0), 255);
    const int green = qBound(0, qRound(color.g * 255.0), 255);
    const int blue = qBound(0, qRound(color.b * 255.0), 255);
    return QStringLiteral("%1 %2 %3").arg(red).arg(green).arg(blue);
}

// 一条路径在 OFD 页面空间里的形态（毫米、左上原点、y 向下）。
struct PagePath {
    // 路径描述（AbbreviatedData 的内容）。
    QString data;
    // 包围盒 "x y w h"（页面空间，毫米）。
    QString boundary;
    // 描边色 / 填充色（"r g b"）。不描边 / 不填充时为空串。
    QString strokeColor;
    QString fillColor;
    // 线宽（毫米）。
    double lineWidthMillimeters = 0.0;
    bool filled = false;
    bool stroked = false;
};

// 一条路径里的全部点（含贝塞尔的控制点）——算包围盒要用。
QVector<QPointF> pathPoints(const pdf::GraphicsPath& path, const PageGeometry& geometry) {
    QVector<QPointF> points;
    for (const pdf::GraphicsSegment& segment : path.segments) {
        switch (segment.verb) {
        case pdf::GraphicsVerb::Move:
        case pdf::GraphicsVerb::Line:
            points.append(toPagePoint(segment.p1, geometry));
            break;
        case pdf::GraphicsVerb::Curve:
            points.append(toPagePoint(segment.p1, geometry));
            points.append(toPagePoint(segment.p2, geometry));
            points.append(toPagePoint(segment.p3, geometry));
            break;
        case pdf::GraphicsVerb::Close:
            break;
        }
    }
    return points;
}

// 把一条 PDF 路径转成 OFD 的路径对象数据。
//
// 路径描述用的操作符与国标 GB/T 33190-2016 的表 35/36 一致
// （参考实现：ofdrw 的 AbbreviatedData）：
//   M x y                移动到
//   L x y                直线到
//   B x1 y1 x2 y2 x3 y3  三次贝塞尔
//   C                    闭合子路径
//
// 两个容易踩的坑：
//   1. OFD 里三次贝塞尔是 **B**，不是 C；**C 是「闭合」**（不是曲线）。
//      照 SVG 的习惯写 C，闭合会变成三次贝塞尔、曲线会变成闭合。
//   2. 图元坐标是**相对 Boundary 左上角**的偏移，不是页面绝对坐标：
//      页面位置 = Boundary 左上角 + CTM × 图元坐标
//      （见 ofdrw 的 PointUtil::adjustPos）。
//
// 为什么贝塞尔不拆成折线：拆了以后曲线在放大后能看出折角，
// 而且控制点信息就永久丢了——OFD 本来就能表达三次贝塞尔，没有理由退化。
PagePath buildPagePath(const pdf::GraphicsPath& path, const PageGeometry& geometry) {
    PagePath result;
    const QVector<QPointF> points = pathPoints(path, geometry);
    if (points.isEmpty()) {
        return result;
    }
    double minX = points.first().x();
    double maxX = minX;
    double minY = points.first().y();
    double maxY = minY;
    for (const QPointF& point : points) {
        minX = qMin(minX, point.x());
        maxX = qMax(maxX, point.x());
        minY = qMin(minY, point.y());
        maxY = qMax(maxY, point.y());
    }

    QStringList data;
    for (const pdf::GraphicsSegment& segment : path.segments) {
        // 路径必须从「移动」开始：畸形内容流里出现过以 l 开头的路径，
        // 那样写出来的 OFD 路径数据没有起点，阅读器只能从 (0,0) 猜。
        if (data.isEmpty() && segment.verb != pdf::GraphicsVerb::Move) {
            const QPointF start = toPagePoint(segment.p0, geometry);
            data.append(QStringLiteral("M %1 %2")
                            .arg(formatMillimeters(start.x() - minX),
                                 formatMillimeters(start.y() - minY)));
        }
        switch (segment.verb) {
        case pdf::GraphicsVerb::Move: {
            const QPointF point = toPagePoint(segment.p1, geometry);
            data.append(QStringLiteral("M %1 %2")
                            .arg(formatMillimeters(point.x() - minX),
                                 formatMillimeters(point.y() - minY)));
            break;
        }
        case pdf::GraphicsVerb::Line: {
            const QPointF point = toPagePoint(segment.p1, geometry);
            data.append(QStringLiteral("L %1 %2")
                            .arg(formatMillimeters(point.x() - minX),
                                 formatMillimeters(point.y() - minY)));
            break;
        }
        case pdf::GraphicsVerb::Curve: {
            const QPointF control1 = toPagePoint(segment.p1, geometry);
            const QPointF control2 = toPagePoint(segment.p2, geometry);
            const QPointF end = toPagePoint(segment.p3, geometry);
            data.append(QStringLiteral("B %1 %2 %3 %4 %5 %6")
                            .arg(formatMillimeters(control1.x() - minX),
                                 formatMillimeters(control1.y() - minY),
                                 formatMillimeters(control2.x() - minX),
                                 formatMillimeters(control2.y() - minY),
                                 formatMillimeters(end.x() - minX),
                                 formatMillimeters(end.y() - minY)));
            break;
        }
        case pdf::GraphicsVerb::Close:
            data.append(QStringLiteral("C"));
            break;
        }
    }

    result.data = data.join(QLatin1Char(' '));
    result.boundary = QStringLiteral("%1 %2 %3 %4")
                          .arg(formatMillimeters(minX), formatMillimeters(minY),
                               formatMillimeters(maxX - minX), formatMillimeters(maxY - minY));
    result.filled = path.filled;
    result.stroked = path.stroked;
    if (path.stroked) {
        result.strokeColor = formatColor(path.strokeColor);
    }
    if (path.filled) {
        result.fillColor = formatColor(path.fillColor);
    }
    result.lineWidthMillimeters = qMax(0.0, path.lineWidth * kPointsToMillimeters);
    return result;
}

// 一张内嵌位图在 OFD 页面空间里的摆放。
struct PageImage {
    // 摆放矩形 "x y w h"（页面空间，毫米）。
    QString boundary;
    // 图像对象的 CTM：把单位正方形拉成摆放矩形。
    QString ctm;
};

// 把一张位图转成 OFD 图像对象的摆放信息。
//
// 图片在 PDF 里是画在单位正方形里的（[0,1]×[0,1]），摆位由 CTM 决定；
// OFD 的图像对象用同样的约定：CTM 把单位正方形映射成 Boundary 那块矩形。
PageImage buildPageImage(const pdf::GraphicsImage& image, const PageGeometry& geometry) {
    PageImage result;
    // 图片矩形的约定与文本层的字符框一致：top() 是纵坐标小的那条边（PDF 里偏下），
    // bottom() 是大的那条边（偏上）。翻到 OFD 的 y 向下之后，上下正好对调。
    const double left = geometry.toMillimetersX(image.rect.left());
    const double right = geometry.toMillimetersX(image.rect.right());
    const double top = geometry.toMillimetersYFromTop(image.rect.bottom());
    const double bottom = geometry.toMillimetersYFromTop(image.rect.top());
    const double width = qMax(0.0, right - left);
    const double height = qMax(0.0, bottom - top);
    result.boundary = QStringLiteral("%1 %2 %3 %4")
                           .arg(formatMillimeters(left), formatMillimeters(top),
                                formatMillimeters(width), formatMillimeters(height));
    result.ctm = QStringLiteral("%1 0 0 %2 0 0")
                     .arg(formatMillimeters(width), formatMillimeters(height));
    return result;
}

// 路径对象的 XML。
QString buildPathObject(int objectId, const PagePath& path) {
    if (path.data.isEmpty()) {
        return {};
    }
    QString out;
    out.append(QStringLiteral("<ofd:PathObject ID=\"%1\" Boundary=\"%2\" CTM=\"1 0 0 1 0 0\" ")
                   .arg(objectId)
                   .arg(path.boundary));
    out.append(QStringLiteral("LineWidth=\"%1\" Stroke=\"%2\" Fill=\"%3\" Rule=\"NonZero\">")
                   .arg(formatMillimeters(path.lineWidthMillimeters))
                   .arg(path.stroked ? QStringLiteral("true") : QStringLiteral("false"))
                   .arg(path.filled ? QStringLiteral("true") : QStringLiteral("false")));
    // 元素的顺序按国标的 XSD：StrokeColor → FillColor → AbbreviatedData。
    if (!path.strokeColor.isEmpty()) {
        out.append(QStringLiteral("<ofd:StrokeColor Value=\"%1\"/>").arg(path.strokeColor));
    }
    if (!path.fillColor.isEmpty()) {
        out.append(QStringLiteral("<ofd:FillColor Value=\"%1\"/>").arg(path.fillColor));
    }
    out.append(QStringLiteral("<ofd:AbbreviatedData>%1</ofd:AbbreviatedData>").arg(path.data));
    out.append(QStringLiteral("</ofd:PathObject>"));
    return out;
}

// 图像对象的 XML。
QString buildImageObject(int objectId, int resourceId, const PageImage& image) {
    QString out;
    out.append(QStringLiteral("<ofd:ImageObject ID=\"%1\" Boundary=\"%2\" CTM=\"%3\" ResourceID=\"%4\"/>")
                   .arg(objectId)
                   .arg(image.boundary)
                   .arg(image.ctm)
                   .arg(resourceId));
    return out;
}

// 一页里的图片：资源（ID、文件名、字节）与摆放位置放在一起，
// 保证「图像对象引用的资源」与「包里的图片文件」永远一一对应。
//
// 为什么放在一起：早先分两个数组（资源一个、摆放一个），
// 遇到「格式认不出来的图片要跳过」时两个数组就错位了——
// 图像对象引的资源号与包里的图片文件对不上，图就显示不出来，
// 而且导出过程一声不响。
struct PageImageEntry {
    int resourceId = 0;
    // 资源表里写的文件名（相对资源目录）。
    QString fileName;
    // 图片格式：png / jpg。
    QString format;
    // 图片字节（PNG 或 JPEG，原样放进包里）。
    QByteArray data;
    // 摆放位置。
    PageImage placement;
};

// 图片的 MIME → OFD 的格式名与文件后缀。
//
// 为什么只认这两种：图形提取层只会给出 PNG 或 JPEG 这两种字节
// （其它格式在那一层已经解不出来了），这里只是把名字对上。
bool imageFormatOf(const QString& mime, QString* format, QString* suffix) {
    if (mime == QStringLiteral("image/png")) {
        *format = QStringLiteral("png");
        *suffix = QStringLiteral("png");
        return true;
    }
    if (mime == QStringLiteral("image/jpeg")) {
        *format = QStringLiteral("jpg");
        *suffix = QStringLiteral("jpg");
        return true;
    }
    return false;
}

// 页面资源文件：图片在这里登记（MultiMedia），图像对象按 ID 引用它。
QByteArray buildPageResXml(const QVector<PageImageEntry>& images) {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    // BaseLoc 是资源目录（相对本文件所在目录）：媒体文件放在同名子目录里。
    out.append(QStringLiteral("<ofd:Res xmlns:ofd=\"%1\" BaseLoc=\"Res_0\">").arg(kOfdNamespace));
    out.append(QStringLiteral("<ofd:MultiMedias>"));
    for (const PageImageEntry& image : images) {
        out.append(QStringLiteral("<ofd:MultiMedia ID=\"%1\" Type=\"Image\" Format=\"%2\">")
                       .arg(image.resourceId)
                       .arg(image.format));
        out.append(QStringLiteral("<ofd:MediaFile>%1</ofd:MediaFile>").arg(image.fileName));
        out.append(QStringLiteral("</ofd:MultiMedia>"));
    }
    out.append(QStringLiteral("</ofd:MultiMedias></ofd:Res>"));
    return out.toUtf8();
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
//
// 图层的绘制顺序：路径 → 图片 → 文字。
//
// 为什么文字放最后：OFD 的图层是自下而上画的，后写的盖在先写的上面。
// PDF 的内容流里文字与图形是交错的，那个顺序在导出时已经丢了；
// 把文字放在最上面至少保证文字不会被图形盖住（盖住了就是「看不见的字」）。
QByteArray buildContentXml(const QVector<TextExportLine>& lines, const QVector<PagePath>& paths,
                            const QVector<PageImageEntry>& images, const PageGeometry& geometry) {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    out.append(QStringLiteral("<ofd:Page xmlns:ofd=\"%1\">").arg(kOfdNamespace));
    // 页面资源（有图片才有）：国标里 PageRes 必须排在 Area 前面。
    if (!images.isEmpty()) {
        out.append(QStringLiteral("<ofd:PageRes>Res/Res_0.xml</ofd:PageRes>"));
    }
    out.append(QStringLiteral("<ofd:Area><ofd:PhysicalBox>0 0 %1 %2</ofd:PhysicalBox></ofd:Area>")
                   .arg(formatMillimeters(geometry.widthMillimeters))
                   .arg(formatMillimeters(geometry.heightMillimeters)));
    out.append(QStringLiteral("<ofd:Content><ofd:Layer ID=\"1\">"));
    int objectId = 1;
    for (const PagePath& path : paths) {
        const QString xml = buildPathObject(objectId, path);
        if (xml.isEmpty()) {
            continue;
        }
        out.append(xml);
        ++objectId;
    }
    for (const PageImageEntry& image : images) {
        out.append(buildImageObject(objectId, image.resourceId, image.placement));
        ++objectId;
    }
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
QByteArray buildDocumentXml(int pageCount, const PageGeometry& geometry, int maxUnitId) {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    out.append(QStringLiteral("<ofd:Document xmlns:ofd=\"%1\">").arg(kOfdNamespace));
    out.append(QStringLiteral("<ofd:CommonData>"));
    out.append(QStringLiteral("<ofd:MaxUnitID>%1</ofd:MaxUnitID>").arg(maxUnitId));
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

OfdExportResult exportPagesToOfd(const QVector<OfdPageContent>& pages, const QSizeF& pageSizePt,
                                 const OfdExportOptions& options) {
    OfdExportResult result;
    if (pages.isEmpty()) {
        result.error = QStringLiteral("导出 OFD 失败：没有可导出的页面。");
        return result;
    }

    QVector<pdf::TextPage> texts;
    texts.reserve(pages.size());
    for (const OfdPageContent& page : pages) {
        texts.append(page.text);
    }

    // 先把每一页的图片资源分配好（资源 ID 整篇文档连续，MaxUnitID 跟着它涨）。
    int nextResourceId = kFirstResourceId;
    int maxUnitId = 999;
    QVector<QVector<PageImageEntry>> pageImages;
    pageImages.reserve(pages.size());
    for (const OfdPageContent& page : pages) {
        const PageGeometry geometry = makeGeometry(page.text, pageSizePt);
        QVector<PageImageEntry> images;
        for (const pdf::GraphicsImage& image : page.graphics.images) {
            QString format;
            QString suffix;
            if (image.data.isEmpty() || !imageFormatOf(image.mime, &format, &suffix)) {
                // 认不出来的格式不写：写一个格式对不上的图片进去，
                // 阅读器会把它当成坏图（表现为「整块不显示」）。
                continue;
            }
            PageImageEntry entry;
            entry.resourceId = nextResourceId++;
            entry.format = format;
            entry.fileName = QStringLiteral("Image_%1.%2").arg(entry.resourceId).arg(suffix);
            entry.data = image.data;
            entry.placement = buildPageImage(image, geometry);
            images.append(entry);
        }
        maxUnitId = qMax(maxUnitId, nextResourceId - 1);
        pageImages.append(images);
    }

    zip::Writer writer;
    writer.addFile(QStringLiteral("OFD.xml"),
                   buildOfdXml(options, makeDocumentId(texts)));
    writer.addFile(QStringLiteral("Doc_0/Document.xml"),
                   buildDocumentXml(pages.size(), makeGeometry(pages.first().text, pageSizePt),
                                    maxUnitId));
    writer.addFile(QStringLiteral("Doc_0/Res/PublicRes.xml"), buildPublicResXml());

    for (int i = 0; i < pages.size(); ++i) {
        const OfdPageContent& page = pages.at(i);
        const PageGeometry geometry = makeGeometry(page.text, pageSizePt);
        const QVector<PageImageEntry>& images = pageImages.at(i);

        // 图片资源先写进包：图像对象按 ID 引用它们，资源缺了图就没了。
        if (!images.isEmpty()) {
            writer.addFile(QStringLiteral("Doc_0/Pages/Page_%1/Res/Res_0.xml").arg(i),
                           buildPageResXml(images));
            for (const PageImageEntry& image : images) {
                writer.addFile(
                    QStringLiteral("Doc_0/Pages/Page_%1/Res/Res_0/%2").arg(i).arg(image.fileName),
                    image.data);
            }
        }

        QVector<PagePath> paths;
        paths.reserve(page.graphics.paths.size());
        for (const pdf::GraphicsPath& path : page.graphics.paths) {
            const PagePath converted = buildPagePath(path, geometry);
            if (converted.data.isEmpty()) {
                continue;
            }
            paths.append(converted);
        }

        const QVector<TextExportLine> lines = buildTextLines(page.text);
        writer.addFile(QStringLiteral("Doc_0/Pages/Page_%1/Content.xml").arg(i),
                       buildContentXml(lines, paths, images, geometry));

        result.pathCount += paths.size();
        result.imageCount += images.size();
    }

    result.bytes = writer.toByteArray();
    if (result.bytes.isEmpty()) {
        result.error = QStringLiteral("导出 OFD 失败：生成的压缩包为空。");
        return result;
    }
    result.ok = true;
    return result;
}

OfdExportResult exportPagesToOfd(const QVector<pdf::TextPage>& pages, const QSizeF& pageSizePt,
                                 const OfdExportOptions& options) {
    QVector<OfdPageContent> contents;
    contents.reserve(pages.size());
    for (const pdf::TextPage& page : pages) {
        OfdPageContent content;
        content.text = page;
        contents.append(content);
    }
    return exportPagesToOfd(contents, pageSizePt, options);
}

OfdExportResult exportPageToOfd(const OfdPageContent& content, const QSizeF& pageSizePt,
                                const OfdExportOptions& options) {
    return exportPagesToOfd(QVector<OfdPageContent>{content}, pageSizePt, options);
}

OfdExportResult exportPageToOfd(const pdf::TextPage& text, const QSizeF& pageSizePt,
                                const OfdExportOptions& options) {
    return exportPagesToOfd(QVector<pdf::TextPage>{text}, pageSizePt, options);
}

}  // namespace aipdf::doc
