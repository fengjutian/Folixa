// v0.13.0：Windows 自带 OCR 的输出解析测试。
//
// 系统 OCR 同样靠「在临时目录里跑 PowerShell」调用，跑不了的地方也能测解析：
// 一段固定 JSON 进，页面坐标的行走出来。
#include "ocr/WindowsOcrEngine.h"

#include <QRectF>
#include <QtTest>

using namespace aipdf::ocr;

namespace {

QString wordJson(int line, const QString& text, double x, double y, double w, double h) {
    return QStringLiteral("{\"line\":%1,\"text\":\"%2\",\"x\":%3,\"y\":%4,\"w\":%5,\"h\":%6}")
        .arg(line)
        .arg(text)
        .arg(x)
        .arg(y)
        .arg(w)
        .arg(h);
}

}  // namespace

class WindowsOcrEngineTests : public QObject {
    Q_OBJECT

private slots:
    void scriptCallsSystemOcr();
    void parsesEnglishLineWithSpaces();
    void joinsChineseWithoutSpaces();
    void groupsWordsByLine();
    void mapsBoxesToPageCoordinates();
    void ignoresJunkLines();
};

void WindowsOcrEngineTests::scriptCallsSystemOcr() {
    const QString script = WindowsOcrEngine::scriptText();
    QVERIFY(script.contains(QStringLiteral("Windows.Media.Ocr.OcrEngine")));
    QVERIFY(script.contains(QStringLiteral("TryCreateFromUserProfileLanguages")));
    QVERIFY(script.contains(QStringLiteral("RecognizeAsync")));
}

void WindowsOcrEngineTests::parsesEnglishLineWithSpaces() {
    const QString json = wordJson(0, QStringLiteral("Hello"), 10, 10, 40, 20) + QStringLiteral("\n") +
                         wordJson(0, QStringLiteral("world"), 60, 10, 50, 20);
    const OcrPageResult result =
        WindowsOcrEngine::parseWordsJson(json, 0, QSize(1000, 1000), QSizeF(500.0, 500.0));
    QCOMPARE(result.lines.size(), 1);
    QCOMPARE(result.lines.first().text, QStringLiteral("Hello world"));
}

void WindowsOcrEngineTests::joinsChineseWithoutSpaces() {
    const QString json = wordJson(0, QStringLiteral("中"), 10, 10, 20, 20) + QStringLiteral("\n") +
                         wordJson(0, QStringLiteral("华"), 35, 10, 20, 20);
    const OcrPageResult result =
        WindowsOcrEngine::parseWordsJson(json, 0, QSize(1000, 1000), QSizeF(500.0, 500.0));
    QCOMPARE(result.lines.first().text, QStringLiteral("中华"));
}

void WindowsOcrEngineTests::groupsWordsByLine() {
    const QString json = wordJson(0, QStringLiteral("第一行"), 10, 10, 60, 20) + QStringLiteral("\n") +
                         wordJson(1, QStringLiteral("第二行"), 10, 60, 60, 20);
    const OcrPageResult result =
        WindowsOcrEngine::parseWordsJson(json, 0, QSize(1000, 1000), QSizeF(500.0, 500.0));
    QCOMPARE(result.lines.size(), 2);
    QCOMPARE(result.lines.at(0).text, QStringLiteral("第一行"));
    QCOMPARE(result.lines.at(1).text, QStringLiteral("第二行"));
}

void WindowsOcrEngineTests::mapsBoxesToPageCoordinates() {
    const QString json = wordJson(0, QStringLiteral("测试"), 100, 100, 100, 20);
    const OcrPageResult result =
        WindowsOcrEngine::parseWordsJson(json, 2, QSize(1000, 1000), QSizeF(500.0, 500.0));
    QCOMPARE(result.pageIndex, 2);
    const QRectF box = result.lines.first().rect;
    QVERIFY2(qAbs(box.x() - 50.0) < 0.01, qPrintable(QString::number(box.x())));
    // 图像 100..120 → 页面 440..450（Y 轴翻转）。
    QVERIFY2(qAbs(box.y() - 440.0) < 0.01, qPrintable(QString::number(box.y())));
    QVERIFY2(qAbs(box.height() - 10.0) < 0.01, qPrintable(QString::number(box.height())));
}

void WindowsOcrEngineTests::ignoresJunkLines() {
    const QString json = QStringLiteral("not json") + QStringLiteral("\n") +
                         QStringLiteral("{\"line\":0,\"text\":\"\",\"x\":0,\"y\":0,\"w\":1,\"h\":1}") +
                         QStringLiteral("\n") +
                         wordJson(0, QStringLiteral("真"), 10, 10, 20, 20);
    const OcrPageResult result =
        WindowsOcrEngine::parseWordsJson(json, 0, QSize(1000, 1000), QSizeF(500.0, 500.0));
    QCOMPARE(result.lines.size(), 1);
    QCOMPARE(result.lines.first().text, QStringLiteral("真"));
}

QTEST_MAIN(WindowsOcrEngineTests)
#include "WindowsOcrEngineTests.moc"
