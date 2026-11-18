#pragma once

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "doc/export/OfficeMedia.h"

namespace aipdf::doc::pptx {

// 一页幻灯片。
//
// 为什么自己定义一份而不复用 ai::Slide：AI 那一层的 Slide 带着「主题 / 来源」
// 这些生成侧的字段，doc 层不该知道；而 PPTX 的打包是**通用能力**
// （AI 生成的 PPT、PDF 导出的 PPT 都要用同一套打包代码），放在 doc 层更合适。
struct Slide {
    // 页码（1 起）。
    int index = 0;
    // 标题，可空（空标题就不放标题框）。
    QString title;
    // 副标题，可空。
    QString subtitle;
    // 要点（正文段落）。
    QStringList bullets;
    // 演讲者备注，可空（空备注就不生成 notesSlide 部件）。
    QString notes;
    // 这一页对应的 **PDF 页面尺寸**（点）。
    //
    // 幻灯片是 16:9，PDF 页面多半是 A4，两者比例不同：
    // 图片与表格要按这个尺寸做**等比缩放 + 居中映射**，否则会飞出版面。
    // 留空就按 A4 处理。
    QSizeF pageSizePoints = QSizeF(595.0, 842.0);
    // 这一页上的图片（坐标是 PDF user space 的点，写入时换算成 EMU）。
    QVector<office::ImagePlacement> images;
    // 这一页上的表格（坐标是 PDF user space 的点，写入时换算成 EMU）。
    QVector<office::TableGrid> tables;
};

// 一整套幻灯片。
struct Deck {
    // 整套的标题（写进 docProps/core.xml）。
    QString title;
    // 逐页内容，顺序即放映顺序。
    QVector<Slide> slides;
};

// 把一整套幻灯片打包成**真正的 .pptx**（Office Open XML 演示文稿，zip 打包）。
//
// 包结构（最小可用集，PowerPoint / WPS / LibreOffice 都能打开）：
//   [Content_Types].xml
//   _rels/.rels
//   docProps/core.xml, docProps/app.xml
//   ppt/presentation.xml + ppt/_rels/presentation.xml.rels
//   ppt/theme/theme1.xml
//   ppt/slideMasters/slideMaster1.xml (+ _rels)
//   ppt/slideLayouts/slideLayout1.xml (+ _rels)
//   ppt/notesMasters/notesMaster1.xml (+ _rels)
//   ppt/slides/slideN.xml (+ _rels)
//   ppt/notesSlides/notesSlideN.xml (+ _rels)   —— 该页有备注才有这一对
//   ppt/media/imageN.png                       —— 该页有图片才有这些
//
// 页面版式：16:9，标题在上、要点在下，全部用**显式坐标的文本框**，
// 不走占位符继承 —— 占位符的位置来自母版与版式，少一层就可能跑到 0,0。
//
// 空 deck（没有页）返回空数组：调用方要能区分「导出失败」与「没有内容」。
QByteArray deckToPptx(const Deck& deck);

// 带图片的打包：media 里的图片会写成 ppt/media/imageN.png，
// 并在对应幻灯片的 _rels 里分配关系号（rId）。
//
// media 的编号必须是**包级全局连续**的：ppt/media/ 是整个包共用的目录，
// 按页从 1 重来会撞名（一页的图会覆盖另一页的图）。
QByteArray deckToPptx(const Deck& deck, const office::MediaSet& media);

}  // namespace aipdf::doc::pptx
