#pragma once

#include "ocr/pipeline/OcrService.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVector>

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

    // ---------------------------------------------------- 识别结果的留存 ----
    //
    // 识别结果以前只并进本地索引，结果本身不留：写隐藏文字层需要的是
    // **词级坐标**，索引里的块没有。所以这里把每一页的识别结果留下来。

    // 取某一页的识别结果。没有识别过这一页时返回 false。
    bool ocrResult(int pageIndex, ocr::OcrPageResult* out) const;
    // 已经留下结果的页码（升序）。
    QVector<int> ocrResultPages() const;
    // 留下的结果一共有几页。
    int ocrResultCount() const;
    // 清掉留下的结果（换文档时调用）。
    void clearResults() {
        results_.clear();
        resultsView_ = nullptr;
    }

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
    // 识别成功之后留下的结果（页序号 → 该页的识别结果）。
    //
    // 写不可见文字层要用词级坐标，索引里的块给不了，所以结果得留住。
    QHash<int, ocr::OcrPageResult> results_;
    // 这些结果属于哪一份文档：控制器是全局的，换文档时要把上一份的结果清掉。
    QPointer<PdfView> resultsView_;
};

}  // namespace aipdf::app
