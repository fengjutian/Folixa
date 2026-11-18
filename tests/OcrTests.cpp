// v0.13.0：扫描页判定与 OCR 坐标换算的单元测试。
//
// 这两件事都不依赖识别引擎，所以能先于引擎落地并被守住：
// 判定错了，扫描页永远不会进索引；坐标错了，引用会高亮到错误的位置。
#include "ocr/core/OcrGeometry.h"
#include "ocr/core/OcrTextUtils.h"
#include "ocr/pipeline/ScannedPageDetector.h"

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
    void unrotatesPageRects_data();
    void unrotatesPageRects();
    void normalizesFullWidthPunctuation();
    void joinsOcrWordsWithoutInventingSpaces();
    void compactsNumericSpacing();
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

void OcrTests::unrotatesPageRects_data() {
    QTest::addColumn<int>("degrees");
    QTest::addColumn<QRectF>("expected");

    // 页面 1000×500，框在旋转后的坐标系里是 (100,200,300,100)。
    // 90°：x = W - y'，y = x'；180°：两个轴都翻；270°：x = y'，y = H - x'。
    QTest::newRow("90") << 90 << QRectF(700, 100, 100, 300);
    QTest::newRow("180") << 180 << QRectF(600, 200, 300, 100);
    QTest::newRow("270") << 270 << QRectF(200, 100, 100, 300);
    QTest::newRow("0") << 0 << QRectF(100, 200, 300, 100);
}

void OcrTests::unrotatesPageRects() {
    QFETCH(int, degrees);
    QFETCH(QRectF, expected);

    const QRectF mapped = unrotatePageRect(QRectF(100, 200, 300, 100), QSizeF(1000.0, 500.0), degrees);
    QVERIFY2(qAbs(mapped.x() - expected.x()) < 0.01, qPrintable(QString::number(mapped.x())));
    QVERIFY2(qAbs(mapped.y() - expected.y()) < 0.01, qPrintable(QString::number(mapped.y())));
    QVERIFY2(qAbs(mapped.width() - expected.width()) < 0.01,
             qPrintable(QString::number(mapped.width())));
    QVERIFY2(qAbs(mapped.height() - expected.height()) < 0.01,
             qPrintable(QString::number(mapped.height())));
}

void OcrTests::normalizesFullWidthPunctuation() {
    // 全角 ASCII 区整段归一化成半角；中文自己的标点（。、）不动。
    QCOMPARE(normalizePunctuation(QStringLiteral("No．04123456")), QStringLiteral("No.04123456"));
    QCOMPARE(normalizePunctuation(QStringLiteral("ＡＢＣ１２３")), QStringLiteral("ABC123"));
    QCOMPARE(normalizePunctuation(QStringLiteral("中文，句号。")), QStringLiteral("中文,句号。"));
    QCOMPARE(normalizePunctuation(QStringLiteral("全角\u3000空格")), QStringLiteral("全角 空格"));
}

void OcrTests::joinsOcrWordsWithoutInventingSpaces() {
    // 中文按字切开时不能加空格。
    QCOMPARE(joinWords({QStringLiteral("中"), QStringLiteral("华"), QStringLiteral("人"),
                        QStringLiteral("民"), QStringLiteral("共"), QStringLiteral("和"),
                        QStringLiteral("国")}),
             QStringLiteral("中华人民共和国"));
    // 英文单词之间的空格要保留。
    QCOMPARE(joinWords({QStringLiteral("hello"), QStringLiteral("world")}),
             QStringLiteral("hello world"));
    // 引擎把一串数字切成多个词：拼回去要能按原文搜到。
    QCOMPARE(joinWords({QStringLiteral("8"), QStringLiteral("，"), QStringLiteral("640"),
                        QStringLiteral("．"), QStringLiteral("00")}),
             QStringLiteral("8,640.00"));
    QCOMPARE(joinWords({QStringLiteral("No．04123456")}), QStringLiteral("No.04123456"));
}

void OcrTests::compactsNumericSpacing() {
    QCOMPARE(compactNumericSpacing(QStringLiteral("1 234 ． 56")), QStringLiteral("1234.56"));
    QCOMPARE(compactNumericSpacing(QStringLiteral("8 , 640 . 00")), QStringLiteral("8,640.00"));
    QCOMPARE(compactNumericSpacing(QStringLiteral("1 234 567")), QStringLiteral("1234567"));
    // 不该合并的：夹汉字、或右侧不是三位。
    QCOMPARE(compactNumericSpacing(QStringLiteral("2024 年 5 月")), QStringLiteral("2024 年 5 月"));
    QCOMPARE(compactNumericSpacing(QStringLiteral("2024 5 人")), QStringLiteral("2024 5 人"));
}

QTEST_MAIN(OcrTests)
#include "OcrTests.moc"
