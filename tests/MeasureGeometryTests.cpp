// 测量几何的单元测试。
//
// 面积算错在界面上看不出来：数字总是显示得很自信。所以三种口径都钉死具体数值。

#include "app/editor/MeasureGeometry.h"

#include <QTest>

using aipdf::app::formatMeasure;
using aipdf::app::measureArea;
using aipdf::app::measureDistance;
using aipdf::app::measurePerimeter;
using aipdf::app::MeasureMode;

class MeasureGeometryTests : public QObject {
    Q_OBJECT

private slots:
    void distanceIsStraightLine();
    void perimeterClosesThePolygon();
    void areaUsesShoelace();
    void areaIsAbsoluteForClockwise();
    void degenerateInputsAreZero();
    void formattingRespectsModeAndScale();
};

void MeasureGeometryTests::distanceIsStraightLine() {
    QCOMPARE(measureDistance(QPointF(0, 0), QPointF(3, 4)), 5.0);
}

void MeasureGeometryTests::perimeterClosesThePolygon() {
    // 直角边 3、4，斜边 5：周长 12。
    const QVector<QPointF> triangle{ QPointF(0, 0), QPointF(3, 0), QPointF(3, 4) };
    QCOMPARE(measurePerimeter(triangle), 12.0);
}

void MeasureGeometryTests::areaUsesShoelace() {
    const QVector<QPointF> square{ QPointF(0, 0), QPointF(10, 0), QPointF(10, 10),
                                   QPointF(0, 10) };
    QCOMPARE(measureArea(square), 100.0);
}

void MeasureGeometryTests::areaIsAbsoluteForClockwise() {
    // 反向绕的多边形面积不能变成负数。
    const QVector<QPointF> square{ QPointF(0, 0), QPointF(0, 10), QPointF(10, 10),
                                   QPointF(10, 0) };
    QCOMPARE(measureArea(square), 100.0);
}

void MeasureGeometryTests::degenerateInputsAreZero() {
    QCOMPARE(measureArea({ QPointF(0, 0), QPointF(1, 1) }), 0.0);
    QCOMPARE(measurePerimeter({ QPointF(0, 0) }), 0.0);
    QVERIFY(formatMeasure(MeasureMode::Area, { QPointF(0, 0) }).isEmpty());
}

void MeasureGeometryTests::formattingRespectsModeAndScale() {
    const QVector<QPointF> triangle{ QPointF(0, 0), QPointF(3, 0), QPointF(3, 4) };
    QCOMPARE(formatMeasure(MeasureMode::Distance, triangle, 1.0), QStringLiteral("3.0 pt"));
    QCOMPARE(formatMeasure(MeasureMode::Perimeter, triangle, 1.0), QStringLiteral("12.0 pt"));
    // 面积 6，比例 2 时按平方放大到 24。
    QCOMPARE(formatMeasure(MeasureMode::Area, triangle, 2.0), QStringLiteral("24.0 pt²"));
}

QTEST_MAIN(MeasureGeometryTests)
#include "MeasureGeometryTests.moc"
