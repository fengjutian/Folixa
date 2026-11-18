#pragma once

#include "ai/EmbeddingProvider.h"

namespace aipdf::ai {

// 本地确定性嵌入：哈希 n-gram 向量。
//
// 这**不是**神经网络嵌入，是一个纯词形方法（hashing trick）：
// 把文本切成 n-gram，每个 n-gram 哈希到固定维度的一个桶里累加权重，
// 最后做 L2 归一化。同样的文本永远得到同样的向量——不联网、不花钱、
// 不依赖任何模型文件，所以整条链路（检索 → 上下文 → 回答 → 引用验证）
// 可以在没有密钥的机器上完整跑通，验收结果可重复。
//
// 为什么要它：v0.11.0 的核心价值是**可验证的引用**（引文要能在原文里核对到），
// 这条链路必须在离线状态下也成立，否则验收只能靠真金白银的云端调用。
//
// 它比纯关键词检索强在哪：n-gram 让「部分重合」也能得分
// （"自动保存" 与 "自动保存文档"），而子串检索要么整串命中要么不命中。
// 它比神经网络嵌入弱在哪：不认识同义词（「费用」与「价格」互相无关）。
// 所以它与关键词检索是**互补**的两路，正好用 RRF 融合（见 HybridRetriever）。
//
// 分词：拉丁文按词，中文按「单字 + 相邻二字组」。
// 中文没有空格，不切词的话整段就是一个词，匹配等于失效；
// 而只取单字又太短，二字组才是中文最小的有效语义单位。
class LocalHashEmbedding final : public EmbeddingProvider {
public:
    // 默认维度。256 维 × 4 字节 = 每块 1 KB，一千块 1 MB，够用。
    static constexpr int kDefaultDimension = 256;

    explicit LocalHashEmbedding(int dimension = kDefaultDimension);

    QString id() const override;
    int dimension() const override;
    bool isDemo() const override { return true; }

    QVector<EmbeddingVector> embed(const QStringList& texts) const override;

    // 把一段文本切成 n-gram 词元（公开出来是为了能直接测分词）。
    static QStringList tokenize(const QString& text);

    // 文本归一化：全角转半角、转小写、压缩空白。
    // 引用核对与向量匹配都用这一套，避免「看起来一样、字节不同」。
    static QString normalize(const QString& text);

private:
    int dimension_;
};

}  // namespace aipdf::ai
