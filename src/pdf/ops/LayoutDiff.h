#pragma once

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVector>

#include "pdf/kernel/LayoutBlocks.h"

namespace aipdf::pdf {

// 版面结构差异的种类。
//
// 为什么单独一个枚举：现有的 DiffKind 只有 5 种，说的是「文字层面的差异」。
// 版面差异说的是另一回事——「这一块被挪到了别处」「这里的图被换掉了」
// 「这张表少了一列」。混进 DiffKind 会把「文字被改过」和「版面块被移动」
// 搅在一起，两边都说不清。
enum class LayoutDiffKind {
    // 块新增：新文里多出来的版面块。
    BlockInserted,
    // 块删除：原文有、新文里没有的版面块。
    BlockDeleted,
    // 块移动：同一块内容被挪到了别的位置（可能挪到了别的页）。
    BlockMoved,
    // 换图：同一个位置上的图被换成了另一张。
    ImageReplaced,
    // 文本改写：同一块文字被改过。
    TextRewritten,
    // 表格行变化：表格的行数变了。
    TableRowsChanged,
    // 表格列变化：表格的列数变了。
    TableColumnsChanged,
    // 表格单元格改写：表格里某一格的文字变了。
    TableCellRewritten,
};

// 种类的 ASCII 名字（技术备注里用；不要拿它拼界面文案）。
QString layoutDiffKindName(LayoutDiffKind kind);

// 一条版面差异。
//
// 每条都带源与目标的页码、矩形：这是「引用定位」的全部依据，
// 界面拿它跳页、拿矩形去高亮。
struct LayoutDifference {
    LayoutDiffKind kind = LayoutDiffKind::BlockInserted;
    // 原文页号 / 新文页号（0 基）；-1 表示这一边没有。
    int oldPageIndex = -1;
    int newPageIndex = -1;
    // 页内块序号；-1 表示没有对应块。
    int oldBlockIndex = -1;
    int newBlockIndex = -1;
    // 块的种类（图片与图形的换图会带上它）。
    LayoutBlockKind blockKind = LayoutBlockKind::Text;
    // 原文 / 新文的文字（图片块为空）。
    QString oldText;
    QString newText;
    // 原文 / 新文里这一块的位置（PDF user space）。
    QRectF oldBounds;
    QRectF newBounds;
    // 文本相似度（0..1）。
    double similarity = 0.0;
    // 归一化矩形的 IoU（0..1）。
    double iou = 0.0;
    // 表格专用：原来的行数 / 列数与现在的行数 / 列数。
    int oldRowCount = 0;
    int newRowCount = 0;
    int oldColumnCount = 0;
    int newColumnCount = 0;
    // 表格单元格专用：改的是第几行第几列（0 基）；-1 表示不针对单元格。
    int rowIndex = -1;
    int columnIndex = -1;
    // 技术备注（ASCII），例如 "graphic content changed"。
    QString note;
};

// 版面差异的选项。
struct LayoutDiffOptions {
    // 单页最多比较多少个块（与 DocumentCompareOptions::maxBlocksPerPage 同义）。
    int maxBlocksPerPage = 400;
    // 复合配对的门槛：归一化 IoU 与文本相似度加权的分数要不低于它。
    //
    // 为什么不是 1.0：文字改写、表格少了一列，这些都会让相似度下降，
    // 门槛定到 1.0 就只能报「删掉 + 新增」，块级归因就没了。
    double matchThreshold = 0.45;
    // 判定「移动」的文本相似度门槛。
    //
    // 为什么不用 DocumentCompareOptions::modifyThreshold（0.5）：
    // 0.5 会把「改了几个字」判成「移动」，那是两回事。
    double moveTextThreshold = 0.98;
    // 判定「移动」的位移门槛（相对页面宽高归一化后的尺寸）。
    //
    // 实际门槛还要看块自身的大小（见 LayoutDiff.cpp）：位移太小不算挪动。
    double moveDisplacement = 0.004;
    // 判定「换图」的位置近似门槛（归一化 IoU）。
    double imageReplaceIou = 0.5;
    // 页与页之间配对的门槛（块指纹集合的 Jaccard）。
    double pagePairThreshold = 0.15;
    // 跨页配块时最多比较多少对页：这是 O(页数²) 的开销，得设上限。
    int maxCrossPageCandidates = 64;
    // 最多给多少页算像素指纹（渲染是重活）。
    int maxFingerprintPages = 200;
    // 表格单元格文字长度的上限（超过就当成正文，不当表格）。
    int maximumCellTextLength = 30;
};

// 版面差异的结果。
struct LayoutDiffResult {
    // 是否做出了版面分析。false 时 error 里有原因（中文）。
    bool analyzed = false;
    // 分析失败 / 做不了的原因（中文）；成功时为空。
    QString error;
    // 两边各自抽出的块数。
    int oldBlockCount = 0;
    int newBlockCount = 0;
    // 配上的块对数。
    int matchedBlockCount = 0;
    // 移动 / 新增 / 删除 / 改写的块数。
    int movedBlockCount = 0;
    int insertedBlockCount = 0;
    int deletedBlockCount = 0;
    int rewrittenBlockCount = 0;
    // 换图的次数。
    int imageReplacedCount = 0;
    // 表格结构变化的次数（行 / 列 / 单元格）。
    int tableRowChangeCount = 0;
    int tableColumnChangeCount = 0;
    int tableCellChangeCount = 0;
    // 完全没动的块数。
    int unchangedBlockCount = 0;
    // 差异明细，按页码、再按块序号排列。
    QVector<LayoutDifference> differences;
    // 技术备注（ASCII），例如因为页数上限少算了多少页的像素指纹。
    QString note;
};

// 比较两份 PDF 的版面结构差异。
//
// 逐页抽出版面块（文字段落 / 图片 / 表格 / 图形），再按
// 「内容指纹精确配对 → 归一化 IoU + 文本相似度贪心配对」的顺序匹配，
// 产出「哪一块被挪到了哪」「哪张表少了一列」这种块级归因。
//
// 失败时返回 false：打不开、页面读不出来时不会崩，原因写进 result->error。
bool compareDocumentLayout(const QByteArray& oldBytes,
                           const QByteArray& newBytes,
                           const LayoutDiffOptions& options,
                           LayoutDiffResult* result,
                           QString* error = nullptr);

// 只做块级匹配（不碰 PDF 文件）。
//
// 抽出版面块之后，匹配与归因全在这一步：拆开是为了让块级规则
// 能被单独验证，也方便别处复用。
LayoutDiffResult diffLayoutBlocks(const QVector<QVector<LayoutBlock>>& oldPages,
                                   const QVector<QVector<LayoutBlock>>& newPages,
                                   const LayoutDiffOptions& options);

}  // namespace aipdf::pdf
