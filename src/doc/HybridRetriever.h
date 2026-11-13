#pragma once

#include "ai/EmbeddingProvider.h"
#include "doc/LocalIndex.h"
#include "doc/VectorIndex.h"

#include <QString>
#include <QVector>

namespace aipdf::doc {

// 一条混合检索命中。
struct HybridHit {
    QString chunkId;
    // 块所在的页（-1 表示未知）。
    int pageIndex = -1;
    // RRF 融合分。越大越相关。
    double score = 0.0;
    // 关键词那半边的名次（1 起；0 表示没进榜）。
    int keywordRank = 0;
    // 向量那半边的名次（1 起；0 表示没进榜）。
    int vectorRank = 0;
    // 块文本（构建上下文用）。
    QString text;
    // 章节路径。
    QString sectionPath;
};

// 混合检索：关键词排名 + 向量排名，用 RRF 融合。
//
// 关键词那半边是**按词元**检索的（中文二字组 + 拉丁整词），
// 不是拿整句去做子串匹配：用户问的是一句自然语言，
// 整句在原文里永远匹配不到，那一半检索就成了摆设。
// 排名看的是命中了几个**不同的**词元，不是命中了几次。
//
// 为什么是 RRF 而不是把两个分数相加：
// 两个分数根本不在同一个量纲上——余弦相似度在 -1..1 之间，
// 而子串匹配的「命中次数」可以任意大。直接相加的话，
// 谁的量级大谁说了算，融合结果就变成了单路检索，混合检索名存实亡。
// RRF 只用**名次**：一个块在两路里都排得靠前，就比只在一路里靠前的更相关。
//
// 这是业界通行的做法（Azure Cosmos DB 的 RRF、Elasticsearch 的 RRF 检索器、
// 以及 LangChain 等 RAG 框架里的 reciprocal rank fusion）。
//
// 混合检索的选项。
//
// 为什么放在类外面：类里的嵌套结构做默认实参（`= Options()`）时，
// 它的默认成员初始化器在类还没解析完时不可用，编译器会直接报错。
struct HybridOptions {
    // 最终返回几条。
    int limit = 8;
    // 关键词那半边的权重。
    double keywordWeight = 1.0;
    // 向量那半边的权重。
    double vectorWeight = 1.0;
    // 每一路各自取多少条进融合池。
    //
    // 池子太小会漏掉「在向量里排第 20、在关键词里排第 1」这类块；
    // 太大则融合分被长尾稀释。两路都取 50 在这个规模上够用。
    int candidatePool = 50;
};

// RRF 公式：score(d) = Σ_i weight_i / (k + rank_i(d))
// k 是常数，用来削弱头部名次的绝对优势（原始论文取 60）。
class HybridRetriever {
public:
    // RRF 的 k：越大越「平」，越小越突出头名。
    static constexpr int kRrfConstant = 60;

    using Options = HybridOptions;

    // index / vectors / embedder 都可以为空：缺哪一路就少哪一路。
    HybridRetriever(const LocalIndex* index,
                    const VectorIndex* vectors,
                    const ai::EmbeddingProvider* embedder);

    // 检索。返回按融合分从高到低排的命中（最多 options.limit 条）。
    QVector<HybridHit> search(const QString& query,
                               const Options& options = Options()) const;

    // 融合打分（公开出来是为了能直接单测公式本身）。
    static double fuseScore(int keywordRank, int vectorRank,
                            double keywordWeight, double vectorWeight);

private:
    const LocalIndex* index_ = nullptr;
    const VectorIndex* vectors_ = nullptr;
    const ai::EmbeddingProvider* embedder_ = nullptr;
};

}  // namespace aipdf::doc
