#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::doc {

// 一个文档块：一段连续的正文，带页码、坐标与章节归属。
//
// 坐标用 **PDF user space**（左下原点，单位是点），与 FPDFText_GetCharBox
// 同一套坐标：索引是文档自己的属性，不该随窗口缩放、用户旋转而变。
// 要画到屏幕上时，由 app 层用 PageTransform 转成显示空间。
//
// 文本块、章节路径、页码、坐标——这四样是后续 AI 回答的“证据坐标”：
// 引用一段话要能指回它在哪一页、哪一块。
struct DocumentChunk {
    // 文档内稳定的块 id：页序号 + 页内块序号（例如 "p3b2"）。
    // 同一页重建索引后 id 不变，外部引用（搜索结果、引用出处）不会错位。
    QString id;
    // 页序号（从 0 开始）。
    int pageIndex = -1;
    // 文档内顺序（按页、按块）。
    int order = 0;
    // 块文本，行与行之间用 \n 分隔。
    QString text;
    // 每行一个包围盒（PDF user space）：高亮时按行画，
    // 不会把行与行之间的空白也涂上。
    QVector<QRectF> lineBoxes;
    // 章节路径：按大纲层级拼出来（例如 "第2章 > 2.1 概述"）。
    QString sectionPath;
    // 标题层级：0 是正文，1 起是标题（1 是最大的一层）。
    int headingLevel = 0;
    // 词数：中文按字、英文按词。
    int wordCount = 0;

    bool isValid() const { return pageIndex >= 0 && !text.isEmpty(); }
    bool isHeading() const { return headingLevel > 0; }
    // 块在页面上的总包围盒。
    QRectF bounds() const;
};

}  // namespace aipdf::doc
