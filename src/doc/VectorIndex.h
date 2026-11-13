#pragma once

#include "ai/EmbeddingProvider.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace aipdf::doc {

// 一条向量检索命中。
struct VectorHit {
    QString chunkId;
    // 余弦相似度（向量已归一化时就是点积，范围 -1..1）。
    double score = 0.0;
};

// 向量索引：块 id → 向量，加一次余弦 topK 检索。
//
// 存在哪儿：与 LocalIndex 同一个目录下的 vectors 子目录
// （可用 AIPDF_INDEX_DIR 改位置），文件名是文档路径的 SHA-1。
// 与 LocalIndex 分开存：一个是「排版切出来的块」，一个是「块 → 向量」，
// 两者的重建节奏不一样（块随内容变，向量还随嵌入模型变）。
//
// 存法：JSON 文件 + 每个向量一段 base64 的 float32（小端）。
// 为什么不用二进制文件：JSON 能人眼检查、能被负例守卫改坏、能原子替换；
// 而 float32 的体积优势在这个规模上并不重要（一千块也就 1 MB）。
//
// 为什么暴力扫描而不是 HNSW/IVF：这个规模下暴力扫一千个 256 维向量
// 只要几毫秒，而引入图索引要带一套构建、持久化、增量更新的复杂度。
// 规模再涨一个量级时再换——这个类的接口（search/put/save/load）不用变。
class VectorIndex {
public:
    // 索引文件路径（按文档路径）。
    static QString filePathFor(const QString& documentPath);

    // 载入 / 保存。
    bool load(const QString& documentPath);
    bool save() const;
    void clear();

    QString documentPath() const;
    void setDocumentPath(const QString& documentPath);

    // 向量维度。0 表示空索引。
    int dimension() const;
    // 嵌入提供者的 id（换了提供者要重建索引）。
    QString providerId() const;
    void setProviderId(const QString& id);

    int vectorCount() const;
    bool isEmpty() const;

    // 写入 / 取回一个块的向量。
    //
    // 页码要一起存：块 id 里没有页号（它是「内容指纹 + 页内块序号」），
    // 只靠 id 反查页会拿错页——v0.9.0 就踩过这个坑（两页内容一样时 id 重复）。
    void put(const QString& chunkId, const ai::EmbeddingVector& vector, int pageIndex);
    bool vectorFor(const QString& chunkId, ai::EmbeddingVector* vector) const;
    // 块所在的页。未知返回 -1。
    int pageFor(const QString& chunkId) const;
    void remove(const QString& chunkId);
    QStringList chunkIds() const;

    // 检索：余弦 topK。
    //
    // 向量已归一化时，余弦就是点积。
    // 分数为负的结果直接丢掉：负相似度意味着「意思相反」，
    // 把它塞进上下文只会误导模型。
    QVector<VectorHit> search(const ai::EmbeddingVector& query, int limit = 20) const;

    // 余弦相似度（两边都按单位向量算；不是单位向量时先归一化）。
    static double cosine(const ai::EmbeddingVector& left, const ai::EmbeddingVector& right);

private:
    QString documentPath_;
    QString providerId_;
    int dimension_ = 0;
    QHash<QString, ai::EmbeddingVector> vectors_;
    // 块 id → 页码。
    QHash<QString, int> chunkPages_;
};

}  // namespace aipdf::doc
