#pragma once

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 两份 PDF 之间的一条差异的种类。
//
// 为什么分这么细：只说「第 3 页不一样」没法核对，
// 必须能分清「这一段是新增的」「这一段被删了」「这一段被改过」，
// 用户才知道该去看哪里。
enum class DiffKind {
    // 只在新文里：新文多出来的文字块。
    Inserted,
    // 只在原文里：原文有、新文里没有的文字块。
    Deleted,
    // 两边都有，但内容被改过。
    Modified,
    // 新文多出来的整页。
    PageAdded,
    // 原文有、新文里没有的整页。
    PageRemoved,
};

// 一页里的一个文字块。
//
// 「块」的定义：同一行里连续的一段文字（横向间隔过大时再切开）。
// 为什么按块比而不是按页比：页级只能说出「第 3 页不一样」，
// 块级能指出「第 3 页的第 2 块」，并把原文与新文并排放出来核对。
struct TextBlock {
    // 页序号（0 基）。
    int pageIndex = -1;
    // 页内序号（按阅读顺序，从 0 开始）。
    int blockIndex = -1;
    // 块里的文字（首尾空白已去掉）。
    QString text;
    // 块的包围盒：PDF user space（左下原点，y 向上）。
    // 与 PdfiumAdapter 给出的字符框同一坐标系，可以直接拿去高亮。
    QRectF bounds;
};

// 比较选项。
struct DocumentCompareOptions {
    // 原文的打开密码。文件加密时必须给，否则打不开。
    QString oldPassword;
    // 新文的打开密码。
    QString newPassword;
    // 块的相似度低于这个值就算「删掉 + 新增」，不算「修改」。
    // 为什么要有这个门槛：两个毫不相干的段落相似度接近 0，
    // 说成「修改」会让人以为只是改了几个字。
    double modifyThreshold = 0.5;
    // 页的相似度低于这个值就算「删页 + 加页」，不算「同一页被改过」。
    double pageThreshold = 0.2;
    // 单页最多比较多少个块：对齐用的是 O(n×m) 的动态规划，
    // 不设上限的话，一页塞满文字的畸形文档能把内存吃光。
    int maxBlocksPerPage = 400;
};

// 一条差异。
//
// 每条都带页码与原文片段：这是「引用定位」的全部依据，
// 界面拿它跳页、拿 oldText/newText 给人核对。
struct CompareDifference {
    DiffKind kind = DiffKind::Inserted;
    // 原文页号（0 基）；-1 表示原文没有这一处。
    int oldPageIndex = -1;
    // 新文页号（0 基）；-1 表示新文没有这一处。
    int newPageIndex = -1;
    // 页内块序号；-1 表示没有对应块。
    int oldBlockIndex = -1;
    int newBlockIndex = -1;
    // 原文片段 / 新文片段。
    QString oldText;
    QString newText;
    // 相似度（0..1）。页级差异给的是整页的相似度。
    double similarity = 0.0;
    // 原文 / 新文里这一处的位置（PDF user space）。
    QRectF oldBounds;
    QRectF newBounds;
};

// 比较结果。
struct DocumentCompareResult {
    // 是否比较成功。失败时 error 里有中文原因。
    bool succeeded = false;
    // 失败原因（中文）。成功时为空。
    QString error;
    int oldPageCount = 0;
    int newPageCount = 0;
    // 两边各自抽出的块总数。
    int oldBlockCount = 0;
    int newBlockCount = 0;
    // 成对比较的页数。
    int comparedPageCount = 0;
    // 新增 / 删除的页数。
    int addedPageCount = 0;
    int removedPageCount = 0;
    // 新增 / 删除 / 修改的块数。
    int insertedBlockCount = 0;
    int deletedBlockCount = 0;
    int modifiedBlockCount = 0;
    // 两边完全一致的块数。
    int unchangedBlockCount = 0;
    // 原文 / 新文有没有文字层。
    //
    // 两边都没有时不能报「没有差异」——那会把「比不了」说成「一样」，
    // 所以那种情况直接当失败处理（error 里写明原因）。
    bool oldHasText = false;
    bool newHasText = false;
    // 整体相似度（0..1）。
    double overallSimilarity = 1.0;
    // 差异明细，按页码、再按块序号排列。
    QVector<CompareDifference> differences;
};

// 比较两份 PDF 的结构差异。
//
// 逐页比对文字块，产出「哪一页、哪一块、原文是什么、新文是什么」的差异清单。
// 页面增删也会报出来。
//
// 失败时返回 false：打不开、页数对不上以外的其它坏情况都不会崩，
// 原因写进 result->error 与 error 参数（中文）。
//
// 注意：这层是领域层，不含任何界面文案，也不放 tr()。
bool compareDocuments(const QByteArray& oldBytes,
                      const QByteArray& newBytes,
                      const DocumentCompareOptions& options,
                      DocumentCompareResult* result,
                      QString* error = nullptr);

// 比较两个文件。读文件失败也会给出中文原因。
bool compareDocumentFiles(const QString& oldPath,
                          const QString& newPath,
                          const DocumentCompareOptions& options,
                          DocumentCompareResult* result,
                          QString* error = nullptr);

// 抽出一份 PDF 每页的文字块（比较的底层动作，界面做高亮时也能用）。
//
// 打开失败返回 false 并写明原因；文件加密时 password 给对了才能打开。
bool extractTextBlocks(const QByteArray& bytes,
                       QVector<QVector<TextBlock>>* pages,
                       QString* error = nullptr,
                       const QString& password = QString());

// 两个文本的相似度（0..1）。
//
// 短文本用归一化 Levenshtein，长文本改用二元组 Dice：
// 一万字的段落做 Levenshtein 要一亿次比较，等于卡死界面。
double textSimilarity(const QString& left, const QString& right);

}  // namespace aipdf::pdf
