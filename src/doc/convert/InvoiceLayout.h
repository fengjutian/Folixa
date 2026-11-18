#pragma once

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::doc {

// 发票的参与方（购买方 / 销售方）。
struct InvoiceParty {
    // 单位名称（开票方是「销售方」）。
    QString name;
    // 纳税人识别号（简称税号）。
    QString taxId;
    // 地址。
    QString address;
    // 电话。
    QString phone;
    // 开户行（可选）。
    QString bankName;
    // 银行账号（可选）。
    QString bankAccount;
};

// 发票的一个行项目。
//
// 金额一律用**整数分**存，不用 double：浮点累加会漂，
// 一张发票几十行，0.01 元的误差最后就是「合计对不上」，还查不出是哪一行。
struct InvoiceLineItem {
    // 名称（货物或服务名称）。不能为空。
    QString name;
    // 规格型号（可选）。
    QString specification;
    // 单位（可选，例如「次」「台」）。
    QString unit;
    // 数量：放大 1000 倍的整数，即最多 3 位小数。
    //
    // 为什么数量不用整数：数量经常是 1.5 台、0.25 公斤。
    // 为什么放大 1000 倍而不是用 double：0.1 + 0.2 != 0.3 这种事在发票上不能有。
    qint64 quantityMilli = 0;
    // 单价：整数分（12.50 元 = 1250）。
    qint64 unitPriceCents = 0;
    // 金额：整数分。必须等于「数量 × 单价」四舍五入到分的结果。
    qint64 amountCents = 0;
    // 税率（万分比，1300 = 13%）。-1 表示沿用发票上的税率。
    qint64 taxRateBasisPoints = -1;
};

// 一张发票。
struct Invoice {
    // 发票号码（可选）。
    QString invoiceNumber;
    // 抬头标题（可选，默认「发 票」）。
    QString title;
    // 销售方（开票方）。名称不能为空。
    InvoiceParty seller;
    // 购买方（可选）。
    InvoiceParty buyer;
    // 开票日期（可选）。
    QDate issueDate;
    // 行项目。至少要有一个。
    QVector<InvoiceLineItem> items;
    // 税率（万分比，1300 = 13%）。0 表示不计税。
    qint64 taxRateBasisPoints = 0;
    // 税额（整数分）。税率大于 0 时必须等于「合计 × 税率」四舍五入的结果。
    qint64 taxCents = 0;
    // 合计（整数分，不含税）。必须等于各行金额之和。
    qint64 totalCents = 0;
    // 金额大写。留空表示由排版层按合计自动生成。
    QString amountInWords;
};

// 校验结果。
struct InvoiceCheckResult {
    // 是否校验通过。
    bool ok = false;
    // 不含税合计（各行金额之和）。
    qint64 subtotalCents = 0;
    // 税额。
    qint64 taxCents = 0;
    // 价税合计（不含税合计 + 税额）。
    qint64 grandTotalCents = 0;
    // 金额大写。
    QString amountInWords;
    // 全部问题（中文，一行一条）。
    QStringList issues;
    // 第一条问题（中文）。issues 为空时它是空串。
    QString error;
};

// 版式选项。
struct InvoiceLayoutOptions {
    // 版式宽度（列数）。中文按两列算。
    //
    // 默认 78：常见的窄纸（80 列）打印出来正好不折行。
    int width = 78;
};

// 算一行项目的金额：数量 × 单价，四舍五入到分。
//
// 数量是放大 1000 倍的整数，单价是分，所以结果是「分 × 1000」再除以 1000。
// 全程整数运算；算不出来（溢出）时 ok 置为 false，调用方必须据此报错，
// 绝不能拿一个错的金额继续算下去。
qint64 invoiceLineAmountCents(qint64 quantityMilli, qint64 unitPriceCents, bool* ok = nullptr);

// 校验一张发票，把所有问题都列出来。
//
// 校验规则（金额全部用整数分比较，**容差取 0 分**）：
//   - 容差为什么是 0：金额已经用整数分表示了，任何一分之差都是真实的算术差异，
//     不是浮点噪声，所以不容忍；
//   - 每个行项目的金额必须等于「数量 × 单价」四舍五入到分的结果；
//   - 数量、单价、金额都不能为负数；
//   - 合计必须等于各行金额之和；
//   - 税率大于 0 时，税额必须等于「合计 × 税率」四舍五入的结果；税率为 0 时税额必须是 0。
//
// 返回值里的 subtotalCents / taxCents / grandTotalCents 在校验失败时也是算出来的值，
// 方便界面上先把数字摆出来给人看。
InvoiceCheckResult checkInvoice(const Invoice& invoice);

// 校验一张发票：不通过就返回 false，并把第一条中文原因写进 error。
bool validateInvoice(const Invoice& invoice, QString* error = nullptr);

// 人民币金额大写（零壹贰叁肆伍陆柒捌玖 拾佰仟 万亿 元角分整）。
//
// 支持范围：整数部分到「万亿」位（10^16 元）以内；超出返回空串
// （调用方应据此报错，而不是给出一个半截的大写）。
//
// 规则：
//   - 0 写作「零元整」；
//   - 负数前面加「负」；
//   - 角分全零写「整」；只有角没有分写「X角整」；只有分没有角写「X元X分」；
//   - 连续的零只写一个「零」。
QString rmbAmountInWords(qint64 cents);

// 排一份可打印的纯文本版式。
//
// 校验不通过时返回空串，并把原因写进 error——
// 数字对不上的发票不该被排出来给人看。
//
// 版式宽度固定（默认 78 列，中文按两列算）；字段放不下时按列宽截断并加省略号，
// 这样每一行的列位置始终对得上（宁可少几个字，也不能让表格歪掉）。
QString renderInvoiceText(const Invoice& invoice,
                           const InvoiceLayoutOptions& options = InvoiceLayoutOptions(),
                           QString* error = nullptr);

}  // namespace aipdf::doc
