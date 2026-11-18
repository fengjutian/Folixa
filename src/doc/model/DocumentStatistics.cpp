#include "doc/model/DocumentStatistics.h"

#include "doc/model/ChunkBuilder.h"

namespace aipdf::doc {

namespace {

// 不算空白的字符个数。
//
// 用 QChar::isSpace：空格、制表、换行都算空白，
// 与「复制出去的一段文字里有多少个非空白字符」这个直觉一致。
int countNonSpaceCharacters(const QString& text) {
    int count = 0;
    for (const QChar ch : text) {
        if (!ch.isSpace()) {
            ++count;
        }
    }
    return count;
}

}  // namespace

DocumentStatistics DocumentStatisticsBuilder::fromChunks(const QVector<DocumentChunk>& chunks,
                                                         int pageCount) {
    DocumentStatistics statistics;

    // 页数：调用方给的页数是准的（空页没有块，但页数必须算进去）。
    // 块里出现了更靠后的页时以块为准，否则那一页的字会被算到不存在的页里。
    int lastPage = pageCount - 1;
    for (const DocumentChunk& chunk : chunks) {
        if (chunk.pageIndex > lastPage) {
            lastPage = chunk.pageIndex;
        }
    }
    statistics.pages = lastPage + 1;
    if (statistics.pages < 0) {
        statistics.pages = 0;
    }

    // 逐页明细先铺满：空页也要有一行，计数为 0。
    // 为什么先铺满再累加：后面对不上的页号只能丢块，
    // 而"这一页没有字"这件事必须让界面看得见（否则总字数与逐页之和对不上）。
    statistics.perPage.resize(statistics.pages);
    for (int index = 0; index < statistics.pages; ++index) {
        statistics.perPage[index].pageIndex = index;
    }

    for (const DocumentChunk& chunk : chunks) {
        if (chunk.pageIndex < 0 || chunk.pageIndex >= statistics.pages) {
            continue;
        }
        // 词数用块上已经算好的值：ChunkBuilder 用的就是这套口径，
        // 在这里重算只会引入第二套口径（而且块文本可能被上层改过）。
        statistics.words += chunk.wordCount;
        statistics.characters += chunk.text.size();
        statistics.charactersNoSpaces += countNonSpaceCharacters(chunk.text);
        if (chunk.headingLevel > 0) {
            ++statistics.headings;
        }
        PageStatistics& page = statistics.perPage[chunk.pageIndex];
        page.words += chunk.wordCount;
        page.characters += chunk.text.size();
    }
    return statistics;
}

DocumentStatistics DocumentStatisticsBuilder::fromPlainText(const QString& text, int pageCount) {
    DocumentStatistics statistics;
    statistics.pages = pageCount > 0 ? pageCount : 0;
    // 词数走同一套口径：中文按字、英文按词。
    statistics.words = ChunkBuilder::countWords(text);
    statistics.characters = text.size();
    statistics.charactersNoSpaces = countNonSpaceCharacters(text);

    statistics.perPage.resize(statistics.pages);
    for (int index = 0; index < statistics.pages; ++index) {
        statistics.perPage[index].pageIndex = index;
    }
    if (statistics.pages > 0) {
        // 纯文本没有页边界，硬切只会切出假数据：
        // 整段算在第一页，其余页记 0，界面据此知道逐页数据只是估算。
        statistics.perPage[0].words = statistics.words;
        statistics.perPage[0].characters = statistics.characters;
    }
    return statistics;
}

}  // namespace aipdf::doc
