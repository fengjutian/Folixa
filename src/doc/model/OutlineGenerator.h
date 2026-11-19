#pragma once

#include "doc/model/DocumentChunk.h"
#include "pdf/compliance/StructTreeReader.h"
#include "pdf/kernel/Navigation.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <vector>

namespace aipdf::pdf {

// 自动创建书签的选项。
//
// 纯数据，不含文案：界面上的开关直接对应这里的字段，
// 领域层只管"怎么把标题变成树"，不管"怎么跟用户解释"。
struct OutlineGeneratorOptions {
    // 小于这个层级的标题不生成书签（层级 0 是正文，本来就不生成）。
    int minHeadingLevel = 1;
    // 最多嵌套几层。超过的层级会被压到最深的这一层，
    // 而不是被丢掉——丢标题等于丢大纲。
    int maxDepth = 6;
    // 标题取块文本的第一行。
    //
    // 为什么默认只取第一行：一个标题块里可能还带着紧随其后的正文，
    // 整段塞进书签会让侧栏变成一堵墙。
    bool useFirstLineAsTitle = true;
    // 标题最长多少个字符（含省略号）。小于等于 0 表示不限制。
    int maxTitleLength = 120;
};

// 标题是从哪儿来的。
//
// 为什么非要分清楚：自动创建书签有两条判标题的路——
// 读文档自己的结构标签（/StructTreeRoot），或按字高比正文大来估算。
// 两条路的可靠性差很多：标签是作者/工具给的，估算是我们猜的。
// 界面上必须能把它们分开说，**不能把估算说成"读标签树读出来的"**。
enum class OutlineTitleSource {
    // 读文档自己的结构标签，用里面的标题元素（H1–H6）当标题。
    StructTree,
    // 按字高比正文大来估算（文档没有结构标签时的回退）。
    FontHeuristic,
};

// 自动创建书签的结果。
struct OutlineGenerationResult {
    // 生成的大纲树。
    std::vector<OutlineItem> outline;
    // 标题来源。
    OutlineTitleSource source = OutlineTitleSource::FontHeuristic;
    // 中文说明。
    //
    // 走字高估算时必填：写明「本文档没有结构标签，标题是按字高估算的」，
    // 以及估算的真实原因（读不到标签树 / 标签树里没有标题元素）。
    // 读标签树时为空串。
    QString note;
    // 用到的标题个数。
    int headingCount = 0;

    // 标题是读标签树读出来的吗？
    bool fromStructTree() const { return source == OutlineTitleSource::StructTree; }
    // 标题是按字高估算的吗？
    bool isEstimated() const { return source == OutlineTitleSource::FontHeuristic; }
};

// 按标签树里的标题元素生成大纲。
//
// 标题层级取元素的结构类型（H1–H6）；通用的 /H（层级写在 /A 属性里）
// 拿不到属性，退回按"在标签树里嵌了几层"推。
// 标题文字取该元素下的文字：自己没有就取第一个有文字的后代
// （很多文档是 H1 → Span 这样，文字挂在后代上）。
//
// 标签树里一个标题元素都没有时，返回空大纲——**不在这里偷偷回退到字高估算**，
// 回退由 generateOutline 统一决定，并把"为什么回退"写进说明里。
OutlineGenerationResult generateOutlineFromStructTree(const StructTreeReport& structTree,
                                                      const OutlineGeneratorOptions& options = {});

// 自动创建书签的统一入口（调用方已经读过标签树）。
//
// 有结构标签就按标签层级判标题；没有（或者标签树里没有标题元素）
// 就退回字高估算，并在结果里标明是估算的。
OutlineGenerationResult generateOutline(const StructTreeReport& structTree,
                                        const QVector<doc::DocumentChunk>& chunks,
                                        const OutlineGeneratorOptions& options = {});

// 自动创建书签的统一入口（调用方给的是 PDF 字节）。
//
// 这一层自己去读标签树（pdf::readStructTree），省得每个调用方都自己读一遍、
// 自己判断该不该回退——回退规则散在两处就一定会走歪。
//
// 注意：读标签树要 qpdf。本仓库的可选依赖没启用时只能估算，
// 这时结果里的说明会写明「当前构建没有启用 qpdf，读不了结构标签」。
OutlineGenerationResult generateOutline(const QByteArray& pdfBytes,
                                        const QVector<doc::DocumentChunk>& chunks,
                                        const OutlineGeneratorOptions& options = {});

// 从分块结果生成大纲树。
//
// headingLevel 决定嵌套层级：level 1 → 顶层；level 2 → 挂到最近的一个
// level 1 下面；level 3 → 挂到最近的 level 2 下面，依此类推。
// 层级跳跃（正文后直接出现 level 3）会向下找最近的可用父级，不会丢标题。
//
// 这个入口是**字高估算**那条路（标题层级是 ChunkBuilder 按字高比正文大判的）。
// 要走"读标签树"那条路请用 generateOutline / generateOutlineFromStructTree。
//
// 为什么依赖 doc::DocumentChunk 而不是自己定义一个输入结构：
// 标题识别已经在 ChunkBuilder 里做完了（headingLevel、pageIndex、text），
// 再定义一份结构体就等于让调用方把块手工拆一遍——多一次转换，
// 还多一处"两边对不上"的可能。pdf → doc 的依赖方向仓库里已经有了
// （DocumentWriter.cpp 就 include pdf/kernel/SafeSave.h）。
std::vector<OutlineItem> generateOutlineFromChunks(const QVector<doc::DocumentChunk>& chunks,
                                                   const OutlineGeneratorOptions& options = {});

}  // namespace aipdf::pdf
