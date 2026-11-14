// v0.13.0：Tesseract 命令行引擎的 TSV 解析测试。
//
// 解析逻辑单独公开，就是为了能在没装 tesseract 的机器上测：
// 一段固定 TSV 进，页面坐标的行走出来。
#include "ocr/TesseractCliEngine.h"

#include <QRectF>
#include <QtTest>

using namespace aipdf::ocr;

namespace {

QString headerRow() {
    return QStringLiteral("level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\t"
                          "left\ttop\twidth\theight\tconf\ttext");
}

QString wordRow(int block, int par, int line, int word, int left, int top, int width,
                int height, double confidence, const QString& text) {
    return QStringLiteral("5\t1\t%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\t%9\t%10")
        .arg(block)
        .arg(par)
        .arg(line)
        .arg(word)
        .arg(left)
        .arg(top)
        .arg(width)
        .arg(height)
        .arg(confidence)
        .arg(text);
}

}  // namespace

class TesseractCliEngineTests : public QObject {
    Q_OBJECT

private slots:
    void missingExecutableIsUnavailable();
    void parsesEnglishWordsWithSpaces();
    void joinsChineseWithoutSpaces();
    void mapsWordBoxesToPageCoordinates();
    void ignoresJunkRows();
    void emptyTsvGivesEmptyResult();
};

void TesseractCliEngineTests::missingExecutableIsUnavailable() {
    TesseractCliEngine engine(QStringLiteral("C:/definitely/not/tesseract.exe"));
    QVERIFY(!engine.isAvailable());
    const OcrPageResult result = engine.recognize(OcrJob{});
    QVERIFY(!result.error.isEmpty());
}

void TesseractCliEngineTests::parsesEnglishWordsWithSpaces() {
    const QString tsv = headerRow() + QStringLiteral("\n") +
                        wordRow(1, 1, 1, 1, 200, 200, 60, 20, 90.0, QStringLiteral("Hello")) +
                        QStringLiteral("\n") +
                        wordRow(1, 1, 1, 2, 260, 200, 50, 20, 90.0, QStringLiteral("world"));
    const OcrPageResult result =
        TesseractCliEngine::parseTsv(tsv, 0, QSize(1000, 1000), QSizeF(500.0, 500.0));
    QCOMPARE(result.lines.size(), 1);
    QCOMPARE(result.lines.first().text, QStringLiteral("Hello world"));
    QVERIFY(result.meanConfidence > 0.85 && result.meanConfidence < 0.95);
}

void TesseractCliEngineTests::joinsChineseWithoutSpaces() {
    const QString tsv = headerRow() + QStringLiteral("\n") +
                        wordRow(1, 1, 1, 1, 100, 100, 40, 20, 96.0, QStringLiteral("中")) +
                        QStringLiteral("\n") +
                        wordRow(1, 1, 1, 2, 140, 100, 40, 20, 96.0, QStringLiteral("华"));
    const OcrPageResult result =
        TesseractCliEngine::parseTsv(tsv, 0, QSize(1000, 1000), QSizeF(500.0, 500.0));
    QCOMPARE(result.lines.size(), 1);
    // 中文之间不能塞空格，否则检索与引用都对不上原文。
    QCOMPARE(result.lines.first().text, QStringLiteral("中华"));
}

void TesseractCliEngineTests::mapsWordBoxesToPageCoordinates() {
    const QString tsv = headerRow() + QStringLiteral("\n") +
                        wordRow(1, 1, 1, 1, 100, 100, 80, 20, 90.0, QStringLiteral("测试"));
    const OcrPageResult result =
        TesseractCliEngine::parseTsv(tsv, 3, QSize(1000, 1000), QSizeF(500.0, 500.0));
    QCOMPARE(result.pageIndex, 3);
    const QRectF box = result.lines.first().rect;
    // 缩放 0.5：x 50、宽 40；Y 翻转：图像 100..120 → 页面 440..450。
    QVERIFY2(qAbs(box.x() - 50.0) < 0.01, qPrintable(QString::number(box.x())));
    QVERIFY2(qAbs(box.y() - 440.0) < 0.01, qPrintable(QString::number(box.y())));
    QVERIFY2(qAbs(box.width() - 40.0) < 0.01, qPrintable(QString::number(box.width())));
    QVERIFY2(qAbs(box.height() - 10.0) < 0.01, qPrintable(QString::number(box.height())));
}

void TesseractCliEngineTests::ignoresJunkRows() {
    const QString tsv = headerRow() + QStringLiteral("\n") +
                        QStringLiteral("1\t1\t0\t0\t0\t0\t0\t0\t1000\t1000\t-1\t") + QStringLiteral("\n") +
                        QStringLiteral("5\t1\t1\t1\t1\t1\t10\t10\t20\t20\t-1\t") + QStringLiteral("\n") +
                        QStringLiteral("garbage") + QStringLiteral("\n") +
                        wordRow(1, 1, 1, 1, 10, 10, 20, 20, 80.0, QStringLiteral("真"));
    const OcrPageResult result =
        TesseractCliEngine::parseTsv(tsv, 0, QSize(1000, 1000), QSizeF(500.0, 500.0));
    QCOMPARE(result.lines.size(), 1);
    QCOMPARE(result.lines.first().text, QStringLiteral("真"));
}

void TesseractCliEngineTests::emptyTsvGivesEmptyResult() {
    const OcrPageResult result =
        TesseractCliEngine::parseTsv(QString(), 0, QSize(100, 100), QSizeF(100.0, 100.0));
    QVERIFY(result.lines.isEmpty());
    QCOMPARE(result.meanConfidence, 0.0);
}

QTEST_MAIN(TesseractCliEngineTests)
#include "TesseractCliEngineTests.moc"
