#pragma once

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一次「应用区域阅读顺序」的**选区**：这一次要作用到哪些内容。
//
// 为什么要有选区：这条命令以前做的是「按视觉顺序重建整篇文档的语义标签树」——
// 那与「辅助工具 → 标签 → 自动添加文档标签」是同一件事，手工调过的顺序会被覆盖。
// 现在它只动**选区里的**语义元素：选区外的内容一个都不碰。
//
// 选区由两部分组成：
//   * 哪些页（`pageIndexes`）；
//   * 每页要不要再圈一块区域（`regions`，PDF user space，左下原点）。
// 没有区域限制时就是「整页」。
struct SemanticOrderScope {
    // 参与的页（0 基）。空 = 没有选区（什么也不做）。
    QVector<int> pageIndexes;
    // 每页的区域限制，与 `pageIndexes` **一一对应**。
    // 某一页没有限制时放空矩形（QRectF() 就是空矩形）。
    QVector<QRectF> regions;

    bool isEmpty() const { return pageIndexes.isEmpty(); }
    // 这一页在选区里的第几格；不在选区里返回 -1。
    int slotOfPage(int pageIndex) const;
    // 第 slot 格的区域限制；没有限制时返回空矩形。
    QRectF regionAt(int slot) const;
};

// 选区里的一个语义元素（= 结构树里一个可排序的内容元素）。
struct SemanticOrderScopeElement {
    // 结构树元素下标（身份）。靠它把元素和结构树对上号。
    int nodeIndex = -1;
    // 内容所在的**物理页**（0 基）。
    int pageIndex = -1;
    // 结构类型（PDF 里的 /S），例如 "H1" / "P" / "LI" / "Table" / "Figure" / "Form"。
    QString role;
    // 文字预览。
    QString text;
    // 元素在页面上的包围盒（PDF user space）。空 = 取不到位置。
    QRectF bounds;
    // 是否落在选区里。
    bool inScope = false;
    // 视觉顺序的序号（0 基）：选区内的元素按「从上到下、从左到右」排的名次。
    // -1 = 不在选区里，或取不到位置（这种元素不会被重排）。
    int visualRank = -1;
};

// 读选区的结果。
struct SemanticOrderScopeReport {
    // 文档里有语义元素的页（0 基，升序）。
    QVector<int> pages;
    // 全部可排序元素（按结构树顺序拍平）。
    QVector<SemanticOrderScopeElement> elements;
    // 落在选区里的元素个数。
    int scopedCount = 0;
    // 失败原因（中文）。空 = 没失败。
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// 读选区：文档里有哪些语义元素、哪些落在选区里、选区内按视觉顺序怎么排。
//
// 失败（error 非空）的情况：输入为空、文档里没有语义标签树、
// 或者内容流与扫描结果对不上（这时候连元素的位置都取不准，不敢动顺序）。
SemanticOrderScopeReport readSemanticOrderScope(const QByteArray& pdfBytes,
                                                const SemanticOrderScope& scope,
                                                QString* error = nullptr);

// 应用选区顺序的结果。
struct SemanticOrderApplyResult {
    // 是否成功。失败时 error 里有中文原因。
    bool ok = false;
    // 失败原因（中文）。
    QString error;
    // 写出来的字节（没有改动时是原来的字节）。
    QByteArray bytes;
    // 选区里的元素个数。
    int scopedCount = 0;
    // 位置真的变了的元素个数。
    int movedCount = 0;
    // 有没有真的改动。没改动时不写回，文档保持原样。
    bool changed = false;
};

// 按视觉顺序重排**选区内**的语义元素。
//
// 语义（关键）：
//   * 只重排**同一个容器**（结构树里的 Part / Sect）里的元素，
//     而且只在它们**已经占着的位置之间**换位——
//     选区外的元素连位置都不变，更不会被搬到别的容器去；
//   * 角色（H1 / P / LI / Table…）、文字、页归属一律不动：
//     这一条命令改的只有「先后」；
//   * 取不到位置的元素（内容不是文字，例如纯图像元素）不参与重排，
//     它们留在原位——动不了的就别动；
//   * 选区是空时、或选区里没有可排序的元素时，文档保持原样。
//
// 失败返回 ok = false，并通过 error 给出中文原因；失败时文档不变。
SemanticOrderApplyResult applySemanticOrderScope(const QByteArray& pdfBytes,
                                                 const SemanticOrderScope& scope,
                                                 QString* error = nullptr);

}  // namespace aipdf::pdf
