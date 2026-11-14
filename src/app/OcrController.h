#pragma once

#include "ocr/OcrService.h"

#include <QObject>
#include <QPointer>

namespace aipdf::app {

class PdfView;

// OCR 编排：判定 → 渲染 → 识别 → 入索引。
//
// 界面只调 requestPage()，不关心引擎是命令行的还是别的实现；
// 识别结果通过 IndexController::applyOcrPage 并入本地索引，
// 之后搜索与问答走的是同一条检索链路。
class OcrController : public QObject {
    Q_OBJECT

public:
    explicit OcrController(QObject* parent = nullptr);
    ~OcrController() override;

    bool isAvailable() const { return engineAvailable_; }
    QString unavailableReason() const;

    // 请求识别某一页。返回 false 表示没有发出，原因写进 reason。
    bool requestPage(PdfView* view, int pageIndex, QString* reason);

signals:
    void pageApplied(int pageIndex, int chunkCount);
    void failed(int pageIndex, const QString& reason);

private:
    void handlePageReady(const aipdf::ocr::OcrPageResult& result);

    ocr::OcrService service_;
    QPointer<PdfView> pendingView_;
    bool engineAvailable_ = false;
};

}  // namespace aipdf::app
