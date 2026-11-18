#include "ocr/pipeline/OcrService.h"

#include "i18n/Translate.h"
#include "ocr/core/OcrGeometry.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThread>
#include <QTransform>
#include <QWaitCondition>

#include <atomic>

namespace aipdf::ocr {

namespace {

// 页面是不是基本空白：用来决定「识别结果为空」时要不要再试旋转。
//
// 判断很粗（缩到 48×48 灰度，看暗像素占比），够用就行：空白页不该为一次失败
// 的识别多付三次；有字的页即使识别失败也一定有足够多的暗像素。
bool looksBlank(const QImage& image) {
    if (image.isNull()) {
        return true;
    }
    const QImage small = image.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_Grayscale8);
    if (small.isNull() || small.width() == 0 || small.height() == 0) {
        return true;
    }
    int dark = 0;
    for (int y = 0; y < small.height(); ++y) {
        const uchar* line = small.constScanLine(y);
        for (int x = 0; x < small.width(); ++x) {
            if (line[x] < 160) {
                ++dark;
            }
        }
    }
    const int total = small.width() * small.height();
    // 暗像素少于 1%：当空白页处理。
    return dark * 100 < total;
}

}  // namespace

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
        // 正在跑的那一页：光把队列清空没用，引擎还会把整页识别完再回来。
        // 引擎的 cancel() 必须能从别的线程调用（接口注释里就是这么约定的），
        // 实现里是原子标志 + 杀掉子进程，所以放在锁外调，避免在跑识别时抢锁。
        if (engine_ != nullptr) {
            engine_->cancel();
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
                // 队列空了、也没有在跑的页：取消这一轮结束，后面的请求正常处理。
                // 队列里还有页时不能清标记——取消对“取消之后紧接着入队”的那批
                // 同样生效（见 OcrServiceTests::cancelDropsQueuedPages）。
                if (cancelled_ && queue_.isEmpty() && !busy_) {
                    cancelled_ = false;
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
                // 扫描件横放（内容侧着）时，正向识别会一个字都拿不到——这不等于这页没字。
                // 依次试 90°/270°/180°：只有第一次结果为空才重试，空白页先筛掉，
                // 免得给每张空白页多付三次识别。
                if (result.lines.isEmpty() && result.error.isEmpty() && !result.cancelled &&
                    !looksBlank(job.image)) {
                    for (const int degrees : {90, 270, 180}) {
                        OcrJob rotated = job;
                        rotated.image = job.image.transformed(QTransform().rotate(degrees));
                        // 旋转 90/270 后宽高互换，页面尺寸也要跟着换，否则比例尺全错。
                        rotated.pageSize = degrees == 180
                                               ? job.pageSize
                                               : QSizeF(job.pageSize.height(), job.pageSize.width());
                        OcrPageResult attempt = engine_->recognize(rotated);
                        if (attempt.cancelled) {
                            result = attempt;
                            break;
                        }
                        if (attempt.lines.isEmpty()) {
                            continue;
                        }
                        for (OcrLine& line : attempt.lines) {
                            line.rect = unrotatePageRect(line.rect, job.pageSize, degrees);
                            for (OcrWord& word : line.words) {
                                word.rect = unrotatePageRect(word.rect, job.pageSize, degrees);
                            }
                        }
                        attempt.pageIndex = job.pageIndex;
                        result = attempt;
                        break;
                    }
                }
            } else {
                result.pageIndex = job.pageIndex;
                result.error =
                    i18n::tr("aipdf::ocr::OcrService", "没有可用的 OCR 引擎");
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

            // 取消是「这一轮」的语义，不是永久开关：这一页处理完、队列也清空了，
            // 就恢复可识别状态，否则调用一次 cancelAll() 之后这个服务终身失效。
            {
                QMutexLocker locker(&mutex_);
                if (cancelled_ && !stopping_ && !busy_ && queue_.isEmpty()) {
                    cancelled_ = false;
                }
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
        if (!worker_->wait(5000)) {
            // 引擎的 recognize() 是阻塞调用，取消不了。此时 delete 一个仍在
            // 运行的 QThread 是未定义行为（Qt 会报 "Destroyed while thread is
            // still running" 并可能崩溃）。进程马上就要退出，宁可泄漏线程对象。
            qWarning("OcrService: OCR 线程 5 秒内没停下，跳过销毁以免崩溃");
            worker_ = nullptr;
            return;
        }
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
