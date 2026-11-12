#include "app/RenderThread.h"

#include "app/TileCache.h"

#include <QMutexLocker>

namespace aipdf::app {

RenderThread::RenderThread(pdf::PdfDocument source, QObject* parent)
    : QThread(parent), source_(std::move(source)) {}

RenderThread::~RenderThread() {
    {
        QMutexLocker locker(&mutex_);
        stop_ = true;
        dirty_ = false;
    }
    condition_.wakeAll();
    wait();
}

void RenderThread::requestRange(int firstPage, int lastPage, double zoom) {
    {
        QMutexLocker locker(&mutex_);
        if (stop_) {
            return;
        }
        firstPage_ = qMax(0, firstPage);
        lastPage_ = lastPage;
        zoom_ = (zoom > 0.0) ? zoom : 1.0;
        ++generation_;
        dirty_ = true;
    }
    condition_.wakeOne();
}

void RenderThread::cancel() {
    {
        QMutexLocker locker(&mutex_);
        ++generation_;
        dirty_ = false;
    }
}

int RenderThread::pendingCount() const {
    QMutexLocker locker(&mutex_);
    return pending_;
}

void RenderThread::run() {
    // 在本线程建立独立句柄：PDFium 的句柄不能跨线程使用。
    // 建立后立刻释放主线程文档的引用，避免句柄在错误的线程里被关闭。
    document_ = source_.isolatedCopy();
    source_ = pdf::PdfDocument();

    if (!document_.isValid()) {
        emit pageFailed(-1, 0);
    }

    while (true) {
        int first = 0;
        int last = -1;
        double zoom = 1.0;
        quint64 generation = 0;

        {
            QMutexLocker locker(&mutex_);
            while (!dirty_ && !stop_) {
                condition_.wait(&mutex_);
            }
            if (stop_) {
                break;
            }
            first = firstPage_;
            last = lastPage_;
            zoom = zoom_;
            generation = generation_;
            dirty_ = false;
            pending_ = qMax(0, last - first + 1);
        }

        const qint64 zoomKey = TileCache::zoomToKey(zoom);
        for (int index = first; index <= last; ++index) {
            {
                QMutexLocker locker(&mutex_);
                if (stop_ || generation != generation_) {
                    // 已经有更新一代的请求，旧的直接丢掉。
                    break;
                }
            }

            const auto page = document_.page(index);
            if (!page.has_value()) {
                emit pageFailed(index, zoomKey);
                continue;
            }
            const QImage image = page->render(zoom);
            if (image.isNull()) {
                emit pageFailed(index, zoomKey);
                continue;
            }
            emit pageRendered(index, zoomKey, image);
        }

        {
            QMutexLocker locker(&mutex_);
            pending_ = 0;
        }
    }
}

}  // namespace aipdf::app
