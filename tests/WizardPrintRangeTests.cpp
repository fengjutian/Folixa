// 动作向导「打印」这一步的页范围解析测试。
//
// 为什么单独测这一段：它是这一步里唯一带**输入解释**的部分
// （页范围字符串 → 页号区间），也是最容易出错的地方
// （1 基还是 0 基、闭区间还是半开、越界怎么办）。
//
// 其余几步（合并 / 加密 / 转格式）要么依赖 qpdf、要么要落盘，
// 属于集成路径，由 src/app 的界面验收脚本覆盖；
// 这里的用例只钉住纯解析逻辑。
#include "app/util/WizardPageRange.h"

#include <QTest>

namespace app = aipdf::app;

class WizardPrintRangeTests final : public QObject {
    Q_OBJECT

private slots:
    void singlePageBecomesOnePageRange();
    void closedIntervalCoversBothEnds();
    void commaSeparatedSegmentsAreMerged();
    void whitespaceIsIgnored();
    void outOfRangeIsClampedToDocument();
    void nonNumericIsRejected();
    void reversedIntervalIsRejected();
    void emptyRangeIsRejected();
    void documentWithoutPagesIsRejected();
    void nullOutputIsRejected();
};

void WizardPrintRangeTests::singlePageBecomesOnePageRange() {
    app::OutputRange range;
    QString error;
    QVERIFY(app::parseWizardPageRange(QStringLiteral("3"), 10, &range, &error));
    // 参数是 1 基，OutputRange 是 0 基。
    QCOMPARE(range.firstPage, 2);
    QCOMPARE(range.lastPage, 2);
}

void WizardPrintRangeTests::closedIntervalCoversBothEnds() {
    app::OutputRange range;
    QString error;
    QVERIFY(app::parseWizardPageRange(QStringLiteral("2-4"), 10, &range, &error));
    // 2-4 是闭区间：第 2、3、4 页 → 0 基的 1..3。
    QCOMPARE(range.firstPage, 1);
    QCOMPARE(range.lastPage, 3);
}

void WizardPrintRangeTests::commaSeparatedSegmentsAreMerged() {
    app::OutputRange range;
    QString error;
    QVERIFY(app::parseWizardPageRange(QStringLiteral("1-3,5"), 10, &range, &error));
    // 两段合成一个覆盖范围：从最早的那页到最晚的那页。
    QCOMPARE(range.firstPage, 0);
    QCOMPARE(range.lastPage, 4);
}

void WizardPrintRangeTests::whitespaceIsIgnored() {
    app::OutputRange range;
    QString error;
    QVERIFY(app::parseWizardPageRange(QStringLiteral(" 1 - 2 , 4 "), 10, &range, &error));
    QCOMPARE(range.firstPage, 0);
    QCOMPARE(range.lastPage, 3);
}

void WizardPrintRangeTests::outOfRangeIsClampedToDocument() {
    app::OutputRange range;
    QString error;
    // 「1-999」是「打印到最后一页」的意思，不该报错。
    QVERIFY(app::parseWizardPageRange(QStringLiteral("1-999"), 10, &range, &error));
    QCOMPARE(range.firstPage, 0);
    QCOMPARE(range.lastPage, 9);
    QVERIFY(error.isEmpty());

    // 起点小于 1 夹到 1。
    QVERIFY(app::parseWizardPageRange(QStringLiteral("0"), 10, &range, &error));
    QCOMPARE(range.firstPage, 0);
    QCOMPARE(range.lastPage, 0);
}

void WizardPrintRangeTests::nonNumericIsRejected() {
    app::OutputRange range;
    QString error;
    QVERIFY(!app::parseWizardPageRange(QStringLiteral("abc"), 10, &range, &error));
    QVERIFY(!error.isEmpty());
    // 原因要说清是哪儿不对，不能是空话。
    QVERIFY(error.contains(QStringLiteral("abc")));
}

void WizardPrintRangeTests::reversedIntervalIsRejected() {
    app::OutputRange range;
    QString error;
    // 5-2 是起止反了，不是「夹一下就好」。
    QVERIFY(!app::parseWizardPageRange(QStringLiteral("5-2"), 10, &range, &error));
    QVERIFY(!error.isEmpty());
}

void WizardPrintRangeTests::emptyRangeIsRejected() {
    app::OutputRange range;
    QString error;
    QVERIFY(!app::parseWizardPageRange(QString(), 10, &range, &error));
    QVERIFY(!error.isEmpty());
}

void WizardPrintRangeTests::documentWithoutPagesIsRejected() {
    app::OutputRange range;
    QString error;
    QVERIFY(!app::parseWizardPageRange(QStringLiteral("1"), 0, &range, &error));
    QVERIFY(!error.isEmpty());
}

void WizardPrintRangeTests::nullOutputIsRejected() {
    QString error;
    // 输出指针为空不能崩。
    QVERIFY(!app::parseWizardPageRange(QStringLiteral("1"), 10, nullptr, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(WizardPrintRangeTests)
#include "WizardPrintRangeTests.moc"
