// v0.13.0：扫描页判定与 OCR 坐标换算的单元测试。
//
// 这两件事都不依赖识别引擎，所以能先于引擎落地并被守住：
// 判定错了，扫描页永远不会进索引；坐标错了，引用会高亮到错误的位置。
#include "ocr/OcrGeometry.h"
#include "ocr/ScannedPageDetector.h"

#include <QRectF>
#include <QtTest>

using namespace aipdf::ocr;

class OcrTests : public QObject {
    Q_OBJECT

private slots:
    void emptyTextLayerNeedsOcr();
    void pageNumberOnlyNeedsOcr();
    void richTextDoesNotNeedOcr();
    void shortButDenseTextDoesNotNeedOcr();

    void mapsPixelRectToPage_data();
    void mapsPixelRectToPage();
    void flipsYAxis();
    void clampsToPageBounds();
    void invalidSizesReturnEmpty();
};

void OcrTests::emptyTextLayerNeedsOcr() {
    PageTextStats stats;
    stats.charCount = 0;
    stats.textAreaRatio = 0.0;
    QVERIFY(needsOcr(stats));
}

void OcrTests::pageNumberOnlyNeedsOcr() {
    // 扫描页上只有页码：有字，但不该当成可检索页。
    PageTextStats stats;
    stats.charCount = 6;
    stats.textAreaRatio = 0.002;
    QVERIFY(needsOcr(stats));
}

void OcrTests::richTextDoesNotNeedOcr() {
    PageTextStats stats;
    stats.charCount = 900;
    stats.textAreaRatio = 0.42;
    QVERIFY(!needsOcr(stats));
}

void OcrTests::shortButDenseTextDoesNotNeedOcr() {
    // 短正文（比如一页只有标题与两行字）：字符少但覆盖面积不小，不该 OCR。
    PageTextStats stats;
    stats.charCount = 30;
    stats.textAreaRatio = 0.18;
    QVERIFY(!needsOcr(stats));
}

void OcrTests::mapsPixelRectToPage_data() {
    QTest::addColumn<QRectF>("pixelRect");
    QTest::addColumn<QRectF>("expected");

    // 2000x1000 的图像对应 1000x500 点的页面：缩放比例 2。
    QTest::newRow("左上角") << QRectF(0, 0, 200, 100) << QRectF(0, 450, 100, 50);
    QTest::newRow("居中") << QRectF(800, 400, 400, 200) << QRectF(400, 200, 200, 100);
}

void OcrTests::mapsPixelRectToPage() {
    QFETCH(QRectF, pixelRect);
    QFETCH(QRectF, expected);
    const QRectF mapped =
        imageRectToPage(pixelRect, QSize(2000, 1000), QSizeF(1000.0, 500.0));
    QVERIFY2(qAbs(mapped.x() - expected.x()) < 0.01, qPrintable(QString::number(mapped.x())));
    QVERIFY2(qAbs(mapped.y() - expected.y()) < 0.01, qPrintable(QString::number(mapped.y())));
    QVERIFY2(qAbs(mapped.width() - expected.width()) < 0.01,
             qPrintable(QString::number(mapped.width())));
    QVERIFY2(qAbs(mapped.height() - expected.height()) < 0.01,
             qPrintable(QString::number(mapped.height())));
}

void OcrTests::flipsYAxis() {
    // 图像顶部的一行字，在页面坐标里应该靠上（y 大）。
    const QRectF topLine =
        imageRectToPage(QRectF(0, 0, 100, 50), QSize(1000, 1000), QSizeF(500.0, 500.0));
    const QRectF bottomLine =
        imageRectToPage(QRectF(0, 950, 100, 50), QSize(1000, 1000), QSizeF(500.0, 500.0));
    QVERIFY(topLine.y() > bottomLine.y());
    QCOMPARE(topLine.y() + topLine.height(), 500.0);
    QCOMPARE(bottomLine.y(), 0.0);
}

void OcrTests::clampsToPageBounds() {
    // 引擎在图像边缘给出的越界框要被裁回页面里。
    const QRectF mapped =
        imageRectToPage(QRectF(-20, -20, 200, 200), QSize(1000, 1000), QSizeF(500.0, 500.0));
    QVERIFY(mapped.left() >= 0.0);
    QVERIFY(mapped.top() >= 0.0);
    QVERIFY(mapped.right() <= 500.0);
    QVERIFY(mapped.bottom() <= 500.0);
}

void OcrTests::invalidSizesReturnEmpty() {
    QVERIFY(imageRectToPage(QRectF(0, 0, 10, 10), QSize(), QSizeF(100.0, 100.0)).isEmpty());
    QVERIFY(imageRectToPage(QRectF(0, 0, 10, 10), QSize(100, 100), QSizeF()).isEmpty());
}

QTEST_MAIN(OcrTests)
#include "OcrTests.moc"
