#include "pdf/ContentStream.h"
#include "pdf/TextReflow.h"

#include <QtTest>

using namespace aipdf::pdf;

class ContentEditingTests final : public QObject {
    Q_OBJECT
private slots:
    void tokenizesNestedStringsAndArrays();
    void rejectsUnbalancedState();
    void rewritesOnlyTextOperands();
    void wrapsLinesAndReportsOverflow();
};

void ContentEditingTests::tokenizesNestedStringsAndArrays() {
    const QByteArray stream = "q BT /F1 12 Tf [(a\\(b\\)) 20 (c)] TJ ET Q";
    const auto parsed = ContentStream::tokenize(stream);
    QVERIFY2(parsed.isValid(), qPrintable(parsed.error));
    QCOMPARE(ContentStream::serialize(parsed.tokens), stream);
}

void ContentEditingTests::rejectsUnbalancedState() {
    const auto parsed = ContentStream::tokenize("BT (hello) Tj");
    QVERIFY(!parsed.isValid());
}

void ContentEditingTests::rewritesOnlyTextOperands() {
    QString error;
    const QByteArray original = "BT (hello) Tj [(hello) -20 (world)] TJ ET /hello Do";
    const QByteArray rewritten = ContentStream::rewriteText(
        original, [](const QString& value) { return value.toUpper(); }, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(rewritten, QByteArray("BT (HELLO) Tj [(HELLO) -20 (WORLD)] TJ ET /hello Do"));
}

void ContentEditingTests::wrapsLinesAndReportsOverflow() {
    TextStyle style;
    style.fontSize = 12.0;
    const auto paragraph = TextReflow::layoutParagraph(
        QStringLiteral("one two three four five six"), QRectF(0, 0, 60, 200), style);
    QVERIFY(paragraph.lines.size() > 1);
    QVERIFY(!paragraph.overflow);
    for (const auto& line : paragraph.lines) QVERIFY(line.width <= 60.01);
    const auto shortBox = TextReflow::layoutParagraph(
        QStringLiteral("one two three four five six"), QRectF(0, 0, 60, 10), style);
    QVERIFY(shortBox.overflow);
}

QTEST_MAIN(ContentEditingTests)
#include "ContentEditingTests.moc"
