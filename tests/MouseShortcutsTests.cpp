#include "app/editor/MouseShortcuts.h"

#include <QTest>

#include <limits>

using aipdf::app::mouse::anchoredScrollValue;
using aipdf::app::mouse::nextClickCount;
using aipdf::app::mouse::wheelGesture;
using aipdf::app::mouse::wheelZoomFactor;
using aipdf::app::mouse::WheelGesture;

class MouseShortcutsTests : public QObject {
    Q_OBJECT

private slots:
    void plainWheelScrollsVertically();
    void shiftWheelScrollsHorizontally();
    void tiltWheelScrollsHorizontally();
    void ctrlWheelZooms();
    void ctrlBeatsShift();
    void zoomFactorIsTenPercentPerNotch();
    void zoomFactorIsSymmetric();
    void zoomFactorClampsHugeDelta();
    void zoomFactorIgnoresZeroDelta();
    void clickCountCountsToFour();
    void clickCountResetsAfterInterval();
    void clickCountResetsAfterDistance();
    void clickCountRestartsAfterFour();
    void anchoredScrollRoundsAndClamps();
    void anchoredScrollHandlesNonFinite();
};

void MouseShortcutsTests::plainWheelScrollsVertically() {
    QCOMPARE(wheelGesture(Qt::NoModifier, QPoint(0, 120)), WheelGesture::VerticalScroll);
    QCOMPARE(wheelGesture(Qt::NoModifier, QPoint(120, 0)), WheelGesture::HorizontalScroll);
}

void MouseShortcutsTests::shiftWheelScrollsHorizontally() {
    QCOMPARE(wheelGesture(Qt::ShiftModifier, QPoint(0, 120)), WheelGesture::HorizontalScroll);
}

void MouseShortcutsTests::tiltWheelScrollsHorizontally() {
    QCOMPARE(wheelGesture(Qt::NoModifier, QPoint(-120, 0)), WheelGesture::HorizontalScroll);
}

void MouseShortcutsTests::ctrlWheelZooms() {
    QCOMPARE(wheelGesture(Qt::ControlModifier, QPoint(0, 120)), WheelGesture::Zoom);
    QCOMPARE(wheelGesture(Qt::MetaModifier, QPoint(0, 120)), WheelGesture::Zoom);
}

void MouseShortcutsTests::ctrlBeatsShift() {
    // 缩放是明确意图：Ctrl+Shift+滚轮不退回横向滚动。
    QCOMPARE(wheelGesture(Qt::ControlModifier | Qt::ShiftModifier, QPoint(0, 120)),
             WheelGesture::Zoom);
}

void MouseShortcutsTests::zoomFactorIsTenPercentPerNotch() {
    QVERIFY(qFuzzyCompare(wheelZoomFactor(120), 1.1));
    QVERIFY(qFuzzyCompare(wheelZoomFactor(240), 1.21));
    QVERIFY(qFuzzyCompare(wheelZoomFactor(-120), 1.0 / 1.1));
}

void MouseShortcutsTests::zoomFactorIsSymmetric() {
    // 放大一格再缩小一格要回到原位，否则连续缩放会漂移。
    QVERIFY(qFuzzyCompare(wheelZoomFactor(120) * wheelZoomFactor(-120), 1.0));
}

void MouseShortcutsTests::zoomFactorClampsHugeDelta() {
    // 触摸板可能给出 1200 的增量；单次事件最多按 4 格累乘（1.1^4），
    // 再大的增量与 480 等价，不会一下从 100% 跳到 800%。
    QVERIFY(qFuzzyCompare(wheelZoomFactor(12000), wheelZoomFactor(480)));
    QVERIFY(qFuzzyCompare(wheelZoomFactor(-12000), wheelZoomFactor(-480)));
    QVERIFY(wheelZoomFactor(12000) <= 2.0);
    QVERIFY(wheelZoomFactor(-12000) >= 0.5);
}

void MouseShortcutsTests::zoomFactorIgnoresZeroDelta() {
    QCOMPARE(wheelZoomFactor(0), 1.0);
}

void MouseShortcutsTests::clickCountCountsToFour() {
    QCOMPARE(nextClickCount(0, 0, 0, 500, 4), 1);
    QCOMPARE(nextClickCount(1, 100, 1, 500, 4), 2);
    QCOMPARE(nextClickCount(2, 100, 1, 500, 4), 3);
    QCOMPARE(nextClickCount(3, 100, 1, 500, 4), 4);
}

void MouseShortcutsTests::clickCountResetsAfterInterval() {
    QCOMPARE(nextClickCount(3, 501, 0, 500, 4), 1);
    QCOMPARE(nextClickCount(3, -1, 0, 500, 4), 1);
}

void MouseShortcutsTests::clickCountResetsAfterDistance() {
    QCOMPARE(nextClickCount(3, 100, 5, 500, 4), 1);
    QCOMPARE(nextClickCount(3, 100, -1, 500, 4), 1);
}

void MouseShortcutsTests::clickCountRestartsAfterFour() {
    // 第五下是新的单击，不能继续累加成 5。
    QCOMPARE(nextClickCount(4, 100, 0, 500, 4), 1);
}

void MouseShortcutsTests::anchoredScrollRoundsAndClamps() {
    QCOMPARE(anchoredScrollValue(320.4, 100, 1000), 220);
    QCOMPARE(anchoredScrollValue(80.0, 100, 1000), 0);
    QCOMPARE(anchoredScrollValue(2000.0, 100, 500), 500);
    QCOMPARE(anchoredScrollValue(200.0, 100, -3), 0);
}

void MouseShortcutsTests::anchoredScrollHandlesNonFinite() {
    QCOMPARE(anchoredScrollValue(std::numeric_limits<double>::quiet_NaN(), 100, 500), 0);
}

QTEST_MAIN(MouseShortcutsTests)
#include "MouseShortcutsTests.moc"
