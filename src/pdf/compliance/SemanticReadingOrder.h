#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 语义树里的一个「可排序单元」：一页里的一个内容元素
// （标题 / 段落 / 列表 / 表格 / 图 / 表单域）。
//
// 为什么单元是「内容元素」而不是「行」：行级顺序说不了「先念标题还是先念正文」，
// 语义级的顺序单位就是结构元素本身——把标题挪到正文前面，读屏软件念的顺序就跟着变。
struct SemanticOrderEntry {
    // 所属页（0 基）。
    int pageIndex = 0;
    // 结构类型（PDF 里的 /S），例如 "H1" / "P" / "LI" / "Table" / "Figure" / "Form"。
    QString role;
    // 文字预览（整棵子树的文字，截断到 120 字左右）。
    QString text;
    // 这个元素自己挂着的标记内容 id（页内编号）。
    //
    // 为什么单列出来：写回时靠它把「文字」钉在原来的元素上，
    // 顺序变了也不能让文字错位。
    QVector<int> mcids;
    // 元素在读回的结构树（先序元素列表）里的下标。
    //
    // 为什么用它当身份：文字会重复（「目录」出现两次），
    // 靠文字认元素会把两个同名元素搞混；下标是唯一的。
    int nodeIndex = -1;
};

// 一页的语义元素（按结构树里的顺序）。
struct SemanticOrderPage {
    int pageIndex = 0;
    QVector<SemanticOrderEntry> entries;
};

// 读语义树的阅读顺序：按页给出每页的内容元素，顺序 = 结构树里的实际顺序。
//
// 口径：
//   * 只认**语义树**（Document → Part(每页) → Sect → H1..H6 / P / L / Table / Figure / Form），
//     行级标签树（Document → P → Span）读不出内容元素；
//   * 文档没有语义树时返回空，并把原因写进 error——
//     **不静默退化成行级**：那会让用户以为自己在调语义顺序，其实调的是行顺序。
QVector<SemanticOrderPage> readSemanticReadingOrder(const QByteArray& input,
                                                      QString* error = nullptr);

// 按给定顺序写回语义树。
//
// 语义（关键）：**只改顺序，不改角色、不改内容**。
//   * 角色（H1/P/LI/Table/…）原样保留；
//   * 每个元素指向的 MCID 跟着元素走，文字不会错位；
//   * 内容流的标记内容、/ParentTree、/StructParents、/Tabs /S 由共用的写手一起重建，
//     写完会重新读回校验：角色或文字对不上就当失败，文档不改。
//
// 失败返回空 QByteArray，并通过 error 给出中文原因。
QByteArray writeSemanticReadingOrder(const QByteArray& input,
                                      const QVector<SemanticOrderPage>& order,
                                      QString* error = nullptr);

}  // namespace aipdf::pdf
