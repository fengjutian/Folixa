#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

#include "app/PageTransform.h"
#include "app/TextLayout.h"
#include "pdf/PdfiumAdapter.h"

#include <memory>

namespace aipdf::app {

// 一条搜索命中。
struct SearchHit {
    int pageIndex = 0;
    // 命中在 PDFium 字符顺序里的区间 [start, end)。
    int start = 0;
    int end = 0;
    // 命中所在行的上下文片段（用于结果列表显示）。
    QString snippet;
};

// 文本控制器：负责提取页面文本层、维护排版结果与选区、执行全文搜索。
//
// 三件事都必须在后台做：
// - 文本提取要调 PDFium，而 PDFium 的句柄只能在创建它的线程里用，
//   所以工作线程自己开一份独立句柄（isolatedCopy）。
// - 全文搜索可能扫几百页，放主线程会把界面卡死；
//   搜索按页推进，随时可取消。
//
// 排版（重新分行、重建阅读顺序）与坐标变换在主线程做：
// 它们依赖当前的用户旋转，而且量很小（一页几千个字符）。
//
// 线程约定：worker 的结果都通过 queued 调用回到主线程再落地，
// 因此本类里的缓存、选区与命中列表**只在主线程访问**，不需要再加锁。
class TextController : public QObject {
    Q_OBJECT

public:
    explicit TextController(pdf::PdfDocument document, QObject* parent = nullptr);
    ~TextController() override;

    // 用户额外旋转（0/90/180/270）。变化后所有页的排版会重算。
    void setUserRotation(int degrees);
    int userRotation() const;

    // 请求提取某页文本（异步）。已经提取过就直接返回。
    void requestTextPage(int pageIndex);

    // 该页是否已经有文本层。
    bool hasTextPage(int pageIndex) const;

    // 该页的排版结果（阅读顺序、行、命中测试）。没有就返回 nullptr。
    const TextLayout* layout(int pageIndex) const;

    // 显示空间坐标 → 该页的阅读顺序下标；没命中返回 -1。
    int hitAt(int pageIndex, const QPointF& displayPoint) const;

    // 选区（阅读顺序区间）。
    void setSelection(int pageIndex, int orderStart, int orderEnd);
    void clearSelection();
    int selectionPage() const;
    int selectionStart() const;
    int selectionEnd() const;
    // 选区文本（跨行用换行）。
    QString selectedText() const;
    // 选区高亮矩形（显示空间）。
    QVector<QRectF> selectionRects() const;

    // 开始搜索。新的搜索会取消上一次。
    void startSearch(const QString& query);
    // 取消当前搜索。
    void cancelSearch();
    bool isSearching() const;
    // 当前搜索的命中列表。
    QVector<SearchHit> searchHits() const;
    // 某页上所有命中的高亮矩形（显示空间）。
    QVector<QRectF> searchHighlightRects(int pageIndex) const;

signals:
    // 某页文本提取完成（排版结果已就绪）。
    void textPageReady(int pageIndex);
    // 选区变化。
    void selectionChanged();
    // 搜索状态变化。
    void searchStarted();
    void searchHitsFound(int count);
    void searchFinished(bool cancelled);

private:
    class Worker;

    void handlePageExtracted(int pageIndex, const pdf::TextPage& text);
    void handleHitsFound(int count);
    void handleSearchFinished(bool cancelled);

    // 用当前旋转把某页的字符框变换到显示空间并重建排版。
    void buildLayout(int pageIndex);
    // 页面的显示空间变换（页面 /Rotate + 用户旋转）。
    bool transformFor(int pageIndex, PageTransform* transform) const;

    pdf::PdfDocument source_;
    std::unique_ptr<Worker> worker_;

    QHash<int, pdf::TextPage> textPages_;
    // 用 shared_ptr 存排版结果：QHash 扩容会搬动元素，
    // 裸指针在插入新页时会失效。
    QHash<int, std::shared_ptr<TextLayout>> layouts_;
    // 已经排队等文本、拿到后需要重建排版的页。
    QSet<int> pendingLayoutPages_;
    QVector<SearchHit> hits_;
    int userRotation_ = 0;

    int selectionPage_ = -1;
    int selectionStart_ = 0;
    int selectionEnd_ = 0;
};

}  // namespace aipdf::app
