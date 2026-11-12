#pragma once

#include <QImage>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include "pdf/PdfiumAdapter.h"

namespace aipdf::app {

// 渲染线程：在自己的线程里持有独立的 PDFium 句柄，按需渲染页面。
//
// 为什么不用 Qt 的排队信号：每页一个信号会让事件队列在快速滚动时越积越长，
// 旧任务全都要先投递再丢弃。这里用互斥量 + 条件变量自己控循环，
// 主线程只保留「当前要哪一段」这一个状态，新状态覆盖旧状态，
// 渲染线程醒来时直接按最新状态干活，因此待处理任务数恒为 1。
//
// 为什么要在自己的线程里打开文档：PDFium 的 FPDF_DOCUMENT / FPDF_PAGE
// 句柄只能在创建它的线程里使用。
class RenderThread : public QThread {
    Q_OBJECT

public:
    explicit RenderThread(pdf::PdfDocument source, QObject* parent = nullptr);
    ~RenderThread() override;

    // 主线程调用：更新要渲染的页区间与缩放。
    // 旧区间的请求自动作废。
    void requestRange(int firstPage, int lastPage, double zoom);

    // 主线程调用：作废当前所有请求（换文档或换缩放时用）。
    void cancel();

    // 已经投递但尚未完成的渲染数量，用于「不会无限积压」的观测。
    int pendingCount() const;

signals:
    // 渲染完成。zoomKey 与 TileCache::zoomToKey 对应。
    void pageRendered(int pageIndex, qint64 zoomKey, const QImage& image);
    void pageFailed(int pageIndex, qint64 zoomKey);

protected:
    void run() override;

private:
    mutable QMutex mutex_;
    QWaitCondition condition_;

    pdf::PdfDocument source_;
    pdf::PdfDocument document_;

    int firstPage_ = 0;
    int lastPage_ = -1;
    double zoom_ = 1.0;
    quint64 generation_ = 0;
    bool dirty_ = false;
    bool stop_ = false;
    int pending_ = 0;
};

}  // namespace aipdf::app
