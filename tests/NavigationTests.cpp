#include "app/LinkHitTest.h"

#include "app/PageTransform.h"
#include "pdf/Navigation.h"

#include <QTest>
#include <QVector>

using aipdf::app::PageTransform;
using aipdf::app::hitTestLink;
using aipdf::pdf::OutlineEntry;
using aipdf::pdf::OutlineItem;
using aipdf::pdf::PageLink;

namespace {

PageLink makeLink(const QRectF& rect, int pageIndex = -1, const QString& uri = QString()) {
    PageLink link;
    link.rect = rect;
    link.pageIndex = pageIndex;
    link.uri = uri;
    return link;
}

PageTransform makeTransform(int rotation = 0) {
    PageTransform transform;
    transform.setPage(0.0, 0.0, 612.0, 400.0);
    transform.setRotationDegrees(rotation);
    return transform;
}

OutlineItem makeItem(const QString& title, int pageIndex) {
    OutlineItem item;
    item.title = title;
    item.pageIndex = pageIndex;
    return item;
}

}  // namespace

class NavigationTests : public QObject {
    Q_OBJECT

private slots:
    void flattenOfEmptyOutlineIsEmpty();
    void flattenKeepsDocumentOrder();
    void flattenRecordsDepth();
    void flattenKeepsBrokenTargets();

    void hitTestOfEmptyListIsMiss();
    void hitTestFindsLinkInsideRect();
    void hitTestMissesOutsideRect();
    void hitTestPrefersLaterLink();
    void hitTestSkipsUnusableLink();
    void hitTestSkipsDegenerateRect();
    void hitTestFollowsPageRotation();
};

// --------------------------------------------------------------- 大纲摊平 ----

void NavigationTests::flattenOfEmptyOutlineIsEmpty() {
    const std::vector<OutlineItem> outline;
    QVERIFY(aipdf::pdf::flattenOutline(outline).empty());
}

void NavigationTests::flattenKeepsDocumentOrder() {
    std::vector<OutlineItem> outline;
    outline.push_back(makeItem("Chapter One", 0));
    outline.push_back(makeItem("Chapter Two", 2));

    const std::vector<OutlineEntry> entries = aipdf::pdf::flattenOutline(outline);
    QCOMPARE(entries.size(), size_t(2));
    QCOMPARE(entries.at(0).title, QStringLiteral("Chapter One"));
    QCOMPARE(entries.at(1).title, QStringLiteral("Chapter Two"));
}

void NavigationTests::flattenRecordsDepth() {
    std::vector<OutlineItem> outline;
    outline.push_back(makeItem("Chapter One", 0));
    outline.at(0).children.push_back(makeItem("Section 1.1", 1));
    outline.at(0).children.at(0).children.push_back(makeItem("Topic 1.1.1", 2));
    outline.push_back(makeItem("Chapter Two", 3));

    const std::vector<OutlineEntry> entries = aipdf::pdf::flattenOutline(outline);
    QCOMPARE(entries.size(), size_t(4));
    QCOMPARE(entries.at(0).title, QStringLiteral("Chapter One"));
    QCOMPARE(entries.at(0).depth, 0);
    QCOMPARE(entries.at(1).title, QStringLiteral("Section 1.1"));
    QCOMPARE(entries.at(1).depth, 1);
    QCOMPARE(entries.at(2).title, QStringLiteral("Topic 1.1.1"));
    QCOMPARE(entries.at(2).depth, 2);
    // 顶层条目之间不会被上一棵子树的尾巴带偏。
    QCOMPARE(entries.at(3).title, QStringLiteral("Chapter Two"));
    QCOMPARE(entries.at(3).depth, 0);
}

void NavigationTests::flattenKeepsBrokenTargets() {
    std::vector<OutlineItem> outline;
    outline.push_back(makeItem("Broken", -1));

    const std::vector<OutlineEntry> entries = aipdf::pdf::flattenOutline(outline);
    QCOMPARE(entries.size(), size_t(1));
    // 目标坏掉的条目仍然显示，只是点不动。
    QCOMPARE(entries.at(0).pageIndex, -1);
}

// --------------------------------------------------------------- 链接命中 ----

void NavigationTests::hitTestOfEmptyListIsMiss() {
    const QVector<PageLink> links;
    QCOMPARE(hitTestLink(links, makeTransform(), QPointF(100.0, 100.0)), -1);
}

void NavigationTests::hitTestFindsLinkInsideRect() {
    QVector<PageLink> links;
    links.append(makeLink(QRectF(60.0, 200.0, 160.0, 35.0), 1));

    // user space 的矩形换算到显示空间是 (60, 165) 160x35。
    QCOMPARE(hitTestLink(links, makeTransform(), QPointF(140.0, 182.0)), 0);
}

void NavigationTests::hitTestMissesOutsideRect() {
    QVector<PageLink> links;
    links.append(makeLink(QRectF(60.0, 200.0, 160.0, 35.0), 1));

    QCOMPARE(hitTestLink(links, makeTransform(), QPointF(140.0, 100.0)), -1);
    QCOMPARE(hitTestLink(links, makeTransform(), QPointF(10.0, 182.0)), -1);
}

void NavigationTests::hitTestPrefersLaterLink() {
    QVector<PageLink> links;
    links.append(makeLink(QRectF(60.0, 200.0, 160.0, 35.0), 1));
    links.append(makeLink(QRectF(60.0, 200.0, 160.0, 35.0), -1, QStringLiteral("https://example.com/overlay")));

    // 重叠时取枚举顺序靠后的那条（Z 序在上面）。
    const int hit = hitTestLink(links, makeTransform(), QPointF(140.0, 182.0));
    QCOMPARE(hit, 1);
    QCOMPARE(links.at(hit).uri, QStringLiteral("https://example.com/overlay"));
}

void NavigationTests::hitTestSkipsUnusableLink() {
    QVector<PageLink> links;
    // 既没有目标页也没有 URI：目标损坏，不能点。
    links.append(makeLink(QRectF(60.0, 200.0, 160.0, 35.0)));

    QCOMPARE(hitTestLink(links, makeTransform(), QPointF(140.0, 182.0)), -1);
}

void NavigationTests::hitTestSkipsDegenerateRect() {
    QVector<PageLink> links;
    links.append(makeLink(QRectF(60.0, 200.0, 0.0, 35.0), 1));

    // 零宽的矩形不构成可点击区域。
    QCOMPARE(hitTestLink(links, makeTransform(), QPointF(60.0, 182.0)), -1);
}

void NavigationTests::hitTestFollowsPageRotation() {
    QVector<PageLink> links;
    links.append(makeLink(QRectF(60.0, 200.0, 160.0, 35.0), 1));

    // 顺时针 90°：user space 的 (200,60) 映射到显示空间的 (60,200)，
    // 矩形因此变成 (200,60) 35x160。
    const PageTransform rotated = makeTransform(90);
    QCOMPARE(hitTestLink(links, rotated, QPointF(215.0, 100.0)), 0);
    // 未旋转时的那个位置在旋转后不再是热区。
    QCOMPARE(hitTestLink(links, rotated, QPointF(140.0, 182.0)), -1);
}

QTEST_MAIN(NavigationTests)
#include "NavigationTests.moc"
