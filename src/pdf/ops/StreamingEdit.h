#pragma once

#include <QByteArray>
#include <QColor>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// ============================================================================
// 这个模块做什么、不做什么
// ============================================================================
//
// 做三件事，全部在**领域层**（不依赖界面）：
//   1. **全文流式改写**：把整篇文档的每一段文字按新的页宽重新流排并写回。
//      写回走仓库既有的内容流通道（pdf::ContentStream 的词法 + qpdf 的页内容流），
//      不新建另一条写回路径。
//   2. **跨文本块合并**：选区跨多个 Tj/TJ 操作数或多个文本块时，
//      合成一段连续段落并自动换行；块与块之间的样式逐块对应。
//   3. 上面两条共用的底座：**把一份 PDF 的内容流拆成「文本块」**，
//      以及**把新的文本块写回内容流**。
//
// 不做——这是取舍，不是缺陷：
//   * **不改页数、不重新分页**。改宽度只会让段落变高；段落变高之后
//     会不会压到下一段，是版面层面的事，本模块只负责把字重新排好，
//     并把「排不下」这件事如实报告出来。
//   * **不碰没有结构标签的文档的整篇重排**。没有标签树就没有可靠的
//     段落边界，这种文档**直接拒绝**整篇重排并说明「只能逐段处理」，
//     不用启发式硬凑一个看着像的版面。
//   * **不写标准 14 号字体表示不了的字**。写不出来的字会被写成 '?'，
//     本模块把这种情况**如实报告**（degraded），绝不假装写成功了；
//     翻译写回那一条更进一步：译文写不进去就整段保留原文。
//
// **门禁**：写回之前先算一遍「重排后应该有多少段、多少字」，
// 写回之后**重新打开、重新统计**，两边对不上就**不写回**，
// 并在 report.error 里写明少了什么。
// 重新打开用的是 qpdf / PDFium（独立于本模块的解析器），不拿自己写的代码自证。

// ---------------------------------------------------------------------------
// 内容流文本块模型（下面三个模块共用）
// ---------------------------------------------------------------------------

// 一段文字的样式。
//
// 为什么样式要**逐块**记下来：合并选区时不能把整段统一成一种样式——
// 标题还是标题、正文还是正文、注释还是注释。
struct TextRunStyle {
    // 页面 /Resources /Font 里的字体资源名（例如 "F1"）。
    QString fontResource = QStringLiteral("F1");
    // 字号（点）。
    double fontSize = 12.0;
    // 非描边颜色。
    QColor color = Qt::black;
    // 颜色是不是真的读到了（读不到时不能瞎写一个颜色进内容流）。
    bool colorKnown = false;
    // 字体族名（只是量宽度用的近似值，不写进 PDF）。
    QString fontFamily = QStringLiteral("Helvetica");
    // 行距倍数。
    double lineHeight = 1.2;
};

// 内容流里的一段文字（= 一个文本块 = 一个段落）。
struct ContentTextBlock {
    // 所在页（0 基）。
    int pageIndex = -1;
    // 文档内唯一的块号（0 基，按页与页内次序）。
    int blockId = -1;
    // 标记内容编号（MCID）。-1 表示这一段没有标记内容。
    int mcid = -1;
    // 块里的文字（各显示操作拼起来）。
    QString text;
    // 这一段的样式（取第一个显示操作的样式）。
    TextRunStyle style;
    // 这一段在页面上的大致范围（PDF user space，左下原点）。
    QRectF bounds;
    // 第一行的基线 y 与起点 x（写回时的新行从这里往下排）。
    double firstBaseline = 0.0;
    double firstX = 0.0;
    // 写回用的定位：这一段的显示操作在内容流 token 里的位置。
    // 一段可能跨多个 BT…ET 区域，所以是「每段区域一个区间」。
    struct Region {
        int firstOperandToken = -1;
        int lastOperatorToken = -1;
    };
    QVector<Region> regions;
    // 这一段与下一段之间是不是夹着图片 / 矢量 / 表格。
    bool imageBetween = false;
    bool tableBetween = false;
    // 字体族（量宽度用）。
    QString fontFamily = QStringLiteral("Helvetica");
    // 这一段之前页面上的填充色（写回时用它把颜色状态还原回去）。
    QColor ambientColor = Qt::black;
    bool ambientColorKnown = false;
    // 写回时用的文本矩阵与 CTM（第一行显示操作时的状态）。
    //
    // 为什么留着：换行之后要把文字放到新的位置上，但**朝向**不能变。
    // 只改平移部分、线性部分沿用原来的，旋转与缩放才不会被改掉。
    double tmA = 1.0, tmB = 0.0, tmC = 0.0, tmD = 1.0;
    double ctmE = 0.0, ctmF = 0.0;
};

// 读取一份 PDF 的内容流文本块。
//
// 失败（error 非空）的情况：PDF 解析不了、内容流词法出错。
// 空文档不是失败：返回空列表，页数为 0。
bool readContentTextBlocks(const QByteArray& pdfBytes, QVector<ContentTextBlock>* blocks,
                            QString* error);

// 一行里的一个片段（片段自带样式）。
struct TextPiece {
    QString text;
    TextRunStyle style;
};

// 一段文字的新内容：若干行，每行若干片段。
struct BlockRewrite {
    int pageIndex = -1;
    // 被改写的块 id 列表。
    //
    // 第一个块的位置用来放新内容，其余的块清空——合并多个块时，
    // 合并后的内容落在第一个块的位置上，后面的块不能让它们还留着字。
    QVector<int> blockIds;
    // 新内容的每一行。
    QVector<QVector<TextPiece>> lines;
    // 第一行的基线 y 与起点 x（用户空间）。
    double firstBaseline = 0.0;
    double firstX = 0.0;
    // 行距（点）。相邻两行的基线之差。
    double lineStep = 0.0;
    // 每一行是不是都从 firstX 起排（否则按 lineX 走）。
    bool leftAligned = true;
    // 每一行自己的起点 x（leftAligned 为假时用）。
    QVector<double> lineX;
};

// 一段（= 一个段落）的文字。
//
// 「段」的口径：**一个块级结构元素**（P、H1、TD…）里的全部文字。
// 标签树里一个 P 下面挂几个 Span 是常事，那几个 Span 仍然是一段。
struct TextParagraph {
    int pageIndex = -1;
    // 这一段包含的文本块（按页面上的先后）。写回时第一个放新内容，其余清空。
    QVector<int> blockIds;
    QString text;
    // 这一段的样式（取第一个块的）。
    TextRunStyle style;
    // 字体族（量宽度用）。
    QString fontFamily = QStringLiteral("Helvetica");
    QRectF bounds;
    double firstBaseline = 0.0;
    double firstX = 0.0;
    // 这份文档有没有结构标签。
    bool hasStructTags = false;
};

// 把内容流里的文本块按标签树归成段落。
//
// 没有标签树时按内容流里的段落归（一个块一段），并在 hasStructTags 里标出来——
// 调用方据此如实说明「这只是逐段处理，不是整篇流式」。
QVector<TextParagraph> groupBlocksIntoParagraphs(const QByteArray& pdfBytes,
                                                 const QVector<ContentTextBlock>& blocks,
                                                 QString* error);

// 一段文字写回时能占用的下边界（点）。
//
// 为什么要看「下一段」：译文（或者窄栏重排后的文字）比原文长时会往下长，
// 长到下一段头上就把两段压在一起了——那不是「放得下」。
// 所以真正的下边界是「页底下沿」与「下一段上沿」里更高的那个。
//
// paragraphs 必须按页面上的先后排好（groupBlocksIntoParagraphs 的顺序即是）。
double paragraphBottomLimit(const QVector<TextParagraph>& paragraphs, int index,
                            const QVector<QRectF>& pageBoxes);

// 用 PDFium 抽出一份 PDF 每页的文字。
//
// 为什么要有这个：写回之后必须用**独立于本模块**的解析器回读校验，
// 不能拿自己写的解析器自证。PDFium 是渲染器，它认得的才算数。
//
// 失败（error 非空）的情况：PDF 打不开。
QStringList extractPageTexts(const QByteArray& pdfBytes, int* pageCount, QString* error);

// 读每一页的 MediaBox（PDF user space）。
//
// 失败（error 非空）的情况：PDF 解析不了。
QVector<QRectF> readPageMediaBoxes(const QByteArray& pdfBytes, QString* error);

// 把新的文字内容写回 PDF 的内容流。
//
// 写回用的是仓库既有的内容流通道：pdf::ContentStream 的词法分析与序列化
// + qpdf 的页内容流读写（与 pdf::ContentStream::rewritePdfTextWith 同一条路）。
// 写回之后**不**在这里校验——校验由调用方用独立解析器做。
QByteArray applyBlockRewrites(const QByteArray& pdfBytes, const QVector<BlockRewrite>& rewrites,
                              QString* error);

// ---------------------------------------------------------------------------
// 全文流式改写
// ---------------------------------------------------------------------------

// 全文重排的参数。
struct StreamingReflowOptions {
    // 目标页宽（点）。必须大于 0。
    double targetPageWidth = 0.0;
    // 页边距（点）。正文栏宽 = 目标页宽 - 左边距 - 右边距。
    double marginLeft = 36.0;
    double marginRight = 36.0;
    // 行距倍数。
    double lineHeight = 1.2;
    // 没有结构标签时是否拒绝整篇重排。
    //
    // 默认真：没有标签树就没有可靠的段落边界，硬凑出来的版面不能叫
    // 「全文流式重排」。把这一项关掉时，报告里会写明
    // 「这份文档没有结构标签，本次只按内容流里的段落逐段处理」。
    bool requireStructTags = true;
    // 没有结构标签时用来量宽度的字体族与字号（只影响断行，不写进 PDF）。
    QString fontFamily = QStringLiteral("Helvetica");
    double fontSize = 12.0;
};

// 全文重排的结果里的一段。
struct ReflowedBlock {
    int blockId = -1;
    int pageIndex = -1;
    QString originalText;
    QString reflowedText;
    int originalLineCount = 0;
    int reflowedLineCount = 0;
    bool written = false;
    // 没写进去的中文原因（为空表示写进去了）。
    QString reason;
};

// 全文重排的报告。
struct StreamingReflowReport {
    int pageCount = 0;
    // 源文档里的段数。
    int blockCount = 0;
    // 实际重排并写回的段数。
    int reflowedBlockCount = 0;
    // 源文档的总字数。
    int sourceCharCount = 0;
    // 写回之后的总字数（门禁比的就是它）。
    int outputCharCount = 0;
    // 这份文档有没有结构标签。
    bool hasStructTags = false;
    // 文字里有标准 14 号字体表示不了的字符、被写成 '?' 占位符的段数。
    int degradedBlockCount = 0;
    QVector<ReflowedBlock> blocks;
    QStringList notes;
    // 失败原因（中文）。为空表示成功。
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// 全文重排的结果。
struct StreamingReflowResult {
    // 重排后的 PDF 字节。失败时为空。
    QByteArray bytes;
    StreamingReflowReport report;

    bool ok() const { return report.ok(); }
};

// 把整篇文档的每一段按新页宽重新流排并写回。
//
// 失败（report.error 非空、bytes 为空）的情况：
//   * 输入不是 PDF（或内容为空）；
//   * 目标页宽不大于 0，或边距把可用宽度吃光了；
//   * **这份文档没有结构标签**（requireStructTags 为真时）：
//     直接拒绝，并在 error 里写明「只能逐段处理」；
//   * 内容流解析不了；
//   * **门禁没过**：写回后重新统计，段数或字数比源文档少，
//     说明有内容没写进去。这种情况下**不写回**，error 里写明少了什么。
StreamingReflowResult reflowDocumentStream(const QByteArray& pdfBytes,
                                            const StreamingReflowOptions& options);

// ---------------------------------------------------------------------------
// 跨文本块合并
// ---------------------------------------------------------------------------

// 合并的参数。
struct MergeBlocksOptions {
    // 合并后的目标栏宽（点）。<= 0 表示沿用第一个块的原宽度。
    double targetWidth = 0.0;
    // 块之间的连接符（默认一个空格）。
    QString separator = QStringLiteral(" ");
    // 行距倍数。
    double lineHeight = 1.2;
};

// 合并的报告。
struct MergeBlocksReport {
    // 文档页数。
    int pageCount = 0;
    // 参与合并的块数。
    int blockCount = 0;
    // 合并之后的行数。
    int mergedLineCount = 0;
    // 合并前的总字数 / 合并后的总字数（门禁比的就是它们）。
    int sourceCharCount = 0;
    int outputCharCount = 0;
    // 块与块之间是不是夹着图片 / 矢量 / 表格。
    bool imageBetween = false;
    bool tableBetween = false;
    // 被合并掉的块 id（写回后这些块不再单独存在）。
    QVector<int> mergedBlockIds;
    QStringList notes;
    // 失败原因（中文）。为空表示成功。
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// 合并的结果。
struct MergeBlocksResult {
    QByteArray bytes;
    MergeBlocksReport report;

    bool ok() const { return report.ok(); }
};

// 把若干个文本块合并成一段连续的段落（自动换行）并写回。
//
// 选区跨多个 Tj/TJ 操作数或多个文本块时用这个：
//   * 块里的文字按**先后顺序**拼成一段，再按目标栏宽重新断行；
//   * 写回时**每一块自己的样式**跟着它自己那段文字走，
//     不会把整段统一成一种样式；
//   * 块与块之间夹着图片 / 矢量 / 表格时**明确拒绝**，
//     并在 error 里写明「块间有图 / 有表」——
//     合并会把中间的图挤掉，这种事不能悄悄做。
//
// 失败（report.error 非空、bytes 为空）的情况：
//   * 输入不是 PDF；
//   * 块 id 不存在；
//   * 块少于两个（没有可合并的东西）；
//   * 块与块之间夹着图片 / 矢量 / 表格；
//   * **门禁没过**：写回后重新统计，字数比合并前少。
MergeBlocksResult mergeTextBlocksAcrossSelection(const QByteArray& pdfBytes,
                                                 const QVector<int>& blockIds,
                                                 const MergeBlocksOptions& options);

}  // namespace aipdf::pdf
