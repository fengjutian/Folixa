#include "app/ScreenCapture.h"

#include <QImage>
#include <QTest>

using namespace aipdf::app;

namespace {

// 造一张纯色图：每个像素的颜色能看出它来自哪里。
QImage solidImage(int width, int height, const QColor& color) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

}  // namespace

class ScreenCaptureTests final : public QObject {
    Q_OBJECT

private slots:
    void dragRegionNormalizesDirection();
    void dragRegionIsEmptyWhenPointsMatch();
    void validRegionNeedsBothSides();
    void cropRegionClipsToImageBounds();
    void cropRegionReturnsEmptyForInvalidRegion();
    void cropRegionReturnsEmptyForNullImage();
    void cropRegionKeepsContent();
};

void ScreenCaptureTests::dragRegionNormalizesDirection() {
    const QRect forward = dragRegion(QPoint(10, 20), QPoint(110, 120));
    const QRect backward = dragRegion(QPoint(110, 120), QPoint(10, 20));
    // 往回拖与往前拖必须是同一个矩形。
    QCOMPARE(forward, backward);
    QCOMPARE(forward, QRect(10, 20, 100, 100));
}

void ScreenCaptureTests::dragRegionIsEmptyWhenPointsMatch() {
    const QRect region = dragRegion(QPoint(30, 30), QPoint(30, 30));
    QVERIFY(!isValidRegion(region));
    QCOMPARE(region.width(), 0);
    QCOMPARE(region.height(), 0);
}

void ScreenCaptureTests::validRegionNeedsBothSides() {
    QVERIFY(isValidRegion(QRect(0, 0, 10, 10)));
    QVERIFY(!isValidRegion(QRect(0, 0, 0, 10)));
    QVERIFY(!isValidRegion(QRect(0, 0, 10, 0)));
    QVERIFY(!isValidRegion(QRect()));
}

void ScreenCaptureTests::cropRegionClipsToImageBounds() {
    const QImage source = solidImage(100, 80, Qt::red);
    // 选区超出图片右边界：按边界裁。
    const QImage cropped = cropRegion(source, QRect(50, 40, 200, 200));
    QCOMPARE(cropped.width(), 50);
    QCOMPARE(cropped.height(), 40);
}

void ScreenCaptureTests::cropRegionReturnsEmptyForInvalidRegion() {
    const QImage source = solidImage(100, 80, Qt::red);
    QVERIFY(cropRegion(source, QRect()).isNull());
    QVERIFY(cropRegion(source, QRect(10, 10, 0, 0)).isNull());
    // 选区整块在图片外：没有内容可裁。
    QVERIFY(cropRegion(source, QRect(200, 200, 10, 10)).isNull());
}

void ScreenCaptureTests::cropRegionReturnsEmptyForNullImage() {
    QVERIFY(cropRegion(QImage(), QRect(0, 0, 10, 10)).isNull());
}

void ScreenCaptureTests::cropRegionKeepsContent() {
    QImage source(40, 40, QImage::Format_RGB32);
    source.fill(Qt::blue);
    source.setPixelColor(5, 5, Qt::green);
    const QImage cropped = cropRegion(source, QRect(0, 0, 10, 10));
    QCOMPARE(cropped.size(), QSize(10, 10));
    QCOMPARE(cropped.pixelColor(5, 5), QColor(Qt::green));
}

QTEST_GUILESS_MAIN(ScreenCaptureTests)
#include "ScreenCaptureTests.moc"
