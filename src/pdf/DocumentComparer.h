#pragma once

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QtGlobal>

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
    // 是否做跨文档自动对齐（页指纹 + 序列对齐）。
    //
    // 默认开：按页号位置配对在「中间插一页」时会整体后移，
    // 后面每一页都被报成「被改过」，这样的结果等于没用。
    // 关掉它就退回「第 i 页对第 i 页」，结果里的 alignmentMode 会写成 "position"。
    bool autoAlign = true;
    // 超过这个页数就不做序列对齐，退回按位置配对。
    //
    // 为什么要有上限：对齐要算 O(页数×页数) 的相似度矩阵，
    // 几千页的文档会让「比较」看起来像卡死了。
    // 退回按位置时 alignmentNote 里会写明原因。
    int maxAlignmentPages = 400;

    // 是否做像素级（渲染位图）比对。
    //
    // 默认关：把每一页画成位图再逐像素比，比抽文字慢一个数量级。
    // 打开它才能看出「纯图形改动」——只挪了表格线、换了颜色、画了个框，
    // 文字层一个字符都没动，文字层比对会报「两份文件一样」。
    bool comparePixels = false;
    // 像素比对的渲染缩放。zoom=1 表示 72 DPI（一像素一点）。
    //
    // 越大越能看出细小改动，代价是成平方级的开销；
    // 1.0 对「挪了几点」这种级别的改动已经够用。
    double pixelZoom = 1.0;
    // 单个颜色通道的容差（0..255）：两个像素每通道的差不超过它就算相同。
    //
    // 为什么默认 0：同一份 PDF 渲染两次应当逐位相同，
    // 放宽容差会把抗锯齿边缘的一两个色阶也算成差异，把真正的图形改动淹掉。
    int pixelColorTolerance = 0;
    // 视觉差异比例超过这个值，才算「这一页视觉上有差异」。
    double pixelDifferenceThreshold = 0.0;
    // 最多比对多少页的像素。
    //
    // 渲染是 O(页数 × 像素数) 的重活，不设上限的话几百页的文档
    // 会让「比较」看起来像卡死了。超出的部分记进结果里的 pixelNote。
    int maxPixelComparedPages = 200;
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

// 一页的视觉（像素级）差异。
//
// 为什么单列一个结构：文字层差异与视觉差异是两回事——
// 「把金额改了个数」文字层看得出来，「把表格线挪了两点」只有像素比对看得出来。
// 混在一张表里，用户分不清「这段文字变了」和「这块图形变了」。
struct VisualPageDifference {
    // 原文页号 / 新文页号（0 基）。
    int oldPageIndex = -1;
    int newPageIndex = -1;
    // 这一页能不能比。false 时 reason 里有原因（ASCII 技术细节）。
    bool comparable = true;
    // 比不了的原因（ASCII 技术细节），例如 "page render failed (zoom=1)"。
    QString reason;
    // 视觉差异比例：不同像素数 / 参与比较的像素数（0..1）。
    double differenceRatio = 0.0;
    // 参与比较的像素数 / 不同的像素数。
    qint64 comparedPixelCount = 0;
    qint64 differingPixelCount = 0;
    // 两页渲染出来的尺寸是否不同（不同 → 只比了左上角对齐的共同区域）。
    bool sizeMismatch = false;
    // 差异区域在原文 / 新文里的包围盒（PDF user space，左下原点）。
    //
    // 注意 QRectF 的 top/bottom 在这里是「谁的 y 小谁叫 top」：
    // top() 是较小的那个 y（PDF 的**下**沿），bottom() 是较大的那个 y。
    // 与 TextBlock::bounds 同一约定，同一套高亮代码两边都能用。
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

    // ------------------------------------------------------ 跨文档自动对齐 ----
    //
    // 下面这些字段说明「这次比较是按页码位置配的页，还是按内容对齐配的页」。
    // 领域层只给结构化字段，界面上的文案由 app 层拼。

    // 这次是不是真的用了序列对齐。false = 退回按页码位置配对了。
    bool alignmentApplied = false;
    // 用的哪种配对口径："position"（按页码位置）/ "sequence"（序列对齐）。
    QString alignmentMode = QStringLiteral("position");
    // 配成的「同一页」页对数（不管是用了哪种口径）。
    int alignedPairCount = 0;
    // 一边独有的页数：原文独有的 / 新文独有的。
    // 与 removedPageCount / addedPageCount 同口径（都按对齐结果算）。
    int oldOnlyPageCount = 0;
    int newOnlyPageCount = 0;
    // 一边独有的页码（0 基），方便界面点「跳到那一页」。
    QVector<int> oldOnlyPageIndices;
    QVector<int> newOnlyPageIndices;
    // 技术备注（ASCII），例如
    // "page count exceeds alignment limit; fell back to position pairing"。
    QString alignmentNote;

    // ---------------------------------------------------------- 视觉差异 ----
    //
    // 下面这些字段只有开了 comparePixels 时才有意义；
    // 没开时 pixelsCompared 为 false，其余都是零值。
    // 文字层的差异结构（differences 与上面那些计数）一个字都没动。

    // 是否做了像素级（渲染位图）比对。
    bool pixelsCompared = false;
    // 像素级比对里：成功比对的页数 / 比不了的页数。
    int pixelComparedPageCount = 0;
    int pixelIncomparablePageCount = 0;
    // 视觉上有差异的页数（差异比例超过阈值）。
    int visualDifferencePageCount = 0;
    // 平均 / 最大视觉差异比例（0..1）。
    double averageVisualDifferenceRatio = 0.0;
    double maxVisualDifferenceRatio = 0.0;
    // 逐页的像素比对结果：视觉上有差异的页 + 比不了的页。
    //
    // 比得了但没有差异的页不进这个列表——它们没有信息量；
    // 「一共比了多少页」看 pixelComparedPageCount。
    QVector<VisualPageDifference> visualDifferences;
    // 像素比对整体失败的原因（ASCII 技术细节）；没失败时为空。
    QString pixelError;
    // 像素比对的备注（ASCII 技术细节），例如因为页数上限跳过了几页。
    QString pixelNote;
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
