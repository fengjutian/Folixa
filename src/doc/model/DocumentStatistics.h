#pragma once

#include "doc/model/DocumentChunk.h"

#include <QString>
#include <QVector>

namespace aipdf::doc {

// 每一页的统计。
//
// 为什么单独一页一份：字数面板（福昕那一类）会同时给「全文」与「逐页」两栏，
// 逐页那栏必须来自同一套口径，否则两栏对不上。
struct PageStatistics {
    // 页序号（从 0 开始），与 DocumentChunk::pageIndex 同一套编号。
    int pageIndex = -1;
    // 词数：中文按字、英文按词。
    int words = 0;
    // 字符数（含空白）。
    int characters = 0;
};

// 一篇文档的统计。
//
// 纯数据：这一层不认识界面，也不放任何文案（文案由 app 层拼）。
// 为什么抽成纯聚合层：同一份统计要出现在「字数统计面板」「导出报告」
// 「AI 回答的引用统计」等好几个地方，口径必须只有一份，
// 放在这里才能让各处都拿到同样的数字。
struct DocumentStatistics {
    // 页数。空页也算页。
    int pages = 0;
    // 词数：中文按字、英文按词（与 ChunkBuilder::countWords 同一口径）。
    int words = 0;
    // 字符数：含空白。
    int characters = 0;
    // 字符数：不含空白。
    //
    // 为什么给两种口径：福昕的统计面板把「字符数（含空格）」与
    // 「字符数（不计空格）」分开显示，用户对不上时会怀疑工具坏了。
    int charactersNoSpaces = 0;
    // 标题块数（headingLevel > 0）。
    int headings = 0;
    // 逐页明细，长度与 pages 一致：没有块的空页也在里面，计数为 0。
    QVector<PageStatistics> perPage;
};

// 字数统计的聚合器。
//
// 全部是静态纯函数：输入分块或纯文本，输出统计结果，不碰文件、不碰界面。
class DocumentStatisticsBuilder final {
public:
    // 从分块结果聚合。
    //
    // pageCount 用于补齐没有块的页（空页也要计入页数）。
    // 词数直接用块上已经算好的 wordCount，不再重算：
    // ChunkBuilder 算的时候用的是同一套口径，这里再算一遍就是第二套口径了。
    static DocumentStatistics fromChunks(const QVector<DocumentChunk>& chunks, int pageCount);

    // 直接从纯文本算（没有建索引时用）。
    //
    // 词数走 ChunkBuilder::countWords，保证与分块那条路口径一致。
    // 纯文本没有页边界，无法可靠切页：整段算在第一页，其余页记 0。
    static DocumentStatistics fromPlainText(const QString& text, int pageCount);
};

}  // namespace aipdf::doc
