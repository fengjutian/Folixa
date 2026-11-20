#include "pdf/ops/AnchorPlanner.h"

#include <QTest>

using aipdf::pdf::AnchorBody;
using aipdf::pdf::AnchorPlan;
using aipdf::pdf::AnchorShift;
using aipdf::pdf::planAnchorMoves;

namespace {

QRectF page() { return QRectF(0, 0, 600, 800); }

}  // namespace

class AnchorPlannerTests : public QObject {
    Q_OBJECT

private slots:
    void movesBodyWithParagraphAbove();
    void ignoresBodyAboveTheShiftedParagraph();
    void reportsOutOfPage();
    void reportsOverlapWithNewText();
    void reportsOverlapBetweenBodies();
    void noShiftNoDecision();
};

void AnchorPlannerTests::movesBodyWithParagraphAbove() {
    const QVector<AnchorBody> bodies{{QRectF(100, 500, 200, 100), QStringLiteral("图片")}};
    const QVector<AnchorShift> shifts{{QRectF(50, 650, 500, 100), 30.0}};
    const AnchorPlan plan = planAnchorMoves(bodies, shifts, page(), {});
    QCOMPARE(plan.decisions.size(), 1);
    QCOMPARE(plan.movedCount, 1);
    QVERIFY(plan.ok());
    QCOMPARE(plan.decisions.first().to, QRectF(100, 470, 200, 100));
}

void AnchorPlannerTests::ignoresBodyAboveTheShiftedParagraph() {
    // 对象在段落上方（底边更高），不该跟着往下走。
    const QVector<AnchorBody> bodies{{QRectF(100, 760, 200, 30), QStringLiteral("图片")}};
    const QVector<AnchorShift> shifts{{QRectF(50, 100, 500, 100), 40.0}};
    const AnchorPlan plan = planAnchorMoves(bodies, shifts, page(), {});
    QVERIFY(plan.decisions.isEmpty());
    QCOMPARE(plan.movedCount, 0);
}

void AnchorPlannerTests::reportsOutOfPage() {
    const QVector<AnchorBody> bodies{{QRectF(100, 20, 200, 10), QStringLiteral("表格")}};
    const QVector<AnchorShift> shifts{{QRectF(50, 200, 500, 100), 80.0}};
    const AnchorPlan plan = planAnchorMoves(bodies, shifts, page(), {});
    QCOMPARE(plan.decisions.size(), 1);
    QVERIFY(!plan.ok());
    QVERIFY2(plan.conflicts.first().contains(QStringLiteral("移出页面")),
             qPrintable(plan.conflicts.first()));
}

void AnchorPlannerTests::reportsOverlapWithNewText() {
    const QVector<AnchorBody> bodies{{QRectF(100, 500, 200, 60), QStringLiteral("图片")}};
    const QVector<AnchorShift> shifts{{QRectF(50, 650, 500, 100), 30.0}};
    // 重排后的文字区正好落在对象的新位置上。
    const QVector<QRectF> avoid{QRectF(50, 460, 500, 60)};
    const AnchorPlan plan = planAnchorMoves(bodies, shifts, page(), avoid);
    QVERIFY(!plan.ok());
    QVERIFY2(plan.conflicts.first().contains(QStringLiteral("压到")),
             qPrintable(plan.conflicts.first()));
}

void AnchorPlannerTests::reportsOverlapBetweenBodies() {
    const QVector<AnchorBody> bodies{{QRectF(100, 500, 200, 60), QStringLiteral("图片")},
                                     {QRectF(110, 520, 200, 60), QStringLiteral("矢量")}};
    const QVector<AnchorShift> shifts{{QRectF(50, 700, 500, 80), 20.0}};
    const AnchorPlan plan = planAnchorMoves(bodies, shifts, page(), {});
    QCOMPARE(plan.decisions.size(), 2);
    QVERIFY(!plan.ok());
    QVERIFY2(plan.conflicts.first().contains(QStringLiteral("重叠")),
             qPrintable(plan.conflicts.first()));
}

void AnchorPlannerTests::noShiftNoDecision() {
    const QVector<AnchorBody> bodies{{QRectF(100, 500, 200, 60), QStringLiteral("图片")}};
    const QVector<AnchorShift> shifts{{QRectF(50, 700, 500, 80), 0.0}};
    const AnchorPlan plan = planAnchorMoves(bodies, shifts, page(), {});
    QVERIFY(plan.decisions.isEmpty());
    QVERIFY(plan.ok());
}

QTEST_MAIN(AnchorPlannerTests)
#include "AnchorPlannerTests.moc"
