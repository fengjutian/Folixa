#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::ai {

// 一个向量。
using EmbeddingVector = QVector<float>;

// 嵌入提供者：把文本变成向量。
//
// 为什么要这层抽象：检索要的不只是「关键词命中」，还有「意思接近」。
// 两者的实现天差地别——云端模型、本地神经网络、纯词形哈希——
// 但上层（向量索引、混合检索）只关心「给我一组文本，返回一组向量」。
//
// v0.11.0 用的是本地确定性实现（见 LocalHashEmbedding）：
// 没有密钥也能跑通整条链路，验收可重复、不花钱、不联网。
// 换成云端嵌入模型时，只要实现这个接口，上层一行不用改。
class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;

    // 服务商标识。
    virtual QString id() const = 0;
    // 向量维度。不同实现的维度不同，索引文件里会记下来；
    // 维度变了就得重建索引。
    virtual int dimension() const = 0;
    // 是不是「演示」实现（本地、不联网）。
    virtual bool isDemo() const { return false; }

    // 把一批文本变成向量：返回的个数必须与传入的个数相同。
    virtual QVector<EmbeddingVector> embed(const QStringList& texts) const = 0;

    // 单条文本的向量（embed 的便捷包装）。
    EmbeddingVector embedOne(const QString& text) const {
        const QVector<EmbeddingVector> result = embed(QStringList{text});
        return result.isEmpty() ? EmbeddingVector() : result.first();
    }
};

}  // namespace aipdf::ai
