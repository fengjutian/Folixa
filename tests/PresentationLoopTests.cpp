// 循环放映的翻页规则。
//
// 这里只测一件事：「翻 delta 页之后停在哪一页」。
//
// 之所以能这么测，是因为规则被抽成了纯函数（app/PresentationLoop.h）：
// 界面层只是调它，装窗口、跑事件循环都跟这条规则无关，
// 也不该由界面层来保证。
//
// 负例盯的是「越界」：返回值必须始终落在 [0, pageCount) 里，
// 否则窗口会翻到一个不存在的页上（渲染空白，看起来像程序坏了）。

#include "app/PresentationLoop.h"

#include <QTest>

using aipdf::app::nextPresentationPage;
using aipdf::app::presentationLoopHint;
using aipdf::app::presentationLoopTitle;

class PresentationLoopTests : public QObject {
    Q_OBJECT

private slots:
    void withoutLoopStopsAtTheEnd();
    void withoutLoopMovesInsideRange();
    void withLoopWrapsToTheOtherEnd();
    void withLoopMovesInsideRange();
    void deltaZeroKeepsPage();
    void emptyDocumentHasNoPage();
    void singlePageDocumentStaysOnPageOne();
    void outOfRangeCurrentPageStaysInRange();
    void userVisibleTextsAreNotEmpty();
};

// 关掉循环：到头就停住（这是验收脚本 scripts/verify-presentation.ps1 依赖的老行为）。
void PresentationLoopTests::withoutLoopStopsAtTheEnd() {
    QCOMPARE(nextPresentationPage(2, 3, 1, false), 2);
    QCOMPARE(nextPresentationPage(0, 3, -1, false), 0);
    // 跨过头也一样不动。
    QCOMPARE(nextPresentationPage(2, 3, 5, false), 2);
    QCOMPARE(nextPresentationPage(0, 3, -5, false), 0);
}

// 关掉循环：中间页正常翻。
void PresentationLoopTests::withoutLoopMovesInsideRange() {
    QCOMPARE(nextPresentationPage(0, 3, 1, false), 1);
    QCOMPARE(nextPresentationPage(1, 3, 1, false), 2);
    QCOMPARE(nextPresentationPage(2, 3, -1, false), 1);
    QCOMPARE(nextPresentationPage(1, 3, -1, false), 0);
}

// 打开循环：翻过最后一页回到第一页，翻过第一页回到最后一页。
void PresentationLoopTests::withLoopWrapsToTheOtherEnd() {
    QCOMPARE(nextPresentationPage(2, 3, 1, true), 0);
    QCOMPARE(nextPresentationPage(0, 3, -1, true), 2);
    // 一次翻好几页也一样绕回来。
    QCOMPARE(nextPresentationPage(2, 3, 4, true), 0);
    QCOMPARE(nextPresentationPage(0, 3, -4, true), 2);
}

// 打开循环：中间页照常翻，不受影响。
void PresentationLoopTests::withLoopMovesInsideRange() {
    QCOMPARE(nextPresentationPage(0, 3, 1, true), 1);
    QCOMPARE(nextPresentationPage(1, 3, 1, true), 2);
    QCOMPARE(nextPresentationPage(2, 3, -1, true), 1);
    QCOMPARE(nextPresentationPage(1, 3, -1, true), 0);
}

// delta 为 0：不动。
void PresentationLoopTests::deltaZeroKeepsPage() {
    QCOMPARE(nextPresentationPage(0, 3, 0, false), 0);
    QCOMPARE(nextPresentationPage(1, 3, 0, false), 1);
    QCOMPARE(nextPresentationPage(1, 3, 0, true), 1);
    QCOMPARE(nextPresentationPage(2, 3, 0, true), 2);
}

// 负例：没有页（页数为 0 或负数）→ 不能返回负数，也不能返回一个越界的下标。
void PresentationLoopTests::emptyDocumentHasNoPage() {
    QCOMPARE(nextPresentationPage(0, 0, 1, false), 0);
    QCOMPARE(nextPresentationPage(0, 0, -1, false), 0);
    QCOMPARE(nextPresentationPage(0, 0, 1, true), 0);
    QCOMPARE(nextPresentationPage(7, 0, 1, true), 0);
    QCOMPARE(nextPresentationPage(0, -3, 1, true), 0);
}

// 只有一页：往后往前都停在这一页（循环与否都一样）。
void PresentationLoopTests::singlePageDocumentStaysOnPageOne() {
    QCOMPARE(nextPresentationPage(0, 1, 1, false), 0);
    QCOMPARE(nextPresentationPage(0, 1, -1, false), 0);
    QCOMPARE(nextPresentationPage(0, 1, 1, true), 0);
    QCOMPARE(nextPresentationPage(0, 1, -1, true), 0);
}

// 负例：当前页本身越界时，返回值也不能越界。
void PresentationLoopTests::outOfRangeCurrentPageStaysInRange() {
    QCOMPARE(nextPresentationPage(99, 3, 1, false), 2);
    QCOMPARE(nextPresentationPage(-4, 3, 1, false), 1);
    QCOMPARE(nextPresentationPage(99, 3, 0, true), 2);
    QCOMPARE(nextPresentationPage(-4, 3, 0, true), 0);
    QCOMPARE(nextPresentationPage(99, 3, -1, true), 1);
    // 当前页夹到第 0 页，再往前翻：循环打开时绕到最后一页。
    QCOMPARE(nextPresentationPage(-4, 3, -1, true), 2);
}

// 用户可见文案不能是空的：翻译没装上时也要有中文兜底。
void PresentationLoopTests::userVisibleTextsAreNotEmpty() {
    QVERIFY(!presentationLoopTitle().isEmpty());
    QVERIFY(!presentationLoopHint(true).isEmpty());
    QVERIFY(!presentationLoopHint(false).isEmpty());
    QVERIFY(presentationLoopHint(true) != presentationLoopHint(false));
}

QTEST_GUILESS_MAIN(PresentationLoopTests)

#include "PresentationLoopTests.moc"
