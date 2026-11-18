#include "app/NavigationController.h"

#include "app/LinkHitTest.h"

#include <QDateTime>
#include <QFile>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

namespace aipdf::app {

namespace {
// 验收探针用的轨迹文件：大纲与链接的取回过程要能被脚本看到。
void trace(const QString& message) {
    const QString path = qEnvironmentVariable("AIPDF_TRACE_FILE");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " + message + "\n").toUtf8());
    }
}
}  // namespace

// 后台线程：取文档大纲与每页的链接注释。
//
// 任务队列里只有「取大纲」和「取某页链接」两种。
// 大纲先取（一次就够），链接按页推进，滚到哪页取哪页。
//
// 这里刻意不用信号：Worker 是只在本文件里定义的类，moc 不会处理它；
// 改用 QMetaObject::invokeMethod 把结果投递回主线程。
class NavigationController::Worker final : public QThread {
public:
    Worker(pdf::PdfDocument source, QObject* callbackContext)
        : source_(std::move(source)), callbackContext_(callbackContext) {}

    ~Worker() override {
        stop();
    }

    void requestOutline() {
        {
            QMutexLocker locker(&mutex_);
            if (outlineRequested_) {
                return;
            }
            outlineRequested_ = true;
            outlinePending_ = true;
        }
        condition_.wakeOne();
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

    void stop() {
        {
            QMutexLocker locker(&mutex_);
            stop_ = true;
            condition_.wakeOne();
        }
        wait(5000);
    }

protected:
    void run() override {
        // 句柄只能在本线程用：在这里新建一份独立副本。
        document_ = source_.isolatedCopy();
        if (!document_.isValid()) {
            return;
        }

        while (true) {
            bool doOutline = false;
            int pageIndex = -1;
            {
                QMutexLocker locker(&mutex_);
                while (!stop_) {
                    if (outlinePending_) {
                        outlinePending_ = false;
                        doOutline = true;
                        break;
                    }
                    if (!pendingPages_.isEmpty()) {
                        pageIndex = pendingPages_.takeFirst();
                        requestedPages_.remove(pageIndex);
                        break;
                    }
                    condition_.wait(&mutex_);
                }
                if (stop_) {
                    return;
                }
            }

            if (doOutline) {
                postOutline(document_.outline());
                continue;
            }
            if (pageIndex >= 0) {
                deliverPage(pageIndex);
            }
        }
    }

private:
    // 取一页的链接与页面几何，一起送回主线程。
    void deliverPage(int pageIndex) {
        PageNavigation navigation;
        const auto page = document_.page(pageIndex);
        if (page.has_value()) {
            navigation.links = page->links();
            navigation.contentBox = page->contentBox();
            navigation.rotationDegrees = page->rotationDegrees();
        }
        postPageLinks(pageIndex, navigation);
    }

    void postOutline(const std::vector<pdf::OutlineItem>& outline) {
        QObject* context = callbackContext_;
        QMetaObject::invokeMethod(
            context,
            [context, outline]() {
                auto* controller = qobject_cast<NavigationController*>(context);
                if (controller != nullptr) {
                    controller->handleOutline(outline);
                }
            },
            Qt::QueuedConnection);
    }

    void postPageLinks(int pageIndex, const PageNavigation& navigation) {
        QObject* context = callbackContext_;
        QMetaObject::invokeMethod(
            context,
            [context, pageIndex, navigation]() {
                auto* controller = qobject_cast<NavigationController*>(context);
                if (controller != nullptr) {
                    controller->handlePageLinks(pageIndex, navigation);
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
    bool outlineRequested_ = false;
    bool outlinePending_ = false;
    bool stop_ = false;
};

// ------------------------------------------------------- NavigationController ----

NavigationController::NavigationController(pdf::PdfDocument document, QObject* parent)
    : QObject(parent),
      worker_(std::make_unique<Worker>(std::move(document), this)) {
    worker_->start();
    // 大纲只要一份，开文档时就排队取。
    worker_->requestOutline();
}

NavigationController::~NavigationController() {
    if (worker_) {
        worker_->stop();
    }
}

void NavigationController::setUserRotation(int degrees) {
    int normalized = ((degrees % 360) + 360) % 360;
    normalized = (normalized / 90) * 90;
    userRotation_ = normalized;
}

int NavigationController::userRotation() const {
    return userRotation_;
}

bool NavigationController::outlineLoaded() const {
    return outlineLoaded_;
}

const std::vector<pdf::OutlineItem>& NavigationController::outline() const {
    return outline_;
}

std::vector<pdf::OutlineEntry> NavigationController::flatOutline() const {
    return pdf::flattenOutline(outline_);
}

void NavigationController::requestPageLinks(int pageIndex) {
    if (pageIndex < 0 || pages_.contains(pageIndex)) {
        return;
    }
    worker_->requestPage(pageIndex);
}

bool NavigationController::hasPageLinks(int pageIndex) const {
    return pages_.contains(pageIndex);
}

const QVector<pdf::PageLink>& NavigationController::linksForPage(int pageIndex) const {
    static const QVector<pdf::PageLink> empty;
    const auto it = pages_.constFind(pageIndex);
    if (it == pages_.constEnd()) {
        return empty;
    }
    return it.value().links;
}

int NavigationController::hitLink(int pageIndex, const QPointF& displayPoint) const {
    const auto it = pages_.constFind(pageIndex);
    if (it == pages_.constEnd()) {
        return -1;
    }
    PageTransform transform;
    if (!transformFor(pageIndex, &transform)) {
        return -1;
    }
    return hitTestLink(it.value().links, transform, displayPoint);
}

bool NavigationController::transformFor(int pageIndex, PageTransform* transform) const {
    if (transform == nullptr) {
        return false;
    }
    const auto it = pages_.constFind(pageIndex);
    if (it == pages_.constEnd()) {
        return false;
    }
    const pdf::MediaBox& box = it.value().contentBox;
    const double left = qMin(box.left, box.right);
    const double right = qMax(box.left, box.right);
    const double bottom = qMin(box.bottom, box.top);
    const double top = qMax(box.bottom, box.top);
    transform->setPage(left, bottom, right - left, top - bottom);
    // 总旋转 = 页面自带的 /Rotate + 用户额外旋转（都是顺时针）。
    transform->setRotationDegrees(it.value().rotationDegrees + userRotation_);
    return true;
}

void NavigationController::handleOutline(const std::vector<pdf::OutlineItem>& outline) {
    outline_ = outline;
    outlineLoaded_ = true;
    const int count = static_cast<int>(outline_.size());
    trace(QStringLiteral("outline ready items=%1").arg(count));
    emit outlineReady();
}

void NavigationController::handlePageLinks(int pageIndex, const PageNavigation& navigation) {
    pages_.insert(pageIndex, navigation);
    trace(QStringLiteral("links ready page=%1 count=%2")
              .arg(pageIndex)
              .arg(navigation.links.size()));
    emit pageLinksReady(pageIndex);
}

}  // namespace aipdf::app
