#include "pdf/ContentStream.h"
#include "pdf/TextReflow.h"

#include <QtTest>

#include <algorithm>

using namespace aipdf::pdf;

class ContentEditingTests final : public QObject {
    Q_OBJECT
private slots:
    void tokenizesNestedStringsAndArrays();
    void rejectsUnbalancedState();
    void rewritesOnlyTextOperands();
    void keepsInlineImageBinaryOpaque();
    void wrapsLinesAndReportsOverflow();
    void alignsReflowedText();
    void flowsAcrossLinkedFrames();
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

void ContentEditingTests::keepsInlineImageBinaryOpaque() {
    const QByteArray stream = "q BI /W 1 /H 1 ID abc[(BT)xyz EI Q";
    const auto parsed = ContentStream::tokenize(stream);
    QVERIFY2(parsed.isValid(), qPrintable(parsed.error));
    QCOMPARE(ContentStream::serialize(parsed.tokens), stream);
    QCOMPARE(std::count_if(parsed.tokens.begin(), parsed.tokens.end(), [](const ContentToken& token) {
        return token.type == ContentTokenType::InlineImage;
    }), 1);
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

void ContentEditingTests::alignsReflowedText() {
    TextStyle style;
    style.fontSize = 12;
    style.alignment = TextAlignment::Right;
    const auto right = TextReflow::layoutLine(QStringLiteral("short"), QRectF(0, 0, 200, 40), style);
    QVERIFY(!right.lines.isEmpty());
    QVERIFY(right.lines.first().baseline.x() > 100);
    style.alignment = TextAlignment::Center;
    const auto center = TextReflow::layoutLine(QStringLiteral("short"), QRectF(0, 0, 200, 40), style);
    QVERIFY(center.lines.first().baseline.x() > 50);
    QVERIFY(center.lines.first().baseline.x() < right.lines.first().baseline.x());
}

void ContentEditingTests::flowsAcrossLinkedFrames() {
    TextStyle style;
    style.fontSize = 12;
    const QVector<QRectF> frames{QRectF(0, 0, 70, 30), QRectF(100, 0, 70, 60)};
    const auto result = TextReflow::layoutFrames(
        QStringLiteral("one two three four five six seven eight nine"), frames, style);
    QVERIFY(result.lines.size() >= 2);
    QVERIFY(std::any_of(result.lines.begin(), result.lines.end(), [](const ReflowLine& line) {
        return line.baseline.x() >= 100;
    }));
}

QTEST_MAIN(ContentEditingTests)
#include "ContentEditingTests.moc"
