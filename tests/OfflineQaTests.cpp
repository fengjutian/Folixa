// OfflineQa 的单元测试。
//
// 覆盖三件事：
//   1. **诚实性**：索引为空、问题与文档完全无关时，必须明确返回「没找到」，
//      而且**不返回任何答案正文**（这是本模块最要紧的一条）。
//   2. **可核对性**：返回的每一条引用，都必须能在原始片段里逐字定位到。
//   3. **可复现性**：同样的输入永远得到同样的答案（本地嵌入、不联网）。

#include "doc/index/OfflineQa.h"

#include "doc/index/LocalHashEmbedding.h"
#include "doc/index/CitationVerifier.h"
#include "doc/model/DocumentChunk.h"
#include "doc/index/LocalIndex.h"
#include "doc/index/VectorIndex.h"

#include <QSet>
#include <QString>
#include <QTest>
#include <QVector>

using aipdf::ai::OfflineAnswer;
using aipdf::ai::OfflineAnswerStatus;
using aipdf::ai::OfflineCitation;
using aipdf::ai::OfflineQa;
using aipdf::ai::OfflineQaOptions;

namespace {

// 一段样例文档：三个片段，两页。
//
// 主题刻意分散（自动保存 / 导出 / 打印），这样「无关问题」才真的无关。
QVector<aipdf::doc::DocumentChunk> sampleChunks() {
    QVector<aipdf::doc::DocumentChunk> chunks;

    aipdf::doc::DocumentChunk first;
    first.id = QStringLiteral("p0b0");
    first.pageIndex = 0;
    first.order = 0;
    first.text = QStringLiteral("本软件在编辑文档时会自动保存草稿。自动保存的间隔默认为 5 分钟。"
                                "用户可以在设置里调整这个间隔。");
    chunks.append(first);

    aipdf::doc::DocumentChunk second;
    second.id = QStringLiteral("p0b1");
    second.pageIndex = 0;
    second.order = 1;
    second.text = QStringLiteral("导出功能支持 PDF、Word 与纯文本三种格式。导出前会提示用户确认目标路径。");
    chunks.append(second);

    aipdf::doc::DocumentChunk third;
    third.id = QStringLiteral("p1b0");
    third.pageIndex = 1;
    third.order = 2;
    third.text = QStringLiteral("打印模块支持双面打印与纸张缩放。打印队列里可以暂停或取消任务。");
    chunks.append(third);

    return chunks;
}

// 造一份内存里的本地索引。
aipdf::doc::LocalIndex buildSampleIndex() {
    aipdf::doc::LocalIndex index;
    index.setDocumentPath(QStringLiteral("memory://sample"));
    const QVector<aipdf::doc::DocumentChunk> chunks = sampleChunks();
    QVector<aipdf::doc::DocumentChunk> pageZero;
    QVector<aipdf::doc::DocumentChunk> pageOne;
    for (const aipdf::doc::DocumentChunk& chunk : chunks) {
        if (chunk.pageIndex == 0) {
            pageZero.append(chunk);
        } else {
            pageOne.append(chunk);
        }
    }
    index.replacePage(0, pageZero, QStringLiteral("fp0"));
    index.replacePage(1, pageOne, QStringLiteral("fp1"));
    return index;
}

// 这个答案里有没有出现过某个词。
bool answerContains(const OfflineAnswer& answer, const QString& needle) {
    return answer.text.contains(needle);
}

}  // namespace

class OfflineQaTests : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ 诚实性 ----
    void emptyChunkListReportsNoMatch();
    void emptyIndexReportsNoMatch();
    void unrelatedQuestionReportsNoMatch();
    void noMatchCarriesNoAnswerText();
    void noMatchListsClosestChunks();
    void emptyQuestionIsRejected();

    // ---------------------------------------------------------- 抽取式答案 ----
    void relatedQuestionReturnsExtractiveAnswer();
    void answerIsTakenFromSourceVerbatim();
    void citationsAppearInSourceChunk();
    void citationOffsetsPointAtQuote();
    void citationsPassRepoVerifier();
    void answerTextCarriesMarkersAndSection();

    // ------------------------------------------------------------ 参数 ----
    void maxSentencesIsHonored();
    void duplicateSentencesAreRemoved();
    void chineseFullWidthPunctuationSplitsSentences();
    void pageScopeRestrictsSearch();
    void pageScopeWithNoChunksReportsNoMatch();
    void emptyChunkIsIgnored();

    // ---------------------------------------------------------- 索引路径 ----
    void indexPathMatchesChunkPath();
    void vectorIndexIsAccepted();
    void closestListIsEmptyForSingleChunkDocument();
};

void OfflineQaTests::emptyChunkListReportsNoMatch() {
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(QVector<aipdf::doc::DocumentChunk>(),
                                     QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.isNoMatch());
    QVERIFY(!answer.hasAnswer());
    QVERIFY(answer.text.isEmpty());
    QVERIFY(answer.citations.isEmpty());
    // 原因要用中文写清楚。
    QVERIFY(answer.notice.contains(QStringLiteral("没有找到")));
}

void OfflineQaTests::emptyIndexReportsNoMatch() {
    aipdf::doc::LocalIndex index;
    const OfflineQa qa(&index);
    const OfflineAnswer answer = qa.answer(QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.isNoMatch());
    QVERIFY(answer.text.isEmpty());
    QVERIFY(answer.citations.isEmpty());
    QVERIFY(answer.notice.contains(QStringLiteral("没有找到")));
    QVERIFY(answer.notice.contains(QStringLiteral("空")));
}

void OfflineQaTests::unrelatedQuestionReportsNoMatch() {
    // 这个问题与文档里的每一句话都没有共同词元。
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("如何申请年假与报销流程"));
    QVERIFY(answer.isNoMatch());
    QVERIFY(answer.text.isEmpty());
    QVERIFY(answer.citations.isEmpty());
    QVERIFY(answer.notice.contains(QStringLiteral("本地索引里没有找到相关内容")));
}

void OfflineQaTests::noMatchCarriesNoAnswerText() {
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("如何申请年假与报销流程"));
    // 「没找到」时**一个字都不能编**：正文必须完全是空的。
    QVERIFY(answer.text.isEmpty());
    QVERIFY(!answer.hasAnswer());
    QVERIFY(answer.verifiedCitationCount == 0);
}

void OfflineQaTests::noMatchListsClosestChunks() {
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("如何申请年假与报销流程"));
    QVERIFY(answer.isNoMatch());
    // 找不到也要给线索：最接近的几个片段必须列出来，供用户自己判断。
    QVERIFY(!answer.closest.isEmpty());
    QVERIFY(answer.closest.size() <= 3);
    for (const OfflineCitation& citation : answer.closest) {
        QVERIFY(!citation.chunkId.isEmpty());
        QVERIFY(citation.pageIndex >= 0);
        QVERIFY(!citation.quote.isEmpty());
    }
    // 线索里的原句确实来自某个片段。
    const QVector<aipdf::doc::DocumentChunk> chunks = sampleChunks();
    for (const OfflineCitation& citation : answer.closest) {
        bool found = false;
        for (const aipdf::doc::DocumentChunk& chunk : chunks) {
            if (chunk.id == citation.chunkId && chunk.text.contains(citation.quote)) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }
}

void OfflineQaTests::emptyQuestionIsRejected() {
    const OfflineAnswer answer = OfflineQa::answerFromChunks(sampleChunks(), QString());
    QVERIFY(answer.isNoMatch());
    QVERIFY(answer.text.isEmpty());
    QVERIFY(!answer.notice.isEmpty());
}

void OfflineQaTests::relatedQuestionReturnsExtractiveAnswer() {
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.hasAnswer());
    QCOMPARE(answer.status, OfflineAnswerStatus::Found);
    QVERIFY(!answer.citations.isEmpty());
    QVERIFY(answerContains(answer, QStringLiteral("自动保存")));
    // 正文里必须带引用编号。
    QVERIFY(answerContains(answer, QStringLiteral("[1]")));
}

void OfflineQaTests::answerIsTakenFromSourceVerbatim() {
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.hasAnswer());
    const QVector<aipdf::doc::DocumentChunk> chunks = sampleChunks();
    for (const OfflineCitation& citation : answer.citations) {
        // 答案里的每句话都必须是原文里**逐字**的一段，不是改写过的。
        bool found = false;
        for (const aipdf::doc::DocumentChunk& chunk : chunks) {
            if (chunk.id == citation.chunkId && chunk.text.contains(citation.quote)) {
                found = true;
                break;
            }
        }
        QVERIFY2(found, qPrintable(QStringLiteral("引用不在原片段里：%1").arg(citation.quote)));
    }
}

void OfflineQaTests::citationsAppearInSourceChunk() {
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.hasAnswer());
    const QVector<aipdf::doc::DocumentChunk> chunks = sampleChunks();
    QSet<QString> chunkIds;
    for (const OfflineCitation& citation : answer.citations) {
        chunkIds.insert(citation.chunkId);
        const aipdf::doc::DocumentChunk* chunk = nullptr;
        for (const aipdf::doc::DocumentChunk& candidate : chunks) {
            if (candidate.id == citation.chunkId) {
                chunk = &candidate;
                break;
            }
        }
        QVERIFY(chunk != nullptr);
        QVERIFY(chunk->text.contains(citation.quote));
        // 页码也要对得上。
        QCOMPARE(citation.pageIndex, chunk->pageIndex);
    }
    QVERIFY(!chunkIds.isEmpty());
}

void OfflineQaTests::citationOffsetsPointAtQuote() {
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.hasAnswer());
    const QVector<aipdf::doc::DocumentChunk> chunks = sampleChunks();
    for (const OfflineCitation& citation : answer.citations) {
        QVERIFY(citation.charOffset >= 0);
        for (const aipdf::doc::DocumentChunk& chunk : chunks) {
            if (chunk.id != citation.chunkId) {
                continue;
            }
            QCOMPARE(chunk.text.mid(citation.charOffset, citation.quote.size()), citation.quote);
        }
    }
}

void OfflineQaTests::citationsPassRepoVerifier() {
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.hasAnswer());
    QVERIFY(answer.citationsVerified());
    QCOMPARE(answer.verifiedCitationCount, answer.citations.size());

    // 用仓库自带的核验器再独立核一遍（不依赖本模块自己的口径）。
    QVector<aipdf::doc::CitationBlock> blocks;
    const QVector<aipdf::doc::DocumentChunk> chunks = sampleChunks();
    for (const OfflineCitation& citation : answer.citations) {
        aipdf::doc::CitationBlock block;
        block.number = citation.number;
        block.pageIndex = citation.pageIndex;
        for (const aipdf::doc::DocumentChunk& chunk : chunks) {
            if (chunk.id == citation.chunkId) {
                block.text = chunk.text;
                break;
            }
        }
        blocks.append(block);
    }
    const aipdf::doc::CitationReport report =
        aipdf::doc::CitationVerifier::verify(answer.text, blocks);
    QCOMPARE(report.invalidCount, 0);
    QCOMPARE(report.validCount, answer.citations.size());
    QVERIFY(!report.hasUnsupportedMarker);
}

void OfflineQaTests::answerTextCarriesMarkersAndSection() {
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.hasAnswer());
    QVERIFY(answerContains(answer, QStringLiteral("引用：")));
    // 正文里的 [n] 标记要与引用清单对得上。
    const QVector<int> markers = aipdf::doc::CitationVerifier::extractMarkers(answer.text);
    QCOMPARE(markers.size(), answer.citations.size());
    for (const OfflineCitation& citation : answer.citations) {
        QVERIFY(markers.contains(citation.number));
    }
}

void OfflineQaTests::maxSentencesIsHonored() {
    OfflineQaOptions options;
    options.maxSentences = 1;
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("自动保存的间隔是多久"), options);
    QVERIFY(answer.hasAnswer());
    QCOMPARE(answer.citations.size(), 1);
}

void OfflineQaTests::duplicateSentencesAreRemoved() {
    // 同一句话在两个片段里各出现一次：答案里只该留一条。
    QVector<aipdf::doc::DocumentChunk> chunks = sampleChunks();
    aipdf::doc::DocumentChunk duplicate = chunks.at(0);
    duplicate.id = QStringLiteral("p1b1");
    duplicate.pageIndex = 1;
    duplicate.order = 3;
    chunks.append(duplicate);

    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(chunks, QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.hasAnswer());
    QSet<QString> quotes;
    for (const OfflineCitation& citation : answer.citations) {
        QVERIFY2(!quotes.contains(citation.quote), "同一句话被引用了两次");
        quotes.insert(citation.quote);
    }
}

void OfflineQaTests::chineseFullWidthPunctuationSplitsSentences() {
    // 中文全角句末标点必须真的能切句。
    //
    // 曾经的缺陷：切句判断写的是 QLatin1Char('。')，而 QLatin1Char 的参数是
    // char（单字节），'。' 被截断成 0x82，比较永远为假 —— 中文文档整段被当成
    // 一句话，引用粒度完全错了。英文标点是单字节，所以只用英文语料的用例测不出来。
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.hasAnswer());
    QVERIFY(!answer.citations.isEmpty());

    bool sawTargetSentence = false;
    for (const OfflineCitation& citation : answer.citations) {
        // 一条引用里最多只能有一个句末标点：跨句的引用说明根本没切开。
        int terminators = 0;
        for (const QChar ch : citation.quote) {
            if (ch == QChar(0x3002) || ch == QChar(0xFF01) || ch == QChar(0xFF1F)
                || ch == QChar(0xFF1B) || ch == QChar(0x2026)) {
                ++terminators;
            }
        }
        QVERIFY2(terminators <= 1,
                 qPrintable(QStringLiteral("引用跨了句，说明没按全角标点切开：%1").arg(citation.quote)));
        if (citation.quote.contains(QStringLiteral("自动保存的间隔默认为 5 分钟"))) {
            sawTargetSentence = true;
        }
    }
    QVERIFY2(sawTargetSentence, "没能引用到目标句");
}

void OfflineQaTests::pageScopeRestrictsSearch() {
    OfflineQaOptions options;
    options.pageScope = {0};
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("打印队列怎么暂停"), options);
    QVERIFY(answer.isNoMatch());
    QVERIFY(answer.text.isEmpty());

    OfflineQaOptions other;
    other.pageScope = {1};
    const OfflineAnswer scoped =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("打印队列怎么暂停"), other);
    QVERIFY(scoped.hasAnswer());
    for (const OfflineCitation& citation : scoped.citations) {
        QCOMPARE(citation.pageIndex, 1);
    }
}

void OfflineQaTests::pageScopeWithNoChunksReportsNoMatch() {
    OfflineQaOptions options;
    options.pageScope = {9};
    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("自动保存"), options);
    QVERIFY(answer.isNoMatch());
    QVERIFY(answer.text.isEmpty());
    QVERIFY(answer.notice.contains(QStringLiteral("没有找到")));
}

void OfflineQaTests::emptyChunkIsIgnored() {
    QVector<aipdf::doc::DocumentChunk> chunks = sampleChunks();
    aipdf::doc::DocumentChunk blank;
    blank.id = QStringLiteral("p2b0");
    blank.pageIndex = 2;
    blank.order = 9;
    blank.text = QString();
    chunks.append(blank);

    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(chunks, QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.hasAnswer());
    for (const OfflineCitation& citation : answer.citations) {
        QVERIFY(citation.chunkId != QStringLiteral("p2b0"));
    }
}

void OfflineQaTests::indexPathMatchesChunkPath() {
    const aipdf::doc::LocalIndex index = buildSampleIndex();
    const OfflineQa qa(&index);
    const OfflineAnswer fromIndex = qa.answer(QStringLiteral("自动保存的间隔是多久"));
    const OfflineAnswer fromChunks =
        OfflineQa::answerFromChunks(sampleChunks(), QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(fromIndex.hasAnswer());
    QVERIFY(fromChunks.hasAnswer());
    QCOMPARE(fromIndex.citations.size(), fromChunks.citations.size());
    for (int index = 0; index < fromIndex.citations.size(); ++index) {
        QCOMPARE(fromIndex.citations.at(index).quote, fromChunks.citations.at(index).quote);
        QCOMPARE(fromIndex.citations.at(index).chunkId, fromChunks.citations.at(index).chunkId);
    }
}

void OfflineQaTests::vectorIndexIsAccepted() {
    // 把片段的向量也灌进向量索引：混合检索的两路都能用。
    aipdf::doc::LocalIndex index = buildSampleIndex();
    aipdf::doc::VectorIndex vectors;
    const aipdf::ai::LocalHashEmbedding embedder;
    const QVector<aipdf::doc::DocumentChunk> chunks = sampleChunks();
    for (const aipdf::doc::DocumentChunk& chunk : chunks) {
        vectors.put(chunk.id, embedder.embedOne(chunk.text), chunk.pageIndex);
    }
    QCOMPARE(vectors.vectorCount(), chunks.size());

    const OfflineQa qa(&index, &vectors);
    const OfflineAnswer answer = qa.answer(QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.hasAnswer());
    QVERIFY(answer.citationsVerified());
}

void OfflineQaTests::closestListIsEmptyForSingleChunkDocument() {
    // 只有一段且已经引用过的文档：相关段落清单里不该再重复它。
    QVector<aipdf::doc::DocumentChunk> chunks;
    aipdf::doc::DocumentChunk only;
    only.id = QStringLiteral("p0b0");
    only.pageIndex = 0;
    only.order = 0;
    only.text = QStringLiteral("自动保存的间隔默认为 5 分钟。");
    chunks.append(only);

    const OfflineAnswer answer =
        OfflineQa::answerFromChunks(chunks, QStringLiteral("自动保存的间隔是多久"));
    QVERIFY(answer.hasAnswer());
    QVERIFY(answer.closest.isEmpty());
}

QTEST_GUILESS_MAIN(OfflineQaTests)
#include "OfflineQaTests.moc"
