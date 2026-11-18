#include "doc/convert/InvoiceLayout.h"

#include <QDate>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

using aipdf::doc::Invoice;
using aipdf::doc::InvoiceCheckResult;
using aipdf::doc::InvoiceLayoutOptions;
using aipdf::doc::InvoiceLineItem;
using aipdf::doc::InvoiceParty;
using aipdf::doc::checkInvoice;
using aipdf::doc::invoiceLineAmountCents;
using aipdf::doc::renderInvoiceText;
using aipdf::doc::rmbAmountInWords;
using aipdf::doc::validateInvoice;

namespace {

// 一张合法的发票：技术服务 2 次 × 1000.00 元 + 软件许可 1.5 套 × 666.67 元。
//
// 第二行故意用 1.5 这种带小数的数量：1.5 × 666.67 = 1000.005 元，
// 按分四舍五入是 1000.01 元（100001 分）——这里就是要验这个舍入。
Invoice makeInvoice() {
    Invoice invoice;
    invoice.invoiceNumber = QStringLiteral("00000001");
    invoice.issueDate = QDate(2026, 1, 31);

    InvoiceParty seller;
    seller.name = QStringLiteral("示例科技有限公司");
    seller.taxId = QStringLiteral("91110000MA0000000X");
    seller.address = QStringLiteral("北京市海淀区示例路 1 号");
    seller.phone = QStringLiteral("010-12345678");
    invoice.seller = seller;

    InvoiceParty buyer;
    buyer.name = QStringLiteral("示例采购有限公司");
    buyer.taxId = QStringLiteral("91110000MA0000000Y");
    buyer.address = QStringLiteral("上海市浦东新区示例路 2 号");
    buyer.phone = QStringLiteral("021-87654321");
    invoice.buyer = buyer;

    InvoiceLineItem service;
    service.name = QStringLiteral("技术服务");
    service.unit = QStringLiteral("次");
    service.quantityMilli = 2000;
    service.unitPriceCents = 100000;
    service.amountCents = 200000;
    invoice.items.append(service);

    InvoiceLineItem license;
    license.name = QStringLiteral("软件许可");
    license.unit = QStringLiteral("套");
    license.quantityMilli = 1500;
    license.unitPriceCents = 66667;
    license.amountCents = 100001;
    invoice.items.append(license);

    // 不含税合计：2000.00 + 1000.01 = 3000.01 元。
    invoice.totalCents = 300001;
    return invoice;
}

}  // namespace

class InvoiceLayoutTests : public QObject {
    Q_OBJECT

private slots:
    void validInvoicePassesCheck();
    void emptySellerNameIsRejected();
    void noLineItemsIsRejected();
    void lineAmountMustMatchQuantityTimesPrice();
    void lineAmountMismatchReportsDifference();
    void lineAmountRoundsHalfUp();
    void totalMustEqualSumOfLines();
    void totalMismatchReportsDifference();
    void negativeAmountIsRejected();
    void negativeQuantityIsRejected();
    void negativeUnitPriceIsRejected();
    void negativeTotalIsRejected();
    void taxMustMatchRate();
    void zeroRateRequiresZeroTax();
    void amountInWordsZero();
    void amountInWordsOnlyFen();
    void amountInWordsOnlyJiao();
    void amountInWordsJiaoAndFen();
    void amountInWordsNegative();
    void amountInWordsHundredMillion();
    void amountInWordsBillion();
    void amountInWordsBeyondSupportIsEmpty();
    void renderProducesFixedWidthLayout();
    void renderShowsEveryPart();
    void renderFailsForInvalidInvoice();

private:
    // 金额文本里的数字部分（把「元」去掉，方便断言）。
    static QString yuanOf(const QString& text) {
        return text;
    }
};

void InvoiceLayoutTests::validInvoicePassesCheck() {
    const Invoice invoice = makeInvoice();
    QString error;
    QVERIFY2(validateInvoice(invoice, &error), qPrintable(error));
    QVERIFY(error.isEmpty());

    const InvoiceCheckResult result = checkInvoice(invoice);
    QVERIFY(result.ok);
    QCOMPARE(result.subtotalCents, qint64(300001));
    QCOMPARE(result.taxCents, qint64(0));
    QCOMPARE(result.grandTotalCents, qint64(300001));
    QVERIFY(result.issues.isEmpty());
}

void InvoiceLayoutTests::emptySellerNameIsRejected() {
    Invoice invoice = makeInvoice();
    invoice.seller.name = QString();
    QString error;
    QVERIFY(!validateInvoice(invoice, &error));
    QVERIFY(error.contains(QStringLiteral("销售方")));
}

void InvoiceLayoutTests::noLineItemsIsRejected() {
    Invoice invoice = makeInvoice();
    invoice.items.clear();
    invoice.totalCents = 0;
    QString error;
    QVERIFY(!validateInvoice(invoice, &error));
    QVERIFY(error.contains(QStringLiteral("行项目")));
}

void InvoiceLayoutTests::lineAmountMustMatchQuantityTimesPrice() {
    Invoice invoice = makeInvoice();
    // 第一行改成 2 次 × 1000.00 元 = 2000.00 元，却写成 1999.00 元。
    invoice.items[0].amountCents = 199900;
    QString error;
    QVERIFY(!validateInvoice(invoice, &error));
    // 报错要说清是「数量 × 单价」对不上。
    QVERIFY(error.contains(QStringLiteral("数量×单价")));
    QVERIFY(error.contains(QStringLiteral("技术服务")));
}

void InvoiceLayoutTests::lineAmountMismatchReportsDifference() {
    Invoice invoice = makeInvoice();
    invoice.items[0].amountCents = 199900;
    const InvoiceCheckResult result = checkInvoice(invoice);
    QVERIFY(!result.ok);
    // 差额必须写出来：1999.00 - 2000.00 = -1.00 元。
    QVERIFY(result.error.contains(QStringLiteral("-1.00")));
    QVERIFY(result.error.contains(QStringLiteral("2000.00")));
    QVERIFY(result.error.contains(QStringLiteral("1999.00")));
}

void InvoiceLayoutTests::lineAmountRoundsHalfUp() {
    bool ok = false;
    // 1.5 × 666.67 = 1000.005 元 → 1000.01 元。
    QCOMPARE(invoiceLineAmountCents(1500, 66667, &ok), qint64(100001));
    QVERIFY(ok);
    // 0.333 × 10.00 = 3.33 元。
    QCOMPARE(invoiceLineAmountCents(333, 1000, &ok), qint64(333));
    QVERIFY(ok);
    // 2.5 × 1.00 = 2.50 元。
    QCOMPARE(invoiceLineAmountCents(2500, 100, &ok), qint64(250));
    QVERIFY(ok);
}

void InvoiceLayoutTests::totalMustEqualSumOfLines() {
    Invoice invoice = makeInvoice();
    invoice.totalCents = 300000; // 少 1 分
    QString error;
    QVERIFY(!validateInvoice(invoice, &error));
    QVERIFY(error.contains(QStringLiteral("合计")));
}

void InvoiceLayoutTests::totalMismatchReportsDifference() {
    Invoice invoice = makeInvoice();
    invoice.totalCents = 310001; // 多 10000 分 = 100.00 元
    const InvoiceCheckResult result = checkInvoice(invoice);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("不一致")));
    // 差额必须是 100.00 元。
    QVERIFY(result.error.contains(QStringLiteral("100.00")));
    QVERIFY(result.error.contains(QStringLiteral("3000.01")));
}

void InvoiceLayoutTests::negativeAmountIsRejected() {
    Invoice invoice = makeInvoice();
    invoice.items[0].amountCents = -100;
    QString error;
    QVERIFY(!validateInvoice(invoice, &error));
    QVERIFY(error.contains(QStringLiteral("不能为负数")));
}

void InvoiceLayoutTests::negativeQuantityIsRejected() {
    Invoice invoice = makeInvoice();
    invoice.items[0].quantityMilli = -2000;
    QString error;
    QVERIFY(!validateInvoice(invoice, &error));
    QVERIFY(error.contains(QStringLiteral("数量不能为负数")));
}

void InvoiceLayoutTests::negativeUnitPriceIsRejected() {
    Invoice invoice = makeInvoice();
    invoice.items[0].unitPriceCents = -1;
    QString error;
    QVERIFY(!validateInvoice(invoice, &error));
    QVERIFY(error.contains(QStringLiteral("单价不能为负数")));
}

void InvoiceLayoutTests::negativeTotalIsRejected() {
    Invoice invoice = makeInvoice();
    invoice.totalCents = -1;
    QString error;
    QVERIFY(!validateInvoice(invoice, &error));
    QVERIFY(error.contains(QStringLiteral("合计不能为负数")));
}

void InvoiceLayoutTests::taxMustMatchRate() {
    Invoice invoice = makeInvoice();
    invoice.taxRateBasisPoints = 1300; // 13%
    invoice.taxCents = 39000;         // 3000.01 × 13% = 390.00 元
    QString error;
    QVERIFY2(validateInvoice(invoice, &error), qPrintable(error));

    const InvoiceCheckResult result = checkInvoice(invoice);
    QCOMPARE(result.taxCents, qint64(39000));
    QCOMPARE(result.grandTotalCents, qint64(339001));

    // 税额算错了必须报错，并说清差多少（380.00 与 390.00 差 10.00 元）。
    invoice.taxCents = 38000;
    const InvoiceCheckResult wrong = checkInvoice(invoice);
    QVERIFY(!wrong.ok);
    QVERIFY(wrong.error.contains(QStringLiteral("税额")));
    QVERIFY(wrong.error.contains(QStringLiteral("390.00")));
    QVERIFY(wrong.error.contains(QStringLiteral("10.00")));
}

void InvoiceLayoutTests::zeroRateRequiresZeroTax() {
    Invoice invoice = makeInvoice();
    invoice.taxRateBasisPoints = 0;
    invoice.taxCents = 100;
    QString error;
    QVERIFY(!validateInvoice(invoice, &error));
    QVERIFY(error.contains(QStringLiteral("税率为 0")));
}

void InvoiceLayoutTests::amountInWordsZero() {
    QCOMPARE(rmbAmountInWords(0), QStringLiteral("零元整"));
}

void InvoiceLayoutTests::amountInWordsOnlyFen() {
    // 0.05 元。
    QCOMPARE(rmbAmountInWords(5), QStringLiteral("零元伍分"));
    // 12.34 元。
    QCOMPARE(rmbAmountInWords(1234), QStringLiteral("壹拾贰元叁角肆分"));
}

void InvoiceLayoutTests::amountInWordsOnlyJiao() {
    // 0.50 元：角有、分没有，末尾写「整」。
    QCOMPARE(rmbAmountInWords(50), QStringLiteral("零元伍角整"));
    // 10.50 元。
    QCOMPARE(rmbAmountInWords(1050), QStringLiteral("壹拾元伍角整"));
}

void InvoiceLayoutTests::amountInWordsJiaoAndFen() {
    QCOMPARE(rmbAmountInWords(123456), QStringLiteral("壹仟贰佰叁拾肆元伍角陆分"));
    QCOMPARE(rmbAmountInWords(100000), QStringLiteral("壹仟元整"));
}

void InvoiceLayoutTests::amountInWordsNegative() {
    QCOMPARE(rmbAmountInWords(-1234), QStringLiteral("负壹拾贰元叁角肆分"));
    QCOMPARE(rmbAmountInWords(-50), QStringLiteral("负零元伍角整"));
}

void InvoiceLayoutTests::amountInWordsHundredMillion() {
    // 100 万元。
    QCOMPARE(rmbAmountInWords(100000000), QStringLiteral("壹佰万元整"));
    // 1 亿元。
    QCOMPARE(rmbAmountInWords(10000000000), QStringLiteral("壹亿元整"));
    // 100 亿元。
    QCOMPARE(rmbAmountInWords(1000000000000), QStringLiteral("壹佰亿元整"));
}

void InvoiceLayoutTests::amountInWordsBillion() {
    // 1000 万：中间空着「万」这一节，要补一个零。
    QCOMPARE(rmbAmountInWords(1000000000), QStringLiteral("壹仟万元整"));
    // 10001 元：万位是 0，个位非零，中间补零。
    QCOMPARE(rmbAmountInWords(1000100), QStringLiteral("壹万零壹元整"));
}

void InvoiceLayoutTests::amountInWordsBeyondSupportIsEmpty() {
    // 10^18 分 = 10^16 元：超出万亿位，返回空串而不是半截的大写。
    QVERIFY(rmbAmountInWords(Q_INT64_C(1000000000000000000)).isEmpty());
}

void InvoiceLayoutTests::renderProducesFixedWidthLayout() {
    const Invoice invoice = makeInvoice();
    QString error;
    const QString text = renderInvoiceText(invoice, InvoiceLayoutOptions(), &error);
    QVERIFY2(!text.isEmpty(), qPrintable(error));

    const QStringList lines = text.split(QLatin1Char('\n'));
    QVERIFY(lines.size() >= 10);
    // 分隔线正好是版式宽度。
    QCOMPARE(lines.first().size(), 78);
    QCOMPARE(lines.at(2).size(), 78);
    QCOMPARE(lines.last().size(), 78);
    // 每一行都不能超出版式宽度。
    for (const QString& line : lines) {
        QVERIFY2(line.size() <= 78, qPrintable(line));
    }
}

void InvoiceLayoutTests::renderShowsEveryPart() {
    const Invoice invoice = makeInvoice();
    QString error;
    const QString text = renderInvoiceText(invoice, InvoiceLayoutOptions(), &error);
    QVERIFY2(!text.isEmpty(), qPrintable(error));

    // 抬头、日期、每个行项目、合计与大写都要一眼看得到。
    QVERIFY(text.contains(QStringLiteral("示例科技有限公司")));
    QVERIFY(text.contains(QStringLiteral("示例采购有限公司")));
    QVERIFY(text.contains(QStringLiteral("2026-01-31")));
    QVERIFY(text.contains(QStringLiteral("技术服务")));
    QVERIFY(text.contains(QStringLiteral("软件许可")));
    QVERIFY(text.contains(QStringLiteral("3000.01")));
    QVERIFY(text.contains(QStringLiteral("叁仟元零壹分")));
    QVERIFY(text.contains(QStringLiteral("金额大写")));
    QVERIFY(text.contains(QStringLiteral("合计")));
}

void InvoiceLayoutTests::renderFailsForInvalidInvoice() {
    Invoice invoice = makeInvoice();
    invoice.totalCents = 1; // 与行项之和对不上
    QString error;
    const QString text = renderInvoiceText(invoice, InvoiceLayoutOptions(), &error);
    QVERIFY(text.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("不一致")));
}

QTEST_GUILESS_MAIN(InvoiceLayoutTests)
#include "InvoiceLayoutTests.moc"
