// v0.11.0 可验证文档问答的单元测试。
//
// 覆盖四块纯逻辑：本地嵌入、向量索引、混合检索（RRF）、引用核验。
// 这四块都不碰 PDFium、也不联网，所以能单独跑、结果可复现。
// 真正把 PDF 读进来的那部分由 PdfIndexTests 覆盖。
#include "ai/ChatTypes.h"
#include "ai/EmbeddingProvider.h"
#include "ai/LocalHashEmbedding.h"
#include "ai/MockProvider.h"
#include "ai/ModelProvider.h"
#include "doc/CitationVerifier.h"
#include "doc/DocumentChunk.h"
#include "doc/HybridRetriever.h"
#include "doc/LocalIndex.h"
#include "doc/VectorIndex.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace aipdf;

namespace {

// 造一个块：id、页码、文本。
doc::DocumentChunk makeChunk(const QString& id, int pageIndex, const QString& text) {
    doc::DocumentChunk chunk;
    chunk.id = id;
    chunk.pageIndex = pageIndex;
    chunk.order = pageIndex;
    chunk.text = text;
    return chunk;
}

// 一个带两块两页的索引。
doc::LocalIndex makeIndex() {
    doc::LocalIndex index;
    index.replacePage(0, {makeChunk(QStringLiteral("a0b0"), 0,
                                   QStringLiteral("Folixa 是一个 PDF 编辑器，支持自动保存。"))},
                      QStringLiteral("fp0"));
    index.replacePage(1, {makeChunk(QStringLiteral("a1b0"), 1,
                                   QStringLiteral("自动保存会在关闭文档时提示用户。"))},
                      QStringLiteral("fp1"));
    return index;
}

// 测试用的嵌入提供者：按文本返回预设的向量。
//
// 为什么不用哈希嵌入测「只有向量那一路」：哈希有桶碰撞，
// 名次会偶发跳变，测试就成了 flaky。用固定向量，结果是确定的。
class FixedEmbedding final : public ai::EmbeddingProvider {
public:
    QString id() const override { return QStringLiteral("fixed"); }
    int dimension() const override { return 4; }
    bool isDemo() const override { return true; }

    QVector<ai::EmbeddingVector> embed(const QStringList& texts) const override {
        QVector<ai::EmbeddingVector> result;
        for (const QString& text : texts) {
            ai::EmbeddingVector vector(4, 0.0f);
            if (text.contains(QStringLiteral("target"))) {
                vector[1] = 1.0f;
            } else {
                vector[0] = 1.0f;
            }
            result.append(vector);
        }
        return result;
    }
};

}  // namespace

class DocumentQaTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ------------------------------------------------------- 本地嵌入 ----
    void embeddingIsDeterministic();
    void embeddingNormalizesWidthAndCase();
    void embeddingTokenizesChineseAndLatin();
    void embeddingIsUnitLength();
    void embeddingOfEmptyTextIsSafe();

    // ------------------------------------------------------- 向量索引 ----
    void vectorIndexStoresChunkAndPage();
    void vectorIndexRoundTripsThroughDisk();
    void vectorIndexRejectsMismatchedDimension();
    void vectorSearchRanksBySimilarity();
    void vectorSearchDropsNegativeSimilarity();

    // ------------------------------------------------------- 混合检索 ----
    void rrfFavoursChunksRankedHighInBothLists();
    void keywordSearchFindsChineseSubstring();
    void keywordSearchRanksByMatchedTerms();
    void vectorSearchFindsSemanticallyCloseChunk();

    // ------------------------------------------------------- 引用核验 ----
    void validCitationPasses();
    void fabricatedQuoteIsRejected();
    void unknownNumberIsRejected();
    void wrongPageIsRejected();
    void markerToInvalidCitationIsFlagged();
    void quoteCompareIgnoresWidthAndWhitespace();
    void answerBodyStopsBeforeCitationSection();

    // ------------------------------------------------- 演示模式引真块 ----
    void mockProviderCitesRealBlocks();

private:
    QTemporaryDir directory_;
};

void DocumentQaTests::initTestCase() {
    QVERIFY(directory_.isValid());
    // 向量索引跟着本地索引的目录走；测试时指到临时目录，
    // 不要污染用户真正的应用数据目录。
    qputenv("AIPDF_INDEX_DIR", directory_.path().toUtf8());
}

void DocumentQaTests::cleanupTestCase() {
    qunsetenv("AIPDF_INDEX_DIR");
}

// ------------------------------------------------------------ 本地嵌入 ----

void DocumentQaTests::embeddingIsDeterministic() {
    ai::LocalHashEmbedding embedder;
    const ai::EmbeddingVector first =
        embedder.embedOne(QStringLiteral("自动保存会在关闭时提示"));
    const ai::EmbeddingVector second =
        embedder.embedOne(QStringLiteral("自动保存会在关闭时提示"));
    QCOMPARE(first.size(), ai::LocalHashEmbedding::kDefaultDimension);
    QCOMPARE(first, second);
}

void DocumentQaTests::embeddingNormalizesWidthAndCase() {
    // 全角转半角 + 转小写：中文文档里全角标点很常见，
    // 不归一化的话「，」和「,」会被当成两个不同的东西。
    QCOMPARE(ai::LocalHashEmbedding::normalize(QStringLiteral("ＡＢＣ")),
             QStringLiteral("abc"));
    QCOMPARE(ai::LocalHashEmbedding::normalize(QStringLiteral("自动保存，文档")),
             QStringLiteral("自动保存,文档"));
    QCOMPARE(ai::LocalHashEmbedding::normalize(QStringLiteral("  多个   空格  ")),
             QStringLiteral("多个 空格"));
}

void DocumentQaTests::embeddingTokenizesChineseAndLatin() {
    const QStringList tokens =
        ai::LocalHashEmbedding::tokenize(QStringLiteral("自动保存 PDF"));
    // 中文：单字 + 二字组；拉丁：整词。
    QVERIFY(tokens.contains(QStringLiteral("自")));
    QVERIFY(tokens.contains(QStringLiteral("自动")));
    QVERIFY(tokens.contains(QStringLiteral("动")));
    QVERIFY(tokens.contains(QStringLiteral("pdf")));
    // 标点不进词元。
    QVERIFY(!tokens.contains(QStringLiteral("，")));
}

void DocumentQaTests::embeddingIsUnitLength() {
    ai::LocalHashEmbedding embedder;
    const ai::EmbeddingVector vector =
        embedder.embedOne(QStringLiteral("归一化之后向量长度应当是一"));
    double squared = 0.0;
    for (float value : vector) {
        squared += static_cast<double>(value) * static_cast<double>(value);
    }
    QVERIFY(std::abs(squared - 1.0) < 1e-5);
}

void DocumentQaTests::embeddingOfEmptyTextIsSafe() {
    ai::LocalHashEmbedding embedder;
    const ai::EmbeddingVector vector = embedder.embedOne(QString());
    QCOMPARE(vector.size(), ai::LocalHashEmbedding::kDefaultDimension);
    // 全零向量：不能是 NaN，也不能有非零分量。
    for (float value : vector) {
        QCOMPARE(value, 0.0f);
    }
}

// ------------------------------------------------------------ 向量索引 ----

void DocumentQaTests::vectorIndexStoresChunkAndPage() {
    doc::VectorIndex index;
    ai::LocalHashEmbedding embedder;
    index.setDocumentPath(QStringLiteral("D:/tmp/vector-index-doc.pdf"));
    index.setProviderId(embedder.id());
    index.put(QStringLiteral("chunk-1"),
              embedder.embedOne(QStringLiteral("第一块")),
              3);

    QCOMPARE(index.vectorCount(), 1);
    QCOMPARE(index.pageFor(QStringLiteral("chunk-1")), 3);
    ai::EmbeddingVector loaded;
    QVERIFY(index.vectorFor(QStringLiteral("chunk-1"), &loaded));
    QCOMPARE(loaded.size(), ai::LocalHashEmbedding::kDefaultDimension);
    QVERIFY(!index.isEmpty());
}

void DocumentQaTests::vectorIndexRoundTripsThroughDisk() {
    const QString documentPath = directory_.filePath(QStringLiteral("round-trip.pdf"));
    ai::LocalHashEmbedding embedder;
    const ai::EmbeddingVector original =
        embedder.embedOne(QStringLiteral("自动保存会在关闭文档时提示用户"));

    {
        doc::VectorIndex index;
        index.setDocumentPath(documentPath);
        index.setProviderId(embedder.id());
        index.put(QStringLiteral("chunk-1"), original, 7);
        index.put(QStringLiteral("chunk-2"), embedder.embedOne(QStringLiteral("另一块")), 8);
        QVERIFY(index.save());
    }

    doc::VectorIndex reloaded;
    QVERIFY(reloaded.load(documentPath));
    QCOMPARE(reloaded.vectorCount(), 2);
    QCOMPARE(reloaded.providerId(), embedder.id());
    QCOMPARE(reloaded.pageFor(QStringLiteral("chunk-1")), 7);
    ai::EmbeddingVector restored;
    QVERIFY(reloaded.vectorFor(QStringLiteral("chunk-1"), &restored));
    QCOMPARE(restored.size(), original.size());
    // float32 存回来只该有舍入误差。
    for (int index = 0; index < original.size(); ++index) {
        QVERIFY(std::abs(restored.at(index) - original.at(index)) < 1e-5);
    }
}

void DocumentQaTests::vectorIndexRejectsMismatchedDimension() {
    // 换了嵌入模型（维度不同）时，两个模型的向量放在同一个空间里比较毫无意义，
    // 所以这一条必须被拒掉，而不是混着存。
    doc::VectorIndex index;
    ai::LocalHashEmbedding embedder(64);
    index.setDocumentPath(QStringLiteral("D:/tmp/dimension-mismatch.pdf"));
    index.put(QStringLiteral("chunk-1"), embedder.embedOne(QStringLiteral("甲")), 0);
    QCOMPARE(index.vectorCount(), 1);

    ai::LocalHashEmbedding other(128);
    index.put(QStringLiteral("chunk-2"), other.embedOne(QStringLiteral("乙")), 1);
    QCOMPARE(index.vectorCount(), 1);
    ai::EmbeddingVector vector;
    QVERIFY(!index.vectorFor(QStringLiteral("chunk-2"), &vector));
}

void DocumentQaTests::vectorSearchRanksBySimilarity() {
    doc::VectorIndex index;
    ai::LocalHashEmbedding embedder;
    index.setDocumentPath(QStringLiteral("D:/tmp/search-rank.pdf"));
    index.put(QStringLiteral("about-save"),
              embedder.embedOne(QStringLiteral("自动保存会在关闭文档时提示用户")), 0);
    index.put(QStringLiteral("about-pdf"),
              embedder.embedOne(QStringLiteral("PDF 阅读器支持书签与目录")), 1);

    const QVector<doc::VectorHit> hits = index.search(
        embedder.embedOne(QStringLiteral("自动保存")), 5);
    QVERIFY(!hits.isEmpty());
    QCOMPARE(hits.first().chunkId, QStringLiteral("about-save"));
    // 分数从高到低。
    for (int position = 1; position < hits.size(); ++position) {
        QVERIFY(hits.at(position - 1).score >= hits.at(position).score);
    }
}

void DocumentQaTests::vectorSearchDropsNegativeSimilarity() {
    // 负相似度 = 意思相反：留着只会误导模型。
    doc::VectorIndex index;
    ai::LocalHashEmbedding embedder;
    index.setDocumentPath(QStringLiteral("D:/tmp/negative.pdf"));
    index.put(QStringLiteral("a"), embedder.embedOne(QStringLiteral("苹果")), 0);
    index.put(QStringLiteral("b"), embedder.embedOne(QStringLiteral("香蕉")), 1);

    const ai::EmbeddingVector query = embedder.embedOne(QStringLiteral("苹果"));
    ai::EmbeddingVector opposite(query.size());
    for (int index = 0; index < query.size(); ++index) {
        opposite[index] = -query.at(index);
    }
    const QVector<doc::VectorHit> hits = index.search(opposite, 5);
    QVERIFY(hits.isEmpty());
}

// ------------------------------------------------------------ 混合检索 ----

void DocumentQaTests::rrfFavoursChunksRankedHighInBothLists() {
    // 两路都靠前的块，融合分要高于只在一路里靠前的块。
    const double both = doc::HybridRetriever::fuseScore(1, 1, 1.0, 1.0);
    const double onlyKeyword = doc::HybridRetriever::fuseScore(1, 0, 1.0, 1.0);
    const double onlyVector = doc::HybridRetriever::fuseScore(0, 1, 1.0, 1.0);
    const double tail = doc::HybridRetriever::fuseScore(10, 10, 1.0, 1.0);
    QVERIFY(both > onlyKeyword);
    QVERIFY(both > onlyVector);
    QVERIFY(both > tail);
    // RRF 认的是「共识」：两路都进了榜（哪怕名次靠后），
    // 也比只在一路里排头名更可信——所以 tail > onlyKeyword。
    QVERIFY(tail > onlyKeyword);
    QCOMPARE(onlyKeyword, onlyVector);
    // 没进榜的那一路贡献 0。
    QCOMPARE(doc::HybridRetriever::fuseScore(0, 0, 1.0, 1.0), 0.0);
    // 权重是线性的：加倍权重，融合分也加倍。
    QVERIFY(std::abs(doc::HybridRetriever::fuseScore(1, 1, 2.0, 1.0) -
                     (doc::HybridRetriever::fuseScore(1, 1, 1.0, 1.0) +
                      doc::HybridRetriever::fuseScore(1, 0, 1.0, 1.0))) < 1e-9);
}

void DocumentQaTests::keywordSearchFindsChineseSubstring() {
    const doc::LocalIndex index = makeIndex();
    doc::HybridRetriever retriever(&index, nullptr, nullptr);
    doc::HybridOptions options;
    options.limit = 5;
    const QVector<doc::HybridHit> hits = retriever.search(QStringLiteral("自动保存"), options);
    QVERIFY(!hits.isEmpty());
    // 两块都提到「自动保存」，但第一块排在前面。
    QCOMPARE(hits.first().chunkId, QStringLiteral("a0b0"));
    QCOMPARE(hits.first().pageIndex, 0);
    QCOMPARE(hits.first().keywordRank, 1);
    QCOMPARE(hits.first().vectorRank, 0);
    QVERIFY(!hits.first().text.isEmpty());
}

void DocumentQaTests::keywordSearchRanksByMatchedTerms() {
    // 自然语言问题不能拿整句去匹配：整句在原文里永远匹配不到，
    // 那一半检索就成了摆设，混合检索退化成单路向量检索。
    //
    // 这里的问法在原文里根本不是子串：必须切成词元才找得到。
    doc::LocalIndex index;
    index.replacePage(0, {makeChunk(QStringLiteral("a0b0"), 0,
                                   QStringLiteral("自动保存会在关闭文档时提示用户。"))},
                      QStringLiteral("fp0"));
    index.replacePage(1, {makeChunk(QStringLiteral("a1b0"), 1,
                                   QStringLiteral("自动保存功能可以在设置里开关。"))},
                      QStringLiteral("fp1"));

    doc::HybridRetriever retriever(&index, nullptr, nullptr);
    doc::HybridOptions options;
    options.limit = 5;
    const QVector<doc::HybridHit> hits =
        retriever.search(QStringLiteral("自动保存 提示 用户"), options);
    QCOMPARE(hits.size(), 2);
    // 命中词元更多的块排前面。
    QCOMPARE(hits.first().chunkId, QStringLiteral("a0b0"));
    QVERIFY(hits.first().keywordRank > 0);
}

void DocumentQaTests::vectorSearchFindsSemanticallyCloseChunk() {
    // 只有向量那一路（关键词完全命中不到）时，检索仍然要能工作：
    // 这是向量那半边存在的意义。
    doc::LocalIndex index;
    index.replacePage(0, {makeChunk(QStringLiteral("a0b0"), 0,
                                   QStringLiteral("alpha chunk"))},
                      QStringLiteral("fp0"));
    index.replacePage(1, {makeChunk(QStringLiteral("a1b0"), 1,
                                   QStringLiteral("beta chunk"))},
                      QStringLiteral("fp1"));

    FixedEmbedding embedder;
    doc::VectorIndex vectors;
    vectors.setDocumentPath(QStringLiteral("D:/tmp/hybrid-vector.pdf"));
    vectors.put(QStringLiteral("a0b0"), ai::EmbeddingVector{1.0f, 0.0f, 0.0f, 0.0f}, 0);
    vectors.put(QStringLiteral("a1b0"), ai::EmbeddingVector{0.0f, 1.0f, 0.0f, 0.0f}, 1);

    doc::HybridRetriever retriever(&index, &vectors, &embedder);
    doc::HybridOptions options;
    options.limit = 5;
    // 「target」在原文里没有，关键词那半边必然落空。
    const QVector<doc::HybridHit> hits = retriever.search(QStringLiteral("target"), options);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().chunkId, QStringLiteral("a1b0"));
    QCOMPARE(hits.first().keywordRank, 0);
    QCOMPARE(hits.first().vectorRank, 1);
    QCOMPARE(hits.first().text, QStringLiteral("beta chunk"));
}

// ------------------------------------------------------------ 引用核验 ----

namespace {

// 三个上下文块：编号 1..3。
QVector<doc::CitationBlock> sampleBlocks() {
    QVector<doc::CitationBlock> blocks;
    doc::CitationBlock first;
    first.number = 1;
    first.pageIndex = 0;
    first.text = QStringLiteral("Folixa 是一个 PDF 编辑器。");
    doc::CitationBlock second;
    second.number = 2;
    second.pageIndex = 1;
    second.text = QStringLiteral("自动保存会在关闭文档时提示用户。");
    doc::CitationBlock third;
    third.number = 3;
    third.pageIndex = 2;
    third.text = QStringLiteral("批注支持导出与撤销。");
    blocks.append(first);
    blocks.append(second);
    blocks.append(third);
    return blocks;
}

}  // namespace

void DocumentQaTests::validCitationPasses() {
    const QString answer = QStringLiteral(
        "Folixa 是一个 PDF 编辑器 [1]。\n"
        "\n"
        "引用：\n"
        "[1] 第 1 页 | 引文：Folixa 是一个 PDF 编辑器。");
    const doc::CitationReport report = doc::CitationVerifier::verify(answer, sampleBlocks());
    QCOMPARE(report.citations.size(), 1);
    QCOMPARE(report.validCount, 1);
    QCOMPARE(report.invalidCount, 0);
    QVERIFY(report.hasEvidence());
    QVERIFY(!report.hasUnsupportedMarker);
    QCOMPARE(report.markers, QVector<int>{1});
}

void DocumentQaTests::fabricatedQuoteIsRejected() {
    // 编号对，但引的那段话不在这一块里——最常见的一种伪造，必须挡住。
    const QString answer = QStringLiteral(
        "Folixa 每年发布很多版本 [1]。\n"
        "\n"
        "引用：\n"
        "[1] 第 1 页 | 引文：Folixa 每年发布很多版本。");
    const doc::CitationReport report = doc::CitationVerifier::verify(answer, sampleBlocks());
    QCOMPARE(report.validCount, 0);
    QCOMPARE(report.invalidCount, 1);
    QVERIFY(!report.hasEvidence());
    QVERIFY(report.citations.first().reason.contains(QStringLiteral("引文")));
}

void DocumentQaTests::unknownNumberIsRejected() {
    // 凭空造一个编号：这条引用直接作废。
    const QString answer = QStringLiteral(
        "某个说法 [7]。\n"
        "\n"
        "引用：\n"
        "[7] 第 1 页 | 引文：Folixa 是一个 PDF 编辑器。");
    const doc::CitationReport report = doc::CitationVerifier::verify(answer, sampleBlocks());
    QCOMPARE(report.validCount, 0);
    QCOMPARE(report.invalidCount, 1);
    QVERIFY(report.citations.first().reason.contains(QStringLiteral("编号")));
    // 正文里的 [7] 指向了一条无效引用。
    QVERIFY(report.hasUnsupportedMarker);
}

void DocumentQaTests::wrongPageIsRejected() {
    // 页码对不上：用户点引用会跳到错误的页。
    const QString answer = QStringLiteral(
        "Folixa 是一个 PDF 编辑器 [1]。\n"
        "\n"
        "引用：\n"
        "[1] 第 3 页 | 引文：Folixa 是一个 PDF 编辑器。");
    const doc::CitationReport report = doc::CitationVerifier::verify(answer, sampleBlocks());
    QCOMPARE(report.validCount, 0);
    QCOMPARE(report.invalidCount, 1);
    QVERIFY(report.citations.first().reason.contains(QStringLiteral("页码")));
}

void DocumentQaTests::markerToInvalidCitationIsFlagged() {
    const QString answer = QStringLiteral(
        "这句话没有出处 [2]。\n"
        "\n"
        "引用：\n"
        "[2] 第 9 页 | 引文：自动保存会在关闭文档时提示用户。");
    const doc::CitationReport report = doc::CitationVerifier::verify(answer, sampleBlocks());
    QCOMPARE(report.invalidCount, 1);
    QVERIFY(report.hasUnsupportedMarker);
}

void DocumentQaTests::quoteCompareIgnoresWidthAndWhitespace() {
    // 全半角与空白差异不是「引文改了」，只是排版。
    QVERIFY(doc::CitationVerifier::quoteAppearsIn(
        QStringLiteral("Folixa是一个PDF编辑器。"),
        QStringLiteral("Folixa 是一个 PDF 编辑器。")));
    QVERIFY(doc::CitationVerifier::quoteAppearsIn(
        QStringLiteral("自动保存会在关闭文档时提示用户。"),
        QStringLiteral("自动保存会在关闭文档时提示用户。")));
    QVERIFY(!doc::CitationVerifier::quoteAppearsIn(
        QStringLiteral("完全不同的另一句话"), QStringLiteral("Folixa 是一个 PDF 编辑器。")));
}

void DocumentQaTests::answerBodyStopsBeforeCitationSection() {
    const QString answer = QStringLiteral(
        "正文第一段。\n"
        "正文第二段 [1]。\n"
        "\n"
        "引用：\n"
        "[1] 第 1 页 | 引文：正文第一段。");
    const QString body = doc::CitationVerifier::answerBody(answer);
    QVERIFY(body.contains(QStringLiteral("正文第一段")));
    QVERIFY(body.contains(QStringLiteral("正文第二段")));
    QVERIFY(!body.contains(QStringLiteral("引文")));
    // 正文里的 [1] 要被算成标记，引用段里的不算。
    QCOMPARE(doc::CitationVerifier::extractMarkers(answer), QVector<int>{1});
}

// -------------------------------------------------- 演示模式引真实块 ----

void DocumentQaTests::mockProviderCitesRealBlocks() {
    // 演示模式也必须引用**真实的**块：引文要能在原文里逐字核对到，
    // 编出来的引文必然核验不过，整条链路就跑不通了。
    ai::ChatRequest request;
    request.task = ai::Task::DocumentQa;
    ai::ContextBlock first;
    first.number = 1;
    first.pageNumber = 1;
    first.text = QStringLiteral("Folixa 是一个 PDF 编辑器。");
    ai::ContextBlock second;
    second.number = 2;
    second.pageNumber = 2;
    second.text = QStringLiteral("自动保存会在关闭文档时提示用户。");
    request.context.append(first);
    request.context.append(second);
    request.messages.append(ai::ChatMessage::system(ai::qaSystemPrompt()));
    request.messages.append(ai::ChatMessage::user(
        QStringLiteral("材料：\n\n%1\n\n问题：%2")
            .arg(ai::formatContextBlocks(request.context), QStringLiteral("它做什么？"))));

    ai::MockProvider provider(1);
    QSignalSpy deltas(&provider, &ai::ModelProvider::streamDelta);
    QSignalSpy finished(&provider, &ai::ModelProvider::streamFinished);
    provider.startChat(request);
    QVERIFY(finished.wait(5000));

    QString answer;
    for (const QList<QVariant>& call : deltas) {
        answer += call.at(0).toString();
    }
    QVERIFY(!answer.isEmpty());

    QVector<doc::CitationBlock> blocks;
    for (const ai::ContextBlock& block : request.context) {
        doc::CitationBlock item;
        item.number = block.number;
        item.pageIndex = block.pageNumber - 1;
        item.text = block.text;
        blocks.append(item);
    }
    const doc::CitationReport report = doc::CitationVerifier::verify(answer, blocks);
    QCOMPARE(report.citations.size(), 2);
    QCOMPARE(report.validCount, 2);
    QCOMPARE(report.invalidCount, 0);
    QVERIFY(report.hasEvidence());
    QVERIFY(!report.hasUnsupportedMarker);
}

QTEST_MAIN(DocumentQaTests)
#include "DocumentQaTests.moc"
