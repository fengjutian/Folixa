#include "doc/DocumentStatistics.h"

#include "doc/ChunkBuilder.h"

#include <QTest>

using aipdf::doc::DocumentChunk;
using aipdf::doc::DocumentStatistics;
using aipdf::doc::DocumentStatisticsBuilder;
using aipdf::doc::PageStatistics;

namespace {

// 造一个块：页码、文本、标题层级、词数都能自己定。
DocumentChunk makeChunk(int pageIndex,
                         const QString& text,
                         int headingLevel = 0,
                         int wordCount = -1) {
    DocumentChunk chunk;
    chunk.pageIndex = pageIndex;
    chunk.order = pageIndex;
    chunk.text = text;
    chunk.headingLevel = headingLevel;
    chunk.wordCount = wordCount >= 0 ? wordCount : aipdf::doc::ChunkBuilder::countWords(text);
    return chunk;
}

}  // namespace

class DocumentStatisticsTests : public QObject {
    Q_OBJECT

private slots:
    void countsChineseCharactersAsWords();
    void countsEnglishWordsByWord();
    void countsMixedText();
    void countsCharactersWithAndWithoutSpaces();
    void emptyTextGivesZeroCounts();
    void aggregatesChunkTotals();
    void usesChunkWordCountVerbatim();
    void countsHeadingBlocks();
    void fillsEmptyPages();
    void keepsPerPageOrder();
    void plainTextMatchesChunkBuilderCount();
};

// 中文按字算：一个字就是一个词。
void DocumentStatisticsTests::countsChineseCharactersAsWords() {
    const DocumentStatistics statistics =
        DocumentStatisticsBuilder::fromPlainText(QStringLiteral("你好世界"), 1);
    QCOMPARE(statistics.words, 4);
    QCOMPARE(statistics.characters, 4);
    QCOMPARE(statistics.charactersNoSpaces, 4);
    QCOMPARE(statistics.pages, 1);
}

// 英文按词算：一串连续的字母算一个词。
void DocumentStatisticsTests::countsEnglishWordsByWord() {
    const DocumentStatistics statistics =
        DocumentStatisticsBuilder::fromPlainText(QStringLiteral("hello world"), 1);
    QCOMPARE(statistics.words, 2);
    QCOMPARE(statistics.characters, 11);
    QCOMPARE(statistics.charactersNoSpaces, 10);
}

// 中英混排：两种规则各管各的。
void DocumentStatisticsTests::countsMixedText() {
    const DocumentStatistics statistics =
        DocumentStatisticsBuilder::fromPlainText(QStringLiteral("中文 mixed 文本"), 1);
    QCOMPARE(statistics.words, 5);
    QCOMPARE(statistics.characters, 11);
    QCOMPARE(statistics.charactersNoSpaces, 9);
}

// 字符数的两种口径：含空白与不含空白必须分开给。
void DocumentStatisticsTests::countsCharactersWithAndWithoutSpaces() {
    const DocumentStatistics statistics =
        DocumentStatisticsBuilder::fromPlainText(QStringLiteral("a b  c"), 1);
    QCOMPARE(statistics.words, 3);
    QCOMPARE(statistics.characters, 6);
    QCOMPARE(statistics.charactersNoSpaces, 3);
}

// 空文本：全是 0，但页数仍然按传入的算。
void DocumentStatisticsTests::emptyTextGivesZeroCounts() {
    const DocumentStatistics statistics = DocumentStatisticsBuilder::fromPlainText(QString(), 3);
    QCOMPARE(statistics.pages, 3);
    QCOMPARE(statistics.words, 0);
    QCOMPARE(statistics.characters, 0);
    QCOMPARE(statistics.charactersNoSpaces, 0);
    QCOMPARE(statistics.headings, 0);
    QCOMPARE(statistics.perPage.size(), 3);
    for (const PageStatistics& page : statistics.perPage) {
        QCOMPARE(page.words, 0);
        QCOMPARE(page.characters, 0);
    }
}

// 分块聚合：总量与逐页都要对上。
void DocumentStatisticsTests::aggregatesChunkTotals() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeChunk(0, QStringLiteral("你好世界")));
    chunks.append(makeChunk(0, QStringLiteral("hello world")));
    chunks.append(makeChunk(1, QStringLiteral("第三章"), 1));

    const DocumentStatistics statistics = DocumentStatisticsBuilder::fromChunks(chunks, 2);
    QCOMPARE(statistics.pages, 2);
    QCOMPARE(statistics.words, 9);
    QCOMPARE(statistics.characters, 18);
    QCOMPARE(statistics.charactersNoSpaces, 17);
    QCOMPARE(statistics.headings, 1);

    QCOMPARE(statistics.perPage.size(), 2);
    QCOMPARE(statistics.perPage.at(0).pageIndex, 0);
    QCOMPARE(statistics.perPage.at(0).words, 6);
    QCOMPARE(statistics.perPage.at(0).characters, 15);
    QCOMPARE(statistics.perPage.at(1).pageIndex, 1);
    QCOMPARE(statistics.perPage.at(1).words, 3);
    QCOMPARE(statistics.perPage.at(1).characters, 3);
}

// 词数用块上已经算好的值：这里再重算一遍就会变成第二套口径。
void DocumentStatisticsTests::usesChunkWordCountVerbatim() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeChunk(0, QStringLiteral("hello world"), 0, 42));

    const DocumentStatistics statistics = DocumentStatisticsBuilder::fromChunks(chunks, 1);
    QCOMPARE(statistics.words, 42);
    // 字符数仍然按块的真实文本算（只有词数是复用的）。
    QCOMPARE(statistics.characters, 11);
}

// 标题块计数：只有 headingLevel > 0 的才算标题。
void DocumentStatisticsTests::countsHeadingBlocks() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeChunk(0, QStringLiteral("第一章"), 1));
    chunks.append(makeChunk(0, QStringLiteral("1.1 节"), 2));
    chunks.append(makeChunk(0, QStringLiteral("正文一段")));

    const DocumentStatistics statistics = DocumentStatisticsBuilder::fromChunks(chunks, 1);
    QCOMPARE(statistics.headings, 2);
}

// 空页补齐：没有任何块的页也要有一行，计数为 0。
void DocumentStatisticsTests::fillsEmptyPages() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeChunk(0, QStringLiteral("只有第一页有字")));

    const DocumentStatistics statistics = DocumentStatisticsBuilder::fromChunks(chunks, 4);
    QCOMPARE(statistics.pages, 4);
    QCOMPARE(statistics.perPage.size(), 4);
    for (int index = 0; index < statistics.perPage.size(); ++index) {
        QCOMPARE(statistics.perPage.at(index).pageIndex, index);
    }
    QCOMPARE(statistics.perPage.at(0).words, 7);
    QCOMPARE(statistics.perPage.at(3).words, 0);
    QCOMPARE(statistics.perPage.at(3).characters, 0);
}

// 逐页顺序：块乱序送来时，perPage 仍然按页码排。
void DocumentStatisticsTests::keepsPerPageOrder() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeChunk(2, QStringLiteral("第三页")));
    chunks.append(makeChunk(0, QStringLiteral("第一页")));
    chunks.append(makeChunk(1, QStringLiteral("第二页")));

    const DocumentStatistics statistics = DocumentStatisticsBuilder::fromChunks(chunks, 3);
    QCOMPARE(statistics.perPage.size(), 3);
    QCOMPARE(statistics.perPage.at(0).pageIndex, 0);
    QCOMPARE(statistics.perPage.at(1).pageIndex, 1);
    QCOMPARE(statistics.perPage.at(2).pageIndex, 2);
    QCOMPARE(statistics.perPage.at(0).words, 3);
    QCOMPARE(statistics.perPage.at(1).words, 3);
    QCOMPARE(statistics.perPage.at(2).words, 3);
}

// 纯文本这条路的词数与 ChunkBuilder::countWords 必须是同一个口径。
void DocumentStatisticsTests::plainTextMatchesChunkBuilderCount() {
    const QString text = QStringLiteral("第 1 章 概述 introduction");
    const DocumentStatistics statistics = DocumentStatisticsBuilder::fromPlainText(text, 1);
    QCOMPARE(statistics.words, 6);
    QCOMPARE(statistics.words, aipdf::doc::ChunkBuilder::countWords(text));
}

QTEST_MAIN(DocumentStatisticsTests)
#include "DocumentStatisticsTests.moc"
