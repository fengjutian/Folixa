#include "app/DocumentQaController.h"

#include "ai/LocalHashEmbedding.h"
#include "app/IndexController.h"
#include "doc/DocumentChunk.h"

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>

#include <utility>

namespace aipdf::app {

namespace {

// 验收探针用的轨迹文件。
void trace(const QString& message) {
    const QString path = qEnvironmentVariable("AIPDF_TRACE_FILE");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " +
                    message + "\n")
                       .toUtf8());
    }
}

}  // namespace

// 后台线程：给每个块算向量。
//
// 为什么放后台：几百页的文档要算几千个向量，
// 放主线程会让界面卡住——而建索引本身就已经是后台的事了。
//
// 这里刻意不用信号：Worker 是只在本文件里定义的类，moc 不会处理它；
// 改用 QMetaObject::invokeMethod 把结果投递回主线程。
class DocumentQaController::Worker final : public QThread {
public:
    // 一条待算的向量。
    struct Entry {
        QString chunkId;
        int pageIndex = -1;
        QString text;
    };

    Worker(std::shared_ptr<ai::EmbeddingProvider> embedder, QObject* callbackContext)
        : embedder_(std::move(embedder)), callbackContext_(callbackContext) {}

    ~Worker() override {
        stop();
    }

    void requestBuild(const QVector<Entry>& entries) {
        {
            QMutexLocker locker(&mutex_);
            entries_ = entries;
            taskPending_ = true;
            cancelled_ = false;
        }
        condition_.wakeOne();
    }

    void cancel() {
        QMutexLocker locker(&mutex_);
        cancelled_ = true;
    }

    void stop() {
        {
            QMutexLocker locker(&mutex_);
            cancelled_ = true;
            stop_ = true;
        }
        condition_.wakeAll();
        // 无限等：线程每算完一批就检查一次取消标志。
        // 带超时的话，超时之后线程还在跑，析构 QThread 就是未定义行为。
        wait();
    }

protected:
    void run() override {
        while (true) {
            QVector<Entry> entries;
            {
                QMutexLocker locker(&mutex_);
                while (!taskPending_ && !stop_) {
                    condition_.wait(&mutex_);
                }
                if (stop_) {
                    return;
                }
                taskPending_ = false;
                entries = entries_;
            }

            // 一次算完整批：逐块调用会让字符串列表与哈希桶反复重建。
            QStringList texts;
            texts.reserve(entries.size());
            for (const Entry& entry : entries) {
                texts.append(entry.text);
            }

            bool cancelled = false;
            {
                QMutexLocker locker(&mutex_);
                cancelled = cancelled_;
            }
            if (cancelled) {
                continue;
            }

            const QVector<ai::EmbeddingVector> computed = embedder_->embed(texts);
            if (computed.size() != entries.size()) {
                postFailed(QStringLiteral("嵌入结果数量与块数不一致"));
                continue;
            }

            QVector<DocumentQaController::VectorEntry> result;
            result.reserve(entries.size());
            for (int index = 0; index < entries.size(); ++index) {
                DocumentQaController::VectorEntry item;
                item.chunkId = entries.at(index).chunkId;
                item.pageIndex = entries.at(index).pageIndex;
                item.vector = computed.at(index);
                result.append(item);
            }
            postReady(result);
        }
    }

private:
    void postReady(const QVector<DocumentQaController::VectorEntry>& entries) {
        QObject* context = callbackContext_;
        QMetaObject::invokeMethod(
            context,
            [context, entries]() {
                auto* controller = qobject_cast<DocumentQaController*>(context);
                if (controller != nullptr) {
                    controller->handleVectorIndexReady(entries);
                }
            },
            Qt::QueuedConnection);
    }

    void postFailed(const QString& message) {
        QObject* context = callbackContext_;
        QMetaObject::invokeMethod(
            context,
            [context, message]() {
                auto* controller = qobject_cast<DocumentQaController*>(context);
                if (controller != nullptr) {
                    controller->handleVectorIndexFailed(message);
                }
            },
            Qt::QueuedConnection);
    }

    std::shared_ptr<ai::EmbeddingProvider> embedder_;
    QObject* callbackContext_ = nullptr;
    mutable QMutex mutex_;
    QWaitCondition condition_;
    QVector<Entry> entries_;
    bool taskPending_ = false;
    bool cancelled_ = false;
    bool stop_ = false;
};

DocumentQaController::DocumentQaController(QObject* parent)
    : QObject(parent),
      embedder_(std::make_shared<ai::LocalHashEmbedding>()),
      worker_(std::make_unique<Worker>(embedder_, this)) {
    worker_->start();
}

DocumentQaController::~DocumentQaController() {
    worker_->stop();
}

void DocumentQaController::setIndexController(IndexController* controller) {
    if (indexController_ == controller) {
        return;
    }
    if (indexController_ != nullptr) {
        disconnect(indexController_, nullptr, this, nullptr);
    }
    indexController_ = controller;
    if (indexController_ != nullptr) {
        // 索引一建好就把向量索引跟上：问答需要两半都在。
        connect(indexController_, &IndexController::indexFinished, this,
                [this](int) { handleIndexFinished(); });
    }
}

bool DocumentQaController::isIndexReady() const {
    if (indexController_ == nullptr) {
        return false;
    }
    if (indexController_->state() != IndexController::State::Completed) {
        return false;
    }
    return !indexController_->index().isEmpty();
}

bool DocumentQaController::isVectorIndexReady() const {
    return vectorIndexReady_;
}

int DocumentQaController::vectorCount() const {
    return vectors_.vectorCount();
}

QString DocumentQaController::embedderName() const {
    if (embedder_ == nullptr) {
        return QString();
    }
    return embedder_->id();
}

bool DocumentQaController::isDemoEmbedder() const {
    return embedder_ != nullptr && embedder_->isDemo();
}

void DocumentQaController::requestIndexing() {
    if (indexController_ == nullptr) {
        return;
    }
    indexController_->startIndexing();
}

QVector<ai::ContextBlock> DocumentQaController::retrieve(const QString& question,
                                                          int limit) const {
    QVector<ai::ContextBlock> result;
    if (indexController_ == nullptr || question.trimmed().isEmpty()) {
        return result;
    }
    const doc::LocalIndex& index = indexController_->index();
    if (index.isEmpty()) {
        return result;
    }

    doc::HybridRetriever retriever(&index, &vectors_, embedder_.get());
    doc::HybridRetriever::Options options;
    options.limit = limit > 0 ? limit : 6;
    const QVector<doc::HybridHit> hits = retriever.search(question, options);

    int number = 0;
    for (const doc::HybridHit& hit : hits) {
        const QString text = hit.text.trimmed();
        if (text.isEmpty()) {
            // 没有正文的块对模型没用，跳过。
            continue;
        }
        ai::ContextBlock block;
        block.number = ++number;
        block.pageNumber = hit.pageIndex + 1;
        block.text = text;
        block.chunkId = hit.chunkId;
        result.append(block);
    }
    return result;
}

doc::CitationReport DocumentQaController::verify(const QString& answer,
                                                  const QVector<ai::ContextBlock>& context) const {
    QVector<doc::CitationBlock> blocks;
    blocks.reserve(context.size());
    for (const ai::ContextBlock& item : context) {
        doc::CitationBlock block;
        block.number = item.number;
        // 核验器里的页码是 0 起的页序号；ContextBlock 里是 1 起的页码。
        block.pageIndex = item.pageNumber - 1;
        block.text = item.text;
        blocks.append(block);
    }
    return doc::CitationVerifier::verify(answer, blocks);
}

void DocumentQaController::handleIndexFinished() {
    rebuildVectorIndex();
}

void DocumentQaController::rebuildVectorIndex() {
    vectorIndexReady_ = false;
    if (indexController_ == nullptr || embedder_ == nullptr) {
        return;
    }
    const doc::LocalIndex& index = indexController_->index();
    if (index.isEmpty()) {
        return;
    }
    const QString documentPath = indexController_->documentPath();

    // 待算的块：只算有正文的块。
    QVector<Worker::Entry> entries;
    QStringList wantedIds;
    for (const doc::DocumentChunk& chunk : index.chunks()) {
        if (chunk.text.trimmed().isEmpty()) {
            continue;
        }
        Worker::Entry entry;
        entry.chunkId = chunk.id;
        entry.pageIndex = chunk.pageIndex;
        entry.text = chunk.text;
        entries.append(entry);
        wantedIds.append(chunk.id);
    }
    wantedIds.sort();
    if (entries.isEmpty()) {
        return;
    }

    // 复用：已存的向量索引覆盖了当前全部块，且嵌入提供者没变。
    //
    // 块 id 是「页内容指纹 + 页内块序号」，所以 id 集合相同
    // 就意味着这些块的正文也没变——向量可以直接用，不必重算。
    doc::VectorIndex cached;
    if (cached.load(documentPath) && cached.providerId() == embedder_->id() &&
        cached.dimension() == embedder_->dimension() && cached.chunkIds() == wantedIds) {
        vectors_ = cached;
        vectorIndexReady_ = true;
        trace(QStringLiteral("qa vectors reused chunks=%1").arg(vectors_.vectorCount()));
        emit vectorIndexReady(vectors_.vectorCount());
        return;
    }

    // 重建：后台算，算完再存。
    vectors_.clear();
    vectors_.setDocumentPath(documentPath);
    vectors_.setProviderId(embedder_->id());
    worker_->requestBuild(entries);
    trace(QStringLiteral("qa vectors rebuilding chunks=%1").arg(entries.size()));
}

void DocumentQaController::handleVectorIndexReady(const QVector<VectorEntry>& entries) {
    vectors_.clear();
    vectors_.setDocumentPath(indexController_ != nullptr
                                 ? indexController_->documentPath()
                                 : QString());
    vectors_.setProviderId(embedder_ != nullptr ? embedder_->id() : QString());
    for (const VectorEntry& entry : entries) {
        vectors_.put(entry.chunkId, entry.vector, entry.pageIndex);
    }
    if (!vectors_.save()) {
        vectorIndexReady_ = false;
        emit vectorIndexFailed(QStringLiteral("向量索引写盘失败"));
        return;
    }
    vectorIndexReady_ = true;
    trace(QStringLiteral("qa vectors ready chunks=%1").arg(vectors_.vectorCount()));
    emit vectorIndexReady(vectors_.vectorCount());
}

void DocumentQaController::handleVectorIndexFailed(const QString& message) {
    vectorIndexReady_ = false;
    trace(QStringLiteral("qa vectors failed reason=%1").arg(message));
    emit vectorIndexFailed(message);
}

}  // namespace aipdf::app
