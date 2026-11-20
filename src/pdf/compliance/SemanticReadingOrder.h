#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 语义树里的一个「可排序单元」：一个内容元素
//（标题 / 段落 / 列表 / 表格 / 图 / 表单域）。
//
// 为什么单元是「内容元素」而不是「行」：行级顺序说不了「先念标题还是先念正文」，
// 语义级的顺序单位就是结构元素本身——把标题挪到正文前面，读屏软件念的顺序就跟着变。
//
// **两件事必须分清**（这是跨页移动能成立的前提）：
//   * 内容**在哪一页**：`pageIndex`（物理页 /Pg，MCID 就挂在那页上）；
//   * 它在**阅读顺序里的位置**：它在槽位列表里的次序。
// 跨页移动只改第二件：元素被挪到别的页的槽位里，
// 但它的内容仍然留在原来的物理页上（MCID 仍然按 `pageIndex` 取，
// 写回时元素的 /Pg 也仍然是它内容所在的那一页）。
struct SemanticOrderEntry {
    // 内容所在的**物理页**（0 基）。跨页移动时**不变**。
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

// 阅读顺序里的一个**槽位**：结构树里一个容器（Part / Sect）底下的单元，
// 按阅读顺序排。
//
// 槽位不是「这一页的元素」——槽位里的元素可以来自别的页（跨页移动的结果）。
//   * `pageIndex` 是容器对应的页（显示与定位用）；
//   * `containerNodeIndex` 是这个槽位对应结构树里的哪个容器（单元的父元素）；
//   * 每个元素自己的 `pageIndex` 才是它内容所在的物理页。
//
// 为什么按**容器**分槽位而不是按物理页：跨页移动之后，元素的**内容**还在
// 原来的物理页上，但它的**阅读位置**换到了别的容器。按物理页分组会把
// 「位置」和「页」混成一件事，写回时就说不清这个元素该写进哪个容器，
// 读回来也会丢掉「它被挪走过」这件事。
struct SemanticOrderSlot {
    // 容器对应的页（0 基）；-1 表示容器没有限定页。
    int pageIndex = 0;
    // 这个槽位对应结构树里的哪个容器（单元的父元素下标）。
    //
    // 为什么显式记下来：跨页移动之后，槽位里的元素可能来自别的容器，
    // 靠「第一个元素的父元素」反推会推错——元素被挪到槽位**开头**时，
    // 第一个元素正是刚挪进来的那个。
    int containerNodeIndex = -1;
    // 槽位里的单元（按阅读顺序）。
    QVector<SemanticOrderEntry> entries;
};

// 读语义树的阅读顺序：按容器（阅读位置）给出每段的内容元素，顺序 = 结构树里的实际顺序。
//
// 口径：
//   * 只认**语义树**（Document → Part(每页) → Sect → H1..H6 / P / L / Table / Figure / Form），
//     行级标签树（Document → P → Span）读不出内容元素；
//   * 文档没有语义树时返回空，并把原因写进 error——
//     **不静默退化成行级**：那会让用户以为自己在调语义顺序，其实调的是行顺序。
QVector<SemanticOrderSlot> readSemanticReadingOrder(const QByteArray& input,
                                                      QString* error = nullptr);

// 按给定顺序写回语义树。
//
// 语义（关键）：**只改顺序，不改角色、不改内容**。
//   * 角色（H1/P/LI/Table/…）原样保留；
//   * 每个元素指向的 MCID 跟着元素走，文字不会错位；
//   * 元素被挪到别的页的槽位时，它的内容**仍然留在原来的物理页**
//     （/Pg 与 MCID 都不变），变的只是它在阅读顺序里的位置；
//   * 图像（/OBJR）与表单域 widget 的引用一起搬走，不会因为重排而丢；
//   * 内容流的标记内容、/ParentTree、/StructParents、/Tabs /S 由共用的写手一起重建，
//     写完会重新读回校验：元素个数、角色与文字、全局顺序对不上就当失败，文档不改。
//
// 失败返回空 QByteArray，并通过 error 给出中文原因。
QByteArray writeSemanticReadingOrder(const QByteArray& input,
                                      const QVector<SemanticOrderSlot>& order,
                                      QString* error = nullptr);

}  // namespace aipdf::pdf
