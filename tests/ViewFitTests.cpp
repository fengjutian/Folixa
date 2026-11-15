// 放映/适配计算的单元测试。
//
// 这块逻辑决定两件事：页面能不能铺满屏幕、以及会不会因为位图太大而渲染成黑屏。
// 后者在 4K 屏上真实存在：一张 612×792 的页面铺到 3840×2160 是 830 万像素，
// 而适配层对单页有硬上限。

#include "app/ViewFit.h"

#include <QTest>

using aipdf::app::fitPageToViewport;
using aipdf::app::FitResult;

namespace {

const QSizeF kLetter(612.0, 792.0);

}  // namespace

class ViewFitTests : public QObject {
    Q_OBJECT

private slots:
    void emptyInputsYieldDefault();
    void fitsByTheTighterDimension();
    void neverUpscalesBeyondEightTimes();
    void respectsPixelBudget();
    void respectsAdapterDimensionLimit();
    void clampedFlagMatchesOutcome();
};

void ViewFitTests::emptyInputsYieldDefault() {
    QCOMPARE(fitPageToViewport(QSizeF(0, 0), QSize(800, 600)).zoom, 1.0);
    QCOMPARE(fitPageToViewport(kLetter, QSize(0, 0)).zoom, 1.0);
}

void ViewFitTests::fitsByTheTighterDimension() {
    // 800×600 的窗口放 612×792 的页面：高度是瓶颈。
    const FitResult fit = fitPageToViewport(kLetter, QSize(800, 600));
    QCOMPARE(fit.zoom, 600.0 / 792.0);
    QVERIFY(fit.pixelSize.height() <= 600);
    QVERIFY(fit.pixelSize.width() <= 800);
}

void ViewFitTests::neverUpscalesBeyondEightTimes() {
    // 极大的屏幕配极小的页面：倍率封顶 8（约 576 DPI），否则位图会爆掉。
    const FitResult fit = fitPageToViewport(QSizeF(10, 10), QSize(8000, 8000));
    QCOMPARE(fit.zoom, 8.0);
    QCOMPARE(fit.pixelSize, QSize(80, 80));
}

void ViewFitTests::respectsPixelBudget() {
    // 预算 100 万像素：铺满 4K 会超，必须缩回去并标记 clamped。
    const FitResult fit = fitPageToViewport(kLetter, QSize(3840, 2160), 1000LL * 1000LL);
    QVERIFY(fit.clampedByPixels);
    const qint64 pixels = static_cast<qint64>(fit.pixelSize.width()) * fit.pixelSize.height();
    QVERIFY2(pixels <= 1000LL * 1000LL, qPrintable(QString::number(pixels)));
    QVERIFY(fit.zoom <= 3840.0 / 612.0);
}

void ViewFitTests::respectsAdapterDimensionLimit() {
    // 一张 2000×3000 点的海报在 20000 像素见方的窗口里：按比例会渲染成两万像素，
    // 单边必须被压回 8192。
    const FitResult fit = fitPageToViewport(QSizeF(2000.0, 3000.0), QSize(20000, 20000),
                                            1024LL * 1024 * 1024);
    QVERIFY(fit.pixelSize.width() <= 8192);
    QVERIFY(fit.pixelSize.height() <= 8192);
    QVERIFY(fit.clampedByPixels);
}

void ViewFitTests::clampedFlagMatchesOutcome() {
    // 小窗口放小页面：没有任何截断，标记必须为假——否则调用方会误以为要留黑边。
    const FitResult fit = fitPageToViewport(QSizeF(200, 200), QSize(400, 400));
    QVERIFY(!fit.clampedByPixels);
    QCOMPARE(fit.zoom, 2.0);
}

QTEST_MAIN(ViewFitTests)
#include "ViewFitTests.moc"
