#include "app/layout/PageTextLines.h"

#include "pdf/kernel/PdfiumAdapter.h"

#include <QTest>

using aipdf::pdf::TextChar;
using aipdf::pdf::TextPage;

namespace {

QString fixturePath(const QString& name) {
    return QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/") + name;
}

// 造一个字符：PDF user space，y 是下边（y 向上），与 TextChar::box 同约定。
TextChar makeChar(const QString& text, double left, double bottom, double width, double height) {
    TextChar character;
    character.text = text;
    character.box = QRectF(left, bottom, width, height);
    return character;
}

// 在页面上加一行字符：行内字符按 advance 挨个排开。
void appendLine(TextPage& page,
                const QString& text,
                double left,
                double bottom,
                double height,
                double advance) {
    double x = left;
    for (const QChar& ch : text) {
        page.chars.append(makeChar(QString(ch), x, bottom, advance * 0.8, height));
        x += advance;
    }
}

}  // namespace

class PageTextLinesTests final : public QObject {
    Q_OBJECT

private slots:
    void emptyPageProducesNoLines();
    void threeLinesStayThreeLines();
    void lineBoxUsesPdfConvention();
    void linesAreOrderedTopToBottom();
    void emptyCharactersAreIgnored();
    void mixedHeightsStayOnOneLine();
    void nearbyButDistinctLinesStaySeparate();
    void fixturePageClustersIntoThreeLines();
    void fixtureSecondPageClustersIntoTwoLines();
    void fixtureLinesAreOrderedTopToBottom();
    void fixtureLineTextKeepsSpaces();
};

// 一页没有字符 → 没有行。
void PageTextLinesTests::emptyPageProducesNoLines() {
    const TextPage page;
    QVERIFY(aipdf::app::userSpaceTextLines(page).isEmpty());
}

// 回归：旧实现用 QRectF::intersected() 判重叠，在 PDF 坐标系下恒为空，
// 于是一行字被拆成「每个字一行」。这里必须仍然是 3 行。
void PageTextLinesTests::threeLinesStayThreeLines() {
    TextPage page;
    appendLine(page, QStringLiteral("AAAAA"), 50, 700, 12, 10);
    appendLine(page, QStringLiteral("BBBBB"), 50, 600, 12, 10);
    appendLine(page, QStringLiteral("CCCCC"), 50, 500, 12, 10);

    const QVector<aipdf::app::UserSpaceLine> lines = aipdf::app::userSpaceTextLines(page);
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0).text, QStringLiteral("AAAAA"));
    QCOMPARE(lines.at(1).text, QStringLiteral("BBBBB"));
    QCOMPARE(lines.at(2).text, QStringLiteral("CCCCC"));
}

// 行的盒子必须是 PDF 约定：y 是下边、高度向上为正。
// pdf::TaggedRegion 收的就是这种矩形，写标签树靠它定位 marked content。
void PageTextLinesTests::lineBoxUsesPdfConvention() {
    TextPage page;
    appendLine(page, QStringLiteral("AAAAA"), 50, 700, 12, 10);

    const QVector<aipdf::app::UserSpaceLine> lines = aipdf::app::userSpaceTextLines(page);
    QCOMPARE(lines.size(), 1);
    const QRectF box = lines.at(0).box;
    QVERIFY2(box.height() > 0.0, "行盒子的高度必须是正的（y 是下边）");
    QVERIFY(qAbs(box.top() - 700.0) < 0.001);
    QVERIFY(qAbs(box.bottom() - 712.0) < 0.001);
    QVERIFY(qAbs(box.left() - 50.0) < 0.001);
    QVERIFY(box.width() > 0.0);
}

// 行按从上到下排：PDF 的 y 向上，所以上面的行 y 更大。
void PageTextLinesTests::linesAreOrderedTopToBottom() {
    TextPage page;
    appendLine(page, QStringLiteral("FIRST"), 50, 700, 12, 10);
    appendLine(page, QStringLiteral("SECOND"), 50, 600, 12, 10);
    appendLine(page, QStringLiteral("THIRD"), 50, 500, 12, 10);

    const QVector<aipdf::app::UserSpaceLine> lines = aipdf::app::userSpaceTextLines(page);
    QCOMPARE(lines.size(), 3);
    for (int index = 1; index < lines.size(); ++index) {
        QVERIFY(lines.at(index).box.top() < lines.at(index - 1).box.top());
    }
}

// 空文本与空框的字符不能变成行。
void PageTextLinesTests::emptyCharactersAreIgnored() {
    TextPage page;
    page.chars.append(makeChar(QString(), 10, 700, 8, 12));
    page.chars.append(makeChar(QStringLiteral("A"), 10, 700, 0, 12));
    page.chars.append(makeChar(QStringLiteral("B"), 20, 700, 8, 12));

    const QVector<aipdf::app::UserSpaceLine> lines = aipdf::app::userSpaceTextLines(page);
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.at(0).text, QStringLiteral("B"));
}

// 同一行里字高不一样（上下标、大小写混排）不能把行拆开。
void PageTextLinesTests::mixedHeightsStayOnOneLine() {
    TextPage page;
    page.chars.append(makeChar(QStringLiteral("a"), 10, 700, 8, 10));
    page.chars.append(makeChar(QStringLiteral("B"), 20, 700, 9, 14));
    page.chars.append(makeChar(QStringLiteral("c"), 30, 700, 8, 10));

    const QVector<aipdf::app::UserSpaceLine> lines = aipdf::app::userSpaceTextLines(page);
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.at(0).text, QStringLiteral("aBc"));
}

// 相邻两行（基线只差 13pt、行高 12pt）不能被并成一行。
void PageTextLinesTests::nearbyButDistinctLinesStaySeparate() {
    TextPage page;
    appendLine(page, QStringLiteral("TOP"), 50, 700, 12, 10);
    appendLine(page, QStringLiteral("BOTTOM"), 50, 687, 12, 10);

    const QVector<aipdf::app::UserSpaceLine> lines = aipdf::app::userSpaceTextLines(page);
    QCOMPARE(lines.size(), 2);
}

// 真实夹具：text.pdf 第 1 页是 3 行文字，必须聚成 3 行（不是几十行）。
void PageTextLinesTests::fixturePageClustersIntoThreeLines() {
    const auto opened = aipdf::pdf::openDocument(fixturePath(QStringLiteral("text.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const auto text = page->extractText();
    QVERIFY(text.has_value());
    QVERIFY(text->chars.size() > 50);

    const QVector<aipdf::app::UserSpaceLine> lines = aipdf::app::userSpaceTextLines(*text);
    QCOMPARE(lines.size(), 3);
}

// text.pdf 第 2 页是 2 行。
void PageTextLinesTests::fixtureSecondPageClustersIntoTwoLines() {
    const auto opened = aipdf::pdf::openDocument(fixturePath(QStringLiteral("text.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    const auto page = opened.document.page(1);
    QVERIFY(page.has_value());
    const auto text = page->extractText();
    QVERIFY(text.has_value());

    const QVector<aipdf::app::UserSpaceLine> lines = aipdf::app::userSpaceTextLines(*text);
    QCOMPARE(lines.size(), 2);
}

// 行按从上到下：夹具里第 1 行的 y 必须比第 3 行大。
void PageTextLinesTests::fixtureLinesAreOrderedTopToBottom() {
    const auto opened = aipdf::pdf::openDocument(fixturePath(QStringLiteral("text.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const auto text = page->extractText();
    QVERIFY(text.has_value());

    const QVector<aipdf::app::UserSpaceLine> lines = aipdf::app::userSpaceTextLines(*text);
    QVERIFY(!lines.isEmpty());
    for (int index = 1; index < lines.size(); ++index) {
        QVERIFY(lines.at(index).box.top() < lines.at(index - 1).box.top());
    }
}

// 行里的文字要保留空格：「更改区域顺序」对话框直接显示这段文字。
void PageTextLinesTests::fixtureLineTextKeepsSpaces() {
    const auto opened = aipdf::pdf::openDocument(fixturePath(QStringLiteral("text.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const auto text = page->extractText();
    QVERIFY(text.has_value());

    const QVector<aipdf::app::UserSpaceLine> lines = aipdf::app::userSpaceTextLines(*text);
    QCOMPARE(lines.size(), 3);
    QVERIFY(lines.at(0).text.contains(QStringLiteral("Folixa")));
    QVERIFY(lines.at(1).text.contains(QStringLiteral("Full text search")));
    QVERIFY(lines.at(2).text.contains(QStringLiteral("quick brown fox")));
}

QTEST_GUILESS_MAIN(PageTextLinesTests)
#include "PageTextLinesTests.moc"
