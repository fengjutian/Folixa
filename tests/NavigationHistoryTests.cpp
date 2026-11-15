// 前进 / 后退历史的单元测试。
//
// 历史这东西最容易出的两个毛病：
//   1) 不去的去重——按十次缩放键产生十条历史，后退要按十次；
//   2) 后退之后再做新动作，前进栈没截断，用户能"前进"到一个已经被改变的分支。
// 这两条都在这里钉死。

#include "app/NavigationHistory.h"

#include <QTest>

using aipdf::app::NavigationHistory;
using aipdf::app::ViewState;

namespace {

ViewState state(int page, double zoom = 1.0) {
    ViewState value;
    value.pageIndex = page;
    value.zoom = zoom;
    return value;
}

}  // namespace

class NavigationHistoryTests : public QObject {
    Q_OBJECT

private slots:
    void freshHistoryCannotMove();
    void duplicateStatesAreNotRecorded();
    void backAndForwardWalkTheStack();
    void newActionAfterBackTruncatesForward();
    void capacityDropsOldest();
    void zoomIsPartOfState();
};

void NavigationHistoryTests::freshHistoryCannotMove() {
    NavigationHistory history;
    QVERIFY(!history.canGoBack());
    QVERIFY(!history.canGoForward());

    history.reset(state(3));
    QVERIFY(!history.canGoBack());
    QCOMPARE(history.current().pageIndex, 3);
}

void NavigationHistoryTests::duplicateStatesAreNotRecorded() {
    NavigationHistory history;
    history.reset(state(1));
    history.record(state(1));
    history.record(state(1));
    QCOMPARE(history.size(), 1);
}

void NavigationHistoryTests::backAndForwardWalkTheStack() {
    NavigationHistory history;
    history.reset(state(1));
    history.record(state(2));
    history.record(state(5));
    QCOMPARE(history.size(), 3);

    QVERIFY(history.canGoBack());
    QCOMPARE(history.goBack().pageIndex, 2);
    QCOMPARE(history.goBack().pageIndex, 1);
    QVERIFY(!history.canGoBack());
    // 已经在最前一项，再后退保持不动。
    QCOMPARE(history.goBack().pageIndex, 1);

    QVERIFY(history.canGoForward());
    QCOMPARE(history.goForward().pageIndex, 2);
    QCOMPARE(history.goForward().pageIndex, 5);
    QVERIFY(!history.canGoForward());
}

void NavigationHistoryTests::newActionAfterBackTruncatesForward() {
    NavigationHistory history;
    history.reset(state(1));
    history.record(state(2));
    history.record(state(3));

    history.goBack();  // 回到 2
    history.record(state(9));

    QVERIFY(!history.canGoForward());
    QCOMPARE(history.current().pageIndex, 9);
    QCOMPARE(history.size(), 3);  // 1, 2, 9
}

void NavigationHistoryTests::capacityDropsOldest() {
    NavigationHistory history;
    history.reset(state(0));
    for (int page = 1; page < 200; ++page) {
        history.record(state(page));
    }
    QCOMPARE(history.size(), 64);
    QCOMPARE(history.current().pageIndex, 199);
    // 最老的几项被丢掉，但最后一项一定在。
    QCOMPARE(history.goBack().pageIndex, 198);
}

void NavigationHistoryTests::zoomIsPartOfState() {
    NavigationHistory history;
    history.reset(state(2, 1.0));
    history.record(state(2, 2.0));  // 同页不同缩放也要记
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.goBack().zoom, 1.0);
}

QTEST_MAIN(NavigationHistoryTests)
#include "NavigationHistoryTests.moc"
