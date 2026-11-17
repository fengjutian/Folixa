#include "doc/InvoiceLayout.h"

#include <QChar>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <limits>

namespace aipdf::doc {
namespace {

// 失败统一走这里。
void setError(QString* error, const QString& reason) {
    if (error != nullptr) *error = reason;
}

// 一个字符占几列：东亚宽字符两列，其余一列。
//
// 为什么自己算：这一层是纯逻辑，不能依赖界面层的字体度量；
// 而中英文混排的发票版式必须按「列」对齐，宽度算错整张表就歪了。
int charWidth(QChar ch) {
    const ushort code = ch.unicode();
    if ((code >= 0x1100 && code <= 0x115F) ||   // 谚文字母
        (code >= 0x2E80 && code <= 0x303E) ||   // CJK 部首、标点
        (code >= 0x3041 && code <= 0x33FF) ||   // 假名、兼容字符
        (code >= 0x3400 && code <= 0x4DBF) ||   // CJK 扩展 A
        (code >= 0x4E00 && code <= 0x9FFF) ||   // CJK 基本区
        (code >= 0xA000 && code <= 0xA4CF) ||   // 彝文
        (code >= 0xAC00 && code <= 0xD7A3) ||   // 谚文音节
        (code >= 0xF900 && code <= 0xFAFF) ||   // CJK 兼容表意
        (code >= 0xFE30 && code <= 0xFE6F) ||   // CJK 兼容形式
        (code >= 0xFF00 && code <= 0xFF60) ||   // 全角形式
        (code >= 0xFFE0 && code <= 0xFFE6) ||   // 全角符号
        (code >= 0x20000 && code <= 0x3FFFD)) { // CJK 扩展 B 及以后
        return 2;
    }
    return 1;
}

// 文本占几列。
int displayWidth(const QString& text) {
    int width = 0;
    for (const QChar ch : text) width += charWidth(ch);
    return width;
}

// 截断到指定列数；放不下时末尾加省略号。
QString fitText(const QString& text, int width) {
    if (width <= 0) return QString();
    if (displayWidth(text) <= width) return text;
    QString out;
    int used = 0;
    for (const QChar ch : text) {
        const int w = charWidth(ch);
        if (used + w > width - 1) break; // 给省略号留一列
        out += ch;
        used += w;
    }
    out += QStringLiteral("…");
    return out;
}

// 截断并按对齐方式补空格，凑满 width 列。
QString cell(const QString& text, int width, Qt::Alignment align) {
    if (width <= 0) return QString();
    const QString fitted = fitText(text, width);
    const int padding = width - displayWidth(fitted);
    if (padding <= 0) return fitted;
    if (align & Qt::AlignRight) return QString(padding, QLatin1Char(' ')) + fitted;
    if (align & Qt::AlignHCenter) {
        const int left = padding / 2;
        return QString(left, QLatin1Char(' ')) + fitted + QString(padding - left, QLatin1Char(' '));
    }
    return fitted + QString(padding, QLatin1Char(' '));
}

// 居中一行。
QString centerText(const QString& text, int width) {
    return cell(text, width, Qt::AlignHCenter);
}

// 左右两栏拼一行。
QString twoColumns(const QString& left, const QString& right, int width) {
    const int leftWidth = width / 2 - 1;
    const int rightWidth = width - leftWidth - 1;
    return cell(left, leftWidth, Qt::AlignLeft) + QLatin1Char(' ') +
           cell(right, rightWidth, Qt::AlignLeft);
}

// a * b / divisor，四舍五入（远离零）。
//
// 全程整数：钱不能用浮点算。
// 溢出时返回 false——宁可报错，也不能给出一个算错的金额。
bool mulDivRound(qint64 a, qint64 b, qint64 divisor, qint64* out) {
    if (out == nullptr || divisor <= 0) return false;
    const qint64 min = std::numeric_limits<qint64>::min();
    if (a == min || b == min) return false;
    const qint64 absA = a < 0 ? -a : a;
    const qint64 absB = b < 0 ? -b : b;
    if (absA != 0 && absB > std::numeric_limits<qint64>::max() / absA) return false;
    const qint64 product = a * b;
    const qint64 half = divisor / 2;
    if (product > std::numeric_limits<qint64>::max() - half) return false;
    if (product < min + half) return false;
    const qint64 rounded =
        product >= 0 ? (product + half) / divisor : -((-product + half) / divisor);
    *out = rounded;
    return true;
}

// 绝对值（qint64 的最小值取绝对值会溢出，这里用无符号数绕开）。
quint64 absolute(qint64 value) {
    return value < 0 ? (~static_cast<quint64>(value) + 1ULL) : static_cast<quint64>(value);
}

// 分 → 「1234.56」这样的文本。
QString formatCents(qint64 cents) {
    const bool negative = cents < 0;
    const quint64 magnitude = absolute(cents);
    const QString text = QString::number(magnitude / 100) + QLatin1Char('.') +
                         QString::number(magnitude % 100).rightJustified(2, QLatin1Char('0'));
    return negative ? QStringLiteral("-") + text : text;
}

// 数量（放大 1000 倍）→ 「1.5」这样的文本，去掉末尾多余的零。
QString formatQuantity(qint64 milli) {
    const bool negative = milli < 0;
    const quint64 magnitude = absolute(milli);
    QString text = QString::number(magnitude / 1000);
    const int fraction = static_cast<int>(magnitude % 1000);
    if (fraction != 0) {
        QString decimals = QString::number(fraction).rightJustified(3, QLatin1Char('0'));
        while (decimals.endsWith(QLatin1Char('0'))) decimals.chop(1);
        text += QLatin1Char('.') + decimals;
    }
    return negative ? QStringLiteral("-") + text : text;
}

// 税率（万分比）→ 「13%」。
QString formatTaxRate(qint64 basisPoints) {
    if (basisPoints == 0) return QStringLiteral("0%");
    const bool negative = basisPoints < 0;
    const quint64 magnitude = absolute(basisPoints);
    QString text = QString::number(magnitude / 100);
    const int fraction = static_cast<int>(magnitude % 100);
    if (fraction != 0) {
        QString decimals = QString::number(fraction).rightJustified(2, QLatin1Char('0'));
        while (decimals.endsWith(QLatin1Char('0'))) decimals.chop(1);
        text += QLatin1Char('.') + decimals;
    }
    text += QLatin1Char('%');
    return negative ? QStringLiteral("-") + text : text;
}

// 金额大写用的数字与节单位。
const char* const kDigits[] = {"零", "壹", "贰", "叁", "肆", "伍", "陆", "柒", "捌", "玖"};
// 个、万、亿、万亿。
const char* const kSectionUnits[] = {"", "万", "亿", "万亿"};
// 支持到万亿位（10^16 元）；超出返回空串。
const int kMaxSections = 4;

// 0..9999 的大写（不含节单位）。
//
// 「零」的规矩：一段零只写一个零；段首的零不写（壹佰零伍，不是零壹佰零伍）。
QString sectionToWords(qint64 value) {
    static const int kDivisors[] = {1000, 100, 10, 1};
    static const char* const kUnits[] = {"仟", "佰", "拾", ""};
    QString out;
    bool pendingZero = false;
    for (int position = 0; position < 4; ++position) {
        const qint64 digit = (value / kDivisors[position]) % 10;
        if (digit == 0) {
            if (!out.isEmpty()) pendingZero = true;
            continue;
        }
        if (pendingZero) {
            out += QStringLiteral("零");
            pendingZero = false;
        }
        out += QString::fromUtf8(kDigits[digit]);
        out += QString::fromUtf8(kUnits[position]);
    }
    return out;
}

// 整数部分的大写。
//
// 分节处理（个、万、亿、万亿），节之间的「零」有两种来源：
//   - 中间整节为零（比如 100000001：亿与个之间空着万这一节）；
//   - 本节最高位为零而高位已有内容（比如 100010000：万这一节是 0010）。
// 两种情况都只补一个「零」——中文大写里连续的零只写一次。
QString integerToWords(qint64 value) {
    if (value == 0) return QStringLiteral("零");
    qint64 groups[kMaxSections] = {0, 0, 0, 0};
    qint64 rest = value;
    for (int i = 0; i < kMaxSections; ++i) {
        groups[i] = rest % 10000;
        rest /= 10000;
    }
    if (rest != 0) return QString(); // 超出支持范围

    QString out;
    bool emitted = false;
    bool pendingZero = false;
    for (int i = kMaxSections - 1; i >= 0; --i) {
        const qint64 section = groups[i];
        if (section == 0) {
            if (emitted) pendingZero = true;
            continue;
        }
        if (pendingZero || (emitted && section < 1000)) out += QStringLiteral("零");
        out += sectionToWords(section);
        if (i > 0) out += QString::fromUtf8(kSectionUnits[i]);
        emitted = true;
        pendingZero = false;
    }
    return out;
}

}  // namespace

qint64 invoiceLineAmountCents(qint64 quantityMilli, qint64 unitPriceCents, bool* ok) {
    if (ok != nullptr) *ok = false;
    qint64 result = 0;
    if (!mulDivRound(quantityMilli, unitPriceCents, 1000, &result)) return 0;
    if (ok != nullptr) *ok = true;
    return result;
}

QString rmbAmountInWords(qint64 cents) {
    if (cents == 0) return QStringLiteral("零元整");
    const bool negative = cents < 0;
    const quint64 magnitude = absolute(cents);
    const qint64 yuan = static_cast<qint64>(magnitude / 100);
    const int jiao = static_cast<int>((magnitude / 10) % 10);
    const int fen = static_cast<int>(magnitude % 10);

    const QString integerPart = integerToWords(yuan);
    if (integerPart.isEmpty()) return QString(); // 超出支持范围

    QString out = integerPart + QStringLiteral("元");
    if (jiao == 0 && fen == 0) {
        out += QStringLiteral("整");
    } else if (jiao != 0 && fen != 0) {
        out += QString::fromUtf8(kDigits[jiao]) + QStringLiteral("角");
        out += QString::fromUtf8(kDigits[fen]) + QStringLiteral("分");
    } else if (jiao != 0) {
        out += QString::fromUtf8(kDigits[jiao]) + QStringLiteral("角整");
    } else {
        // 只有分没有角：整数部分不为零时要补一个「零」（壹拾元零伍分）。
        // 整数部分本来就是零时不补（零元伍分）。
        if (yuan > 0) out += QStringLiteral("零");
        out += QString::fromUtf8(kDigits[fen]) + QStringLiteral("分");
    }
    return negative ? QStringLiteral("负") + out : out;
}

InvoiceCheckResult checkInvoice(const Invoice& invoice) {
    InvoiceCheckResult result;

    if (invoice.seller.name.trimmed().isEmpty()) {
        result.issues.append(QStringLiteral("销售方（开票方）名称不能为空"));
    }
    if (invoice.items.isEmpty()) {
        result.issues.append(QStringLiteral("发票没有任何行项目"));
    }

    // 逐行校验：金额必须等于「数量 × 单价」四舍五入到分的结果。
    qint64 subtotal = 0;
    for (int i = 0; i < invoice.items.size(); ++i) {
        const InvoiceLineItem& item = invoice.items.at(i);
        const QString position = QStringLiteral("第 %1 个行项目").arg(i + 1);
        if (item.name.trimmed().isEmpty()) {
            result.issues.append(QStringLiteral("%1的名称不能为空").arg(position));
            continue;
        }
        if (item.quantityMilli < 0) {
            result.issues.append(QStringLiteral("%1「%2」的数量不能为负数").arg(position, item.name));
        }
        if (item.unitPriceCents < 0) {
            result.issues.append(QStringLiteral("%1「%2」的单价不能为负数").arg(position, item.name));
        }
        if (item.amountCents < 0) {
            result.issues.append(
                QStringLiteral("%1「%2」的金额不能为负数（当前为 %3 元）")
                    .arg(position, item.name, formatCents(item.amountCents)));
        }

        bool amountOk = false;
        const qint64 expected = invoiceLineAmountCents(item.quantityMilli, item.unitPriceCents, &amountOk);
        if (!amountOk) {
            result.issues.append(QStringLiteral("%1「%2」的金额算不出来（数量乘单价溢出了）")
                                     .arg(position, item.name));
            continue;
        }
        if (item.amountCents != expected) {
            const qint64 difference = item.amountCents - expected;
            result.issues.append(QStringLiteral("%1「%2」的金额与数量×单价对不上：应为 %3 元，实际 %4 元，差额 %5 元")
                                       .arg(position, item.name, formatCents(expected),
                                            formatCents(item.amountCents), formatCents(difference)));
            continue;
        }
        subtotal += item.amountCents;
    }
    result.subtotalCents = subtotal;

    // 合计必须等于各行金额之和（容差 0 分：金额已经是整数分，任何差额都是真差异）。
    if (invoice.totalCents < 0) {
        result.issues.append(QStringLiteral("合计不能为负数（当前为 %1 元）").arg(formatCents(invoice.totalCents)));
    } else if (invoice.totalCents != subtotal) {
        const qint64 difference = invoice.totalCents - subtotal;
        result.issues.append(QStringLiteral("合计与各行金额之和不一致：应为 %1 元，实际 %2 元，差额 %3 元")
                                   .arg(formatCents(subtotal), formatCents(invoice.totalCents),
                                        formatCents(difference)));
    }
    // 税额：税率大于 0 时按「合计 × 税率」四舍五入到分。
    qint64 expectedTax = 0;
    if (invoice.taxRateBasisPoints > 0) {
        bool taxOk = false;
        if (!mulDivRound(invoice.totalCents, invoice.taxRateBasisPoints, 10000, &expectedTax)) {
            result.issues.append(QStringLiteral("税额算不出来（合计乘税率溢出了）"));
        } else if (invoice.taxCents != expectedTax) {
            const qint64 difference = invoice.taxCents - expectedTax;
            result.issues.append(QStringLiteral("税额与合计×税率对不上：应为 %1 元，实际 %2 元，差额 %3 元")
                                       .arg(formatCents(expectedTax), formatCents(invoice.taxCents),
                                            formatCents(difference)));
        }
    } else if (invoice.taxCents != 0) {
        result.issues.append(QStringLiteral("税率为 0 时税额必须是 0，当前为 %1 元")
                                   .arg(formatCents(invoice.taxCents)));
    }
    result.taxCents = invoice.taxCents;
    result.grandTotalCents = invoice.totalCents + invoice.taxCents;
    // 金额大写：调用方给了就用它，没给就按价税合计生成。
    if (!invoice.amountInWords.isEmpty()) {
        result.amountInWords = invoice.amountInWords;
    } else {
        result.amountInWords = rmbAmountInWords(result.grandTotalCents);
        if (result.amountInWords.isEmpty()) {
            result.issues.append(QStringLiteral("金额超出大写支持范围（最多 10^16 元）"));
        }
    }

    result.ok = result.issues.isEmpty();
    result.error = result.issues.isEmpty() ? QString() : result.issues.first();
    return result;
}

bool validateInvoice(const Invoice& invoice, QString* error) {
    if (error != nullptr) error->clear();
    const InvoiceCheckResult result = checkInvoice(invoice);
    if (result.ok) return true;
    setError(error, result.error);
    return false;
}

QString renderInvoiceText(const Invoice& invoice, const InvoiceLayoutOptions& options,
                           QString* error) {
    if (error != nullptr) error->clear();

    const InvoiceCheckResult check = checkInvoice(invoice);
    if (!check.ok) {
        setError(error, check.error);
        return QString();
    }

    const int width = qBound(48, options.width, 200);
    const QString rule = QString(width, QLatin1Char('='));
    const QString thin = QString(width, QLatin1Char('-'));

    // 明细表的列宽：按可用宽度按比例分配，最后一列兜底，保证总宽不超版式宽度。
    const int available = width - 5; // 5 个列间空格
    const int nameWidth = available * 22 / 77;
    const int specificationWidth = available * 9 / 77;
    const int unitWidth = available * 6 / 77;
    const int quantityWidth = available * 10 / 77;
    const int priceWidth = available * 12 / 77;
    const int amountWidth = available - nameWidth - specificationWidth - unitWidth -
                            quantityWidth - priceWidth;

    QStringList lines;
    lines.append(rule);
    lines.append(centerText(invoice.title.isEmpty() ? QStringLiteral("发 票") : invoice.title, width));
    lines.append(rule);

    const QString dateText = invoice.issueDate.isValid() ? invoice.issueDate.toString(Qt::ISODate)
                                                          : QString();
    lines.append(twoColumns(QStringLiteral("发票号码：%1").arg(invoice.invoiceNumber),
                             QStringLiteral("开票日期：%1").arg(dateText), width));
    lines.append(twoColumns(QStringLiteral("购买方名称：%1").arg(invoice.buyer.name),
                             QStringLiteral("销售方名称：%1").arg(invoice.seller.name), width));
    lines.append(twoColumns(QStringLiteral("购买方税号：%1").arg(invoice.buyer.taxId),
                             QStringLiteral("销售方税号：%1").arg(invoice.seller.taxId), width));
    lines.append(twoColumns(QStringLiteral("购买方地址电话：%1")
                                 .arg(QStringLiteral("%1 %2").arg(invoice.buyer.address,
                                                                invoice.buyer.phone).trimmed()),
                             QStringLiteral("销售方地址电话：%1")
                                 .arg(QStringLiteral("%1 %2").arg(invoice.seller.address,
                                                                invoice.seller.phone).trimmed()),
                             width));
    if (!invoice.seller.bankName.isEmpty() || !invoice.seller.bankAccount.isEmpty()) {
        lines.append(twoColumns(QStringLiteral("购买方开户行：%1").arg(invoice.buyer.bankName),
                                 QStringLiteral("销售方开户行：%1").arg(invoice.seller.bankName), width));
        lines.append(twoColumns(QStringLiteral("购买方账号：%1").arg(invoice.buyer.bankAccount),
                                 QStringLiteral("销售方账号：%1").arg(invoice.seller.bankAccount), width));
    }

    lines.append(thin);
    QString header = cell(QStringLiteral("项目名称"), nameWidth, Qt::AlignLeft) + QLatin1Char(' ');
    header += cell(QStringLiteral("规格"), specificationWidth, Qt::AlignLeft) + QLatin1Char(' ');
    header += cell(QStringLiteral("单位"), unitWidth, Qt::AlignLeft) + QLatin1Char(' ');
    header += cell(QStringLiteral("数量"), quantityWidth, Qt::AlignRight) + QLatin1Char(' ');
    header += cell(QStringLiteral("单价(元)"), priceWidth, Qt::AlignRight) + QLatin1Char(' ');
    header += cell(QStringLiteral("金额(元)"), amountWidth, Qt::AlignRight);
    lines.append(header);
    lines.append(thin);

    for (const InvoiceLineItem& item : invoice.items) {
        QString row = cell(item.name, nameWidth, Qt::AlignLeft) + QLatin1Char(' ');
        row += cell(item.specification, specificationWidth, Qt::AlignLeft) + QLatin1Char(' ');
        row += cell(item.unit, unitWidth, Qt::AlignLeft) + QLatin1Char(' ');
        row += cell(formatQuantity(item.quantityMilli), quantityWidth, Qt::AlignRight) + QLatin1Char(' ');
        row += cell(formatCents(item.unitPriceCents), priceWidth, Qt::AlignRight) + QLatin1Char(' ');
        row += cell(formatCents(item.amountCents), amountWidth, Qt::AlignRight);
        lines.append(row);
    }

    lines.append(thin);
    lines.append(QStringLiteral("不含税合计：%1 元").arg(formatCents(check.subtotalCents)));
    lines.append(QStringLiteral("税额（%1）：%2 元")
                     .arg(formatTaxRate(invoice.taxRateBasisPoints), formatCents(check.taxCents)));
    lines.append(QStringLiteral("价税合计：%1 元").arg(formatCents(check.grandTotalCents)));
    lines.append(QStringLiteral("金额大写：%1").arg(check.amountInWords));
    lines.append(rule);

    return lines.join(QLatin1Char('\n'));
}

}  // namespace aipdf::doc
