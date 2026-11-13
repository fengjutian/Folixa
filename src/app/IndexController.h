#pragma once

#include "doc/LocalIndex.h"

#include "pdf/PdfiumAdapter.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

namespace aipdf::app {

// 索引控制器：把文档变成可检索、可定位的块。
//
// 干活的是后台线程：PDFium 的句柄只能在创建它的线程里用，
// 而且几百页抽文本要几秒，不能占着界面。
// 主线程这边只管状态、进度与结果。
//
// 索引是增量的：每一页记一个「内容指纹」，重建时内容没变的页
// 直接复用旧块（前面插了一页导致页码整体后移，也能按内容认出来）。
class IndexController : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,       // 还没有索引
        Running,    // 正在建
        Completed,  // 建完了
        Cancelled,  // 用户取消了
        Failed      // 出错了
    };
    Q_ENUM(State)

    explicit IndexController(pdf::PdfDocument document, QObject* parent = nullptr);
    ~IndexController() override;

    // 文档路径：索引文件按它命名。
    void setDocumentPath(const QString& path);
    QString documentPath() const;

    // 开始建索引。已有的索引会先载入，只有内容变了的页才重建。
    void startIndexing();
    // 取消（后台线程会在处理完当前页后停下）。
    void cancelIndexing();
    bool isIndexing() const;

    State state() const;
    QString statusText() const;
    int processedPages() const;
    int totalPages() const;
    // 这一轮真正重建了几页（其余页面内容没变，直接复用）。
    int rebuiltPages() const;

    // 本地索引（建完才有内容）。
    const doc::LocalIndex& index() const;

    // 检索：子串匹配，忽略大小写。
    QVector<doc::IndexHit> search(const QString& query, int limit = 200) const;
    // 按 id 取块（结果 → 坐标）。
    bool chunkById(const QString& chunkId, doc::DocumentChunk* chunk) const;
    // 某一页里按 id 取块。
    //
    // 定位用这个：两页内容完全一样时，按 id 全局取会拿到另一页的块，
    // 坐标就错位了。
    bool chunkByIdOnPage(int pageIndex, const QString& chunkId, doc::DocumentChunk* chunk) const;

signals:
    void indexStarted(int totalPages);
    void indexProgress(int donePages, int totalPages);
    void indexFinished(int chunkCount);
    void indexCancelled();
    void indexFailed(const QString& message);

private:
    class Worker;

    void handleTotalPages(int totalPages);
    void handlePageReady(int pageIndex,
                         const QVector<doc::DocumentChunk>& chunks,
                         const QString& fingerprint,
                         bool reused);
    void handleWorkerFinished(bool cancelled,
                              const QString& errorMessage,
                              double referenceCharHeight);

    std::unique_ptr<Worker> worker_;
    doc::LocalIndex index_;
    QString documentPath_;
    State state_ = State::Idle;
    int processedPages_ = 0;
    int totalPages_ = 0;
    int rebuiltPages_ = 0;
    QString errorMessage_;
};

}  // namespace aipdf::app
