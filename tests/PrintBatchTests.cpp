#include "doc/PrintBatch.h"

#include <QSet>
#include <QString>
#include <QTest>
#include <QVector>

using aipdf::doc::PrintBatchItem;
using aipdf::doc::PrintBatchOptions;
using aipdf::doc::PrintBatchPlan;
using aipdf::doc::planPrintBatch;

namespace {

PrintBatchItem makeItem(const QString& path, int copies = 1, const QString& id = QString(),
                        const QString& pageRange = QString()) {
    PrintBatchItem item;
    item.filePath = path;
    item.copies = copies;
    item.id = id;
    item.pageRange = pageRange;
    return item;
}

}  // namespace

class PrintBatchTests : public QObject {
    Q_OBJECT

private slots:
    void emptyQueueFails();
    void singleFileSingleCopyExpandsToOne();
    void threeCopiesExpandToThree();
    void zeroCopiesIsClampedToOne();
    void negativeCopiesIsClampedToOne();
    void emptyPathIsSkippedWithWarning();
    void combineWithoutOutputPathFails();
    void totalJobsCountsExpandedQueue();
    void expandedIdsAreUnique();
    void pageRangeIsKeptVerbatim();
    void skippedItemDoesNotCountTowardsTotalJobs();
    void emptyQueueWritesReasonToErrorOutParam();
    void allItemsSkippedStillKeepsOtherWarnings();
};

void PrintBatchTests::emptyQueueFails() {
    QString error;
    const PrintBatchPlan plan = planPrintBatch({}, PrintBatchOptions(), &error);
    QVERIFY(!plan.ok);
    QVERIFY(!plan.error.isEmpty());
    QVERIFY(plan.expanded.isEmpty());
    QCOMPARE(plan.totalJobs, 0);
}

void PrintBatchTests::singleFileSingleCopyExpandsToOne() {
    const QVector<PrintBatchItem> items{makeItem(QStringLiteral("C:/docs/a.pdf"))};
    const PrintBatchPlan plan = planPrintBatch(items, PrintBatchOptions());
    QVERIFY(plan.ok);
    QCOMPARE(plan.expanded.size(), 1);
    QCOMPARE(plan.expanded.first().filePath, QStringLiteral("C:/docs/a.pdf"));
}

void PrintBatchTests::threeCopiesExpandToThree() {
    const QVector<PrintBatchItem> items{makeItem(QStringLiteral("C:/docs/a.pdf"), 3)};
    const PrintBatchPlan plan = planPrintBatch(items, PrintBatchOptions());
    QVERIFY(plan.ok);
    QCOMPARE(plan.expanded.size(), 3);
    // 展开后每一项就是一份。
    for (const PrintBatchItem& item : plan.expanded) {
        QCOMPARE(item.copies, 1);
        QCOMPARE(item.filePath, QStringLiteral("C:/docs/a.pdf"));
    }
}

void PrintBatchTests::zeroCopiesIsClampedToOne() {
    const QVector<PrintBatchItem> items{makeItem(QStringLiteral("C:/docs/a.pdf"), 0)};
    const PrintBatchPlan plan = planPrintBatch(items, PrintBatchOptions());
    QVERIFY(plan.ok);
    QCOMPARE(plan.expanded.size(), 1);
    QVERIFY(!plan.warnings.isEmpty());
}

void PrintBatchTests::negativeCopiesIsClampedToOne() {
    const QVector<PrintBatchItem> items{makeItem(QStringLiteral("C:/docs/a.pdf"), -3)};
    const PrintBatchPlan plan = planPrintBatch(items, PrintBatchOptions());
    QVERIFY(plan.ok);
    QCOMPARE(plan.expanded.size(), 1);
}

void PrintBatchTests::emptyPathIsSkippedWithWarning() {
    const QVector<PrintBatchItem> items{makeItem(QString(), 1, QStringLiteral("empty")),
                                        makeItem(QStringLiteral("C:/docs/a.pdf"), 1,
                                                 QStringLiteral("kept"))};
    const PrintBatchPlan plan = planPrintBatch(items, PrintBatchOptions());
    // 空路径只跳过那一项，不让整批失败。
    QVERIFY(plan.ok);
    QCOMPARE(plan.expanded.size(), 1);
    QCOMPARE(plan.expanded.first().filePath, QStringLiteral("C:/docs/a.pdf"));
    QVERIFY(!plan.warnings.isEmpty());
}

void PrintBatchTests::combineWithoutOutputPathFails() {
    const QVector<PrintBatchItem> items{makeItem(QStringLiteral("C:/docs/a.pdf"))};
    PrintBatchOptions options;
    options.combineIntoOne = true;
    options.outputPath.clear();
    QString error;
    const PrintBatchPlan plan = planPrintBatch(items, options, &error);
    QVERIFY(!plan.ok);
    QVERIFY(!plan.error.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(plan.expanded.isEmpty());
}

void PrintBatchTests::totalJobsCountsExpandedQueue() {
    const QVector<PrintBatchItem> items{makeItem(QStringLiteral("C:/docs/a.pdf"), 2),
                                        makeItem(QStringLiteral("C:/docs/b.pdf"), 1)};
    const PrintBatchPlan plan = planPrintBatch(items, PrintBatchOptions());
    QVERIFY(plan.ok);
    QCOMPARE(plan.totalJobs, 3);
    QCOMPARE(plan.totalJobs, plan.expanded.size());
}

void PrintBatchTests::expandedIdsAreUnique() {
    const QVector<PrintBatchItem> items{makeItem(QStringLiteral("C:/docs/a.pdf"), 2,
                                                 QStringLiteral("a")),
                                        makeItem(QStringLiteral("C:/docs/b.pdf"), 2,
                                                 QStringLiteral("b")),
                                        makeItem(QStringLiteral("C:/docs/c.pdf"), 1)};
    const PrintBatchPlan plan = planPrintBatch(items, PrintBatchOptions());
    QVERIFY(plan.ok);
    QCOMPARE(plan.expanded.size(), 5);

    QSet<QString> ids;
    for (const PrintBatchItem& item : plan.expanded) {
        QVERIFY(!item.id.isEmpty());
        QVERIFY2(!ids.contains(item.id), qPrintable(item.id));
        ids.insert(item.id);
    }
}

void PrintBatchTests::pageRangeIsKeptVerbatim() {
    const QVector<PrintBatchItem> items{makeItem(QStringLiteral("C:/docs/a.pdf"), 2,
                                                 QStringLiteral("a"), QStringLiteral("1-3,5"))};
    const PrintBatchPlan plan = planPrintBatch(items, PrintBatchOptions());
    QVERIFY(plan.ok);
    QCOMPARE(plan.expanded.size(), 2);
    for (const PrintBatchItem& item : plan.expanded) {
        QCOMPARE(item.pageRange, QStringLiteral("1-3,5"));
    }
}

void PrintBatchTests::skippedItemDoesNotCountTowardsTotalJobs() {
    const QVector<PrintBatchItem> items{makeItem(QString(), 5, QStringLiteral("empty")),
                                        makeItem(QStringLiteral("C:/docs/a.pdf"), 2,
                                                 QStringLiteral("a"))};
    const PrintBatchPlan plan = planPrintBatch(items, PrintBatchOptions());
    QVERIFY(plan.ok);
    // 空路径那一项连份数带路径一起跳过，不占任务数。
    QCOMPARE(plan.totalJobs, 2);
    QCOMPARE(plan.expanded.size(), 2);
}

void PrintBatchTests::emptyQueueWritesReasonToErrorOutParam() {
    QString error;
    const PrintBatchPlan plan = planPrintBatch({}, PrintBatchOptions(), &error);
    QVERIFY(!plan.ok);
    QVERIFY(!error.isEmpty());
    QCOMPARE(error, plan.error);
    // 原因必须是中文。
    bool hasChinese = false;
    for (const QChar character : error) {
        if (character.unicode() >= 0x4E00 && character.unicode() <= 0x9FFF) {
            hasChinese = true;
            break;
        }
    }
    QVERIFY2(hasChinese, qPrintable(error));
}

void PrintBatchTests::allItemsSkippedStillKeepsOtherWarnings() {
    const QVector<PrintBatchItem> items{makeItem(QString(), 1, QStringLiteral("one")),
                                        makeItem(QStringLiteral("   "), 1,
                                                 QStringLiteral("two"))};
    const PrintBatchPlan plan = planPrintBatch(items, PrintBatchOptions());
    // 全被跳过时不算规划失败（空路径不废整批），但一条任务也没有。
    QVERIFY(plan.ok);
    QCOMPARE(plan.expanded.size(), 0);
    QCOMPARE(plan.totalJobs, 0);
    QCOMPARE(plan.warnings.size(), 2);
}

QTEST_GUILESS_MAIN(PrintBatchTests)
#include "PrintBatchTests.moc"
