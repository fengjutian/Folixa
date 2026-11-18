#pragma once

#include <QByteArray>

#include "ai/content/PptGeneration.h"

namespace aipdf::ai {

// 把整套 PPT 导出成**真正的 .pptx**（Office Open XML 演示文稿，zip 打包）。
//
// 为什么不用 Markdown 顶替：命令叫「生成 PPT」，产出的却是一份 Markdown ——
// 用户拿到的东西在 PowerPoint 里打不开。这是**命令名与产出不符**，
// 不是「少一个功能」。
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
//
// 页面版式：16:9，标题在上、要点在下，全部用**显式坐标的文本框**，
// 不走占位符继承 —— 占位符的位置来自母版与版式，少一层就可能跑到 0,0。
//
// 空 deck（没有页）返回空数组：调用方要能区分「导出失败」与「没有内容」。
QByteArray deckToPptx(const PptDeck& deck);

}  // namespace aipdf::ai
