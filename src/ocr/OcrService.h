#pragma once

#include "ocr/OcrEngine.h"

#include <QImage>
#include <QObject>
#include <QSizeF>
#include <memory>

class QThread;

namespace aipdf::ocr {

// 一页 OCR 任务：页面渲染图 + 页面尺寸（PDF 点），后者用于把像素框换回页面坐标。
struct OcrJob {
    int pageIndex = 0;
    QImage image;
    QSizeF pageSize;
};

// 本地 OCR 服务：一条后台线程 + 单并发队列。
//
// 为什么单并发：OCR 是 CPU 密集的，开两条线程只会让两页都变慢，还会和渲染线程抢核。
// 为什么要有队列：自动触发会在打开文档时一次排进几十页，界面不能被它拖住。
//
// 取消的语义：
//   - 还没开始的页直接丢弃，并以 cancelled 结果回报（调用方据此清掉占位）；
//   - 正在识别的页无法中断引擎本身，但结果会被标记 cancelled 并丢弃；
//   - 引擎抽象的 cancel() 留给真正支持中断的实现。
class OcrService : public QObject {
    Q_OBJECT

public:
    explicit OcrService(QObject* parent = nullptr);
    ~OcrService() override;

    // 必须在入队前设置；服务自己持有引擎，线程结束时销毁。
    void setEngine(std::unique_ptr<OcrEngine> engine);
    bool hasEngine() const;

    // 入队一页。同一页已在队列里时忽略重复请求。
    void enqueue(const OcrJob& job);

    // 取消所有未开始与进行中的任务。
    void cancelAll();

    // 队列 + 正在处理的页数。
    int pendingCount() const;

signals:
    void pageReady(const aipdf::ocr::OcrPageResult& result);
    void pageFailed(int pageIndex, const QString& error);
    void idle();

private:
    class Worker;
    Worker* worker_ = nullptr;
    QThread* thread_ = nullptr;
};

}  // namespace aipdf::ocr
