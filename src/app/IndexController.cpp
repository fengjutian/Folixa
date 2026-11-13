#include "app/IndexController.h"

#include "app/PageTransform.h"
#include "app/TextLayout.h"
#include "doc/ChunkBuilder.h"
#include "doc/LocalIndex.h"
#include "pdf/Navigation.h"
#include "pdf/PdfiumAdapter.h"
#include "pdf/TextPage.h"

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <functional>

namespace aipdf::app {

namespace {

// 验收探针用的轨迹文件：索引的进度与结果要能被脚本看到。
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

// 预探几页来估正文字高。
//
// 探 8 页够了：正文字高是整篇文档的属性，少数几页就能代表；
// 探太多只是白花时间（每一页都要抽一次文本）。
constexpr int kProbePages = 8;

// 一页的文本 → 行（PDF user space）。
//
// 行距、缩进、字高都是排版特征，分块就靠它们。
QVector<doc::ChunkLine> buildLines(const pdf::TextPage& text) {
    QVector<doc::ChunkLine> lines;

    // 用页面自己的 /Rotate 建变换：索引是文档的属性，
    // 不该跟着用户的临时旋转变。
    PageTransform transform;
    transform.setPage(text.contentBox.left,
                      text.contentBox.bottom,
                      text.contentBox.width(),
                      text.contentBox.height());
    transform.setRotationDegrees(text.rotationDegrees);

    QVector<LayoutChar> chars;
    chars.reserve(text.chars.size());
    for (const pdf::TextChar& character : text.chars) {
        if (character.text.isEmpty()) {
            continue;
        }
        const QChar first = character.text.at(0);
        if (character.text.size() == 1 && first.unicode() < 0x20) {
            // 换行之类的控制字符没有字形，盒子是 0 宽 0 高，
            // 混进行聚类会凭空多出一行。
            continue;
        }
        LayoutChar item;
        item.text = character.text;
        item.box = transform.toDisplay(character.box);
        item.sourceIndex = character.index;
        chars.append(item);
    }
    if (chars.isEmpty()) {
        return lines;
    }

    TextLayout layout;
    layout.setChars(chars);
    const QVector<int>& order = layout.readingOrder();

    for (const TextLayout::Line& line : layout.lines()) {
        doc::ChunkLine result;
        result.text = layout.textForRange(line.start, line.end);
        QRectF box;
        QVector<double> heights;
        for (int position = line.start; position < line.end; ++position) {
            if (position < 0 || position >= order.size()) {
                continue;
            }
            const int source = order.at(position);
            if (source < 0 || source >= text.chars.size()) {
                continue;
            }
            const QRectF& charBox = text.chars.at(source).box;
            box = box.isNull() ? charBox : box.united(charBox);
            heights.append(charBox.height());
        }
        if (!heights.isEmpty()) {
            std::sort(heights.begin(), heights.end());
            result.charHeight = heights.at(heights.size() / 2);
        }
        result.box = box;
        if (!result.text.trimmed().isEmpty()) {
            lines.append(result);
        }
    }
    return lines;
}

// 一页文本的整体包围盒（PDF user space）。
//
// 指纹里要带它：同样的文字排在页上的不同位置，坐标不一样，
// 块不能直接复用。
QRectF textBoundsOf(const pdf::TextPage& text) {
    QRectF bounds;
    for (const pdf::TextChar& character : text.chars) {
        if (character.box.isNull()) {
            continue;
        }
        bounds = bounds.isNull() ? character.box : bounds.united(character.box);
    }
    return bounds;
}

// 大纲 → 每一页的章节路径。
//
// 章节路径给检索结果一个归属：「第2章 > 2.1 概述」比「第 5 页」有用得多。
QVector<QString> buildSectionPaths(const std::vector<pdf::OutlineItem>& outline,
                                    int pageCount) {
    struct Entry {
        QString title;
        int pageIndex = -1;
        int depth = 0;
    };
    QVector<Entry> entries;
    std::function<void(const std::vector<pdf::OutlineItem>&, int)> walk =
        [&](const std::vector<pdf::OutlineItem>& items, int depth) {
            for (const pdf::OutlineItem& item : items) {
                entries.append(Entry{item.title, item.pageIndex, depth});
                walk(item.children, depth + 1);
            }
        };
    walk(outline, 0);

    // 大纲条目未必按页序排（PDF 允许），按页排一下。
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& left, const Entry& right) {
                         const int leftPage = left.pageIndex < 0 ? 0 : left.pageIndex;
                         const int rightPage = right.pageIndex < 0 ? 0 : right.pageIndex;
                         return leftPage < rightPage;
                     });

    QVector<QString> paths(pageCount);
    QStringList stack;
    int entryIndex = 0;
    for (int page = 0; page < pageCount; ++page) {
        while (entryIndex < entries.size() &&
               entries.at(entryIndex).pageIndex >= 0 &&
               entries.at(entryIndex).pageIndex <= page) {
            const Entry& entry = entries.at(entryIndex);
            while (stack.size() > entry.depth) {
                stack.removeLast();
            }
            if (stack.size() == entry.depth) {
                stack.append(entry.title);
            } else {
                stack[entry.depth] = entry.title;
            }
            ++entryIndex;
        }
        paths[page] = stack.join(QStringLiteral(" > "));
    }
    return paths;
}

}  // namespace

// 后台线程：一页一页抽文本、分块。
//
// 这里刻意不用信号：Worker 是只在本文件里定义的类，moc 不会处理它；
// 改用 QMetaObject::invokeMethod 把结果投递回主线程。
class IndexController::Worker final : public QThread {
public:
    Worker(pdf::PdfDocument source, QObject* callbackContext)
        : source_(std::move(source)), callbackContext_(callbackContext) {}

    ~Worker() override {
        stop();
    }

    // 派一个建索引的任务。已有索引（knownIndex）用来复用内容没变的页。
    void requestBuild(const doc::LocalIndex& knownIndex) {
        {
            QMutexLocker locker(&mutex_);
            knownIndex_ = knownIndex;
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
        // 无限等：线程每处理完一页就检查一次取消标志，最多多做一页的活。
        // 带超时的话，超时之后线程还在跑，析构 QThread 就是未定义行为——
        // 这种坑上次踩过：残留进程把 exe 锁住，后续构建全部失败。
        wait();
    }

protected:
    void run() override {
        // 句柄只能在本线程用：在这里新建一份独立副本。
        // 原来的那份留着不动：它归主线程所有，要由主线程来释放。
        document_ = source_.isolatedCopy();
        if (!document_.isValid()) {
            postFinished(false, QStringLiteral("文档句柄不可用"));
            return;
        }

        const int pageCount = document_.pageCount();
        const QVector<QString> sectionPaths = buildSectionPaths(document_.outline(), pageCount);
        // 总页数只有后台线程知道（句柄在它自己手里），报回主线程。
        postTotalPages(pageCount);

        int order = 0;

        while (true) {
            doc::LocalIndex knownIndex;
            {
                QMutexLocker locker(&mutex_);
                while (!taskPending_ && !stop_) {
                    condition_.wait(&mutex_);
                }
                if (stop_) {
                    return;
                }
                taskPending_ = false;
                knownIndex = knownIndex_;
            }

            // 正文字高：整篇文档统一一个值，标题层级才跨页一致。
            // 索引里存过就复用，没存过才探。
            referenceHeight_ = knownIndex.referenceCharHeight();
            probed_.clear();
            if (referenceHeight_ <= 0.0) {
                referenceHeight_ = probeBodyHeight(&probed_);
            }

            bool cancelled = false;
            for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
                {
                    QMutexLocker locker(&mutex_);
                    cancelled = cancelled_;
                }
                if (cancelled) {
                    postFinished(true, QString());
                    break;
                }

                // 预探阶段已经抽好的页直接用，不用再抽一次。
                const auto probed = probed_.constFind(pageIndex);
                QString plain;
                QRectF pageBox;
                QRectF bounds;
                QVector<doc::ChunkLine> lines;
                if (probed != probed_.constEnd()) {
                    plain = probed->plain;
                    pageBox = probed->pageBox;
                    bounds = probed->bounds;
                    lines = probed->lines;
                } else {
                    const auto page = document_.page(pageIndex);
                    if (!page.has_value()) {
                        postPage(pageIndex, {}, QString(), false);
                        continue;
                    }
                    const std::optional<pdf::TextPage> text = page->extractText();
                    if (!text.has_value() || text->isEmpty()) {
                        // 没有文本层的页（扫描件、空页）：记一个指纹，
                        // 免得每次重建都当成「变了」。
                        const QString fingerprint = doc::LocalIndex::fingerprintOf(
                            QString(), QRectF(), QRectF());
                        postPage(pageIndex, {}, fingerprint, false);
                        continue;
                    }
                    plain = text->plainText();
                    const pdf::MediaBox& box = text->contentBox;
                    // 页面盒子按 (left, bottom, width, height) 传给指纹函数。
                    pageBox = QRectF(box.left, box.bottom, box.width(), box.height());
                    bounds = textBoundsOf(*text);
                    lines = buildLines(*text);
                }

                const QString fingerprint = doc::LocalIndex::fingerprintOf(
                    plain, pageBox, bounds);

                // 这一页的内容以前索引过没有？
                int knownPage = -1;
                if (knownIndex.findPageByFingerprint(fingerprint, &knownPage)) {
                    // 内容没变：直接复用那页的块，只改页序号。
                    QVector<doc::DocumentChunk> chunks = knownIndex.chunksForPage(knownPage);
                    for (doc::DocumentChunk& chunk : chunks) {
                        chunk.pageIndex = pageIndex;
                        chunk.order = order++;
                    }
                    postPage(pageIndex, chunks, fingerprint, true);
                    continue;
                }

                doc::ChunkBuilder::Options options;
                options.referenceCharHeight = referenceHeight_;
                const QVector<doc::DocumentChunk> chunks = doc::ChunkBuilder::build(
                    pageIndex,
                    lines,
                    sectionPaths.value(pageIndex),
                    order,
                    options,
                    fingerprint.left(8));
                order += chunks.size();
                postPage(pageIndex, chunks, fingerprint, false);
            }
            if (cancelled) {
                continue;
            }
            postFinished(false, QString());
        }
    }

private:
    // 预探阶段缓存的一页：文本、页面盒子、包围盒与行。
    struct ProbedPage {
        QString plain;
        QRectF pageBox;
        QRectF bounds;
        QVector<doc::ChunkLine> lines;
    };

    // 探前几页，估出整篇文档的正文字高。
    //
    // 探的时候把文本与行都缓存下来，主循环直接用——抽文本是这一步最贵的，
    // 不能做两遍。
    double probeBodyHeight(QHash<int, ProbedPage>* probed) {
        QVector<doc::ChunkBuilder::HeightSample> samples;
        const int probeCount = qMin(document_.pageCount(), kProbePages);
        for (int pageIndex = 0; pageIndex < probeCount; ++pageIndex) {
            const auto page = document_.page(pageIndex);
            if (!page.has_value()) {
                continue;
            }
            const std::optional<pdf::TextPage> text = page->extractText();
            if (!text.has_value() || text->isEmpty()) {
                continue;
            }
            ProbedPage item;
            item.plain = text->plainText();
            const pdf::MediaBox& box = text->contentBox;
            item.pageBox = QRectF(box.left, box.bottom, box.width(), box.height());
            item.bounds = textBoundsOf(*text);
            item.lines = buildLines(*text);
            for (const doc::ChunkLine& line : item.lines) {
                if (line.charHeight > 0.0) {
                    doc::ChunkBuilder::HeightSample sample;
                    sample.charHeight = line.charHeight;
                    sample.charCount = line.text.trimmed().size();
                    samples.append(sample);
                }
            }
            probed->insert(pageIndex, item);
        }
        return doc::ChunkBuilder::estimateBodyHeight(samples);
    }

    void postTotalPages(int totalPages) {
        QObject* context = callbackContext_;
        QMetaObject::invokeMethod(
            context,
            [context, totalPages]() {
                auto* controller = qobject_cast<IndexController*>(context);
                if (controller != nullptr) {
                    controller->handleTotalPages(totalPages);
                }
            },
            Qt::QueuedConnection);
    }

    void postPage(int pageIndex,
                  const QVector<doc::DocumentChunk>& chunks,
                  const QString& fingerprint,
                  bool reused) {
        QObject* context = callbackContext_;
        QMetaObject::invokeMethod(
            context,
            [context, pageIndex, chunks, fingerprint, reused]() {
                auto* controller = qobject_cast<IndexController*>(context);
                if (controller != nullptr) {
                    controller->handlePageReady(pageIndex, chunks, fingerprint, reused);
                }
            },
            Qt::QueuedConnection);
    }

    void postFinished(bool cancelled, const QString& errorMessage) {
        QObject* context = callbackContext_;
        const double referenceHeight = referenceHeight_;
        QMetaObject::invokeMethod(
            context,
            [context, cancelled, errorMessage, referenceHeight]() {
                auto* controller = qobject_cast<IndexController*>(context);
                if (controller != nullptr) {
                    controller->handleWorkerFinished(cancelled, errorMessage, referenceHeight);
                }
            },
            Qt::QueuedConnection);
    }

    pdf::PdfDocument source_;
    pdf::PdfDocument document_;
    QObject* callbackContext_ = nullptr;
    mutable QMutex mutex_;
    QWaitCondition condition_;
    doc::LocalIndex knownIndex_;
    // 预探阶段缓存的页：抽文本是这一步最贵的活，不能做两遍。
    QHash<int, ProbedPage> probed_;
    // 本轮用的正文字高（点）。
    double referenceHeight_ = 0.0;
    bool taskPending_ = false;
    bool cancelled_ = false;
    bool stop_ = false;
};

IndexController::IndexController(pdf::PdfDocument document, QObject* parent)
    : QObject(parent), worker_(std::make_unique<Worker>(std::move(document), this)) {
    worker_->start();
}

IndexController::~IndexController() {
    worker_->stop();
}

void IndexController::setDocumentPath(const QString& path) {
    documentPath_ = path;
    index_.setDocumentPath(path);
}

QString IndexController::documentPath() const {
    return documentPath_;
}

void IndexController::startIndexing() {
    if (worker_ == nullptr || state_ == State::Running) {
        return;
    }
    // 已有索引先载入：内容没变的页直接复用。
    doc::LocalIndex known;
    if (!documentPath_.isEmpty()) {
        known.load(documentPath_);
    }
    index_.clear();
    index_.setDocumentPath(documentPath_);

    state_ = State::Running;
    processedPages_ = 0;
    rebuiltPages_ = 0;
    errorMessage_.clear();
    // 真实页数只有后台线程知道（句柄在它手里），它算完会报回来。
    totalPages_ = 0;
    emit indexStarted(totalPages_);

    worker_->requestBuild(known);
    trace(QStringLiteral("index start path=%1").arg(documentPath_));
}

void IndexController::cancelIndexing() {
    if (worker_ != nullptr) {
        worker_->cancel();
    }
}

bool IndexController::isIndexing() const {
    return state_ == State::Running;
}

IndexController::State IndexController::state() const {
    return state_;
}

QString IndexController::statusText() const {
    switch (state_) {
        case State::Idle:
            return QStringLiteral("尚未建立索引");
        case State::Running:
            return QStringLiteral("索引中 %1/%2 页").arg(processedPages_).arg(totalPages_);
        case State::Completed:
            return QStringLiteral("索引完成：%1 个块（重建 %2 页）")
                .arg(index_.chunkCount())
                .arg(rebuiltPages_);
        case State::Cancelled:
            return QStringLiteral("索引已取消");
        case State::Failed:
            return QStringLiteral("索引失败：%1").arg(errorMessage_);
    }
    return QString();
}

int IndexController::processedPages() const {
    return processedPages_;
}

int IndexController::totalPages() const {
    return totalPages_;
}

int IndexController::rebuiltPages() const {
    return rebuiltPages_;
}

const doc::LocalIndex& IndexController::index() const {
    return index_;
}

QVector<doc::IndexHit> IndexController::search(const QString& query, int limit) const {
    return index_.search(query, limit);
}

bool IndexController::chunkById(const QString& chunkId, doc::DocumentChunk* chunk) const {
    return index_.chunkById(chunkId, chunk);
}

bool IndexController::chunkByIdOnPage(int pageIndex,
                                     const QString& chunkId,
                                     doc::DocumentChunk* chunk) const {
    return index_.chunkByIdOnPage(pageIndex, chunkId, chunk);
}

void IndexController::handleTotalPages(int totalPages) {
    totalPages_ = totalPages;
    emit indexProgress(processedPages_, totalPages_);
}

void IndexController::handlePageReady(int pageIndex,
                                      const QVector<doc::DocumentChunk>& chunks,
                                      const QString& fingerprint,
                                      bool reused) {
    index_.replacePage(pageIndex, chunks, fingerprint);
    ++processedPages_;
    if (!reused) {
        ++rebuiltPages_;
    }
    emit indexProgress(processedPages_, totalPages_);
}

void IndexController::handleWorkerFinished(bool cancelled,
                                          const QString& errorMessage,
                                          double referenceCharHeight) {
    if (!errorMessage.isEmpty()) {
        state_ = State::Failed;
        errorMessage_ = errorMessage;
        emit indexFailed(errorMessage_);
        return;
    }
    if (cancelled) {
        state_ = State::Cancelled;
        trace(QStringLiteral("index cancelled processed=%1").arg(processedPages_));
        emit indexCancelled();
        return;
    }
    index_.setReferenceCharHeight(referenceCharHeight);
    index_.setPageCount(qMax(index_.pageCount(), totalPages_));
    index_.save();
    state_ = State::Completed;
    trace(QStringLiteral("index done chunks=%1 rebuilt=%2 pages=%3")
              .arg(index_.chunkCount())
              .arg(rebuiltPages_)
              .arg(totalPages_));
    emit indexFinished(index_.chunkCount());
}

}  // namespace aipdf::app
