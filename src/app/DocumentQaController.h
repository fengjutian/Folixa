#pragma once

#include "ai/ChatTypes.h"
#include "ai/EmbeddingProvider.h"
#include "doc/CitationVerifier.h"
#include "doc/HybridRetriever.h"
#include "doc/LocalIndex.h"
#include "doc/VectorIndex.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

namespace aipdf::app {

class IndexController;

// 整篇问答的「检索 + 核验」两件事。
//
// 模型调用不在这里：模型与密钥都在 Worker 进程里，
// 这里只负责把材料挑出来、把模型的回答核对一遍。
// 边界很清楚——主程序永远不碰网络与密钥。
//
// 检索是**混合**的：关键词（子串）+ 向量（本地哈希嵌入），
// 两路的名次用 RRF 融合（见 doc::HybridRetriever）。
// 核验是**离线**的：编号存在性 + 引文逐字核对 + 页码一致
// （见 doc::CitationVerifier），不额外调模型。
class DocumentQaController final : public QObject {
    Q_OBJECT

public:
    explicit DocumentQaController(QObject* parent = nullptr);
    ~DocumentQaController() override;

    // 绑定索引控制器（块与文本都从它那里来）。
    void setIndexController(IndexController* controller);

    // 检索：混合检索（关键词 + 向量），返回带编号的上下文块。
    //
    // 没有索引时返回空数组：上层要提示「先建立索引」。
    QVector<ai::ContextBlock> retrieve(const QString& question,
                                       int limit = 6) const;

    // 核验回答里的引用。
    doc::CitationReport verify(const QString& answer,
                               const QVector<ai::ContextBlock>& context) const;

    // 索引建好了没有（没建好就检索不到东西）。
    bool isIndexReady() const;
    // 向量索引就绪了吗。
    bool isVectorIndexReady() const;
    int vectorCount() const;

    // 让上层能触发建索引（问答前没索引要先建）。
    void requestIndexing();

    // 当前用的嵌入提供者（界面上要说明「这半边是本地算的」）。
    QString embedderName() const;
    bool isDemoEmbedder() const;

signals:
    void vectorIndexReady(int chunkCount);
    void vectorIndexFailed(const QString& message);

private:
    class Worker;

    // 后台线程算好的一条向量。
    struct VectorEntry {
        QString chunkId;
        int pageIndex = -1;
        ai::EmbeddingVector vector;
    };

    void handleIndexFinished();
    void rebuildVectorIndex();
    void handleVectorIndexReady(const QVector<VectorEntry>& entries);
    void handleVectorIndexFailed(const QString& message);

    IndexController* indexController_ = nullptr;
    // 嵌入提供者是**无状态**的（embed 是 const），
    // 所以主线程与后台线程可以共用同一个实例。
    std::shared_ptr<ai::EmbeddingProvider> embedder_;
    doc::VectorIndex vectors_;
    std::unique_ptr<Worker> worker_;
    bool vectorIndexReady_ = false;
};

}  // namespace aipdf::app
