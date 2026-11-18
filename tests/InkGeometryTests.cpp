// 橡皮命中判定的单元测试。
//
// 判定写成纯几何函数，是因为它决定"擦得掉 / 擦不掉"这种手感问题：
// 放到画布里就只能靠鼠标试，出错也说不清是几何算错了还是坐标换错了。

#include "app/editor/InkGeometry.h"

#include <QTest>

using aipdf::app::distanceToSegment;
using aipdf::app::strokeHitsCircle;

class InkGeometryTests : public QObject {
    Q_OBJECT

private slots:
    void distanceToSegmentHandlesPerpendicular();
    void distanceToSegmentClampsBeyondEnds();
    void distanceToSegmentHandlesDegenerateSegment();
    void strokeHitsCircleDetectsCrossing();
    void strokeHitsCircleRejectsFarStroke();
    void emptyStrokeNeverHits();
};

void InkGeometryTests::distanceToSegmentHandlesPerpendicular() {
    // 点在 (5,5)，线段是 x 轴上的 (0,0)-(10,0)：垂距 5。
    QCOMPARE(distanceToSegment(QPointF(5, 5), QPointF(0, 0), QPointF(10, 0)), 5.0);
}

void InkGeometryTests::distanceToSegmentClampsBeyondEnds() {
    // 超出端点的部分按端点算距离，而不是按直线无限延长算。
    QCOMPARE(distanceToSegment(QPointF(15, 0), QPointF(0, 0), QPointF(10, 0)), 5.0);
    QCOMPARE(distanceToSegment(QPointF(-3, 4), QPointF(0, 0), QPointF(10, 0)), 5.0);
}

void InkGeometryTests::distanceToSegmentHandlesDegenerateSegment() {
    // 单击留下的"点笔"：两端重合，退化成点距。
    QCOMPARE(distanceToSegment(QPointF(3, 4), QPointF(0, 0), QPointF(0, 0)), 5.0);
}

void InkGeometryTests::strokeHitsCircleDetectsCrossing() {
    const QVector<QPointF> stroke{ QPointF(0, 0), QPointF(10, 0), QPointF(20, 0) };
    QVERIFY(strokeHitsCircle(stroke, QPointF(5, 3), 4.0));
    QVERIFY(strokeHitsCircle(stroke, QPointF(15, -3), 4.0));
}

void InkGeometryTests::strokeHitsCircleRejectsFarStroke() {
    const QVector<QPointF> stroke{ QPointF(0, 0), QPointF(20, 0) };
    QVERIFY(!strokeHitsCircle(stroke, QPointF(10, 10), 4.0));
    // 半径为零时永远不命中：避免"手一抖就删掉一笔"。
    QVERIFY(!strokeHitsCircle(stroke, QPointF(10, 0), 0.0));
}

void InkGeometryTests::emptyStrokeNeverHits() {
    QVERIFY(!strokeHitsCircle({}, QPointF(0, 0), 10.0));
}

QTEST_MAIN(InkGeometryTests)
#include "InkGeometryTests.moc"
