#pragma once

#include "doc/model/DocumentChunk.h"
#include "pdf/kernel/Navigation.h"

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

// 从分块结果生成大纲树。
//
// headingLevel 决定嵌套层级：level 1 → 顶层；level 2 → 挂到最近的一个
// level 1 下面；level 3 → 挂到最近的 level 2 下面，依此类推。
// 层级跳跃（正文后直接出现 level 3）会向下找最近的可用父级，不会丢标题。
//
// 为什么依赖 doc::DocumentChunk 而不是自己定义一个输入结构：
// 标题识别已经在 ChunkBuilder 里做完了（headingLevel、pageIndex、text），
// 再定义一份结构体就等于让调用方把块手工拆一遍——多一次转换，
// 还多一处"两边对不上"的可能。pdf → doc 的依赖方向仓库里已经有了
// （DocumentWriter.cpp 就 include doc/persist/SafeSave.h）。
std::vector<OutlineItem> generateOutlineFromChunks(const QVector<doc::DocumentChunk>& chunks,
                                                   const OutlineGeneratorOptions& options = {});

}  // namespace aipdf::pdf
