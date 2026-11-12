#include <QtTest>

#include "app/PageLayout.h"
#include "app/TileCache.h"

using namespace aipdf::app;

class PdfViewportTests final : public QObject {
    Q_OBJECT

private slots:
    void layoutPlacesPagesInOrder();
    void layoutHonoursRotation();
    void layoutHitTestFindsPage();
    void layoutDominantPageFollowsVisibleArea();
    void layoutHandlesEmptyDocument();
    void cacheEvictsLeastRecentlyUsed();
    void cacheRespectsMemoryLimit();
    void cacheSkipsOversizedTile();
    void cacheZoomKeyIsStable();
};

namespace {
PageLayout::PageInfo makePage(double width, double height) {
    PageLayout::PageInfo info;
    info.widthPoints = width;
    info.heightPoints = height;
    return info;
}
}  // namespace

void PdfViewportTests::layoutPlacesPagesInOrder() {
    PageLayout layout;
    layout.setPages({makePage(200, 100), makePage(200, 300), makePage(200, 100)});
    layout.setScale(1.0);
    layout.setPageSpacing(10.0);

    QCOMPARE(layout.pageCount(), 3);
    QCOMPARE(layout.pageTop(0), 0.0);
    QCOMPARE(layout.pageTop(1), 110.0);
    QCOMPARE(layout.pageTop(2), 420.0);
    // 总高 = 100 + 10 + 300 + 10 + 100
    QCOMPARE(layout.totalHeight(), 520.0);
    QCOMPARE(layout.pageSize(1).height(), 300.0);
}

void PdfViewportTests::layoutHonoursRotation() {
    PageLayout layout;
    // 显示尺寸已经包含页面自带的 /Rotate：MediaBox 100×200 旋转 90 度后
    // 显示为 200 宽 × 100 高。
    layout.setPages({makePage(200, 100)});
    layout.setScale(1.0);

    QCOMPARE(layout.pageSize(0).width(), 200.0);
    QCOMPARE(layout.pageSize(0).height(), 100.0);

    // 用户再转 90 度，宽高互换。
    layout.setUserRotation(90);
    QCOMPARE(layout.pageSize(0).width(), 100.0);
    QCOMPARE(layout.pageSize(0).height(), 200.0);

    // 用户旋转 360 度等于没转。
    layout.setUserRotation(360);
    QCOMPARE(layout.pageSize(0).width(), 200.0);
    QCOMPARE(layout.pageSize(0).height(), 100.0);
}

void PdfViewportTests::layoutHitTestFindsPage() {
    PageLayout layout;
    layout.setPages({makePage(200, 100), makePage(200, 300), makePage(200, 100)});
    layout.setScale(1.0);
    layout.setPageSpacing(10.0);

    QCOMPARE(layout.pageAt(0.0), 0);
    QCOMPARE(layout.pageAt(99.0), 0);
    QCOMPARE(layout.pageAt(110.0), 1);
    QCOMPARE(layout.pageAt(400.0), 1);
    QCOMPARE(layout.pageAt(420.0), 2);
    QCOMPARE(layout.pageAt(10000.0), 2);
}

void PdfViewportTests::layoutDominantPageFollowsVisibleArea() {
    PageLayout layout;
    layout.setPages({makePage(200, 400), makePage(200, 400)});
    layout.setScale(1.0);
    layout.setPageSpacing(12.0);

    // 初始位置：第 1 页露出 400，第 2 页露出 181 → 报第 1 页。
    QCOMPARE(layout.dominantPage(0.0, 593.0), 0);
    // 滚到底（内容 812、视口 593，最大滚动量 231）：
    // 第 1 页只剩 169，第 2 页露出 400 → 报第 2 页。
    // （这正是「跳到第 2 页」的实际场景：最后一页没法滚到视口顶部。）
    QCOMPARE(layout.dominantPage(231.0, 593.0), 1);
    // 视口正好落在页间距里：一页都没露出，退回顶部那一页。
    QCOMPARE(layout.dominantPage(405.0, 5.0), 0);
    // 空文档没有主导页。
    PageLayout empty;
    QCOMPARE(empty.dominantPage(0.0, 500.0), -1);
}

void PdfViewportTests::layoutHandlesEmptyDocument() {
    PageLayout layout;
    QVERIFY(layout.isEmpty());
    QCOMPARE(layout.pageCount(), 0);
    QCOMPARE(layout.totalHeight(), 0.0);
    QCOMPARE(layout.pageAt(10.0), -1);
    QCOMPARE(layout.pageSize(0), QSizeF());
}

void PdfViewportTests::cacheEvictsLeastRecentlyUsed() {
    TileCache cache(1024 * 1024);
    const QImage small(100, 100, QImage::Format_ARGB32);

    cache.insert({0, 1000}, small);
    cache.insert({1, 1000}, small);
    QCOMPARE(cache.count(), 2);

    // 命中第 0 项，它变成最新，第 1 项变成最旧。
    QVERIFY(!cache.lookup({0, 1000}).isNull());

    cache.insert({2, 1000}, small);
    // 容量还够，三项都在。
    QCOMPARE(cache.count(), 3);

    cache.setMaxBytes(small.sizeInBytes() * 2);
    // 第 1 项最旧，被淘汰。
    QVERIFY(cache.contains({0, 1000}));
    QVERIFY(!cache.contains({1, 1000}));
    QVERIFY(cache.contains({2, 1000}));
}

void PdfViewportTests::cacheRespectsMemoryLimit() {
    TileCache cache(100 * 100 * 4);
    const QImage tile(100, 100, QImage::Format_ARGB32);

    cache.insert({0, 1000}, tile);
    QCOMPARE(cache.bytes(), 100 * 100 * 4);
    QVERIFY(cache.bytes() <= cache.maxBytes());

    // 再插一张就超限，最旧的被丢掉。
    cache.insert({1, 1000}, tile);
    QVERIFY(cache.bytes() <= cache.maxBytes());
    QCOMPARE(cache.count(), 1);
    QVERIFY(cache.contains({1, 1000}));
}

void PdfViewportTests::cacheSkipsOversizedTile() {
    TileCache cache(1000);
    const QImage big(100, 100, QImage::Format_ARGB32);

    cache.insert({0, 1000}, big);
    // 单张就超过上限：不缓存，但缓存自身仍然不超限。
    QCOMPARE(cache.count(), 0);
    QCOMPARE(cache.bytes(), 0);
    QVERIFY(cache.bytes() <= cache.maxBytes());
}

void PdfViewportTests::cacheZoomKeyIsStable() {
    QCOMPARE(TileCache::zoomToKey(1.0), TileCache::zoomToKey(1.0));
    QCOMPARE(TileCache::zoomToKey(1.0), 1000);
    QCOMPARE(TileCache::zoomToKey(1.25), 1250);
    // 极小的浮点抖动不应让缓存键变化。
    QVERIFY(TileCache::zoomToKey(1.0 + 1e-12) == TileCache::zoomToKey(1.0));
    QVERIFY(TileCache::zoomToKey(1.0) != TileCache::zoomToKey(1.5));
}

QTEST_GUILESS_MAIN(PdfViewportTests)
#include "PdfViewportTests.moc"
