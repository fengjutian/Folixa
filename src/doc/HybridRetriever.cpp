#include "doc/HybridRetriever.h"

#include "ai/LocalHashEmbedding.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace aipdf::doc {

namespace {

// 关键词检索用的词元：中文取二字组，拉丁取整词，丢掉单字。
//
// 单字（「的」「是」「在」）在中文里几乎每段都有，命中它没有区分度，
// 只会让排名被噪声占满。
QStringList keywordTerms(const QString& query) {
    QStringList terms;
    for (const QString& token : ai::LocalHashEmbedding::tokenize(query)) {
        if (token.size() >= 2 && !terms.contains(token)) {
            terms.append(token);
        }
    }
    return terms;
}

}  // namespace

HybridRetriever::HybridRetriever(const LocalIndex* index,
                                 const VectorIndex* vectors,
                                 const ai::EmbeddingProvider* embedder)
    : index_(index), vectors_(vectors), embedder_(embedder) {}

double HybridRetriever::fuseScore(int keywordRank, int vectorRank,
                                  double keywordWeight, double vectorWeight) {
    double score = 0.0;
    if (keywordRank > 0) {
        score += keywordWeight / (static_cast<double>(kRrfConstant) + keywordRank);
    }
    if (vectorRank > 0) {
        score += vectorWeight / (static_cast<double>(kRrfConstant) + vectorRank);
    }
    return score;
}

QVector<HybridHit> HybridRetriever::search(const QString& query,
                                           const Options& options) const {
    QVector<HybridHit> hits;
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty() || options.limit <= 0) {
        return hits;
    }

    // 关键词那半边：按**词元**逐个检索，再按命中的词元数排名。
    //
    // 不能拿整句去子串匹配：用户问的是「自动保存在什么时候提示」，
    // 整句在原文里永远匹配不到——那一半检索就成了摆设，
    // 混合检索也就退化成了单路向量检索。
    //
    // 排名看的是**命中了几个不同的词元**，不是命中了几次：
    // 一段里同一个词出现十次，不代表它比另一段只出现一次更相关。
    //
    // 页码在这里顺手记下：后面要靠它把块取回来（块 id 里没有页号）。
    QHash<QString, int> keywordRanks;
    QHash<QString, int> keywordPages;
    if (index_ != nullptr && !index_->isEmpty()) {
        // 块 id → 命中的词元集合。
        QHash<QString, QSet<QString>> matchedTerms;
        // 块 id → 首次出现顺序（命中词元数相同时用它定先后）。
        QHash<QString, int> firstSeen;
        for (const QString& term : keywordTerms(trimmed)) {
            const QVector<IndexHit> hits = index_->search(term, options.candidatePool);
            for (const IndexHit& hit : hits) {
                if (hit.chunkId.isEmpty()) {
                    continue;
                }
                if (!matchedTerms.contains(hit.chunkId)) {
                    matchedTerms.insert(hit.chunkId, QSet<QString>());
                    firstSeen.insert(hit.chunkId, firstSeen.size());
                    keywordPages.insert(hit.chunkId, hit.pageIndex);
                }
                matchedTerms[hit.chunkId].insert(term);
            }
        }

        // 按「命中词元数」降序排；词元数相同按首次出现顺序升序。
        QVector<QString> candidates;
        candidates.reserve(matchedTerms.size());
        for (auto it = matchedTerms.constBegin(); it != matchedTerms.constEnd(); ++it) {
            candidates.append(it.key());
        }
        std::sort(candidates.begin(), candidates.end(),
                  [&matchedTerms, &firstSeen](const QString& left, const QString& right) {
                      const int leftCount = matchedTerms.value(left).size();
                      const int rightCount = matchedTerms.value(right).size();
                      if (leftCount != rightCount) {
                          return leftCount > rightCount;
                      }
                      const int leftOrder = firstSeen.value(left, 0);
                      const int rightOrder = firstSeen.value(right, 0);
                      if (leftOrder != rightOrder) {
                          return leftOrder < rightOrder;
                      }
                      return left < right;
                  });

        int rank = 0;
        for (const QString& chunkId : candidates) {
            keywordRanks.insert(chunkId, ++rank);
        }
    }

    // 向量那半边：余弦 topK。
    QHash<QString, int> vectorRanks;
    if (vectors_ != nullptr && embedder_ != nullptr && !vectors_->isEmpty()) {
        const ai::EmbeddingVector queryVector = embedder_->embedOne(trimmed);
        if (!queryVector.isEmpty()) {
            const QVector<VectorHit> vectorHits =
                vectors_->search(queryVector, options.candidatePool);
            for (const VectorHit& hit : vectorHits) {
                if (hit.chunkId.isEmpty() || vectorRanks.contains(hit.chunkId)) {
                    continue;
                }
                vectorRanks.insert(hit.chunkId, vectorRanks.size() + 1);
            }
        }
    }

    if (keywordRanks.isEmpty() && vectorRanks.isEmpty()) {
        return hits;
    }

    // 融合：两路名次都出现过的块自然排在前面。
    QSet<QString> candidates;
    for (auto it = keywordRanks.constBegin(); it != keywordRanks.constEnd(); ++it) {
        candidates.insert(it.key());
    }
    for (auto it = vectorRanks.constBegin(); it != vectorRanks.constEnd(); ++it) {
        candidates.insert(it.key());
    }

    for (const QString& chunkId : candidates) {
        HybridHit hit;
        hit.chunkId = chunkId;
        hit.keywordRank = keywordRanks.value(chunkId, 0);
        hit.vectorRank = vectorRanks.value(chunkId, 0);
        hit.score = fuseScore(hit.keywordRank, hit.vectorRank,
                              options.keywordWeight, options.vectorWeight);

        // 页码：关键词那半边带着命中页；只有向量那半边命中时问向量索引
        // （它存了块 id → 页码）。
        int pageIndex = keywordPages.value(chunkId, -1);
        if (pageIndex < 0 && vectors_ != nullptr) {
            pageIndex = vectors_->pageFor(chunkId);
        }
        hit.pageIndex = pageIndex;

        if (index_ != nullptr && pageIndex >= 0) {
            DocumentChunk chunk;
            if (index_->chunkByIdOnPage(pageIndex, chunkId, &chunk)) {
                hit.text = chunk.text;
                hit.sectionPath = chunk.sectionPath;
            }
        }
        hits.append(hit);
    }

    std::sort(hits.begin(), hits.end(), [](const HybridHit& left, const HybridHit& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        // 同分按块 id 排：结果稳定，验收才可重复。
        return left.chunkId < right.chunkId;
    });
    if (hits.size() > options.limit) {
        hits.resize(options.limit);
    }
    return hits;
}

}  // namespace aipdf::doc
