#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 结构树里的一个元素（PDF 里的 /StructElem）。
struct StructElement {
    // 标签名（/S），例如 P、H1、Figure、Link。
    //
    // 有些文件把 /S 写成数组（一个元素有多个标签），这里取第一个。
    QString tag;
    // 替代文本（/Alt）：给读屏软件用的图片说明。
    QString alt;
    // 语言（/Lang）：元素自身的语言标记，空表示文件里没写。
    QString language;
    // 实际文字（/ActualText）：元素内容的文字替身。
    QString actualText;
    // 标题（/T）：给人看的名字。
    QString title;
    // 所属页码（0 基）。-1 表示文件里没写、也没能推出来。
    int pageIndex = -1;
    // 这个元素直接挂着的 MCID（内容流里的标记内容编号）。
    QVector<int> mcids;
    // 这些 MCID 对应的实际文字（按 MCID 顺序拼起来）。
    QString text;
    // 父元素在报告 elements 里的下标；-1 表示顶层。
    int parentIndex = -1;
    // 层级深度：顶层为 0。
    int depth = 0;
};

// 页面内容流里一段标记内容对应的文字。
struct MarkedContentText {
    int pageIndex = -1;
    int mcid = -1;
    QString text;
};

// 读一棵既有标签树的结果。
struct StructTreeReport {
    // 文件里有没有 /StructTreeRoot。
    bool hasStructTree = false;
    // 文档级语言（catalog 的 /Lang）。hasDocumentLanguage 为假表示文件里没写。
    QString documentLanguage;
    bool hasDocumentLanguage = false;
    // 文档级 /DisplayDocTitle：标题是否要显示（而不是只做元数据）。
    bool displayDocTitle = false;
    bool hasDisplayDocTitle = false;

    // 结构元素，按树的先序排列（父在子前）。
    QVector<StructElement> elements;
    // 页面里每一段标记内容的文字。
    QVector<MarkedContentText> markedContent;

    // 中文说明：缺了什么、哪里只能按大概处理，都写在这里。
    QStringList notes;
    // 失败原因（中文）：结构损坏、标签树里有环这类问题。
    // 注意：没有标签树**不算**失败，只是 hasStructTree 为假。
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// 读一份 PDF 的既有标签树（/StructTreeRoot）。
//
// 这不是「读回自己写的标签树」：仓库里 pdf::writeTaggedStructure 能写一棵
// 行粒度的标签树，但别的工具（Word、Acrobat、Acrobat accessibility checker…）
// 写出来的树形状完全不一样。本函数按 PDF 32000-1 §14.7 的通用规则解析：
//   * /StructTreeRoot 的 /K 可以是结构元素、可以是 MCID 数字、也可以是数组
//     （三种混在一起也行），三种都处理；
//   * 元素的 /Parent 指向父元素，用它兜住「只靠 /Parent 串起来」的树；
//   * 元素的 /Pg 指向页面，没有就继承父元素的页；
//   * 遍历有防环：同一个元素被第二次访问到就停手并报错，
//     不会把自己绕死在里面。
//
// MCID 对应的文字从页面内容流里取：找 `/Tag << /MCID n >> BDC … EMC`
// （以及 `/Tag BMC … EMC`）标记内容，取中间显示出来的文字。
// 文字按字体的编码还原：有 /ToUnicode 就按它，没有就按 /Encoding 的
// /Differences 字形名，再没有就按 WinAnsi/Latin-1。
//
// 失败（error 非空）的情况：PDF 解析不了、标签树里有环。
// 没有标签树不是失败：hasStructTree 为假，notes 里说明原因。
StructTreeReport readStructTree(const QByteArray& pdfBytes);

}  // namespace aipdf::pdf
