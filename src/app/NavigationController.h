#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QVector>

#include "app/PageTransform.h"
#include "pdf/Navigation.h"
#include "pdf/PdfiumAdapter.h"

#include <memory>
#include <vector>

namespace aipdf::app {

// 一页的导航数据：链接矩形与页面几何。
//
// 链接矩形在 PDF user space（左下原点），与字符框同一坐标系；
// 页面盒子与旋转随链接一起取回，主线程不必再碰 PDFium。
struct PageNavigation {
    QVector<pdf::PageLink> links;
    pdf::MediaBox contentBox;
    int rotationDegrees = 0;
};

// 导航控制器：负责取文档大纲（书签）与页面链接注释。
//
// 两件事都在后台做：
// - 读取要调 PDFium，而 PDFium 的句柄只能在创建它的线程里用，
//   所以工作线程自己开一份独立句柄（isolatedCopy）。
// - 几百页文档的链接注释要多，一次扫完不能把界面卡住；
//   链接按页取回，随时可取消。
//
// 线程约定：worker 的结果都通过 queued 调用回到主线程再落地，
// 因此本类里的缓存**只在主线程访问**，不需要再加锁。
class NavigationController : public QObject {
    Q_OBJECT

public:
    explicit NavigationController(pdf::PdfDocument document, QObject* parent = nullptr);
    ~NavigationController() override;

    // 用户额外旋转（0/90/180/270）。只影响坐标换算，不重新取数据。
    void setUserRotation(int degrees);
    int userRotation() const;

    // 大纲是否已取回（取回过哪怕是空的也算）。
    bool outlineLoaded() const;
    // 文档大纲（树）。没取回时为空。
    const std::vector<pdf::OutlineItem>& outline() const;
    // 大纲摊平后的列表（书签页签用）：同一份大纲，保持文档顺序。
    std::vector<pdf::OutlineEntry> flatOutline() const;

    // 请求取某页的链接（异步）。已经取过就直接返回。
    void requestPageLinks(int pageIndex);
    // 该页的链接是否已取回。
    bool hasPageLinks(int pageIndex) const;
    // 该页的链接（PDF user space 矩形）。没有就返回空列表。
    const QVector<pdf::PageLink>& linksForPage(int pageIndex) const;

    // 显示空间坐标 → 命中的链接下标；没命中返回 -1。
    int hitLink(int pageIndex, const QPointF& displayPoint) const;

    // 该页的显示空间变换（页面 /Rotate + 用户旋转）。
    // 页面数据还没取回来时返回 false。
    bool transformFor(int pageIndex, PageTransform* transform) const;

signals:
    // 大纲已取回（可能为空）。
    void outlineReady();
    // 某页的链接已取回。
    void pageLinksReady(int pageIndex);

private:
    class Worker;

    void handleOutline(const std::vector<pdf::OutlineItem>& outline);
    void handlePageLinks(int pageIndex, const PageNavigation& navigation);

    std::unique_ptr<Worker> worker_;
    std::vector<pdf::OutlineItem> outline_;
    QHash<int, PageNavigation> pages_;
    int userRotation_ = 0;
    bool outlineLoaded_ = false;
};

}  // namespace aipdf::app
