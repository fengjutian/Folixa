#include "app/TextController.h"

#include <QDateTime>
#include <QFile>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <optional>

namespace aipdf::app {

namespace {
// 验收探针用的轨迹文件：文本层与选区的过程要能被脚本看到。
void trace(const QString& message) {
    const QString path = qEnvironmentVariable("AIPDF_TRACE_FILE");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write((QDateTime::currentDateTime().toString(Qt::ISODate) + " " + message + "\n").toUtf8());
    }
}
}  // namespace

// 后台线程：提取页面文本 + 执行全文搜索。
//
// 任务队列里只有「要提取哪几页」和「搜索任务」两种，
// 页面文本优先于搜索——搜索慢可以等，选中文字不能等。
//
// 这里刻意不用信号：Worker 是只在本文件里定义的类，
// moc 不会处理它；改用 QMetaObject::invokeMethod 把结果投递回主线程。
class TextController::Worker final : public QThread {
public:
    Worker(pdf::PdfDocument source, QObject* callbackContext)
        : source_(std::move(source)), callbackContext_(callbackContext) {}

    ~Worker() override {
        stop();
    }

    void requestPage(int pageIndex) {
        {
            QMutexLocker locker(&mutex_);
            if (requestedPages_.contains(pageIndex)) {
                return;
            }
            requestedPages_.insert(pageIndex);
            pendingPages_.append(pageIndex);
        }
        condition_.wakeOne();
    }

    void startSearch(const QString& query) {
        {
            QMutexLocker locker(&mutex_);
            searchQuery_ = query;
            searchCancelled_ = false;
            searching_ = !query.isEmpty();
            searchHits_.clear();
            scannedPages_ = 0;
            // 有命中的页会缓存文本，换个关键词再搜时不必重新提取。
            hitPageCache_.clear();
        }
        condition_.wakeOne();
    }

    void cancelSearch() {
        {
            QMutexLocker locker(&mutex_);
            searchCancelled_ = true;
        }
        condition_.wakeOne();
    }

    void stop() {
        {
            QMutexLocker locker(&mutex_);
            stop_ = true;
            searching_ = false;
            condition_.wakeOne();
        }
        wait(5000);
    }

    bool isSearching() const {
        QMutexLocker locker(&mutex_);
        return searching_;
    }

    QVector<SearchHit> searchHits() const {
        QMutexLocker locker(&mutex_);
        return searchHits_;
    }

protected:
    void run() override {
        // 句柄只能在本线程用：在这里新建一份独立副本。
        document_ = source_.isolatedCopy();
        if (!document_.isValid()) {
            return;
        }

        while (true) {
            int pageIndex = -1;
            bool doSearch = false;
            {
                QMutexLocker locker(&mutex_);
                while (!stop_) {
                    if (!pendingPages_.isEmpty()) {
                        pageIndex = pendingPages_.takeFirst();
                        requestedPages_.remove(pageIndex);
                        break;
                    }
                    if (searching_ && !searchCancelled_) {
                        doSearch = true;
                        break;
                    }
                    condition_.wait(&mutex_);
                }
                if (stop_) {
                    return;
                }
            }

            if (pageIndex >= 0) {
                deliverPage(pageIndex);
                continue;
            }

            if (doSearch) {
                runSearchStep();
            }
        }
    }

private:
    std::optional<aipdf::pdf::TextPage> extractPage(int pageIndex) const {
        const auto page = document_.page(pageIndex);
        if (!page.has_value()) {
            return std::nullopt;
        }
        return page->extractText();
    }

    // 取一页文本并送回主线程。命中页优先用缓存，避免重复提取。
    void deliverPage(int pageIndex) {
        std::optional<aipdf::pdf::TextPage> text = cachedText(pageIndex);
        if (!text.has_value()) {
            text = extractPage(pageIndex);
        }
        if (text.has_value()) {
            postPage(pageIndex, *text);
        }
    }

    std::optional<aipdf::pdf::TextPage> cachedText(int pageIndex) const {
        QMutexLocker locker(&mutex_);
        const auto it = hitPageCache_.constFind(pageIndex);
        if (it == hitPageCache_.constEnd()) {
            return std::nullopt;
        }
        return it.value();
    }

    // 搜索推进一页：取出文本，找命中，记录后继续。
    void runSearchStep() {
        QString query;
        int pageIndex = 0;
        {
            QMutexLocker locker(&mutex_);
            if (!searching_ || searchCancelled_) {
                return;
            }
            query = searchQuery_;
            pageIndex = scannedPages_;
            const int totalPages = document_.pageCount();
            if (pageIndex >= totalPages) {
                searching_ = false;
                const bool cancelled = searchCancelled_;
                locker.unlock();
                notifySearchFinished(cancelled);
                return;
            }
            scannedPages_ = pageIndex + 1;
        }

        const auto text = extractPage(pageIndex);
        QVector<SearchHit> hits;
        if (text.has_value() && !query.isEmpty()) {
            hits = findHits(*text, query, pageIndex);
        }

        bool cancelled = false;
        int total = 0;
        {
            QMutexLocker locker(&mutex_);
            if (searchCancelled_) {
                cancelled = true;
                searching_ = false;
            } else {
                for (const SearchHit& hit : hits) {
                    searchHits_.append(hit);
                }
                total = searchHits_.size();
                if (!hits.isEmpty() && text.has_value()) {
                    hitPageCache_.insert(pageIndex, *text);
                }
            }
        }
        if (cancelled) {
            notifySearchFinished(true);
            return;
        }
        if (!hits.isEmpty()) {
            // 先送文本页再报命中：主线程收到命中时要有字符框才能画高亮。
            if (text.has_value()) {
                postPage(pageIndex, *text);
            }
            notifyHitsFound(total);
        }
    }

    // 在一页文本里找关键词。返回的区间是**字符下标**，
    // 不是 QString 下标——代理对（UTF-16 两个码元）会让两者错位。
    static QVector<SearchHit> findHits(const pdf::TextPage& page,
                                        const QString& query,
                                        int pageIndex) {
        QVector<SearchHit> hits;
        const QString plain = page.plainText();
        if (plain.isEmpty() || query.isEmpty()) {
            return hits;
        }

        // plain 的每个 UTF-16 位置属于哪个字符。
        QVector<int> charOfPosition;
        charOfPosition.reserve(plain.size() + 1);
        for (int index = 0; index < page.chars.size(); ++index) {
            const int units = qMax(1, page.chars.at(index).text.size());
            for (int unit = 0; unit < units; ++unit) {
                charOfPosition.append(index);
            }
        }
        charOfPosition.append(page.chars.size());  // 末尾哨兵

        int from = 0;
        while (true) {
            const int at = plain.indexOf(query, from, Qt::CaseInsensitive);
            if (at < 0) {
                break;
            }
            const int lastUnit = at + query.size() - 1;
            if (lastUnit >= 0 && lastUnit < charOfPosition.size() && at < charOfPosition.size()) {
                SearchHit hit;
                hit.pageIndex = pageIndex;
                hit.start = charOfPosition.at(at);
                hit.end = charOfPosition.at(lastUnit) + 1;
                hit.snippet = buildSnippet(plain, at, query.size());
                hits.append(hit);
            }
            from = at + qMax(1, query.size());
        }
        return hits;
    }

    static QString buildSnippet(const QString& text, int at, int length) {
        const int start = qMax(0, at - 24);
        const int end = qMin(text.size(), at + length + 24);
        QString snippet = text.mid(start, end - start).simplified();
        if (start > 0) {
            snippet.prepend(QStringLiteral("…"));
        }
        if (end < text.size()) {
            snippet.append(QStringLiteral("…"));
        }
        return snippet;
    }

    void postPage(int pageIndex, const pdf::TextPage& text) {
        QObject* context = callbackContext_;
        QMetaObject::invokeMethod(
            context,
            [context, pageIndex, text]() {
                auto* controller = qobject_cast<TextController*>(context);
                if (controller != nullptr) {
                    controller->handlePageExtracted(pageIndex, text);
                }
            },
            Qt::QueuedConnection);
    }

    void notifyHitsFound(int count) {
        QObject* context = callbackContext_;
        QMetaObject::invokeMethod(
            context,
            [context, count]() {
                auto* controller = qobject_cast<TextController*>(context);
                if (controller != nullptr) {
                    controller->handleHitsFound(count);
                }
            },
            Qt::QueuedConnection);
    }

    void notifySearchFinished(bool cancelled) {
        QObject* context = callbackContext_;
        QMetaObject::invokeMethod(
            context,
            [context, cancelled]() {
                auto* controller = qobject_cast<TextController*>(context);
                if (controller != nullptr) {
                    controller->handleSearchFinished(cancelled);
                }
            },
            Qt::QueuedConnection);
    }

    pdf::PdfDocument source_;
    pdf::PdfDocument document_;
    QObject* callbackContext_ = nullptr;

    mutable QMutex mutex_;
    QWaitCondition condition_;
    QVector<int> pendingPages_;
    QSet<int> requestedPages_;
    QString searchQuery_;
    QVector<SearchHit> searchHits_;
    // 有命中的页的文本，换关键词时可以直接复用。
    QHash<int, pdf::TextPage> hitPageCache_;
    int scannedPages_ = 0;
    bool searching_ = false;
    bool searchCancelled_ = false;
    bool stop_ = false;
};

// ------------------------------------------------------------ TextController ----

TextController::TextController(pdf::PdfDocument document, QObject* parent)
    : QObject(parent),
      worker_(std::make_unique<Worker>(std::move(document), this)) {
    worker_->start();
}

TextController::~TextController() {
    if (worker_) {
        worker_->stop();
    }
}

void TextController::setUserRotation(int degrees) {
    int normalized = ((degrees % 360) + 360) % 360;
    normalized = (normalized / 90) * 90;
    if (normalized == userRotation_) {
        return;
    }
    userRotation_ = normalized;

    // 坐标变了，所有已提取页的排版都要重算。
    const QList<int> pages = textPages_.keys();
    for (int index : pages) {
        buildLayout(index);
        emit textPageReady(index);
    }
    emit selectionChanged();
}

int TextController::userRotation() const {
    return userRotation_;
}

void TextController::requestTextPage(int pageIndex) {
    trace(QStringLiteral("request page=%1 hasText=%2 hasLayout=%3")
              .arg(pageIndex)
              .arg(textPages_.contains(pageIndex) ? 1 : 0)
              .arg(layouts_.contains(pageIndex) ? 1 : 0));
    if (textPages_.contains(pageIndex)) {
        // 文本层已经在手边，直接补排版。
        if (!layouts_.contains(pageIndex)) {
            buildLayout(pageIndex);
            emit textPageReady(pageIndex);
        }
        return;
    }
    pendingLayoutPages_.insert(pageIndex);
    worker_->requestPage(pageIndex);
}

bool TextController::hasTextPage(int pageIndex) const {
    return textPages_.contains(pageIndex);
}

const TextLayout* TextController::layout(int pageIndex) const {
    const auto it = layouts_.constFind(pageIndex);
    if (it == layouts_.constEnd()) {
        return nullptr;
    }
    return it.value().get();
}

int TextController::hitAt(int pageIndex, const QPointF& displayPoint) const {
    const TextLayout* textLayout = layout(pageIndex);
    if (textLayout == nullptr) {
        return -1;
    }
    return textLayout->hitTest(displayPoint);
}

void TextController::setSelection(int pageIndex, int orderStart, int orderEnd) {
    if (pageIndex < 0) {
        clearSelection();
        return;
    }
    const TextLayout* textLayout = layout(pageIndex);
    const int count = textLayout != nullptr ? textLayout->charCount() : 0;
    const int start = qBound(0, orderStart, count);
    const int end = qBound(start, orderEnd, count);
    if (selectionPage_ == pageIndex && selectionStart_ == start && selectionEnd_ == end) {
        return;
    }
    selectionPage_ = pageIndex;
    selectionStart_ = start;
    selectionEnd_ = end;
    emit selectionChanged();
}

void TextController::clearSelection() {
    if (selectionPage_ < 0 && selectionStart_ == 0 && selectionEnd_ == 0) {
        return;
    }
    selectionPage_ = -1;
    selectionStart_ = 0;
    selectionEnd_ = 0;
    emit selectionChanged();
}

int TextController::selectionPage() const {
    return selectionPage_;
}

int TextController::selectionStart() const {
    return selectionStart_;
}

int TextController::selectionEnd() const {
    return selectionEnd_;
}

QString TextController::selectedText() const {
    if (selectionPage_ < 0) {
        return {};
    }
    const TextLayout* textLayout = layout(selectionPage_);
    if (textLayout == nullptr) {
        return {};
    }
    return textLayout->textForRange(selectionStart_, selectionEnd_);
}

QVector<QRectF> TextController::selectionRects() const {
    if (selectionPage_ < 0) {
        return {};
    }
    const TextLayout* textLayout = layout(selectionPage_);
    if (textLayout == nullptr) {
        return {};
    }
    return textLayout->highlightRects(selectionStart_, selectionEnd_);
}

void TextController::startSearch(const QString& query) {
    hits_.clear();
    worker_->startSearch(query);
    emit searchStarted();
}

void TextController::cancelSearch() {
    worker_->cancelSearch();
}

bool TextController::isSearching() const {
    return worker_ != nullptr && worker_->isSearching();
}

QVector<SearchHit> TextController::searchHits() const {
    return hits_;
}

QVector<QRectF> TextController::searchHighlightRects(int pageIndex) const {
    QVector<QRectF> rects;
    if (hits_.isEmpty()) {
        return rects;
    }
    const auto pageIt = textPages_.constFind(pageIndex);
    if (pageIt == textPages_.constEnd()) {
        return rects;
    }
    const pdf::TextPage& page = pageIt.value();

    PageTransform transform;
    if (!transformFor(pageIndex, &transform)) {
        return rects;
    }

    for (const SearchHit& hit : hits_) {
        if (hit.pageIndex != pageIndex) {
            continue;
        }
        bool hasBox = false;
        QRectF box;
        for (int index = hit.start; index < hit.end && index < page.chars.size(); ++index) {
            const QRectF display = transform.toDisplay(page.chars.at(index).box);
            box = hasBox ? box.united(display) : display;
            hasBox = true;
        }
        if (hasBox) {
            rects.append(box);
        }
    }
    return rects;
}

void TextController::handlePageExtracted(int pageIndex, const pdf::TextPage& text) {
    textPages_.insert(pageIndex, text);
    const bool needsLayout = pendingLayoutPages_.remove(pageIndex);
    if (needsLayout) {
        buildLayout(pageIndex);
    }
    trace(QStringLiteral("text page=%1 chars=%2 layout=%3")
              .arg(pageIndex)
              .arg(text.charCount())
              .arg(needsLayout ? 1 : 0));
    emit textPageReady(pageIndex);
}

void TextController::handleHitsFound(int count) {
    Q_UNUSED(count);
    hits_ = worker_->searchHits();
    emit searchHitsFound(hits_.size());
}

void TextController::handleSearchFinished(bool cancelled) {
    hits_ = worker_->searchHits();
    emit searchFinished(cancelled);
}

void TextController::buildLayout(int pageIndex) {
    const auto it = textPages_.constFind(pageIndex);
    if (it == textPages_.constEnd()) {
        trace(QStringLiteral("build layout page=%1 no text page").arg(pageIndex));
        return;
    }
    const pdf::TextPage& page = it.value();

    PageTransform transform;
    if (!transformFor(pageIndex, &transform)) {
        trace(QStringLiteral("build layout page=%1 transform failed").arg(pageIndex));
        return;
    }

    QVector<LayoutChar> chars;
    chars.reserve(page.chars.size());
    for (const pdf::TextChar& character : page.chars) {
        // PDFium 会在换行处插入 \r 之类的控制字符：它们没有字形，
        // 盒子是 0 宽 0 高，混进行聚类会凭空多出一行。
        if (character.text.isEmpty()) {
            continue;
        }
        const QChar first = character.text.at(0);
        if (character.text.size() == 1 && first.unicode() < 0x20) {
            continue;
        }
        LayoutChar item;
        item.text = character.text;
        item.box = transform.toDisplay(character.box);
        item.sourceIndex = character.index;
        chars.append(item);
    }

    auto textLayout = std::make_shared<TextLayout>();
    textLayout->setChars(chars);
    layouts_.insert(pageIndex, std::move(textLayout));
    trace(QStringLiteral("build layout page=%1 chars=%2").arg(pageIndex).arg(chars.size()));
}

bool TextController::transformFor(int pageIndex, PageTransform* transform) const {
    if (transform == nullptr) {
        return false;
    }
    // 页面盒子与旋转随文本层一起取回：主线程不必再碰 PDFium，
    // 也就不会和渲染线程/后台线程抢 PDFium 的全局锁。
    const auto it = textPages_.constFind(pageIndex);
    if (it == textPages_.constEnd()) {
        return false;
    }
    const pdf::TextPage& page = it.value();
    const pdf::MediaBox box = page.contentBox;
    const double left = qMin(box.left, box.right);
    const double right = qMax(box.left, box.right);
    const double bottom = qMin(box.bottom, box.top);
    const double top = qMax(box.bottom, box.top);
    transform->setPage(left, bottom, right - left, top - bottom);
    // 总旋转 = 页面自带的 /Rotate + 用户额外旋转（都是顺时针）。
    transform->setRotationDegrees(page.rotationDegrees + userRotation_);
    return true;
}

}  // namespace aipdf::app
