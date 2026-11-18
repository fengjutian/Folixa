#include "doc/export/PptxWriter.h"

#include <QRectF>

#include "doc/export/XmlText.h"
#include "doc/export/ZipWriter.h"

namespace aipdf::doc::pptx {

namespace {

using aipdf::doc::escapeOoxmlText;

// 1 英寸 = 914400 EMU（English Metric Unit）。
constexpr int kEmuPerInch = 914400;

// 1 点 = 12700 EMU。
constexpr double kEmuPerPoint = 12700.0;

// 16:9 幻灯片：13.333 × 7.5 英寸。
constexpr int kSlideWidth = 12192000;
constexpr int kSlideHeight = 6858000;

// 标题框与正文框的位置与大小（EMU）。
constexpr int kTitleX = 838200;   // 0.63 英寸
constexpr int kTitleY = 457200;   // 0.375 英寸
constexpr int kTitleWidth = 10515600;
constexpr int kTitleHeight = 1143000;

constexpr int kBodyX = 838200;
constexpr int kBodyY = 1825625;
constexpr int kBodyWidth = 10515600;
constexpr int kBodyHeight = 4114800;

constexpr auto kNsA = "http://schemas.openxmlformats.org/drawingml/2006/main";
constexpr auto kNsR = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr auto kNsP = "http://schemas.openxmlformats.org/presentationml/2006/main";

QString header() {
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
}

QByteArray contentTypesXml(int slideCount, int notesCount, const office::MediaSet& media) {
    QString out = header();
    out.append(QStringLiteral(
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" "
        "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"));
    // 图片的默认内容类型：少了这一项，PowerPoint 不知道 png 是什么，
    // 打开时会直接报「文件已损坏」。
    for (const QPair<QString, QString>& entry : media.contentTypeDefaults()) {
        out.append(QStringLiteral("<Default Extension=\"%1\" ContentType=\"%2\"/>")
                       .arg(entry.first, entry.second));
    }
    out.append(QStringLiteral(
        "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
        "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
        "<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.presentationml.slideLayout+xml\"/>"
        "<Override PartName=\"/ppt/notesMasters/notesMaster1.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.presentationml.notesMaster+xml\"/>"
        "<Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.theme+xml\"/>"));
    for (int i = 1; i <= slideCount; ++i) {
        out.append(QStringLiteral(
            "<Override PartName=\"/ppt/slides/slide%1.xml\" ContentType=\"application/vnd."
            "openxmlformats-officedocument.presentationml.slide+xml\"/>").arg(i));
    }
    for (int i = 1; i <= notesCount; ++i) {
        out.append(QStringLiteral(
            "<Override PartName=\"/ppt/notesSlides/notesSlide%1.xml\" ContentType=\"application/vnd."
            "openxmlformats-officedocument.presentationml.notesSlide+xml\"/>").arg(i));
    }
    out.append(QStringLiteral(
        "<Override PartName=\"/docProps/core.xml\" "
        "ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
        "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.extended-properties+xml\"/>"
        "</Types>"));
    return out.toUtf8();
}

QByteArray packageRelsXml() {
    QString out = header();
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"ppt/presentation.xml\"/>"
        "<Relationship Id=\"rId2\" "
        "Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" "
        "Target=\"docProps/core.xml\"/>"
        "<Relationship Id=\"rId3\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" "
        "Target=\"docProps/app.xml\"/>"
        "</Relationships>"));
    return out.toUtf8();
}

QByteArray corePropsXml(const QString& title) {
    QString out = header();
    out.append(QStringLiteral(
        "<cp:coreProperties "
        "xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
        "<dc:title>%1</dc:title>"
        "<dc:creator>Folixa</dc:creator>"
        "<cp:lastModifiedBy>Folixa</cp:lastModifiedBy>"
        "</cp:coreProperties>").arg(escapeOoxmlText(title)));
    return out.toUtf8();
}

QByteArray appPropsXml(int slideCount) {
    QString out = header();
    out.append(QStringLiteral(
        "<Properties "
        "xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
        "<Application>Folixa</Application>"
        "<Slides>%1</Slides>"
        "</Properties>").arg(slideCount));
    return out.toUtf8();
}

QByteArray presentationXml(int slideCount, int notesCount) {
    QString out = header();
    out.append(QStringLiteral(
        "<p:presentation xmlns:a=\"%1\" xmlns:r=\"%2\" xmlns:p=\"%3\">"
        "<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>")
                   .arg(kNsA, kNsR, kNsP));
    if (notesCount > 0) {
        // 备注母版必须被 presentation 引用，否则 notesMaster 与 notesSlide
        // 都是孤儿部件：PowerPoint 打开后备注窗格为空。
        // 关系编号与 presentationRelsXml 保持一致：主题 = slideCount+2，
        // 备注母版 = slideCount+3。
        out.append(QStringLiteral(
            "<p:notesMasterIdLst><p:notesMasterId r:id=\"rId%1\"/></p:notesMasterIdLst>")
                       .arg(slideCount + 3));
    }
    out.append(QStringLiteral("<p:sldIdLst>"));
    // sldId 从 256 起：小于它的编号是保留的。
    for (int i = 0; i < slideCount; ++i) {
        out.append(QStringLiteral("<p:sldId id=\"%1\" r:id=\"rId%2\"/>")
                       .arg(256 + i)
                       .arg(i + 2));
    }
    out.append(QStringLiteral(
        "</p:sldIdLst>"
        "<p:sldSz cx=\"%1\" cy=\"%2\"/>"
        "<p:notesSz cx=\"6858000\" cy=\"9144000\"/>"
        "</p:presentation>").arg(kSlideWidth).arg(kSlideHeight));
    return out.toUtf8();
}

QByteArray presentationRelsXml(int slideCount) {
    QString out = header();
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster\" "
        "Target=\"slideMasters/slideMaster1.xml\"/>"));
    for (int i = 0; i < slideCount; ++i) {
        out.append(QStringLiteral(
            "<Relationship Id=\"rId%1\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" "
            "Target=\"slides/slide%2.xml\"/>").arg(i + 2).arg(i + 1));
    }
    // 主题与备注母版的编号排在所有幻灯片之后。
    const int themeId = slideCount + 2;
    out.append(QStringLiteral(
        "<Relationship Id=\"rId%1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" "
        "Target=\"theme/theme1.xml\"/>").arg(themeId));
    out.append(QStringLiteral(
        "<Relationship Id=\"rId%1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesMaster\" "
        "Target=\"notesMasters/notesMaster1.xml\"/>").arg(themeId + 1));
    out.append(QStringLiteral("</Relationships>"));
    return out.toUtf8();
}

QByteArray themeXml() {
    QString out = header();
    out.append(QStringLiteral("<a:theme xmlns:a=\"%1\" name=\"Folixa\"><a:themeElements>").arg(kNsA));
    out.append(QStringLiteral(
        "<a:clrScheme name=\"Folixa\">"
        "<a:dk1><a:sysClr val=\"windowText\" lastClr=\"000000\"/></a:dk1>"
        "<a:lt1><a:sysClr val=\"window\" lastClr=\"FFFFFF\"/></a:lt1>"
        "<a:dk2><a:srgbClr val=\"1F2A3A\"/></a:dk2>"
        "<a:lt2><a:srgbClr val=\"EEF2F7\"/></a:lt2>"
        "<a:accent1><a:srgbClr val=\"2F6FED\"/></a:accent1>"
        "<a:accent2><a:srgbClr val=\"1FA37A\"/></a:accent2>"
        "<a:accent3><a:srgbClr val=\"E0742B\"/></a:accent3>"
        "<a:accent4><a:srgbClr val=\"8B5CF6\"/></a:accent4>"
        "<a:accent5><a:srgbClr val=\"0EA5A5\"/></a:accent5>"
        "<a:accent6><a:srgbClr val=\"E05263\"/></a:accent6>"
        "<a:hlink><a:srgbClr val=\"0563C1\"/></a:hlink>"
        "<a:folHlink><a:srgbClr val=\"954F72\"/></a:folHlink>"
        "</a:clrScheme>"));
    out.append(QStringLiteral(
        "<a:fontScheme name=\"Folixa\">"
        "<a:majorFont><a:latin typeface=\"Calibri Light\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont>"
        "<a:minorFont><a:latin typeface=\"Calibri\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont>"
        "</a:fontScheme>"));
    // fmtScheme：每张列表至少三项，少了 PowerPoint 会判文件损坏。
    out.append(QStringLiteral(
        "<a:fmtScheme name=\"Folixa\">"
        "<a:fillStyleLst>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "</a:fillStyleLst>"
        "<a:lnStyleLst>"
        "<a:ln w=\"6350\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\">"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>"
        "<a:ln w=\"12700\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\">"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>"
        "<a:ln w=\"19050\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\">"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>"
        "</a:lnStyleLst>"
        "<a:effectStyleLst>"
        "<a:effectStyle><a:effectLst/></a:effectStyle>"
        "<a:effectStyle><a:effectLst/></a:effectStyle>"
        "<a:effectStyle><a:effectLst/></a:effectStyle>"
        "</a:effectStyleLst>"
        "<a:bgFillStyleLst>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "</a:bgFillStyleLst>"
        "</a:fmtScheme>"));
    out.append(QStringLiteral("</a:themeElements></a:theme>"));
    return out.toUtf8();
}

// 形状树的空组属性（每个 spTree 都要有这一段）。
QString groupShapeProperties() {
    return QStringLiteral(
        "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
        "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/>"
        "<a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>");
}

QByteArray slideMasterXml() {
    QString out = header();
    out.append(QStringLiteral(
        "<p:sldMaster xmlns:a=\"%1\" xmlns:r=\"%2\" xmlns:p=\"%3\"><p:cSld>"
        "<p:bg><p:bgPr><a:solidFill><a:schemeClr val=\"bg1\"/></a:solidFill><a:effectLst/></p:bgPr></p:bg>"
        "<p:spTree>%4</p:spTree></p:cSld>"
        "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" "
        "accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" "
        "accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>"
        "<p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/></p:sldLayoutIdLst>"
        "<p:txStyles>"
        "<p:titleStyle><a:lvl1pPr algn=\"l\"><a:defRPr sz=\"3200\"/></a:lvl1pPr></p:titleStyle>"
        "<p:bodyStyle><a:lvl1pPr marL=\"285750\" indent=\"-285750\">"
        "<a:buChar char=\"•\"/></a:lvl1pPr></p:bodyStyle>"
        "<p:otherStyle><a:lvl1pPr><a:defRPr sz=\"1800\"/></a:lvl1pPr></p:otherStyle>"
        "</p:txStyles></p:sldMaster>").arg(kNsA, kNsR, kNsP, groupShapeProperties()));
    return out.toUtf8();
}

QByteArray slideMasterRelsXml() {
    QString out = header();
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" "
        "Target=\"../slideLayouts/slideLayout1.xml\"/>"
        "<Relationship Id=\"rId2\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" "
        "Target=\"../theme/theme1.xml\"/>"
        "</Relationships>"));
    return out.toUtf8();
}

QByteArray slideLayoutXml() {
    QString out = header();
    out.append(QStringLiteral(
        "<p:sldLayout xmlns:a=\"%1\" xmlns:r=\"%2\" xmlns:p=\"%3\" type=\"blank\" preserve=\"1\">"
        "<p:cSld name=\"Blank\"><p:spTree>%4</p:spTree></p:cSld>"
        "<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>"
        "</p:sldLayout>").arg(kNsA, kNsR, kNsP, groupShapeProperties()));
    return out.toUtf8();
}

QByteArray slideLayoutRelsXml() {
    QString out = header();
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster\" "
        "Target=\"../slideMasters/slideMaster1.xml\"/>"
        "</Relationships>"));
    return out.toUtf8();
}

QByteArray notesMasterXml() {
    QString out = header();
    out.append(QStringLiteral(
        "<p:notesMaster xmlns:a=\"%1\" xmlns:r=\"%2\" xmlns:p=\"%3\"><p:cSld>"
        "<p:spTree>%4</p:spTree></p:cSld>"
        "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" "
        "accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" "
        "accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>"
        "<p:notesStyle><a:lvl1pPr marL=\"0\" algn=\"l\" defTabSz=\"914400\" rtl=\"0\" "
        "eaLnBrk=\"1\" latinLnBrk=\"0\" hangingPunct=\"1\">"
        "<a:defRPr sz=\"1200\" kern=\"1200\"><a:solidFill><a:schemeClr val=\"tx1\"/></a:solidFill>"
        "<a:latin typeface=\"+mn-lt\"/><a:ea typeface=\"+mn-ea\"/><a:cs typeface=\"+mn-cs\"/>"
        "</a:defRPr></a:lvl1pPr></p:notesStyle>"
        "</p:notesMaster>").arg(kNsA, kNsR, kNsP, groupShapeProperties()));
    return out.toUtf8();
}

QByteArray notesMasterRelsXml() {
    QString out = header();
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" "
        "Target=\"../theme/theme1.xml\"/>"
        "</Relationships>"));
    return out.toUtf8();
}

// 一个文本框。
//
// 不用占位符：占位符的位置继承自母版与版式，少一层就可能跑到 0,0。
QString textBox(int shapeId, const QString& name, int x, int y, int width, int height,
                const QString& paragraphs) {
    return QStringLiteral(
        "<p:sp><p:nvSpPr><p:cNvPr id=\"%1\" name=\"%2\"/>"
        "<p:cNvSpPr txBox=\"1\"/><p:nvPr/></p:nvSpPr>"
        "<p:spPr><a:xfrm><a:off x=\"%3\" y=\"%4\"/><a:ext cx=\"%5\" cy=\"%6\"/></a:xfrm>"
        "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:noFill/></p:spPr>"
        "<p:txBody><a:bodyPr wrap=\"square\" rtlCol=\"0\"><a:normAutofit/></a:bodyPr>"
        "<a:lstStyle/>%7</p:txBody></p:sp>")
        .arg(shapeId)
        .arg(escapeOoxmlText(name))
        .arg(x)
        .arg(y)
        .arg(width)
        .arg(height)
        .arg(paragraphs);
}

// 一个段落。
//
// bullet=false 时用 buNone（不要项目符号）。
QString paragraph(const QString& text, int size, bool bold, bool bullet) {
    QString properties;
    if (bullet) {
        properties = QStringLiteral(
            "<a:pPr marL=\"285750\" indent=\"-285750\"><a:buFont typeface=\"Arial\"/>"
            "<a:buChar char=\"•\"/></a:pPr>");
    } else {
        properties = QStringLiteral("<a:pPr><a:buNone/></a:pPr>");
    }
    return QStringLiteral("<a:p>%1<a:r><a:rPr lang=\"zh-CN\" sz=\"%2\" b=\"%3\" dirty=\"0\"/>"
                          "<a:t>%4</a:t></a:r></a:p>")
        .arg(properties)
        .arg(size)
        .arg(bold ? 1 : 0)
        .arg(escapeOoxmlText(text));
}

// PDF 页面 → 幻灯片 的坐标映射。
//
// 为什么必须做这一步：幻灯片是 16:9（12192000 × 6858000 EMU），
// PDF 页面多半是 A4（595 × 842 点 = 7556500 × 10693400 EMU）。
// 两者比例不同，直接把 PDF 的坐标当成幻灯片坐标，图片会飞到版心外面
// （A4 比 16:9 高很多，按原坐标放会掉到幻灯片下沿之外）。
//
// 所以：等比缩放到能放下的最大尺寸，再**居中**贴上去。
struct PageMapper {
    // PDF 页面尺寸（点）。
    double pageWidthPoints = 595.0;
    double pageHeightPoints = 842.0;
    // 缩放系数（EMU/EMU）。
    double scale = 1.0;
    // 居中后的左上角偏移（EMU）。
    double offsetX = 0.0;
    double offsetY = 0.0;

    void setup() {
        if (!(pageWidthPoints > 0.0) || !(pageHeightPoints > 0.0)) {
            pageWidthPoints = 595.0;
            pageHeightPoints = 842.0;
        }
        const double pageWidthEmu = pageWidthPoints * kEmuPerPoint;
        const double pageHeightEmu = pageHeightPoints * kEmuPerPoint;
        scale = qMin(kSlideWidth / pageWidthEmu, kSlideHeight / pageHeightEmu);
        offsetX = (kSlideWidth - pageWidthEmu * scale) * 0.5;
        offsetY = (kSlideHeight - pageHeightEmu * scale) * 0.5;
    }

    // PDF 矩形（左下原点，点）→ 幻灯片矩形（左上原点，EMU）。
    QRectF toSlide(double left, double bottom, double width, double height) const {
        const double x = offsetX + left * kEmuPerPoint * scale;
        // PDF 的 y 向上，幻灯片的 y 向下：先翻到「距顶边多远」。
        const double y = offsetY + (pageHeightPoints - (bottom + height)) * kEmuPerPoint * scale;
        return QRectF(x, y, width * kEmuPerPoint * scale, height * kEmuPerPoint * scale);
    }
};

// 一张图片。
QString picture(int shapeId, const QString& relationship, const QRectF& rect) {
    return QStringLiteral(
        "<p:pic>"
        "<p:nvPicPr>"
        "<p:cNvPr id=\"%1\" name=\"图片 %1\"/>"
        "<p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr>"
        "<p:nvPr/>"
        "</p:nvPicPr>"
        "<p:blipFill>"
        "<a:blip r:embed=\"%2\"/>"
        "<a:stretch><a:fillRect/></a:stretch>"
        "</p:blipFill>"
        "<p:spPr>"
        "<a:xfrm><a:off x=\"%3\" y=\"%4\"/><a:ext cx=\"%5\" cy=\"%6\"/></a:xfrm>"
        "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
        "</p:spPr>"
        "</p:pic>")
        .arg(shapeId)
        .arg(relationship)
        .arg(qRound(rect.left()))
        .arg(qRound(rect.top()))
        .arg(qRound(rect.width()))
        .arg(qRound(rect.height()));
}

// 一个表格。
//
// 列宽与行高都用 EMU；单元格里的文字是一个段落。
QString tableShape(int shapeId, const office::TableGrid& grid, const QRectF& rect) {
    const int columns = grid.columnCount();
    if (columns <= 0 || grid.rows.isEmpty()) {
        return {};
    }
    QVector<double> widths = grid.columnWidthsPoints;
    while (widths.size() < columns) {
        widths.append(0.0);
    }
    double total = 0.0;
    for (int column = 0; column < columns; ++column) {
        total += qMax(0.0, widths.at(column));
    }
    if (total <= 0.0) {
        total = qMax(1.0, grid.widthPoints);
    }
    const double widthEmu = rect.width();
    const double heightEmu = rect.height();
    const double rowHeight = grid.rows.isEmpty() ? heightEmu : heightEmu / grid.rows.size();

    QString out;
    out.append(QStringLiteral(
        "<p:graphicFrame>"
        "<p:nvGraphicFramePr>"
        "<p:cNvPr id=\"%1\" name=\"表格 %1\"/>"
        "<p:cNvGraphicFramePr><a:graphicFrameLocks noGrpt=\"0\"/></p:cNvGraphicFramePr>"
        "<p:nvPr/>"
        "</p:nvGraphicFramePr>"
        "<p:xfrm><a:off x=\"%2\" y=\"%3\"/><a:ext cx=\"%4\" cy=\"%5\"/></p:xfrm>"
        "<a:graphic>"
        "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/table\">"
        "<a:tbl>")
                   .arg(shapeId)
                   .arg(qRound(rect.left()))
                   .arg(qRound(rect.top()))
                   .arg(qRound(widthEmu))
                   .arg(qRound(heightEmu)));
    out.append(QStringLiteral("<a:tblPr firstRow=\"1\" bandRow=\"1\"/><a:tblGrid>"));
    for (int column = 0; column < columns; ++column) {
        const double columnEmu = qMax(1.0, widths.at(column) / total * widthEmu);
        out.append(QStringLiteral("<a:gridCol w=\"%1\"/>").arg(qRound(columnEmu)));
    }
    out.append(QStringLiteral("</a:tblGrid>"));
    for (const QVector<QString>& row : grid.rows) {
        out.append(QStringLiteral("<a:tr h=\"%1\">").arg(qRound(rowHeight)));
        for (int column = 0; column < columns; ++column) {
            const QString text = column < row.size() ? row.at(column) : QString();
            out.append(QStringLiteral(
                "<a:tc><a:txBody><a:bodyPr/><a:lstStyle/>%1</a:txBody><a:tcPr/></a:tc>")
                           .arg(paragraph(text, 1200, false, false)));
        }
        out.append(QStringLiteral("</a:tr>"));
    }
    out.append(QStringLiteral("</a:tbl></a:graphicData></a:graphic></p:graphicFrame>"));
    return out;
}

QByteArray slideXml(const Slide& slide, int firstImageOrdinal) {
    QString out = header();
    out.append(QStringLiteral(
        "<p:sld xmlns:a=\"%1\" xmlns:r=\"%2\" xmlns:p=\"%3\"><p:cSld><p:spTree>%4")
        .arg(kNsA, kNsR, kNsP, groupShapeProperties()));

    // 标题：空标题就不放这个框（放一个空框会在放映时留一条空标题栏）。
    if (!slide.title.trimmed().isEmpty()) {
        out.append(textBox(2, QStringLiteral("Title"), kTitleX, kTitleY, kTitleWidth,
                           kTitleHeight, paragraph(slide.title, 3200, true, false)));
    }
    if (!slide.subtitle.trimmed().isEmpty()) {
        out.append(textBox(3, QStringLiteral("Subtitle"), kTitleX,
                           kTitleY + kTitleHeight, kTitleWidth, 800000,
                           paragraph(slide.subtitle, 1800, false, false)));
    }

    QString body;
    for (const QString& bullet : slide.bullets) {
        if (bullet.trimmed().isEmpty()) {
            continue;
        }
        body.append(paragraph(bullet, 1800, false, true));
    }
    if (!body.isEmpty()) {
        out.append(textBox(4, QStringLiteral("Body"), kBodyX, kBodyY, kBodyWidth, kBodyHeight,
                           body));
    }

    // 图片与表格：坐标由 PDF 页面映射到幻灯片（等比缩放 + 居中）。
    PageMapper mapper;
    mapper.pageWidthPoints = slide.pageSizePoints.width();
    mapper.pageHeightPoints = slide.pageSizePoints.height();
    mapper.setup();
    int shapeId = 5;
    int imageOrdinal = firstImageOrdinal;
    for (int index = 0; index < slide.images.size(); ++index) {
        const office::ImagePlacement& image = slide.images.at(index);
        if (image.mediaIndex < 0) {
            continue;
        }
        const QRectF rect = mapper.toSlide(image.leftPoints, image.bottomPoints,
                                           image.widthPoints, image.heightPoints);
        out.append(picture(shapeId++, office::relationshipId(imageOrdinal++), rect));
    }
    for (const office::TableGrid& grid : slide.tables) {
        const QRectF rect = mapper.toSlide(grid.leftPoints, grid.bottomPoints, grid.widthPoints,
                                           grid.heightPoints);
        out.append(tableShape(shapeId++, grid, rect));
    }

    out.append(QStringLiteral(
        "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>"));
    return out.toUtf8();
}

// 幻灯片的关系文件。
//
// 关系号的分配（**顺序不能乱**）：
//   rId1 = 版式（slideLayout）
//   rId2 = 备注（notesSlide）—— 该页有备注时才有
//   之后才是图片：rId3 起（没备注）/ rId4 起（有备注）
//
// 正文里的 <a:blip r:embed="..."> 用的就是这个号。号对不上部件，
// PowerPoint 打开时会判「文件已损坏」。
QByteArray slideRelsXml(int notesNumber, const QVector<int>& mediaIndexes,
                        const office::MediaSet& media) {
    QString out = header();
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" "
        "Target=\"../slideLayouts/slideLayout1.xml\"/>"));
    // 图片的关系号从 rId3 起（rId1 是版式、rId2 留给备注）；
    // 该页有备注时备注占了 rId2，图片就从 rId4 起。
    int ordinal = 2;
    if (notesNumber > 0) {
        // 幻灯片 → 备注的反向关系。少了它，notesSlide 部件虽然写在包里，
        // 却没有任何引用能到达，PowerPoint 的备注窗格是空的。
        out.append(QStringLiteral(
            "<Relationship Id=\"rId2\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesSlide\" "
            "Target=\"../notesSlides/notesSlide%1.xml\"/>").arg(notesNumber));
        ordinal = 3;
    }
    for (int index = 0; index < mediaIndexes.size(); ++index) {
        const int mediaIndex = mediaIndexes.at(index);
        if (mediaIndex < 0 || mediaIndex >= media.count()) {
            continue;
        }
        // Target 相对于 ppt/slides/ 目录（rels 文件在 ppt/slides/_rels/ 下），
        // 所以是 "../media/image1.png"。
        out.append(QStringLiteral(
            "<Relationship Id=\"%1\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
            "Target=\"../%2\"/>")
                       .arg(office::relationshipId(ordinal + index),
                            media.at(mediaIndex).partName.section(QLatin1Char('/'), 1)));
    }
    out.append(QStringLiteral("</Relationships>"));
    return out.toUtf8();
}

QByteArray notesSlideXml(const QString& notes) {
    QString out = header();
    out.append(QStringLiteral(
        "<p:notes xmlns:a=\"%1\" xmlns:r=\"%2\" xmlns:p=\"%3\"><p:cSld><p:spTree>%4")
        .arg(kNsA, kNsR, kNsP, groupShapeProperties()));
    out.append(QStringLiteral(
        "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Notes Placeholder 1\"/>"
        "<p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr>"
        "<p:nvPr><p:ph type=\"body\" idx=\"1\"/></p:nvPr></p:nvSpPr><p:spPr/>"
        "<p:txBody><a:bodyPr/><a:lstStyle/>%1</p:txBody></p:sp>")
        .arg(paragraph(notes, 1200, false, false)));
    out.append(QStringLiteral(
        "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:notes>"));
    return out.toUtf8();
}

QByteArray notesSlideRelsXml(int slideNumber) {
    QString out = header();
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesMaster\" "
        "Target=\"../notesMasters/notesMaster1.xml\"/>"
        "<Relationship Id=\"rId2\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" "
        "Target=\"../slides/slide%1.xml\"/>"
        "</Relationships>").arg(slideNumber));
    return out.toUtf8();
}

}  // namespace

QByteArray deckToPptx(const Deck& deck) {
    return deckToPptx(deck, office::MediaSet());
}

QByteArray deckToPptx(const Deck& deck, const office::MediaSet& media) {
    if (deck.slides.isEmpty()) {
        return QByteArray();
    }

    // 有备注的页才生成 notesSlide；备注编号按「第几个有备注的页」连续排。
    QVector<int> notesSlideNumbers;  // 与 deck.slides 同下标：值 = notesSlide 的序号，0 表示无备注
    int notesCount = 0;
    for (const Slide& slide : deck.slides) {
        if (slide.notes.trimmed().isEmpty()) {
            notesSlideNumbers.append(0);
            continue;
        }
        ++notesCount;
        notesSlideNumbers.append(notesCount);
    }

    doc::zip::Writer writer;
    writer.addFile(QStringLiteral("[Content_Types].xml"),
                   contentTypesXml(deck.slides.size(), notesCount, media));
    writer.addFile(QStringLiteral("_rels/.rels"), packageRelsXml());
    writer.addFile(QStringLiteral("docProps/core.xml"), corePropsXml(deck.title));
    writer.addFile(QStringLiteral("docProps/app.xml"), appPropsXml(deck.slides.size()));
    writer.addFile(QStringLiteral("ppt/presentation.xml"), presentationXml(deck.slides.size(), notesCount));
    writer.addFile(QStringLiteral("ppt/_rels/presentation.xml.rels"),
                   presentationRelsXml(deck.slides.size()));
    writer.addFile(QStringLiteral("ppt/theme/theme1.xml"), themeXml());
    writer.addFile(QStringLiteral("ppt/slideMasters/slideMaster1.xml"), slideMasterXml());
    writer.addFile(QStringLiteral("ppt/slideMasters/_rels/slideMaster1.xml.rels"),
                   slideMasterRelsXml());
    writer.addFile(QStringLiteral("ppt/slideLayouts/slideLayout1.xml"), slideLayoutXml());
    writer.addFile(QStringLiteral("ppt/slideLayouts/_rels/slideLayout1.xml.rels"),
                   slideLayoutRelsXml());
    writer.addFile(QStringLiteral("ppt/notesMasters/notesMaster1.xml"), notesMasterXml());
    writer.addFile(QStringLiteral("ppt/notesMasters/_rels/notesMaster1.xml.rels"),
                   notesMasterRelsXml());

    // 媒体部件：ppt/media/imageN.png。
    //
    // 编号是**包级全局**的（MediaSet 负责），所以这里按 media 的顺序写，
    // 幻灯片里的关系号才能和文件名字对得上。
    for (int index = 0; index < media.count(); ++index) {
        writer.addFile(media.at(index).partName, media.at(index).bytes);
    }

    for (int i = 0; i < deck.slides.size(); ++i) {
        const int number = i + 1;
        const Slide& slide = deck.slides.at(i);
        // 这一页的图片在 media 里的下标（连续的一段）。
        QVector<int> mediaIndexes;
        for (int index = 0; index < slide.images.size(); ++index) {
            const int mediaIndex = slide.images.at(index).mediaIndex;
            if (mediaIndex >= 0 && mediaIndex < media.count()) {
                mediaIndexes.append(mediaIndex);
            }
        }
        // 图片的关系号：版式占 rId1，有备注时备注占 rId2，图片从 rId3 起。
        const int firstImageOrdinal = notesSlideNumbers.at(i) > 0 ? 3 : 2;
        writer.addFile(QStringLiteral("ppt/slides/slide%1.xml").arg(number),
                       slideXml(slide, firstImageOrdinal));
        writer.addFile(QStringLiteral("ppt/slides/_rels/slide%1.xml.rels").arg(number),
                       slideRelsXml(notesSlideNumbers.at(i), mediaIndexes, media));
        const int notesNumber = notesSlideNumbers.at(i);
        if (notesNumber <= 0) {
            continue;
        }
        writer.addFile(QStringLiteral("ppt/notesSlides/notesSlide%1.xml").arg(notesNumber),
                       notesSlideXml(slide.notes));
        writer.addFile(QStringLiteral("ppt/notesSlides/_rels/notesSlide%1.xml.rels").arg(notesNumber),
                       notesSlideRelsXml(number));
    }

    return writer.toByteArray();
}

}  // namespace aipdf::doc::pptx
