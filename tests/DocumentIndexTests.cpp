// v0.9.0 文档索引底座的单元测试：分块器与本地索引。
//
// 这两个都是纯逻辑，不碰 PDFium，所以单独一个可执行文件就能测。
// 走真实 PDF 的那部分在 PdfIndexTests 里。

#include "doc/ChunkBuilder.h"
#include "doc/DocumentChunk.h"
#include "doc/LocalIndex.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace aipdf::doc;

namespace {

// 造一行：文本 + 盒子 + 字高。
//
// 盒子用 (left, bottom, width, height)：PDF user space 的下沿是 y，
// 行高就是盒子高度。
ChunkLine makeLine(const QString& text, double left, double bottom,
                   double height = 12.0, double charHeight = 12.0) {
    ChunkLine line;
    line.text = text;
    line.box = QRectF(left, bottom, text.size() * 6.0, height);
    line.charHeight = charHeight;
    return line;
}

// 一段够长的正文（超过 minChunkChars，免得被并进相邻块）。
QString body(int characters = 60) {
    return QString(characters, QLatin1Char('x'));
}

}  // namespace

class DocumentIndexTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ------------------------------------------------------- 分块器 ----
    void bigLineGapSplitsParagraph();
    void indentChangeSplitsChunk();
    void tallerLineBecomesHeading();
    void headingLevelFollowsHeightRatio();
    void shortBodyChunkMergesIntoNeighbour();
    void longChunkSplitByLines();
    void countWordsCountsChinesePerCharacter();
    void bodyHeightPrefersBodyOverHeading();
    void idPrefixBindsIdToContent();
    void sectionPathIsKept();
    void emptyLinesProduceNoChunks();

    // ----------------------------------------------------- 本地索引 ----
    void fingerprintIsStableAndGeometrySensitive();
    void replacePageStoresChunks();
    void lookupByIdNeedsTheRightPage();
    void pagesNeedingRebuildSkipsUnchanged();
    void fingerprintLookupFindsPage();
    void searchFindsChineseSubstring();
    void searchIsCaseInsensitive();
    void searchReportsLineAndSnippet();
    void saveAndLoadRoundTrip();
};

// --------------------------------------------------------------- 分块器 ----

void DocumentIndexTests::initTestCase() {
}

void DocumentIndexTests::bigLineGapSplitsParagraph() {
    // 两段正文：行距 14（正常），中间空 30（明显是段落间隔）。
    QVector<ChunkLine> lines;
    lines.append(makeLine(body(), 60.0, 300.0));
    lines.append(makeLine(body(), 60.0, 286.0));
    lines.append(makeLine(body(), 60.0, 256.0));
    lines.append(makeLine(body(), 60.0, 242.0));

    const QVector<DocumentChunk> chunks =
        ChunkBuilder::build(0, lines, QString(), 0);
    QCOMPARE(chunks.size(), 2);
    QCOMPARE(chunks.at(0).pageIndex, 0);
    QCOMPARE(chunks.at(0).order, 0);
    QCOMPARE(chunks.at(1).order, 1);
    // 两个块各自带两行的包围盒。
    QCOMPARE(chunks.at(0).lineBoxes.size(), 2);
    QCOMPARE(chunks.at(1).lineBoxes.size(), 2);
}

void DocumentIndexTests::indentChangeSplitsChunk() {
    // 行距正常，但第二行缩进多了 20 点：这是列表项，要另起一块。
    QVector<ChunkLine> lines;
    lines.append(makeLine(body(), 60.0, 300.0));
    lines.append(makeLine(body(), 60.0, 286.0));
    lines.append(makeLine(body(), 80.0, 272.0));
    lines.append(makeLine(body(), 80.0, 258.0));

    const QVector<DocumentChunk> chunks =
        ChunkBuilder::build(0, lines, QString(), 0);
    QCOMPARE(chunks.size(), 2);
}

void DocumentIndexTests::tallerLineBecomesHeading() {
    // 第一行字高 24（正文 12 的两倍），其余是正文。
    QVector<ChunkLine> lines;
    lines.append(makeLine(QStringLiteral("Chapter Title"), 60.0, 320.0, 24.0, 24.0));
    lines.append(makeLine(body(), 60.0, 290.0));
    lines.append(makeLine(body(), 60.0, 276.0));

    const QVector<DocumentChunk> chunks =
        ChunkBuilder::build(0, lines, QString(), 0);
    QCOMPARE(chunks.size(), 2);
    QVERIFY(chunks.at(0).isHeading());
    QVERIFY(!chunks.at(1).isHeading());
    QCOMPARE(chunks.at(0).headingLevel, 1);
}

void DocumentIndexTests::headingLevelFollowsHeightRatio() {
    // 三个不同倍数的字高 → 三个不同的层级。
    // 正文字高要显式给：这一页大半都是标题，让分块器自己估会估歪
    // （真实调用方 IndexController 会先探出全篇的正文字高再传进来）。
    ChunkBuilder::Options options;
    options.referenceCharHeight = 12.0;

    QVector<ChunkLine> lines;
    lines.append(makeLine(QStringLiteral("L1"), 60.0, 380.0, 24.0, 24.0));  // 2.0 倍
    lines.append(makeLine(QStringLiteral("L2"), 60.0, 350.0, 19.2, 19.2)); // 1.6 倍
    lines.append(makeLine(QStringLiteral("L3"), 60.0, 320.0, 16.2, 16.2)); // 1.35 倍
    lines.append(makeLine(body(), 60.0, 280.0));

    const QVector<DocumentChunk> chunks =
        ChunkBuilder::build(0, lines, QString(), 0, options);
    QCOMPARE(chunks.size(), 4);
    QCOMPARE(chunks.at(0).headingLevel, 1);
    QCOMPARE(chunks.at(1).headingLevel, 2);
    QCOMPARE(chunks.at(2).headingLevel, 3);
    QCOMPARE(chunks.at(3).headingLevel, 0);
}

void DocumentIndexTests::shortBodyChunkMergesIntoNeighbour() {
    // 第一块只有 10 个字（太短），应当并进相邻块。
    QVector<ChunkLine> lines;
    lines.append(makeLine(QStringLiteral("short"), 60.0, 300.0));
    lines.append(makeLine(body(), 60.0, 256.0));
    lines.append(makeLine(body(), 60.0, 242.0));

    const QVector<DocumentChunk> chunks =
        ChunkBuilder::build(0, lines, QString(), 0);
    QCOMPARE(chunks.size(), 1);
    QVERIFY(chunks.at(0).text.contains(QStringLiteral("short")));
}

void DocumentIndexTests::longChunkSplitByLines() {
    ChunkBuilder::Options options;
    options.maxChunkChars = 100;
    options.minChunkChars = 10;

    QVector<ChunkLine> lines;
    for (int index = 0; index < 10; ++index) {
        lines.append(makeLine(body(30), 60.0, 300.0 - index * 14.0));
    }

    const QVector<DocumentChunk> chunks =
        ChunkBuilder::build(0, lines, QString(), 0, options);
    QVERIFY(chunks.size() > 1);
    // 每一块都不能超过上限太多（按行切开，最后一行算一行的量）。
    for (const DocumentChunk& chunk : chunks) {
        QVERIFY(chunk.wordCount <= 100);
    }
    // 切开之后块 id 不能重号。
    QSet<QString> ids;
    for (const DocumentChunk& chunk : chunks) {
        QVERIFY(!ids.contains(chunk.id));
        ids.insert(chunk.id);
    }
}

void DocumentIndexTests::countWordsCountsChinesePerCharacter() {
    QCOMPARE(ChunkBuilder::countWords(QStringLiteral("人工智能")), 4);
    QCOMPARE(ChunkBuilder::countWords(QStringLiteral("hello world")), 2);
    QCOMPARE(ChunkBuilder::countWords(QStringLiteral("AI 助手")), 3); // 2 个汉字 + 1 个英文词
    QCOMPARE(ChunkBuilder::countWords(QString()), 0);
}

void DocumentIndexTests::bodyHeightPrefersBodyOverHeading() {
    // 一个 24pt 的短标题 + 若干 12pt 的正文行。
    // 加权之后基准应当是 12，而不是被标题带走变成 24。
    QVector<ChunkBuilder::HeightSample> samples;
    samples.append({24.0, 6});
    samples.append({12.0, 40});
    samples.append({12.0, 40});
    samples.append({12.0, 40});

    QCOMPARE(ChunkBuilder::estimateBodyHeight(samples), 12.0);

    // 只有两行、其中一个是标题：加权之后仍然是正文的高度。
    QVector<ChunkBuilder::HeightSample> two;
    two.append({18.0, 36});
    two.append({14.0, 43});
    QCOMPARE(ChunkBuilder::estimateBodyHeight(two), 14.0);

    QCOMPARE(ChunkBuilder::estimateBodyHeight(QVector<ChunkBuilder::HeightSample>()), 0.0);
}

void DocumentIndexTests::idPrefixBindsIdToContent() {
    QVector<ChunkLine> lines;
    lines.append(makeLine(body(), 60.0, 300.0));
    lines.append(makeLine(body(), 60.0, 286.0));

    const QVector<DocumentChunk> chunks =
        ChunkBuilder::build(3, lines, QString(), 0, ChunkBuilder::Options(),
                            QStringLiteral("abc12345"));
    QCOMPARE(chunks.size(), 1);
    QVERIFY(chunks.at(0).id.startsWith(QStringLiteral("abc12345")));
    // 默认前缀是页序号。
    const QVector<DocumentChunk> fallback = ChunkBuilder::build(3, lines, QString(), 0);
    QVERIFY(fallback.at(0).id.startsWith(QStringLiteral("p3")));
}

void DocumentIndexTests::sectionPathIsKept() {
    QVector<ChunkLine> lines;
    lines.append(makeLine(body(), 60.0, 300.0));

    const QVector<DocumentChunk> chunks =
        ChunkBuilder::build(0, lines, QStringLiteral("第2章 > 2.1 概述"), 0);
    QCOMPARE(chunks.size(), 1);
    QCOMPARE(chunks.at(0).sectionPath, QStringLiteral("第2章 > 2.1 概述"));
}

void DocumentIndexTests::emptyLinesProduceNoChunks() {
    QVector<ChunkLine> lines;
    lines.append(makeLine(QStringLiteral("   "), 60.0, 300.0));
    const QVector<DocumentChunk> chunks =
        ChunkBuilder::build(0, lines, QString(), 0);
    QVERIFY(chunks.isEmpty());
}

// ------------------------------------------------------------- 本地索引 ----

void DocumentIndexTests::fingerprintIsStableAndGeometrySensitive() {
    const QRectF pageBox(0.0, 0.0, 612.0, 792.0);
    const QRectF bounds(60.0, 700.0, 400.0, 60.0);

    const QString first = LocalIndex::fingerprintOf(QStringLiteral("hello"), pageBox, bounds);
    const QString second = LocalIndex::fingerprintOf(QStringLiteral("hello"), pageBox, bounds);
    QCOMPARE(first, second);
    QVERIFY(!first.isEmpty());

    // 文本变了 → 指纹变。
    QVERIFY(first != LocalIndex::fingerprintOf(QStringLiteral("world"), pageBox, bounds));
    // 页面盒子变了 → 指纹变（坐标不一样，不能复用）。
    QVERIFY(first != LocalIndex::fingerprintOf(QStringLiteral("hello"),
                                               QRectF(0.0, 0.0, 612.0, 700.0), bounds));
    // 文本在页上的位置变了 → 指纹变。
    QVERIFY(first != LocalIndex::fingerprintOf(QStringLiteral("hello"), pageBox,
                                               QRectF(60.0, 600.0, 400.0, 60.0)));
}

void DocumentIndexTests::replacePageStoresChunks() {
    LocalIndex index;
    index.setDocumentPath(QStringLiteral("C:/tmp/doc.pdf"));
    index.setPageCount(2);

    DocumentChunk chunk;
    chunk.id = QStringLiteral("p0b0");
    chunk.pageIndex = 0;
    chunk.order = 0;
    chunk.text = QStringLiteral("hello world");
    chunk.sectionPath = QStringLiteral("Chapter 1");
    chunk.headingLevel = 2;
    chunk.wordCount = 2;
    chunk.lineBoxes.append(QRectF(10.0, 20.0, 30.0, 40.0));

    index.replacePage(0, {chunk}, QStringLiteral("fp0"));
    QCOMPARE(index.chunkCount(), 1);
    QCOMPARE(index.chunksForPage(0).size(), 1);
    QCOMPARE(index.pageFingerprint(0), QStringLiteral("fp0"));

    DocumentChunk found;
    QVERIFY(index.chunkById(QStringLiteral("p0b0"), &found));
    QCOMPARE(found.text, QStringLiteral("hello world"));
    QCOMPARE(found.headingLevel, 2);
    QCOMPARE(found.lineBoxes.size(), 1);
    QCOMPARE(found.lineBoxes.at(0).width(), 30.0);
}

void DocumentIndexTests::lookupByIdNeedsTheRightPage() {
    // 两页内容一样 → 块 id 也会一样。
    // 按 id 全局取只能拿到第一页；定位必须带页号。
    LocalIndex index;
    index.setDocumentPath(QStringLiteral("C:/tmp/doc.pdf"));

    DocumentChunk first;
    first.id = QStringLiteral("same");
    first.pageIndex = 0;
    first.text = QStringLiteral("same content");
    index.replacePage(0, {first}, QStringLiteral("fp"));

    DocumentChunk second;
    second.id = QStringLiteral("same");
    second.pageIndex = 5;
    second.text = QStringLiteral("same content");
    index.replacePage(5, {second}, QStringLiteral("fp"));

    DocumentChunk found;
    QVERIFY(index.chunkByIdOnPage(5, QStringLiteral("same"), &found));
    QCOMPARE(found.pageIndex, 5);
    QVERIFY(!index.chunkByIdOnPage(2, QStringLiteral("same"), &found));
}

void DocumentIndexTests::pagesNeedingRebuildSkipsUnchanged() {
    LocalIndex index;
    index.setDocumentPath(QStringLiteral("C:/tmp/doc.pdf"));
    index.replacePage(0, {}, QStringLiteral("fpA"));
    index.replacePage(1, {}, QStringLiteral("fpB"));

    const QStringList current = {QStringLiteral("fpA"), QStringLiteral("fpB")};
    QVERIFY(index.pagesNeedingRebuild(current).isEmpty());

    const QStringList changed = {QStringLiteral("fpA"), QStringLiteral("fpC")};
    const QVector<int> needs = index.pagesNeedingRebuild(changed);
    QCOMPARE(needs.size(), 1);
    QCOMPARE(needs.at(0), 1);
}

void DocumentIndexTests::fingerprintLookupFindsPage() {
    LocalIndex index;
    index.setDocumentPath(QStringLiteral("C:/tmp/doc.pdf"));
    index.replacePage(2, {}, QStringLiteral("fpZ"));

    int pageIndex = -1;
    QVERIFY(index.findPageByFingerprint(QStringLiteral("fpZ"), &pageIndex));
    QCOMPARE(pageIndex, 2);
    QVERIFY(!index.findPageByFingerprint(QStringLiteral("nope"), &pageIndex));
}

void DocumentIndexTests::searchFindsChineseSubstring() {
    LocalIndex index;
    index.setDocumentPath(QStringLiteral("C:/tmp/doc.pdf"));

    DocumentChunk chunk;
    chunk.id = QStringLiteral("p0b0");
    chunk.pageIndex = 0;
    chunk.text = QStringLiteral("第一段讲的是人工智能与文档检索。\n第二段讲别的。");
    index.replacePage(0, {chunk}, QStringLiteral("fp"));

    const QVector<IndexHit> hits = index.search(QStringLiteral("人工智能"));
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.at(0).pageIndex, 0);
    QCOMPARE(hits.at(0).chunkId, QStringLiteral("p0b0"));
    QCOMPARE(hits.at(0).charOffset, 6);
    QVERIFY(hits.at(0).snippet.contains(QStringLiteral("人工智能")));
}

void DocumentIndexTests::searchIsCaseInsensitive() {
    LocalIndex index;
    index.setDocumentPath(QStringLiteral("C:/tmp/doc.pdf"));

    DocumentChunk chunk;
    chunk.id = QStringLiteral("p0b0");
    chunk.pageIndex = 0;
    chunk.text = QStringLiteral("Folixa Editor");
    index.replacePage(0, {chunk}, QStringLiteral("fp"));

    QCOMPARE(index.search(QStringLiteral("folixa")).size(), 1);
    QCOMPARE(index.search(QStringLiteral("FOLIXA")).size(), 1);
    QCOMPARE(index.search(QStringLiteral("editor")).size(), 1);
    QCOMPARE(index.search(QStringLiteral("nothing")).size(), 0);
    QCOMPARE(index.search(QString()).size(), 0);
}

void DocumentIndexTests::searchReportsLineAndSnippet() {
    LocalIndex index;
    index.setDocumentPath(QStringLiteral("C:/tmp/doc.pdf"));

    DocumentChunk chunk;
    chunk.id = QStringLiteral("p0b0");
    chunk.pageIndex = 0;
    chunk.text = QStringLiteral("first line\nsecond line\nthird line");
    index.replacePage(0, {chunk}, QStringLiteral("fp"));

    const QVector<IndexHit> hits = index.search(QStringLiteral("third"));
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.at(0).lineIndex, 2);
}

void DocumentIndexTests::saveAndLoadRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 索引目录指到临时目录：不要污染真实的用户数据目录。
    qputenv("AIPDF_INDEX_DIR", dir.path().toUtf8());

    const QString documentPath = QStringLiteral("C:/tmp/round-trip.pdf");
    {
        LocalIndex index;
        index.setDocumentPath(documentPath);
        index.setPageCount(1);
        index.setReferenceCharHeight(12.5);

        DocumentChunk chunk;
        chunk.id = QStringLiteral("fp123456b0");
        chunk.pageIndex = 0;
        chunk.order = 0;
        chunk.text = QStringLiteral("保存与读回");
        chunk.sectionPath = QStringLiteral("第1章");
        chunk.headingLevel = 1;
        chunk.wordCount = 5;
        chunk.lineBoxes.append(QRectF(1.0, 2.0, 3.0, 4.0));
        chunk.lineBoxes.append(QRectF(5.0, 6.0, 7.0, 8.0));
        index.replacePage(0, {chunk}, QStringLiteral("fp123456"));

        QVERIFY(index.save());
    }

    LocalIndex loaded;
    QVERIFY(loaded.load(documentPath));
    QCOMPARE(loaded.pageCount(), 1);
    QCOMPARE(loaded.chunkCount(), 1);
    QCOMPARE(loaded.referenceCharHeight(), 12.5);
    QCOMPARE(loaded.pageFingerprint(0), QStringLiteral("fp123456"));

    DocumentChunk found;
    QVERIFY(loaded.chunkByIdOnPage(0, QStringLiteral("fp123456b0"), &found));
    QCOMPARE(found.text, QStringLiteral("保存与读回"));
    QCOMPARE(found.sectionPath, QStringLiteral("第1章"));
    QCOMPARE(found.headingLevel, 1);
    QCOMPARE(found.lineBoxes.size(), 2);
    QCOMPARE(found.lineBoxes.at(1).x(), 5.0);

    qunsetenv("AIPDF_INDEX_DIR");
}

QTEST_MAIN(DocumentIndexTests)
#include "DocumentIndexTests.moc"
