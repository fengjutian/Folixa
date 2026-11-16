#pragma once

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一页里的一块内容：一个矩形区域 + 它在阅读顺序里的位置。
//
// rect 用的是 **PDF user space**（左下原点），和仓库里其它地方
// （pdf::MediaBox、PDFium 字符框、app/PageTransform）是同一套坐标。
// 写法是 QRectF(x, y, 宽, 高)：覆盖 [x, x+宽] × [y, y+高]，
// **y 是矩形的下边**（PDF 的 y 向上为正，所以 y 越大越靠上）。
// 页面自带的 /Rotate 没有被应用：调用方若按显示顺序排版，
// 先用 app/PageTransform 换算回用户空间再传进来。
//
// order 是**这一页内部**的阅读次序：小的先读。
// order 相同的区域按传入次序排（稳定、可预期）。
struct TaggedRegion {
    QRectF rect;    // PDF user space（左下原点），y 是下边
    int order = 0;  // 该区域在本页里的阅读顺序序号（小的先读）
};

// 给整份 PDF 写一棵真正的标签树：
//   * 内容流里每个显示操作符（Tj / TJ / ' / "）都被包进 marked content：
//     `/Tag <</MCID n>> BDC … EMC`，n 是该页内从 0 开始的 MCID；
//   * 每页页面字典加 /StructParents（键 = 页序号，从 0 连续）；
//   * catalog 加 /StructTreeRoot，文档加 /MarkInfo <</Marked true>>；
//   * /StructTreeRoot → Document → 每页一个 /P → 每行文本一个 /Span，
//     Span 的 /K 是该行包含的 MCID 整数数组。
//
// 语义要点：
//   * 区域按 order 升序排好后，写进标签树的 Span 顺序就是它——
//     **阅读顺序由标签树决定，与 MCID 的数字大小无关**；
//   * 标记是**渲染层**的：marked content 不改变页面外观，
//     写出来的 PDF 渲染结果与写之前逐像素一致；
//   * 区域与内容流的对应关系是「就近匹配」：把每个显示操作符按它算出来的
//     基线位置归到离它最近的那个区域。矩形与文字完全对不上时（一个都没落进
//     矩形里），退化成「按视觉顺序把整页的显示操作符均分成几段」，
//     一段对应一个区域——宁可粗一点，也不去内容流里做有风险的裁剪；
//   * 写之前先在内存里重开校验一次，写完再重开校验一次，两次都过才返回；
//   * 幂等：对**已经打过标签的 PDF** 再调用一次，会先摘掉上一轮的标记
//     （`/Tag <</MCID n>> BDC` 与配对的 EMC）再重新打，
//     不会把同一个显示操作符套两层 BDC，MCID 也就不会重复。
//
// 失败返回空 QByteArray，并通过 error 给出中文原因。
//
// 口径：pageRegions 为空（或者所有页都没有区域）时**原样返回输入字节**，
// 什么都不写。这不是错误：没有区域就没有可标记的东西，调用方不必特判。
// pageRegions 的下标是页序号；比页数少时缺的那些页不打标记（但仍然会有
// /StructParents，键要连续），比页数多时多出来的部分忽略。
QByteArray writeTaggedStructure(const QByteArray& pdfBytes,
                                 const QVector<QVector<TaggedRegion>>& pageRegions,
                                 QString* error = nullptr);

// 读回一棵标签树（用于测试与界面回显）：页序号 → 该页按树顺序排好的
// 区域序号列表。
//
// 「区域序号」是这一页的 Span 按**最小 MCID** 排序后的下标：
// 它给每个 Span 一个与内容流无关的稳定身份，于是返回的列表能看出
// 「树里的顺序」与「MCID 顺序」是否一致（一致时就是 0,1,2,…）。
// 没有标签树时返回空列表。
QVector<QVector<int>> readTaggedOrder(const QByteArray& pdfBytes, QString* error = nullptr);

}  // namespace aipdf::pdf
