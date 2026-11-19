// 动作向导「加书签」「拆分文档」两步的 app 层接线测试。
//
// 测的是这两步里能脱离窗口测的部分：参数怎么解释（mode / ranges / pageIndex）、
// 页区间怎么换算（1 基含两端 → 0 基半开区间）、文件名怎么拼、页码越不越界。
//
// 为什么单独测 pdf::PageSplitPlanner：页区间的切法归 pdf 层
// （src/pdf/ops/PageSplitPlanner.h），app 层只是把向导参数翻译成它的入参。
// 切法本身的行为在 app 层也顺带钉一遍，免得两边各算各的、边界对不上。
//
// 为什么不测 PdfView 本身：它是个 QWidget 派生类，要真开窗口、要 PDFium、
// 要一份真的文档；这一步的可测部分已经被抽成自由函数放在 app/editor/PdfView.h 里，
// 这里直接调那些自由函数即可。

#include "app/editor/PdfView.h"

#include "pdf/ops/PageSplitPlanner.h"

#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

using aipdf::app::WizardSplitMode;
using aipdf::app::parseWizardSplitMode;
using aipdf::app::parseWizardSplitRangeItem;
using aipdf::app::resolveWizardBookmarkPage;
using aipdf::app::wizardSplitPartFileName;
using aipdf::pdf::PageRange;
using aipdf::pdf::PageSplitPlanner;

class WizardDocumentActionsTests : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------ 拆分口径（mode）----
    void modeEmptyDefaultsToRange();
    void modeRangeIsAccepted();
    void modeCountWithoutNumberMeansOnePage();
    void modeCountWithNumberKeepsIt();
    void modeEachIsAliasOfCountOne();
    void modeOutlineIsAccepted();
    void modeIsTrimmedAndCaseInsensitive();
    void modeRejectsUnknownValue();
    void modeCountRejectsZeroPages();
    void modeCountRejectsGarbageNumber();
    void modeCountRejectsNegativePages();

    // -------------------------------------------- 页区间项（ranges）----
    void rangeItemSinglePage();
    void rangeItemSpan();
    void rangeItemTrimsSpaces();
    void rangeItemRejectsEmpty();
    void rangeItemRejectsGarbage();
    void rangeItemRejectsReversed();
    void rangeItemRejectsZeroPage();
    void rangeItemRejectsExtraDash();

    // ------------------------------------------- 书签目标页（pageIndex）----
    void bookmarkPageConvertsToZeroBased();
    void bookmarkPageRejectsZero();
    void bookmarkPageRejectsBeyondPageCount();
    void bookmarkPageRejectsEmptyDocument();

    // ------------------------------------------------ 输出文件名 ----
    void partFileNameUsesOneBasedInclusivePages();
    void partFileNameFallsBackToPart();

    // ------------------------------- 领域层：pdf::PageSplitPlanner ----
    void plannerByCountSplitsPages();
    void plannerByCountRejectsInvalidInput();
    void plannerByOutlineUsesTopLevelStarts();
    void plannerByOutlineSkipsBrokenTargets();
    void plannerBySizeSplitsOnLimit();
    void plannerExtractConvertsToHalfOpen();
    void plannerExtractClampsOutOfRange();

    // -------------------------------------- app 层：参数 → 页区间 ----
    void rangeModeTurnsItemsIntoRanges();
    void countModeProducesExpectedRanges();
};

// ------------------------------------------------ 拆分口径（mode）----

void WizardDocumentActionsTests::modeEmptyDefaultsToRange() {
    WizardSplitMode mode = WizardSplitMode::Outline;
    int pagesPerFile = 0;
    QString error;
    // 空字符串按默认的 range 处理（界面层参数表里 mode 的默认值就是 range）。
    QVERIFY(parseWizardSplitMode(QString(), &mode, &pagesPerFile, &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(mode, WizardSplitMode::Range);
    QCOMPARE(pagesPerFile, 1);
}

void WizardDocumentActionsTests::modeRangeIsAccepted() {
    WizardSplitMode mode = WizardSplitMode::Count;
    int pagesPerFile = 0;
    QString error;
    QVERIFY(parseWizardSplitMode(QStringLiteral("range"), &mode, &pagesPerFile, &error));
    QCOMPARE(mode, WizardSplitMode::Range);
}

void WizardDocumentActionsTests::modeCountWithoutNumberMeansOnePage() {
    WizardSplitMode mode = WizardSplitMode::Range;
    int pagesPerFile = 0;
    QString error;
    // 只写 "count" 没带页数：按每 1 页一份（等价 count:1）。
    QVERIFY(parseWizardSplitMode(QStringLiteral("count"), &mode, &pagesPerFile, &error));
    QCOMPARE(mode, WizardSplitMode::Count);
    QCOMPARE(pagesPerFile, 1);
}

void WizardDocumentActionsTests::modeCountWithNumberKeepsIt() {
    WizardSplitMode mode = WizardSplitMode::Range;
    int pagesPerFile = 0;
    QString error;
    QVERIFY(parseWizardSplitMode(QStringLiteral("count:3"), &mode, &pagesPerFile, &error));
    QCOMPARE(mode, WizardSplitMode::Count);
    QCOMPARE(pagesPerFile, 3);
}

void WizardDocumentActionsTests::modeEachIsAliasOfCountOne() {
    WizardSplitMode mode = WizardSplitMode::Range;
    int pagesPerFile = 0;
    QString error;
    // "each" 是领域层 WizardActions 里的写法：逐页拆，与 count:1 同义。
    QVERIFY(parseWizardSplitMode(QStringLiteral("each"), &mode, &pagesPerFile, &error));
    QCOMPARE(mode, WizardSplitMode::Count);
    QCOMPARE(pagesPerFile, 1);
}

void WizardDocumentActionsTests::modeOutlineIsAccepted() {
    WizardSplitMode mode = WizardSplitMode::Range;
    int pagesPerFile = 0;
    QString error;
    QVERIFY(parseWizardSplitMode(QStringLiteral("outline"), &mode, &pagesPerFile, &error));
    QCOMPARE(mode, WizardSplitMode::Outline);
}

void WizardDocumentActionsTests::modeIsTrimmedAndCaseInsensitive() {
    WizardSplitMode mode = WizardSplitMode::Range;
    int pagesPerFile = 0;
    QString error;
    QVERIFY(parseWizardSplitMode(QStringLiteral("  Count:2  "), &mode, &pagesPerFile, &error));
    QCOMPARE(mode, WizardSplitMode::Count);
    QCOMPARE(pagesPerFile, 2);
}

void WizardDocumentActionsTests::modeRejectsUnknownValue() {
    WizardSplitMode mode = WizardSplitMode::Range;
    int pagesPerFile = 0;
    QString error;
    QVERIFY(!parseWizardSplitMode(QStringLiteral("bySize"), &mode, &pagesPerFile, &error));
    // 报错要说清支持哪些取值。
    QVERIFY(error.contains(QStringLiteral("range")));
    QVERIFY(error.contains(QStringLiteral("count")));
    QVERIFY(error.contains(QStringLiteral("outline")));
}

void WizardDocumentActionsTests::modeCountRejectsZeroPages() {
    WizardSplitMode mode = WizardSplitMode::Range;
    int pagesPerFile = 0;
    QString error;
    QVERIFY(!parseWizardSplitMode(QStringLiteral("count:0"), &mode, &pagesPerFile, &error));
    QVERIFY(!error.isEmpty());
}

void WizardDocumentActionsTests::modeCountRejectsGarbageNumber() {
    WizardSplitMode mode = WizardSplitMode::Range;
    int pagesPerFile = 0;
    QString error;
    QVERIFY(!parseWizardSplitMode(QStringLiteral("count:abc"), &mode, &pagesPerFile, &error));
    QVERIFY(!error.isEmpty());
}

void WizardDocumentActionsTests::modeCountRejectsNegativePages() {
    WizardSplitMode mode = WizardSplitMode::Range;
    int pagesPerFile = 0;
    QString error;
    QVERIFY(!parseWizardSplitMode(QStringLiteral("count:-2"), &mode, &pagesPerFile, &error));
    QVERIFY(!error.isEmpty());
}

// ------------------------------------------------ 页区间项（ranges）----

void WizardDocumentActionsTests::rangeItemSinglePage() {
    int first = 0;
    int last = 0;
    QString error;
    QVERIFY(parseWizardSplitRangeItem(QStringLiteral("5"), &first, &last, &error));
    QCOMPARE(first, 5);
    QCOMPARE(last, 5);
}

void WizardDocumentActionsTests::rangeItemSpan() {
    int first = 0;
    int last = 0;
    QString error;
    QVERIFY(parseWizardSplitRangeItem(QStringLiteral("1-3"), &first, &last, &error));
    QCOMPARE(first, 1);
    QCOMPARE(last, 3);
}

void WizardDocumentActionsTests::rangeItemTrimsSpaces() {
    int first = 0;
    int last = 0;
    QString error;
    QVERIFY(parseWizardSplitRangeItem(QStringLiteral(" 2 - 4 "), &first, &last, &error));
    QCOMPARE(first, 2);
    QCOMPARE(last, 4);
}

void WizardDocumentActionsTests::rangeItemRejectsEmpty() {
    int first = 0;
    int last = 0;
    QString error;
    QVERIFY(!parseWizardSplitRangeItem(QStringLiteral("   "), &first, &last, &error));
    QVERIFY(!error.isEmpty());
}

void WizardDocumentActionsTests::rangeItemRejectsGarbage() {
    int first = 0;
    int last = 0;
    QString error;
    QVERIFY(!parseWizardSplitRangeItem(QStringLiteral("abc"), &first, &last, &error));
    QVERIFY(!error.isEmpty());
}

void WizardDocumentActionsTests::rangeItemRejectsReversed() {
    int first = 0;
    int last = 0;
    QString error;
    QVERIFY(!parseWizardSplitRangeItem(QStringLiteral("3-1"), &first, &last, &error));
    QVERIFY(!error.isEmpty());
}

void WizardDocumentActionsTests::rangeItemRejectsZeroPage() {
    int first = 0;
    int last = 0;
    QString error;
    QVERIFY(!parseWizardSplitRangeItem(QStringLiteral("0"), &first, &last, &error));
    QVERIFY(!error.isEmpty());
}

void WizardDocumentActionsTests::rangeItemRejectsExtraDash() {
    int first = 0;
    int last = 0;
    QString error;
    QVERIFY(!parseWizardSplitRangeItem(QStringLiteral("1-3-5"), &first, &last, &error));
    QVERIFY(!error.isEmpty());
}

// -------------------------------------------- 书签目标页（pageIndex）----

void WizardDocumentActionsTests::bookmarkPageConvertsToZeroBased() {
    int resolved = -1;
    QString error;
    // 参数是 1 基，OutlineItem::pageIndex 是 0 基。
    QVERIFY(resolveWizardBookmarkPage(1, 10, &resolved, &error));
    QCOMPARE(resolved, 0);
    QVERIFY(resolveWizardBookmarkPage(10, 10, &resolved, &error));
    QCOMPARE(resolved, 9);
}

void WizardDocumentActionsTests::bookmarkPageRejectsZero() {
    int resolved = -1;
    QString error;
    QVERIFY(!resolveWizardBookmarkPage(0, 10, &resolved, &error));
    QVERIFY(!error.isEmpty());
}

void WizardDocumentActionsTests::bookmarkPageRejectsBeyondPageCount() {
    int resolved = -1;
    QString error;
    QVERIFY(!resolveWizardBookmarkPage(11, 10, &resolved, &error));
    // 报错要带上文档一共多少页。
    QVERIFY(error.contains(QStringLiteral("10")));
}

void WizardDocumentActionsTests::bookmarkPageRejectsEmptyDocument() {
    int resolved = -1;
    QString error;
    QVERIFY(!resolveWizardBookmarkPage(1, 0, &resolved, &error));
    QVERIFY(!error.isEmpty());
}

// ---------------------------------------------------- 输出文件名 ----

void WizardDocumentActionsTests::partFileNameUsesOneBasedInclusivePages() {
    // 与 PdfView::splitDocumentDialog 的命名一致：%1_%2-%3.pdf，页码 1 基含两端。
    QCOMPARE(wizardSplitPartFileName(QStringLiteral("doc"), PageRange{0, 3}),
             QStringLiteral("doc_1-3.pdf"));
    QCOMPARE(wizardSplitPartFileName(QStringLiteral("doc"), PageRange{2, 5}),
             QStringLiteral("doc_3-5.pdf"));
}

void WizardDocumentActionsTests::partFileNameFallsBackToPart() {
    // 文档没有文件名时用 "part" 兜底（与既有拆分对话框一致）。
    QCOMPARE(wizardSplitPartFileName(QString(), PageRange{0, 1}),
             QStringLiteral("part_1-1.pdf"));
}

// ------------------------------- 领域层：pdf::PageSplitPlanner ----

void WizardDocumentActionsTests::plannerByCountSplitsPages() {
    const QVector<PageRange> ranges = PageSplitPlanner::byCount(10, 3);
    QCOMPARE(ranges.size(), 4);
    QCOMPARE(ranges.at(0).first, 0);
    QCOMPARE(ranges.at(0).last, 3);
    // 末段不足 N 页时按实际页数算。
    QCOMPARE(ranges.at(3).first, 9);
    QCOMPARE(ranges.at(3).last, 10);
}

void WizardDocumentActionsTests::plannerByCountRejectsInvalidInput() {
    QCOMPARE(PageSplitPlanner::byCount(10, 0).size(), 0);
    QCOMPARE(PageSplitPlanner::byCount(0, 3).size(), 0);
}

void WizardDocumentActionsTests::plannerByOutlineUsesTopLevelStarts() {
    std::vector<aipdf::pdf::OutlineItem> outline;
    aipdf::pdf::OutlineItem first;
    first.title = QStringLiteral("第一章");
    first.pageIndex = 0;
    aipdf::pdf::OutlineItem second;
    second.title = QStringLiteral("第二章");
    second.pageIndex = 5;
    outline.push_back(first);
    outline.push_back(second);
    const QVector<PageRange> ranges = PageSplitPlanner::byOutline(10, outline);
    QCOMPARE(ranges.size(), 2);
    QCOMPARE(ranges.at(0).first, 0);
    QCOMPARE(ranges.at(0).last, 5);
    QCOMPARE(ranges.at(1).first, 5);
    QCOMPARE(ranges.at(1).last, 10);
}

void WizardDocumentActionsTests::plannerByOutlineSkipsBrokenTargets() {
    std::vector<aipdf::pdf::OutlineItem> outline;
    aipdf::pdf::OutlineItem broken;
    broken.title = QStringLiteral("坏目标");
    broken.pageIndex = -1;
    aipdf::pdf::OutlineItem beyond;
    beyond.title = QStringLiteral("指向文档外");
    beyond.pageIndex = 99;
    aipdf::pdf::OutlineItem good;
    good.title = QStringLiteral("第一章");
    good.pageIndex = 0;
    outline.push_back(broken);
    outline.push_back(beyond);
    outline.push_back(good);
    const QVector<PageRange> ranges = PageSplitPlanner::byOutline(10, outline);
    // 坏目标与越界目标都不参与分段，只剩一个有效起点就只出一段。
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.at(0).first, 0);
    QCOMPARE(ranges.at(0).last, 10);
}

void WizardDocumentActionsTests::plannerBySizeSplitsOnLimit() {
    // 每页 10 字节、上限 15 字节：两页就超限，于是每页自成一档。
    const QVector<qint64> sizes{10, 10, 10};
    const QVector<PageRange> ranges = PageSplitPlanner::bySize(sizes, 15);
    QCOMPARE(ranges.size(), 3);
    QCOMPARE(ranges.at(0).first, 0);
    QCOMPARE(ranges.at(0).last, 1);
    QCOMPARE(ranges.at(2).last, 3);
}

void WizardDocumentActionsTests::plannerExtractConvertsToHalfOpen() {
    // 1 基含两端 → 0 基半开区间：[0, 3) 是 3 页。
    const QVector<PageRange> ranges = PageSplitPlanner::extract(10, 1, 3);
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.at(0).first, 0);
    QCOMPARE(ranges.at(0).last, 3);
    QCOMPARE(ranges.at(0).count(), 3);
}

void WizardDocumentActionsTests::plannerExtractClampsOutOfRange() {
    // 领域层会把越界页码夹到文档范围内（app 层另外自己判越界并报错）。
    const QVector<PageRange> ranges = PageSplitPlanner::extract(10, 0, 99);
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.at(0).first, 0);
    QCOMPARE(ranges.at(0).last, 10);
}

// -------------------------------------- app 层：参数 → 页区间 ----

void WizardDocumentActionsTests::rangeModeTurnsItemsIntoRanges() {
    // app 层做的两步换算：先解析成 1 基含两端，再交给领域层换成 0 基半开区间。
    int first = 0;
    int last = 0;
    QString error;
    QVERIFY(parseWizardSplitRangeItem(QStringLiteral("1-3"), &first, &last, &error));
    QVector<PageRange> ranges = PageSplitPlanner::extract(10, first, last);
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.at(0).first, 0);
    QCOMPARE(ranges.at(0).last, 3);

    QVERIFY(parseWizardSplitRangeItem(QStringLiteral("8"), &first, &last, &error));
    ranges = PageSplitPlanner::extract(10, first, last);
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.at(0).first, 7);
    QCOMPARE(ranges.at(0).last, 8);
}

void WizardDocumentActionsTests::countModeProducesExpectedRanges() {
    WizardSplitMode mode = WizardSplitMode::Range;
    int pagesPerFile = 0;
    QString error;
    QVERIFY(parseWizardSplitMode(QStringLiteral("count:4"), &mode, &pagesPerFile, &error));
    QCOMPARE(mode, WizardSplitMode::Count);
    const QVector<PageRange> ranges = PageSplitPlanner::byCount(10, pagesPerFile);
    QCOMPARE(ranges.size(), 3);
    QCOMPARE(ranges.at(0).first, 0);
    QCOMPARE(ranges.at(0).last, 4);
    QCOMPARE(ranges.at(2).first, 8);
    QCOMPARE(ranges.at(2).last, 10);
}

QTEST_GUILESS_MAIN(WizardDocumentActionsTests)

#include "WizardDocumentActionsTests.moc"
