#pragma once

#include "ocr/OcrService.h"

#include <QObject>
#include <QPointer>
#include <QSet>

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

    // 自动模式：切到没有文本层的页时自动识别一次（每页每次会话最多一次）。
    void setAutoEnabled(bool enabled) { autoEnabled_ = enabled; }
    bool isAutoEnabled() const { return autoEnabled_; }
    // 界面在页面切换时调用；不需要识别就静默返回。
    void handlePageShown(PdfView* view, int pageIndex);
    // 换文档时清掉「识别过哪些页」。
    void resetAttempts() { attemptedPages_.clear(); }

signals:
    void pageApplied(int pageIndex, int chunkCount);
    void failed(int pageIndex, const QString& reason);

private:
    void handlePageReady(const aipdf::ocr::OcrPageResult& result);

    ocr::OcrService service_;
    QPointer<PdfView> pendingView_;
    bool engineAvailable_ = false;
    bool autoEnabled_ = true;
    // 已经尝试过 OCR 的页：失败的不反复重试，成功的不重复识别。
    QSet<int> attemptedPages_;
};

}  // namespace aipdf::app
