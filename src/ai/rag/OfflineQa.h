#pragma once

#include <QString>
#include <QVector>

#include "doc/model/DocumentChunk.h"

namespace aipdf::doc {
// 前向声明：这两个头文件会把 ai/ 拉进来，而本文件在 ai/ 里，
// 直接包含就绕成环了。这里只用到指针，前向声明就够。
class LocalIndex;
class VectorIndex;
}  // namespace aipdf::doc

namespace aipdf::ai {

// ======================================================== 本地离线问答 =====
//
// 领域层模块：在**本地索引**里检索，再用**抽取式**的方式回答问题。
//
// 三条硬约束（这三条是本模块存在的理由，别为了「更聪明」破掉）：
//
//   1. **不联网**。不调用任何云端 Provider，不需要密钥，不需要模型文件。
//   2. **不做模型推理**。答案是从原文里**摘出来**的句子，不是生成的。
//      摘出来的句子一定能在原文里逐字找到（见 OfflineCitation.quote）。
//   3. **找不到就说找不到**。检索不到相关内容时，返回「没找到」，
//      **不返回任何答案正文**，只把最接近的几个片段列出来让用户自己判断。
//      编一句「看起来像答案」的话，比不给答案坏得多——用户没有能力分辨。
//
// 句子是怎么挑出来的（规则都在下面，行为可预测、可复现）：
//
//   1. **候选片段**：先用仓库自带的混合检索（关键词 + 向量，RRF 融合，
//      见 doc::HybridRetriever）取前 N 个片段；检索不到时退回全部片段
//      ——「没找到」也要给出「最接近的几个」供人工判断。
//   2. **切句**：按中英文句末标点（。！？；… 与 . ! ? ;）和换行切句。
//      句子的起始字符下标会记下来，引用因此能定位回原文的具体位置。
//   3. **打分**：用仓库自带的本地哈希嵌入（doc::LocalHashEmbedding 的
//      兄弟实现 ai::LocalHashEmbedding）算问题与句子的余弦相似度。
//      它不联网、不调模型，同样的输入永远得到同样的分数。
//   4. **门槛**：一条句子要同时满足三个条件才算相关：
//      相似度 ≥ minSentenceScore、相似度 ≥ minChunkScore，
//      **并且**与问题至少共享一个词元（中文二字组或拉丁整词）。
//      只看相似度会误判：哈希嵌入在维度不高时
//      不相干的文本也可能撞出一点余弦。
//   5. **排序**：相似度降序；同分按片段顺序（页码、块序号）与句在片段里的位置。
//   6. **去重**：同一句文字只保留一次（保留相似度最高的那个）。
//   7. **取前 N 句**组成答案，每句后面挂一个引用编号。
//
// 引用是可核对的：answer.citations 里的每一条都带片段 id、页码、原文句子，
// 并且在返回之前用仓库自带的 doc::CitationVerifier 逐条核过——
// 核不过的句子不会被放进答案（verifiedCitationCount 会如实反映条数）。

// 一条可核对的引用。
struct OfflineCitation {
    // 引用编号（1 起），与答案正文里的 [n] 对应。
    int number = 0;
    // 片段 id（本地索引里的块 id，例如 "p1b0"）。
    QString chunkId;
    // 页序号（0 起）。-1 表示不知道。
    int pageIndex = -1;
    // 被引用的原文句子。这句话一定能在片段文本里逐字找到。
    QString quote;
    // 句子在片段文本里的起始字符下标（-1 表示未知）。
    int charOffset = -1;
    // 句子与问题的相似度。
    double score = 0.0;
    // 章节路径（本地索引里的原文归属）。
    QString sectionPath;
};

// 答案的状态。
enum class OfflineAnswerStatus {
    // 找到了有依据的答案。
    Found,
    // 本地索引里没有相关内容（**没有**任何答案正文）。
    NoMatch,
};

// 一次问答的结果。
struct OfflineAnswer {
    // 找到 / 没找到。
    OfflineAnswerStatus status = OfflineAnswerStatus::NoMatch;
    // 原始问题。
    QString question;
    // 答案正文（抽取出来的句子 + 引用编号 + 引用清单）。
    // **没找到时为空字符串**：不编答案。
    QString text;
    // 答案里的引用（每条都核过）。
    QVector<OfflineCitation> citations;
    // 没找到时：最接近的几个片段（供用户自己判断）。
    // 找到时：默认也带上，方便用户看到「还有哪些相关段落」。
    QVector<OfflineCitation> closest;
    // 中文说明（没找到时写明原因；找到时一般为空）。
    QString notice;
    // 通过引用核验的条数（等于 citations.size() 表示全部核过）。
    int verifiedCitationCount = 0;

    bool hasAnswer() const { return status == OfflineAnswerStatus::Found && !text.isEmpty(); }
    // 是不是「明确没找到」。
    bool isNoMatch() const { return status == OfflineAnswerStatus::NoMatch; }
    // 引用是不是全部核过。
    bool citationsVerified() const {
        return !citations.isEmpty() && verifiedCitationCount == citations.size();
    }
};

// 问答参数。
struct OfflineQaOptions {
    // 答案里最多放几句。
    int maxSentences = 3;
    // 没找到时列出几个最接近的片段。
    int maxClosest = 3;
    // 句子级相似度门槛：低于它不算相关。
    double minSentenceScore = 0.12;
    // 片段级相似度门槛：句子要同时过这一关；列「相关段落」时也用它。
    double minChunkScore = 0.06;
    // 检索时先取多少个候选片段。
    int candidateChunks = 8;
    // 范围限制：只在这几页里找（页序号，0 起）。空 = 不限。
    QVector<int> pageScope;
    // 找到答案时，要不要也列出「还有哪些相关段落」。
    bool includeClosestWhenFound = true;
};

// 本地离线问答。
class OfflineQa {
public:
    // index 可以为空；vectors 可以为空（只有向量那一路可用时才传）。
    OfflineQa(const doc::LocalIndex* index, const doc::VectorIndex* vectors = nullptr);

    // 提问。返回的结构能区分「有依据的答案」与「没找到」。
    OfflineAnswer answer(const QString& question,
                          const OfflineQaOptions& options = OfflineQaOptions()) const;

    // 直接给片段（不经过索引文件）。
    //
    // 单独暴露这一版有两个理由：调用方可能已经有一份块列表（不必再落盘成索引），
    // 而测试也不必为了测问答先造一个索引文件。
    static OfflineAnswer answerFromChunks(const QVector<doc::DocumentChunk>& chunks,
                                           const QString& question,
                                           const OfflineQaOptions& options = OfflineQaOptions());

private:
    const doc::LocalIndex* index_ = nullptr;
    const doc::VectorIndex* vectors_ = nullptr;
};

}  // namespace aipdf::ai
