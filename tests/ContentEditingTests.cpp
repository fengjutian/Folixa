#include "pdf/content/ContentStream.h"
#include "pdf/content/TextReflow.h"
#include "pdf/content/TableStructure.h"

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
    void preservesTextAcrossLinkedFrames();
    void shapesBidirectionalAndLigatureText();
    void detectsTableRowsColumnsAndCells();
    void laysOutVerticalText();
#ifdef AIPDF_WITH_QPDF
    void safelyRewritesRealPdfContent();
#endif
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

void ContentEditingTests::preservesTextAcrossLinkedFrames() {
    TextStyle style;
    style.fontSize = 11;
    const QString source = QStringLiteral("first  line\nsecond paragraph 中文内容 and final words");
    const QVector<QRectF> frames{QRectF(0, 0, 75, 32), QRectF(100, 0, 90, 70)};
    const auto result = TextReflow::layoutFrames(source, frames, style);
    QCOMPARE(source.left(result.consumedLength) + result.remainingText, source);
    QVERIFY(result.consumedLength > 0);
    for (const ReflowLine& line : result.lines) {
        QVERIFY(line.sourceStart >= 0);
        QVERIFY(line.sourceStart + line.sourceLength <= source.size());
        QCOMPARE(line.text, source.mid(line.sourceStart, line.sourceLength));
    }
}

void ContentEditingTests::shapesBidirectionalAndLigatureText() {
    TextStyle style;
    style.fontSize = 12;
    const QString source = QStringLiteral("office العربية עברית 中文");
    const auto result = TextReflow::layoutParagraph(source, QRectF(0, 0, 105, 120), style);
    QVERIFY(!result.lines.isEmpty());
    QCOMPARE(source.left(result.consumedLength) + result.remainingText, source);
    QCOMPARE(result.consumedLength, source.size());
    QVERIFY(!result.overflow);
}

void ContentEditingTests::detectsTableRowsColumnsAndCells() {
    const QVector<TableTextObject> objects{
        {1, QRectF(10, 80, 40, 12), QStringLiteral("Name")},
        {2, QRectF(100, 80, 50, 12), QStringLiteral("Score")},
        {3, QRectF(10, 50, 40, 12), QStringLiteral("Alice")},
        {4, QRectF(100, 50, 50, 12), QStringLiteral("95")},
        {5, QRectF(10, 20, 40, 12), QStringLiteral("Bob")},
        {6, QRectF(100, 20, 50, 12), QStringLiteral("88")},
    };
    const TableStructure table = TableStructure::detect(objects);
    QVERIFY(table.isValid());
    QCOMPARE(table.rowCount, 3);
    QCOMPARE(table.columnCount, 2);
    QVERIFY(table.confidence >= 0.99);
    QVERIFY(table.cell(1, 0));
    QCOMPARE(table.cell(1, 0)->text, QStringLiteral("Alice"));
    QCOMPARE(table.cell(2, 1)->objectIds, QVector<int>{6});
}

void ContentEditingTests::laysOutVerticalText() {
    TextStyle style;
    style.fontSize = 12;
    style.vertical = true;
    const auto result = TextReflow::layoutParagraph(
        QStringLiteral("纵向文字布局测试"), QRectF(0, 0, 50, 55), style);
    QVERIFY(result.lines.size() >= 4);
    QVERIFY(std::any_of(result.lines.cbegin(), result.lines.cend(),
                        [firstX = result.lines.constFirst().baseline.x()](const ReflowLine& line) {
                            return line.baseline.x() < firstX;
                        }));
}

#ifdef AIPDF_WITH_QPDF
void ContentEditingTests::safelyRewritesRealPdfContent() {
    QFile file(QStringLiteral(AIPDF_PDF_FIXTURES_DIR) + QStringLiteral("/text.pdf"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    int replacements = 0;
    QString error;
    const QByteArray rewritten = ContentStream::rewritePdfText(
        file.readAll(), {0}, QStringLiteral("Folixa"), QStringLiteral("SAFE"),
        Qt::CaseSensitive, &replacements, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(replacements > 0);
    QVERIFY(!rewritten.isEmpty());
    int secondCount = 0;
    const QByteArray rewrittenAgain = ContentStream::rewritePdfText(
        rewritten, {0}, QStringLiteral("SAFE"), QStringLiteral("DONE"),
        Qt::CaseSensitive, &secondCount, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(secondCount > 0);
    QVERIFY(!rewrittenAgain.isEmpty());
}
#endif

QTEST_MAIN(ContentEditingTests)
#include "ContentEditingTests.moc"
