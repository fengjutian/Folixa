#include "ocr/OcrService.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThread>
#include <QWaitCondition>

#include <atomic>

namespace aipdf::ocr {

class OcrService::Worker final : public QThread {
public:
    using ResultSink = std::function<void(const OcrPageResult&)>;
    using FailureSink = std::function<void(int, const QString&)>;
    using IdleSink = std::function<void()>;

    Worker(std::unique_ptr<OcrEngine> engine, ResultSink onResult, FailureSink onFailure, IdleSink onIdle)
        : engine_(std::move(engine)),
          onResult_(std::move(onResult)),
          onFailure_(std::move(onFailure)),
          onIdle_(std::move(onIdle)) {}

    void enqueue(const OcrJob& job) {
        {
            QMutexLocker locker(&mutex_);
            if (queuedPages_.contains(job.pageIndex)) {
                return;
            }
            queuedPages_.insert(job.pageIndex);
            queue_.append(job);
        }
        wake_.wakeOne();
    }

    void cancelAll() {
        {
            QMutexLocker locker(&mutex_);
            cancelled_ = true;
            for (const OcrJob& job : queue_) {
                dropped_.append(job.pageIndex);
            }
            queuedPages_.clear();
            queue_.clear();
        }
        wake_.wakeOne();
    }

    int pendingCount() const {
        QMutexLocker locker(&mutex_);
        return static_cast<int>(queue_.size()) + (busy_ ? 1 : 0);
    }

    void stop() {
        {
            QMutexLocker locker(&mutex_);
            stopping_ = true;
            cancelled_ = true;
        }
        wake_.wakeAll();
    }

protected:
    void run() override {
        forever {
            QList<int> dropped;
            OcrJob job;
            bool hasJob = false;
            {
                QMutexLocker locker(&mutex_);
                if (queue_.isEmpty()) {
                    dropped.swap(dropped_);
                } else if (!cancelled_) {
                    job = queue_.takeFirst();
                    hasJob = true;
                    busy_ = true;
                } else {
                    dropped_.append(queue_.takeFirst().pageIndex);
                }
            }

            for (const int page : dropped) {
                OcrPageResult cancelledResult;
                cancelledResult.pageIndex = page;
                cancelledResult.cancelled = true;
                if (onResult_) {
                    onResult_(cancelledResult);
                }
            }

            if (!hasJob) {
                QMutexLocker locker(&mutex_);
                if (stopping_) {
                    return;
                }
                const bool idle = queue_.isEmpty() && !busy_;
                locker.unlock();
                if (idle && onIdle_) {
                    onIdle_();
                }
                locker.relock();
                if (stopping_) {
                    return;
                }
                wake_.wait(&mutex_, 100);
                continue;
            }

            OcrPageResult result;
            if (engine_ != nullptr) {
                result = engine_->recognize(job);
            } else {
                result.pageIndex = job.pageIndex;
                result.error = QStringLiteral("没有可用的 OCR 引擎");
            }

            bool isCancelled = false;
            {
                QMutexLocker locker(&mutex_);
                isCancelled = cancelled_;
                queuedPages_.remove(job.pageIndex);
                busy_ = false;
            }
            result.pageIndex = job.pageIndex;
            if (isCancelled) {
                result.cancelled = true;
            }

            if (result.cancelled) {
                // 取消的页也回报：调用方靠它把「识别中」的占位清掉。
                if (onResult_) {
                    onResult_(result);
                }
            } else if (!result.error.isEmpty()) {
                if (onFailure_) {
                    onFailure_(job.pageIndex, result.error);
                }
            } else if (onResult_) {
                onResult_(result);
            }
        }
    }

private:
    std::unique_ptr<OcrEngine> engine_;
    ResultSink onResult_;
    FailureSink onFailure_;
    IdleSink onIdle_;

    mutable QMutex mutex_;
    QWaitCondition wake_;
    QList<OcrJob> queue_;
    QSet<int> queuedPages_;
    QList<int> dropped_;
    bool busy_ = false;
    bool cancelled_ = false;
    bool stopping_ = false;
};

OcrService::OcrService(QObject* parent) : QObject(parent) {
    // 跨线程信号要带上结果类型：没有注册的话 queued 连接会静默丢弃。
    qRegisterMetaType<OcrPageResult>("aipdf::ocr::OcrPageResult");
}

OcrService::~OcrService() {
    if (worker_ != nullptr) {
        worker_->stop();
        worker_->wait(5000);
        delete worker_;
        worker_ = nullptr;
    }
}

void OcrService::setEngine(std::unique_ptr<OcrEngine> engine) {
    if (worker_ != nullptr) {
        // 引擎在服务生命周期内固定：换引擎等于换实现，重启服务更省事。
        return;
    }
    worker_ = new Worker(
        std::move(engine),
        [this](const OcrPageResult& result) {
            QMetaObject::invokeMethod(this, [this, result] { emit pageReady(result); },
                                      Qt::QueuedConnection);
        },
        [this](int page, const QString& error) {
            QMetaObject::invokeMethod(this, [this, page, error] { emit pageFailed(page, error); },
                                      Qt::QueuedConnection);
        },
        [this] {
            QMetaObject::invokeMethod(this, [this] { emit idle(); }, Qt::QueuedConnection);
        });
    thread_ = worker_;
    worker_->start();
}

bool OcrService::hasEngine() const {
    return worker_ != nullptr;
}

void OcrService::enqueue(const OcrJob& job) {
    if (worker_ != nullptr) {
        worker_->enqueue(job);
    }
}

void OcrService::cancelAll() {
    if (worker_ != nullptr) {
        worker_->cancelAll();
    }
}

int OcrService::pendingCount() const {
    return worker_ != nullptr ? worker_->pendingCount() : 0;
}

}  // namespace aipdf::ocr
