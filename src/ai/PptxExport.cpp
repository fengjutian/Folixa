#include "ai/PptxExport.h"

#include "doc/XmlText.h"
#include "doc/ZipWriter.h"

namespace aipdf::ai {

namespace {

using doc::escapeOoxmlText;

// 1 英寸 = 914400 EMU（English Metric Unit）。
constexpr int kEmuPerInch = 914400;

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

QByteArray contentTypesXml(int slideCount, int notesCount) {
    QString out = header();
    out.append(QStringLiteral(
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" "
        "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
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

QByteArray presentationXml(int slideCount) {
    QString out = header();
    out.append(QStringLiteral(
        "<p:presentation xmlns:a=\"%1\" xmlns:r=\"%2\" xmlns:p=\"%3\">"
        "<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>"
        "<p:sldIdLst>").arg(kNsA, kNsR, kNsP));
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

QByteArray slideXml(const Slide& slide) {
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

    out.append(QStringLiteral(
        "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>"));
    return out.toUtf8();
}

QByteArray slideRelsXml() {
    QString out = header();
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" "
        "Target=\"../slideLayouts/slideLayout1.xml\"/>"
        "</Relationships>"));
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

QByteArray deckToPptx(const PptDeck& deck) {
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
                   contentTypesXml(deck.slides.size(), notesCount));
    writer.addFile(QStringLiteral("_rels/.rels"), packageRelsXml());
    writer.addFile(QStringLiteral("docProps/core.xml"), corePropsXml(deck.title));
    writer.addFile(QStringLiteral("docProps/app.xml"), appPropsXml(deck.slides.size()));
    writer.addFile(QStringLiteral("ppt/presentation.xml"), presentationXml(deck.slides.size()));
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

    for (int i = 0; i < deck.slides.size(); ++i) {
        const int number = i + 1;
        writer.addFile(QStringLiteral("ppt/slides/slide%1.xml").arg(number),
                       slideXml(deck.slides.at(i)));
        writer.addFile(QStringLiteral("ppt/slides/_rels/slide%1.xml.rels").arg(number),
                       slideRelsXml());
        const int notesNumber = notesSlideNumbers.at(i);
        if (notesNumber <= 0) {
            continue;
        }
        writer.addFile(QStringLiteral("ppt/notesSlides/notesSlide%1.xml").arg(notesNumber),
                       notesSlideXml(deck.slides.at(i).notes));
        writer.addFile(QStringLiteral("ppt/notesSlides/_rels/notesSlide%1.xml.rels").arg(notesNumber),
                       notesSlideRelsXml(number));
    }

    return writer.toByteArray();
}

}  // namespace aipdf::ai
