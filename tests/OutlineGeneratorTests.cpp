#include "pdf/ops/OutlineGenerator.h"

#include "doc/model/ChunkBuilder.h"
#include "pdf/kernel/Navigation.h"
#include "pdf/ops/OutlineWriter.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QFile>
#include <QTest>

using aipdf::doc::DocumentChunk;
using aipdf::pdf::OutlineItem;

namespace {

QString fixturePath(const char* name) {
    return QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QLatin1Char('/') +
           QString::fromLatin1(name);
}

// 造一个标题块。
DocumentChunk makeHeading(int pageIndex, int level, const QString& text) {
    DocumentChunk chunk;
    chunk.pageIndex = pageIndex;
    chunk.order = pageIndex;
    chunk.text = text;
    chunk.headingLevel = level;
    chunk.wordCount = aipdf::doc::ChunkBuilder::countWords(text);
    return chunk;
}

// 造一个正文块。
DocumentChunk makeBody(int pageIndex, const QString& text) {
    DocumentChunk chunk = makeHeading(pageIndex, 0, text);
    return chunk;
}

}  // namespace

class OutlineGeneratorTests : public QObject {
    Q_OBJECT

private slots:
    void bodyOnlyDocumentHasNoOutline();
    void singleHeadingBecomesRoot();
    void secondLevelBecomesChild();
    void threeLevelsNest();
    void twoTopLevelHeadingsAreSiblings();
    void levelJumpKeepsNearestParent();
    void longTitleIsTruncated();
    void titleUsesFirstLineOnly();
    void minHeadingLevelSkipsShallowHeadings();
    void maxDepthLimitsNesting();
    void pageIndexComesFromChunk();
    void outlineSurvivesPdfRoundTrip();
};

// 只有正文：没有标题就没有书签。
void OutlineGeneratorTests::bodyOnlyDocumentHasNoOutline() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeBody(0, QStringLiteral("正文第一段")));
    chunks.append(makeBody(0, QStringLiteral("正文第二段")));

    const std::vector<OutlineItem> outline = aipdf::pdf::generateOutlineFromChunks(chunks);
    QVERIFY(outline.empty());
}

// 一个 level 1：顶层一个节点。
void OutlineGeneratorTests::singleHeadingBecomesRoot() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeHeading(0, 1, QStringLiteral("第一章")));

    const std::vector<OutlineItem> outline = aipdf::pdf::generateOutlineFromChunks(chunks);
    QCOMPARE(outline.size(), size_t(1));
    QCOMPARE(outline.front().title, QStringLiteral("第一章"));
    QVERIFY(outline.front().children.empty());
}

// level 1 + level 2：level 2 挂到 level 1 下面。
void OutlineGeneratorTests::secondLevelBecomesChild() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeHeading(0, 1, QStringLiteral("第一章")));
    chunks.append(makeHeading(0, 2, QStringLiteral("1.1 节")));

    const std::vector<OutlineItem> outline = aipdf::pdf::generateOutlineFromChunks(chunks);
    QCOMPARE(outline.size(), size_t(1));
    QCOMPARE(outline.front().children.size(), size_t(1));
    QCOMPARE(outline.front().children.front().title, QStringLiteral("1.1 节"));
}

// level 1 + 2 + 3：三层嵌套。
void OutlineGeneratorTests::threeLevelsNest() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeHeading(0, 1, QStringLiteral("第一章")));
    chunks.append(makeHeading(0, 2, QStringLiteral("1.1 节")));
    chunks.append(makeHeading(0, 3, QStringLiteral("1.1.1 小节")));

    const std::vector<OutlineItem> outline = aipdf::pdf::generateOutlineFromChunks(chunks);
    QCOMPARE(outline.size(), size_t(1));
    const OutlineItem& level1 = outline.front();
    QCOMPARE(level1.children.size(), size_t(1));
    const OutlineItem& level2 = level1.children.front();
    QCOMPARE(level2.title, QStringLiteral("1.1 节"));
    QCOMPARE(level2.children.size(), size_t(1));
    QCOMPARE(level2.children.front().title, QStringLiteral("1.1.1 小节"));
    QVERIFY(level2.children.front().children.empty());
}

// 两个 level 1：两个顶层兄弟。
void OutlineGeneratorTests::twoTopLevelHeadingsAreSiblings() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeHeading(0, 1, QStringLiteral("第一章")));
    chunks.append(makeHeading(1, 1, QStringLiteral("第二章")));

    const std::vector<OutlineItem> outline = aipdf::pdf::generateOutlineFromChunks(chunks);
    QCOMPARE(outline.size(), size_t(2));
    QCOMPARE(outline.at(0).title, QStringLiteral("第一章"));
    QCOMPARE(outline.at(1).title, QStringLiteral("第二章"));
    QVERIFY(outline.at(0).children.empty());
    QVERIFY(outline.at(1).children.empty());
}

// 层级跳跃：level 1 之后直接来 level 3，level 3 挂到 level 1 下面（不能丢）。
void OutlineGeneratorTests::levelJumpKeepsNearestParent() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeBody(0, QStringLiteral("正文")));
    chunks.append(makeHeading(0, 1, QStringLiteral("第一章")));
    chunks.append(makeHeading(0, 3, QStringLiteral("1.1.1 小节")));

    const std::vector<OutlineItem> outline = aipdf::pdf::generateOutlineFromChunks(chunks);
    QCOMPARE(outline.size(), size_t(1));
    QCOMPARE(outline.front().title, QStringLiteral("第一章"));
    QCOMPARE(outline.front().children.size(), size_t(1));
    QCOMPARE(outline.front().children.front().title, QStringLiteral("1.1.1 小节"));
}

// 标题太长：截断并加省略号。
void OutlineGeneratorTests::longTitleIsTruncated() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeHeading(0, 1, QString(30, QLatin1Char('x'))));

    aipdf::pdf::OutlineGeneratorOptions options;
    options.maxTitleLength = 10;
    const std::vector<OutlineItem> outline =
        aipdf::pdf::generateOutlineFromChunks(chunks, options);
    QCOMPARE(outline.size(), size_t(1));
    const QString title = outline.front().title;
    QCOMPARE(title.size(), 10);
    QVERIFY(title.endsWith(QStringLiteral("…")));
    QCOMPARE(title, QStringLiteral("xxxxxxxxx…"));
}

// 标题只取块文本的第一行。
void OutlineGeneratorTests::titleUsesFirstLineOnly() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeHeading(0, 1, QStringLiteral("章节标题\n后面还跟着一段正文")));

    const std::vector<OutlineItem> outline = aipdf::pdf::generateOutlineFromChunks(chunks);
    QCOMPARE(outline.size(), size_t(1));
    QCOMPARE(outline.front().title, QStringLiteral("章节标题"));
}

// minHeadingLevel：比它浅的标题不生成书签。
void OutlineGeneratorTests::minHeadingLevelSkipsShallowHeadings() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeHeading(0, 1, QStringLiteral("第一章")));
    chunks.append(makeHeading(0, 2, QStringLiteral("1.1 节")));

    aipdf::pdf::OutlineGeneratorOptions options;
    options.minHeadingLevel = 2;
    const std::vector<OutlineItem> outline =
        aipdf::pdf::generateOutlineFromChunks(chunks, options);
    QCOMPARE(outline.size(), size_t(1));
    QCOMPARE(outline.front().title, QStringLiteral("1.1 节"));
    QVERIFY(outline.front().children.empty());
}

// maxDepth：更深的层级压到最深的这一层，而不是被丢掉。
void OutlineGeneratorTests::maxDepthLimitsNesting() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeHeading(0, 1, QStringLiteral("A")));
    chunks.append(makeHeading(0, 2, QStringLiteral("B")));
    chunks.append(makeHeading(0, 3, QStringLiteral("C")));

    aipdf::pdf::OutlineGeneratorOptions options;
    options.maxDepth = 2;
    const std::vector<OutlineItem> outline =
        aipdf::pdf::generateOutlineFromChunks(chunks, options);
    QCOMPARE(outline.size(), size_t(1));
    const OutlineItem& root = outline.front();
    QCOMPARE(root.title, QStringLiteral("A"));
    QCOMPARE(root.children.size(), size_t(2));
    QCOMPARE(root.children.at(0).title, QStringLiteral("B"));
    QCOMPARE(root.children.at(1).title, QStringLiteral("C"));
    QVERIFY(root.children.at(0).children.empty());
    QVERIFY(root.children.at(1).children.empty());
}

// 页码取块上的页序号。
void OutlineGeneratorTests::pageIndexComesFromChunk() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeHeading(3, 1, QStringLiteral("第四章")));
    chunks.append(makeHeading(7, 2, QStringLiteral("4.1 节")));

    const std::vector<OutlineItem> outline = aipdf::pdf::generateOutlineFromChunks(chunks);
    QCOMPARE(outline.size(), size_t(1));
    QCOMPARE(outline.front().pageIndex, 3);
    QCOMPARE(outline.front().children.front().pageIndex, 7);
}

// 落盘往返：生成的树写进 PDF，再用 PDFium 读回来，标题、顺序、层级都要对。
// 这一条是"自动书签真能写进 PDF"的唯一证据。
void OutlineGeneratorTests::outlineSurvivesPdfRoundTrip() {
    QVector<DocumentChunk> chunks;
    chunks.append(makeHeading(0, 1, QStringLiteral("第一章")));
    chunks.append(makeHeading(0, 2, QStringLiteral("1.1 概述")));
    chunks.append(makeHeading(0, 1, QStringLiteral("第二章")));

    const std::vector<OutlineItem> outline = aipdf::pdf::generateOutlineFromChunks(chunks);
    QCOMPARE(outline.size(), size_t(2));

    QFile file(fixturePath("normal.pdf"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const QByteArray base = file.readAll();
    QVERIFY(!base.isEmpty());

    const auto written = aipdf::pdf::rewriteOutline(base, outline);
    QVERIFY2(written.ok, qPrintable(written.error));
    QVERIFY(!written.bytes.isEmpty());

    const aipdf::pdf::PdfOpenResult reopened = aipdf::pdf::openDocumentFromBytes(written.bytes);
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));

    const std::vector<OutlineItem> parsed = reopened.document.outline();
    QCOMPARE(parsed.size(), size_t(2));
    QCOMPARE(parsed.at(0).title, QStringLiteral("第一章"));
    QCOMPARE(parsed.at(1).title, QStringLiteral("第二章"));
    QCOMPARE(parsed.at(0).children.size(), size_t(1));
    QCOMPARE(parsed.at(0).children.front().title, QStringLiteral("1.1 概述"));
    QCOMPARE(parsed.at(0).pageIndex, 0);

    // 摊平后层级要一致：第一章在顶层，1.1 概述在它下面一层。
    const std::vector<aipdf::pdf::OutlineEntry> flat = aipdf::pdf::flattenOutline(parsed);
    QCOMPARE(flat.size(), size_t(3));
    QCOMPARE(flat.at(0).title, QStringLiteral("第一章"));
    QCOMPARE(flat.at(0).depth, 0);
    QCOMPARE(flat.at(1).title, QStringLiteral("1.1 概述"));
    QCOMPARE(flat.at(1).depth, 1);
    QCOMPARE(flat.at(2).title, QStringLiteral("第二章"));
    QCOMPARE(flat.at(2).depth, 0);
}

QTEST_MAIN(OutlineGeneratorTests)
#include "OutlineGeneratorTests.moc"
