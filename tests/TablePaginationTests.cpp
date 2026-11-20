#include "pdf/ops/TablePagination.h"

#include <QTest>

using aipdf::pdf::paginateTableRows;
using aipdf::pdf::TableRowChunk;

class TablePaginationTests : public QObject {
    Q_OBJECT

private slots:
    void splitsWhenTableIsTallerThanPage();
    void keepsWholeTableOnOnePageWhenItFits();
    void returnsEmptyWhenEvenOneRowDoesNotFit();
    void spacingCountsAgainstThePage();
};

void TablePaginationTests::splitsWhenTableIsTallerThanPage() {
    // 3 行 × 17pt，正文区高 40pt：第一页 2 行，第二页 1 行。
    const QVector<TableRowChunk> chunks = paginateTableRows(3, 17.0, 0.0, 40.0, 0.0, 0.0);
    QCOMPARE(chunks.size(), 2);
    QCOMPARE(chunks.at(0).firstRow, 0);
    QCOMPARE(chunks.at(0).rowCount, 2);
    QCOMPARE(chunks.at(0).height, 34.0);
    QVERIFY(!chunks.at(0).newPage);
    QCOMPARE(chunks.at(1).firstRow, 2);
    QCOMPARE(chunks.at(1).rowCount, 1);
    QVERIFY(chunks.at(1).newPage);
    // 行不能丢也不能重复。
    int total = 0;
    for (const TableRowChunk& chunk : chunks) total += chunk.rowCount;
    QCOMPARE(total, 3);
}

void TablePaginationTests::keepsWholeTableOnOnePageWhenItFits() {
    const QVector<TableRowChunk> chunks = paginateTableRows(2, 10.0, 0.0, 100.0, 0.0, 0.0);
    QCOMPARE(chunks.size(), 1);
    QCOMPARE(chunks.at(0).firstRow, 0);
    QCOMPARE(chunks.at(0).rowCount, 2);
    QVERIFY(!chunks.at(0).newPage);
}

void TablePaginationTests::returnsEmptyWhenEvenOneRowDoesNotFit() {
    const QVector<TableRowChunk> chunks = paginateTableRows(3, 50.0, 0.0, 40.0, 0.0, 0.0);
    QVERIFY(chunks.isEmpty());
}

void TablePaginationTests::spacingCountsAgainstThePage() {
    // 2 行 × 17 = 34，加上 10 的段间距就超过 40：第二行要换页。
    const QVector<TableRowChunk> chunks = paginateTableRows(3, 17.0, 0.0, 40.0, 0.0, 10.0);
    QVERIFY(chunks.size() >= 2);
    int total = 0;
    for (const TableRowChunk& chunk : chunks) total += chunk.rowCount;
    QCOMPARE(total, 3);
}

QTEST_MAIN(TablePaginationTests)
#include "TablePaginationTests.moc"
