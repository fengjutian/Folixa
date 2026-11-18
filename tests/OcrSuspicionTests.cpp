#include "pdf/ops/OcrSuspicion.h"

#include <QTest>
#include <QVector>

using aipdf::pdf::SuspiciousChar;
using aipdf::pdf::TextChar;
using aipdf::pdf::TextPage;

namespace {

// 造一页文本层。
TextPage makePage(int pageIndex, const QString& text) {
    TextPage page;
    page.pageIndex = pageIndex;
    for (const QChar ch : text) {
        TextChar character;
        character.text = QString(ch);
        page.chars.append(character);
    }
    return page;
}

// 结果里有没有某一条（按认出来的字找）。
bool hasRecognized(const QVector<SuspiciousChar>& items, const QString& recognized) {
    for (const SuspiciousChar& item : items) {
        if (item.recognized == recognized) return true;
    }
    return false;
}

// 取第一条认出来的字等于 recognized 的结论。
SuspiciousChar firstWithRecognized(const QVector<SuspiciousChar>& items, const QString& recognized) {
    for (const SuspiciousChar& item : items) {
        if (item.recognized == recognized) return item;
    }
    return SuspiciousChar();
}

}  // namespace

class OcrSuspicionTests : public QObject {
    Q_OBJECT

private slots:
    void emptyTextReturnsEmpty();
    void chineseTextDoesNotTrigger();
    void confusableInEnglishWordIsFlagged();
    void confusableInNumberIsFlagged();
    void resultsAreSortedByPage();
    void suggestionPointsToPairedCharacter();
    void reasonIsNotEmpty();
    void cleanTextHasNoFindings();
    void caseDifferenceStillCounts();
    void emptyPagesDoNotCrash();
    void contextSnippetIsProvided();
    void recognizedTextFallbackIsUsed();
};

// 1. 空文本 → 空结果。
void OcrSuspicionTests::emptyTextReturnsEmpty() {
    const QVector<SuspiciousChar> items =
        aipdf::pdf::findSuspiciousCharacters({}, QString());
    QVERIFY(items.isEmpty());
}

// 2. 纯中文正文里的 0 / O 不提示。
void OcrSuspicionTests::chineseTextDoesNotTrigger() {
    QVector<TextPage> pages;
    pages.append(makePage(0, QStringLiteral("订单号 0 与 O 的区别：第 0 号和 O 型配件")));
    const QVector<SuspiciousChar> items = aipdf::pdf::findSuspiciousCharacters(pages, QString());
    QVERIFY2(items.isEmpty(),
             qPrintable(QStringLiteral("不该提示：%1")
                            .arg(items.isEmpty() ? QString()
                                                 : items.first().recognized)));
}

// 3. 英文单词里的易混字符要提示。
void OcrSuspicionTests::confusableInEnglishWordIsFlagged() {
    QVector<TextPage> pages;
    pages.append(makePage(0, QStringLiteral("login l0g1n")));
    const QVector<SuspiciousChar> items = aipdf::pdf::findSuspiciousCharacters(pages, QString());
    QVERIFY(!items.isEmpty());
    // l0g1n 里的两个数字都该被点名。
    QVERIFY(hasRecognized(items, QStringLiteral("0")));
    QVERIFY(hasRecognized(items, QStringLiteral("1")));
    QCOMPARE(firstWithRecognized(items, QStringLiteral("0")).suggestion, QStringLiteral("O"));
    QCOMPARE(firstWithRecognized(items, QStringLiteral("1")).suggestion, QStringLiteral("l"));
}

// 4. 数字里的易混字符要提示。
void OcrSuspicionTests::confusableInNumberIsFlagged() {
    QVector<TextPage> pages;
    pages.append(makePage(0, QStringLiteral("编号 2024-O8 到期")));
    const QVector<SuspiciousChar> items = aipdf::pdf::findSuspiciousCharacters(pages, QString());
    QVERIFY(!items.isEmpty());
    QVERIFY(hasRecognized(items, QStringLiteral("O")));
    QCOMPARE(firstWithRecognized(items, QStringLiteral("O")).suggestion, QStringLiteral("0"));
}

// 5. 结果按页码排序。
void OcrSuspicionTests::resultsAreSortedByPage() {
    QVector<TextPage> pages;
    // 故意把页码大的放在前面：排序必须按页号来，而不是按传入顺序。
    pages.append(makePage(3, QStringLiteral("2O24")));
    pages.append(makePage(1, QStringLiteral("3B45")));
    const QVector<SuspiciousChar> items = aipdf::pdf::findSuspiciousCharacters(pages, QString());
    QVERIFY(items.size() >= 2);
    for (int i = 0; i < items.size(); ++i) {
        QVERIFY(items.at(i).pageIndex >= 0);
        if (i > 0) {
            QVERIFY(items.at(i - 1).pageIndex <= items.at(i).pageIndex);
        }
    }
    QCOMPARE(items.first().pageIndex, 1);
}

// 6. suggestion 指向配对字符。
void OcrSuspicionTests::suggestionPointsToPairedCharacter() {
    QVector<TextPage> pages;
    pages.append(makePage(0, QStringLiteral("2024-O8")));
    const QVector<SuspiciousChar> items = aipdf::pdf::findSuspiciousCharacters(pages, QString());
    QVERIFY(!items.isEmpty());
    for (const SuspiciousChar& item : items) {
        QVERIFY(!item.suggestion.isEmpty());
        // 建议的字必须与认出来的字不同，否则没有意义。
        QVERIFY(item.suggestion != item.recognized);
        // 且必须是形近字表里的配对。
        const bool paired =
            (item.recognized == QStringLiteral("O") && item.suggestion == QStringLiteral("0")) ||
            (item.recognized == QStringLiteral("0") && item.suggestion == QStringLiteral("O")) ||
            (item.recognized == QStringLiteral("l") && item.suggestion == QStringLiteral("1")) ||
            (item.recognized == QStringLiteral("1") && item.suggestion == QStringLiteral("l")) ||
            (item.recognized == QStringLiteral("I") && item.suggestion == QStringLiteral("1")) ||
            (item.recognized == QStringLiteral("Z") && item.suggestion == QStringLiteral("2")) ||
            (item.recognized == QStringLiteral("S") && item.suggestion == QStringLiteral("5")) ||
            (item.recognized == QStringLiteral("B") && item.suggestion == QStringLiteral("8")) ||
            (item.recognized == QStringLiteral("8") && item.suggestion == QStringLiteral("B"));
        QVERIFY2(paired, qPrintable(QStringLiteral("%1 → %2").arg(item.recognized, item.suggestion)));
    }
}

// 7. reason 非空。
void OcrSuspicionTests::reasonIsNotEmpty() {
    QVector<TextPage> pages;
    pages.append(makePage(0, QStringLiteral("l0g1n 与 2O24")));
    const QVector<SuspiciousChar> items = aipdf::pdf::findSuspiciousCharacters(pages, QString());
    QVERIFY(!items.isEmpty());
    for (const SuspiciousChar& item : items) {
        QVERIFY(!item.reason.isEmpty());
    }
}

// 8. 没有易混字符时不误报。
void OcrSuspicionTests::cleanTextHasNoFindings() {
    QVector<TextPage> pages;
    pages.append(makePage(0, QStringLiteral("PDF export ok 2024")));
    const QVector<SuspiciousChar> items = aipdf::pdf::findSuspiciousCharacters(pages, QString());
    QVERIFY2(items.isEmpty(),
             qPrintable(items.isEmpty()
                            ? QString()
                            : QStringLiteral("%1 → %2").arg(items.first().recognized,
                                                            items.first().suggestion)));
}

// 9. 大小写不同也算易混。
void OcrSuspicionTests::caseDifferenceStillCounts() {
    QVector<TextPage> upper;
    upper.append(makePage(0, QStringLiteral("2O4")));
    QVector<TextPage> lower;
    lower.append(makePage(0, QStringLiteral("2o4")));
    const QVector<SuspiciousChar> upperItems =
        aipdf::pdf::findSuspiciousCharacters(upper, QString());
    const QVector<SuspiciousChar> lowerItems =
        aipdf::pdf::findSuspiciousCharacters(lower, QString());
    QVERIFY(!upperItems.isEmpty());
    QVERIFY(!lowerItems.isEmpty());
    QCOMPARE(firstWithRecognized(upperItems, QStringLiteral("O")).suggestion, QStringLiteral("0"));
    QCOMPARE(firstWithRecognized(lowerItems, QStringLiteral("o")).suggestion, QStringLiteral("0"));
}

// 10. 空 pages 不崩。
void OcrSuspicionTests::emptyPagesDoNotCrash() {
    QVector<TextPage> pages;
    const QVector<SuspiciousChar> items =
        aipdf::pdf::findSuspiciousCharacters(pages, QStringLiteral("2024-O8"));
    // 没有页也没有文本层来源时按整篇处理，仍然要给出结论而不是崩掉。
    QVERIFY(!items.isEmpty());
    QCOMPARE(items.first().pageIndex, 0);
}

// 11. 上下文片段带得上 surrounding 文本。
void OcrSuspicionTests::contextSnippetIsProvided() {
    QVector<TextPage> pages;
    pages.append(makePage(0, QStringLiteral("前面的说明文字 l0g1n 后面的说明文字")));
    const QVector<SuspiciousChar> items = aipdf::pdf::findSuspiciousCharacters(pages, QString());
    QVERIFY(!items.isEmpty());
    for (const SuspiciousChar& item : items) {
        QVERIFY(!item.context.isEmpty());
        QVERIFY(item.context.contains(QStringLiteral("l0g1n")));
    }
}

// 12. 没有文本层时用 recognizedText 兜底。
void OcrSuspicionTests::recognizedTextFallbackIsUsed() {
    QVector<TextPage> pages;
    const QVector<SuspiciousChar> items =
        aipdf::pdf::findSuspiciousCharacters(pages, QStringLiteral("编号 2024-O8"));
    QVERIFY(!items.isEmpty());
    QVERIFY(hasRecognized(items, QStringLiteral("O")));
    QCOMPARE(items.first().pageIndex, 0);
}

QTEST_GUILESS_MAIN(OcrSuspicionTests)
#include "OcrSuspicionTests.moc"
