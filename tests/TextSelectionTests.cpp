#include "app/PageTransform.h"
#include "app/TextLayout.h"

#include <QTest>

using aipdf::app::LayoutChar;
using aipdf::app::PageTransform;
using aipdf::app::TextLayout;

namespace {

LayoutChar makeChar(const QString& text, double x, double y, double width, double height) {
    LayoutChar character;
    character.text = text;
    character.box = QRectF(x, y, width, height);
    return character;
}

}  // namespace

class TextSelectionTests : public QObject {
    Q_OBJECT

private slots:
    void transformKeepsDisplaySize();
    void transformMapsUserSpacePoints();
    void transformMapsRects();
    void layoutGroupsRowsIntoReadingOrder();
    void layoutCopiesAcrossLinesWithNewline();
    void layoutInsertsSpaceOnWideGap();
    void layoutDoesNotInsertSpaceBetweenCjk();
    void layoutHitTestFindsNearestChar();
    void layoutWordRangeSelectsWholeWord();
    void layoutHandlesVerticalCjk();
    void layoutHighlightRectsSplitByLine();
};

// ---------------------------------------------------------------- 坐标变换 ----

void TextSelectionTests::transformKeepsDisplaySize() {
    PageTransform transform;
    transform.setPage(0.0, 0.0, 200.0, 100.0);

    transform.setRotationDegrees(0);
    QCOMPARE(transform.displaySize(), QSizeF(200.0, 100.0));
    transform.setRotationDegrees(180);
    QCOMPARE(transform.displaySize(), QSizeF(200.0, 100.0));
    transform.setRotationDegrees(90);
    QCOMPARE(transform.displaySize(), QSizeF(100.0, 200.0));
    transform.setRotationDegrees(270);
    QCOMPARE(transform.displaySize(), QSizeF(100.0, 200.0));
}

void TextSelectionTests::transformMapsUserSpacePoints() {
    PageTransform transform;
    transform.setPage(0.0, 0.0, 200.0, 100.0);

    // 未旋转：左下原点翻转到左上原点。
    transform.setRotationDegrees(0);
    QCOMPARE(transform.toDisplay(QPointF(0.0, 0.0)), QPointF(0.0, 100.0));
    QCOMPARE(transform.toDisplay(QPointF(0.0, 100.0)), QPointF(0.0, 0.0));
    QCOMPARE(transform.toDisplay(QPointF(200.0, 0.0)), QPointF(200.0, 100.0));

    // 顺时针 90°：显示尺寸变成 高 × 宽。
    transform.setRotationDegrees(90);
    QCOMPARE(transform.toDisplay(QPointF(0.0, 0.0)), QPointF(0.0, 0.0));
    QCOMPARE(transform.toDisplay(QPointF(0.0, 100.0)), QPointF(100.0, 0.0));
    QCOMPARE(transform.toDisplay(QPointF(200.0, 0.0)), QPointF(0.0, 200.0));

    // 180°。
    transform.setRotationDegrees(180);
    QCOMPARE(transform.toDisplay(QPointF(0.0, 0.0)), QPointF(200.0, 0.0));
    QCOMPARE(transform.toDisplay(QPointF(200.0, 100.0)), QPointF(0.0, 100.0));
}

void TextSelectionTests::transformMapsRects() {
    PageTransform transform;
    transform.setPage(0.0, 0.0, 200.0, 100.0);
    transform.setRotationDegrees(0);

    // 用户空间里 y 向上，矩形用 (left, bottom, width, height) 表示。
    const QRectF userRect(10.0, 20.0, 30.0, 40.0);
    const QRectF display = transform.toDisplay(userRect);
    QCOMPARE(display.left(), 10.0);
    QCOMPARE(display.width(), 30.0);
    QCOMPARE(display.height(), 40.0);
    // 用户空间底边 y=20 → 显示空间 top = 100 - 60 = 40。
    QCOMPARE(display.top(), 40.0);
}

// ------------------------------------------------------------ 行分组与顺序 ----

void TextSelectionTests::layoutGroupsRowsIntoReadingOrder() {
    // 两行，每行三个词；给的是乱序输入。
    TextLayout layout;
    layout.setChars({
        makeChar("C", 60.0, 0.0, 10.0, 12.0),
        makeChar("A", 20.0, 0.0, 10.0, 12.0),
        makeChar("F", 40.0, 20.0, 10.0, 12.0),
        makeChar("B", 40.0, 0.0, 10.0, 12.0),
        makeChar("E", 20.0, 20.0, 10.0, 12.0),
        makeChar("D", 60.0, 20.0, 10.0, 12.0),
    });

    QCOMPARE(layout.charCount(), 6);
    QCOMPARE(layout.lines().size(), 2);
    QString ordered;
    for (int index : layout.readingOrder()) {
        ordered += QString::number(index);
    }
    // 阅读顺序：先第一行（从左到右：A、B、C），再第二行（E、F、D）。
    // 下标分别是 A=1、B=3、C=0、E=4、F=2、D=5。
    QCOMPARE(ordered, QStringLiteral("130425"));
    QCOMPARE(layout.lines().at(0).start, 0);
    QCOMPARE(layout.lines().at(0).end, 3);
    QCOMPARE(layout.lines().at(1).start, 3);
    QCOMPARE(layout.lines().at(1).end, 6);
}

void TextSelectionTests::layoutCopiesAcrossLinesWithNewline() {
    TextLayout layout;
    layout.setChars({
        makeChar("A", 0.0, 0.0, 10.0, 12.0),
        makeChar("B", 12.0, 0.0, 10.0, 12.0),
        makeChar("C", 0.0, 20.0, 10.0, 12.0),
    });

    // 同一行内直接拼接。
    QCOMPARE(layout.textForRange(0, 2), QStringLiteral("AB"));
    // 跨行用换行。
    QCOMPARE(layout.textForRange(0, 3), QStringLiteral("AB\nC"));
    // 反向区间（拖选反方向）也要能给出文本。
    QCOMPARE(layout.textForRange(2, 3), QStringLiteral("C"));
    QCOMPARE(layout.textForRange(0, 0), QString());
}

void TextSelectionTests::layoutInsertsSpaceOnWideGap() {
    TextLayout layout;
    layout.setChars({
        makeChar("A", 0.0, 0.0, 10.0, 12.0),
        // 间隔 15 点，远大于字高 20% → 补一个空格。
        makeChar("B", 25.0, 0.0, 10.0, 12.0),
    });
    QCOMPARE(layout.textForRange(0, 2), QStringLiteral("A B"));
}

void TextSelectionTests::layoutDoesNotInsertSpaceBetweenCjk() {
    TextLayout layout;
    layout.setChars({
        makeChar(QStringLiteral("中"), 0.0, 0.0, 12.0, 12.0),
        makeChar(QStringLiteral("文"), 20.0, 0.0, 12.0, 12.0),
    });
    // CJK 之间不插空格。
    QCOMPARE(layout.textForRange(0, 2), QStringLiteral("中文"));
}

void TextSelectionTests::layoutHitTestFindsNearestChar() {
    TextLayout layout;
    layout.setChars({
        makeChar("A", 0.0, 0.0, 10.0, 12.0),
        makeChar("B", 12.0, 0.0, 10.0, 12.0),
    });

    // 命中框内。
    QCOMPARE(layout.hitTest(QPointF(5.0, 5.0)), 0);
    QCOMPARE(layout.hitTest(QPointF(15.0, 5.0)), 1);
    // 框外但很近 → 仍然命中最近的字符。
    QCOMPARE(layout.hitTest(QPointF(12.5, 13.0)), 1);
    // 离得很远 → 不命中。
    QCOMPARE(layout.hitTest(QPointF(400.0, 400.0)), -1);
}

void TextSelectionTests::layoutWordRangeSelectsWholeWord() {
    TextLayout layout;
    layout.setChars({
        makeChar("H", 0.0, 0.0, 10.0, 12.0),
        makeChar("i", 10.0, 0.0, 6.0, 12.0),
        makeChar(" ", 16.0, 0.0, 6.0, 12.0),
        makeChar("P", 22.0, 0.0, 10.0, 12.0),
        makeChar("d", 32.0, 0.0, 10.0, 12.0),
    });

    int start = -1;
    int end = -1;
    QVERIFY(layout.wordRangeAt(1, &start, &end));
    QCOMPARE(start, 0);
    QCOMPARE(end, 2);
    QCOMPARE(layout.textForRange(start, end), QStringLiteral("Hi"));

    QVERIFY(layout.wordRangeAt(4, &start, &end));
    QCOMPARE(start, 3);
    QCOMPARE(end, 5);
    QCOMPARE(layout.textForRange(start, end), QStringLiteral("Pd"));
}

void TextSelectionTests::layoutHandlesVerticalCjk() {
    // 两列竖排，每列三行（CJK 竖排从右往左、列内从上到下）。
    TextLayout layout;
    layout.setChars({
        makeChar(QStringLiteral("上"), 40.0, 0.0, 12.0, 12.0),
        makeChar(QStringLiteral("左"), 0.0, 0.0, 12.0, 12.0),
        makeChar(QStringLiteral("下"), 40.0, 14.0, 12.0, 12.0),
        makeChar(QStringLiteral("右"), 0.0, 14.0, 12.0, 12.0),
        makeChar(QStringLiteral("中"), 40.0, 28.0, 12.0, 12.0),
        makeChar(QStringLiteral("文"), 0.0, 28.0, 12.0, 12.0),
    });

    QVERIFY(layout.isVertical());
    // 行数（4）多于列数（2）→ 判定为竖排。
    QCOMPARE(layout.lines().size(), 2);
    // 右列先读：上、下、中；再左列：左、右、文（列与列之间是换行）。
    QCOMPARE(layout.textForRange(0, layout.charCount()),
             QStringLiteral("上下中\n左右文"));
}

void TextSelectionTests::layoutHighlightRectsSplitByLine() {
    TextLayout layout;
    layout.setChars({
        makeChar("A", 0.0, 0.0, 10.0, 12.0),
        makeChar("B", 12.0, 0.0, 10.0, 12.0),
        makeChar("C", 0.0, 20.0, 10.0, 12.0),
        makeChar("D", 12.0, 20.0, 10.0, 12.0),
    });

    // 选第一行 → 一个矩形。
    const QVector<QRectF> firstLine = layout.highlightRects(0, 2);
    QCOMPARE(firstLine.size(), 1);
    QCOMPARE(firstLine.at(0).left(), 0.0);
    QCOMPARE(firstLine.at(0).right(), 22.0);

    // 跨两行 → 两个矩形。
    const QVector<QRectF> bothLines = layout.highlightRects(1, 4);
    QCOMPARE(bothLines.size(), 2);
    QCOMPARE(bothLines.at(0).top(), 0.0);
    QCOMPARE(bothLines.at(1).top(), 20.0);
}

QTEST_MAIN(TextSelectionTests)
#include "TextSelectionTests.moc"
