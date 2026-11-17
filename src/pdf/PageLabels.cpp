#include "pdf/PageLabels.h"

#include <QPair>

#include <algorithm>

#ifdef AIPDF_WITH_QPDF

#include <qpdf/Buffer.hh>
#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>
#include <vector>
#endif

namespace aipdf::pdf {
namespace {

// 失败原因统一走这里。
//
// 为什么用中文直写而不是 tr()：这一层是领域层，翻译的锅在 app 层；
// 但错误信息会进日志、会显示给用户，所以必须是中文的、说人话的。
void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

// 把样式写法规整成规范形式（带前导斜杠的 "/D"、"/r" …）。
//
// 为什么一定要带斜杠：PDF 里的样式是一个**名字对象**，写出来必须是 "/S /r"。
// 少一个斜杠就成了「值是 r 的名字」，阅读器直接读不懂这一段。
// 调用方给 "D" 或 "/D" 都认；空串保持空串（= 沿用上一段）。
QString normalizeStyle(const QString& style) {
    const QString value = style.trimmed();
    if (value.isEmpty()) return QString();
    if (value.startsWith(QLatin1Char('/'))) return value;
    return QStringLiteral("/") + value;
}

// PDF 32000-1 Table 159 允许的五种样式。
bool isKnownStyle(const QString& normalized) {
    return normalized == QLatin1String("/D") || normalized == QLatin1String("/R") ||
           normalized == QLatin1String("/r") || normalized == QLatin1String("/A") ||
           normalized == QLatin1String("/a");
}

// 罗马数字（标准写法：4 是 IV 而不是 IIII）。
//
// 为什么上限 3999：罗马数字没有公认的 4000 以上写法，
// 硬拼出来的 "MMMM" 阅读器也不认，所以超范围时退回阿拉伯数字。
QString toRoman(int number) {
    if (number <= 0 || number > 3999) return QString();
    static const int values[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char* const symbols[] = {"M",  "CM", "D",  "CD", "C",  "XC", "L",
                                          "XL", "X",  "IX", "V",  "IV", "I"};
    QString out;
    for (int i = 0; i < 13; ++i) {
        while (number >= values[i]) {
            out += QLatin1String(symbols[i]);
            number -= values[i];
        }
    }
    return out;
}

// 字母编号。
//
// 这里用的是 **PDF 自己的规矩**，不是 Excel 的规矩：
// PDF 32000-1 Table 159 写的是 "A–Z, AA–ZZ, AAA–ZZZ"——
// 也就是说第 27 页是 "AA"、第 28 页是 "BB"（同一个字母重复两遍），
// 而不是 Excel 那种 "AA、AB、AC"。Acrobat 就是这么显示的，这里必须照抄。
QString toLetters(int number, QChar base) {
    if (number <= 0) return QString();
    int length = 1;
    while (number > 26 * length) ++length;
    const int first = 26 * (length - 1) + 1;
    const QChar letter(static_cast<ushort>(base.unicode() + (number - first) % 26));
    return QString(length, letter);
}

// 解析每一段实际用的样式：空样式沿用上一段；第一段没有样式 = 这一段不带编号。
//
// 为什么要在写之前先解析：沿用出来的样式如果不写进文件，
// 各家阅读器对「没有 /S」的解释可能不一样（有的按阿拉伯，有的只显示前缀），
// 显式写下去才能保证「界面里预览到的」和「阅读器里看到的」是同一个东西。
QVector<QString> resolveStyles(const QVector<PageLabelRange>& ranges) {
    QVector<QString> styles;
    styles.reserve(ranges.size());
    QString carried;
    for (int i = 0; i < ranges.size(); ++i) {
        const QString own = normalizeStyle(ranges.at(i).style);
        if (!own.isEmpty()) {
            carried = own;
            styles.append(own);
        } else if (i == 0) {
            styles.append(QString());
        } else {
            styles.append(carried);
        }
    }
    return styles;
}

// 按起点排序（稳定排序：起点相同的保持调用方给的先后次序）。
QVector<PageLabelRange> sortedRanges(QVector<PageLabelRange> ranges) {
    std::stable_sort(ranges.begin(), ranges.end(),
                     [](const PageLabelRange& lhs, const PageLabelRange& rhs) {
                         return lhs.startIndex < rhs.startIndex;
                     });
    return ranges;
}

// 每一段的起始号码（对应 PDF 里的 /St）。
//
// 规则：
//   * 第一段用 firstPageNumber；
//   * 后面某段如果**沿用同一套编号样式**，就接着上一段往下数——
//     上一段盖了多少页由起点差算出来（PDF 里一段盖到下一个起点为止）；
//   * 换了样式就从头数（1）：这正是「前言罗马数字、正文阿拉伯数字从 1 开始」的做法。
QVector<int> startNumbers(const QVector<PageLabelRange>& ranges, const QVector<QString>& styles,
                           int firstPageNumber) {
    QVector<int> numbers;
    numbers.reserve(ranges.size());
    for (int i = 0; i < ranges.size(); ++i) {
        if (i == 0) {
            numbers.append(qMax(1, firstPageNumber));
            continue;
        }
        const bool sameStyle = i < styles.size() && styles.at(i) == styles.at(i - 1);
        if (!sameStyle) {
            numbers.append(1);
            continue;
        }
        const int previousPages = ranges.at(i).startIndex - ranges.at(i - 1).startIndex;
        numbers.append(numbers.at(i - 1) + qMax(0, previousPages));
    }
    return numbers;
}

}  // namespace

QByteArray writePageLabels(const QByteArray& input, const PageLabelScheme& scheme, QString* error) {
    if (error) error->clear();
    if (input.isEmpty()) {
        setError(error, QStringLiteral("输入的 PDF 内容为空"));
        return {};
    }
#ifndef AIPDF_WITH_QPDF
    setError(error, QStringLiteral("当前构建没有启用 qpdf，无法写入页标签"));
    return {};
#else
    if (scheme.ranges.isEmpty()) {
        setError(error, QStringLiteral("没有可写的页码规则"));
        return {};
    }

    const QVector<PageLabelRange> ranges = sortedRanges(scheme.ranges);
    for (const PageLabelRange& range : ranges) {
        if (range.startIndex < 0) {
            setError(error, QStringLiteral("页码规则的起始页号不能为负数"));
            return {};
        }
        if (range.count < 0) {
            setError(error, QStringLiteral("页码规则的页数不能为负数"));
            return {};
        }
        const QString style = normalizeStyle(range.style);
        if (!style.isEmpty() && !isKnownStyle(style)) {
            setError(error, QStringLiteral("未知的页码样式：%1").arg(style));
            return {};
        }
    }

    const QVector<QString> styles = resolveStyles(ranges);
    const QVector<int> numbers = startNumbers(ranges, styles, scheme.firstPageNumber);

    try {
        QPDF pdf;
        pdf.processMemoryFile("page-labels.pdf", input.constData(), static_cast<size_t>(input.size()));

        QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            setError(error, QStringLiteral("PDF 的根对象不是字典"));
            return {};
        }

        // /Nums 里是「起点 标签字典」成对出现（PDF 32000-1 §12.4.2）。
        std::vector<QPDFObjectHandle> nums;
        nums.reserve(static_cast<size_t>(ranges.size()) * 2);
        for (int i = 0; i < ranges.size(); ++i) {
            const PageLabelRange& range = ranges.at(i);
            QPDFObjectHandle label = QPDFObjectHandle::newDictionary();
            if (!styles.at(i).isEmpty()) {
                label.replaceKey("/S", QPDFObjectHandle::newName(styles.at(i).toLatin1().constData()));
            }
            if (!range.prefix.isEmpty()) {
                // 用 Unicode 字符串写前缀：中文前缀（例如「第 页」）才不会变成乱码。
                label.replaceKey("/P",
                                 QPDFObjectHandle::newUnicodeString(range.prefix.toUtf8().constData()));
            }
            // /St 是这一段的起始号码：跟 pageLabelText 用的是同一套算法，
            // 所以界面里预览到的编号和文件里写下的编号必然一致。
            label.replaceKey("/St", QPDFObjectHandle::newInteger(numbers.at(i)));

            nums.push_back(QPDFObjectHandle::newInteger(range.startIndex));
            nums.push_back(pdf.makeIndirectObject(label));
        }

        QPDFObjectHandle pageLabels = QPDFObjectHandle::newDictionary();
        pageLabels.replaceKey("/Nums", QPDFObjectHandle::newArray(nums));
        catalog.replaceKey("/PageLabels", pdf.makeIndirectObject(pageLabels));

        QPDFWriter writer(pdf);
        // 关掉对象流：交叉引用表保持传统形态，出问题时人眼能直接看。
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setOutputMemory();
        writer.write();
        std::unique_ptr<Buffer> output(writer.getBuffer());
        if (!output || output->getSize() == 0) {
            setError(error, QStringLiteral("写出的 PDF 内容为空"));
            return {};
        }
        const QByteArray written(reinterpret_cast<const char*>(output->getBuffer()),
                                 static_cast<qsizetype>(output->getSize()));

        // 自检：重新打开读一遍，读回来的必须和写进去的一致。
        // 这一步挡住的是「写出了字节，但 /PageLabels 根本没生效」的静默失败。
        QString checkError;
        const QVector<PageLabelRange> check = readPageLabels(written, &checkError);
        if (check.size() != ranges.size()) {
            setError(error, QStringLiteral("写出的页标签读不回来：%1").arg(checkError));
            return {};
        }
        for (int i = 0; i < ranges.size(); ++i) {
            const PageLabelRange& expected = ranges.at(i);
            const PageLabelRange& actual = check.at(i);
            if (actual.startIndex != expected.startIndex || actual.style != styles.at(i) ||
                actual.prefix != expected.prefix) {
                setError(error, QStringLiteral("写出的页标签与规则不一致"));
                return {};
            }
        }
        return written;
    } catch (const std::exception& failure) {
        setError(error, QStringLiteral("写入页标签失败：%1").arg(QString::fromUtf8(failure.what())));
        return {};
    }
#endif
}

#ifdef AIPDF_WITH_QPDF
namespace {

// 从 /PageLabels 里把 (页号, 标签字典) 对收出来。
//
// /PageLabels 按规范是一个数树（PDF 32000-1 §7.9.7）：节点里有 /Nums 数组，
// 大文档还会带 /Kids 子节点，所以两处都要收。
// 递归深度有上限：坏文件里的数树可能很深，不设限会把自己栈爆掉。
void collectLabelPairs(const QPDFObjectHandle& node, QVector<QPair<int, QPDFObjectHandle>>* pairs,
                       int depth) {
    if (depth > 32) return;
    if (node.isArray()) {
        const std::vector<QPDFObjectHandle> items = node.getArrayAsVector();
        for (size_t i = 0; i + 1 < items.size(); i += 2) {
            if (!items.at(i).isInteger()) continue;
            pairs->append({static_cast<int>(items.at(i).getIntValue()), items.at(i + 1)});
        }
        return;
    }
    if (!node.isDictionary()) return;

    const QPDFObjectHandle nums = node.getKey("/Nums");
    if (nums.isArray()) {
        const std::vector<QPDFObjectHandle> items = nums.getArrayAsVector();
        for (size_t i = 0; i + 1 < items.size(); i += 2) {
            if (!items.at(i).isInteger()) continue;
            pairs->append({static_cast<int>(items.at(i).getIntValue()), items.at(i + 1)});
        }
    }
    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (kids.isArray()) {
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            collectLabelPairs(kid, pairs, depth + 1);
        }
    }
}

}  // namespace
#endif

QVector<PageLabelRange> readPageLabels(const QByteArray& input, QString* error) {
    if (error) error->clear();
    if (input.isEmpty()) {
        setError(error, QStringLiteral("输入的 PDF 内容为空"));
        return {};
    }
#ifndef AIPDF_WITH_QPDF
    setError(error, QStringLiteral("当前构建没有启用 qpdf，无法读取页标签"));
    return {};
#else
    try {
        QPDF pdf;
        pdf.processMemoryFile("page-labels-read.pdf", input.constData(),
                              static_cast<size_t>(input.size()));
        const QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            setError(error, QStringLiteral("PDF 的根对象不是字典"));
            return {};
        }
        const QPDFObjectHandle pageLabels = catalog.getKey("/PageLabels");
        // 没有 /PageLabels 是常态（绝大多数文档都没有），不是错误。
        if (pageLabels.isNull()) return {};

        QVector<QPair<int, QPDFObjectHandle>> pairs;
        collectLabelPairs(pageLabels, &pairs, 0);
        if (pairs.isEmpty()) return {};

        std::stable_sort(pairs.begin(), pairs.end(),
                         [](const QPair<int, QPDFObjectHandle>& lhs,
                            const QPair<int, QPDFObjectHandle>& rhs) {
                             return lhs.first < rhs.first;
                         });
        // 起点重复只留第一个（正常文件不会有，坏文件可能有）。
        QVector<QPair<int, QPDFObjectHandle>> unique;
        for (const QPair<int, QPDFObjectHandle>& pair : pairs) {
            if (!unique.isEmpty() && unique.last().first == pair.first) continue;
            unique.append(pair);
        }

        QVector<PageLabelRange> ranges;
        ranges.reserve(unique.size());
        for (int i = 0; i < unique.size(); ++i) {
            PageLabelRange range;
            range.startIndex = unique.at(i).first;
            // 覆盖多少页由下一个起点决定；最后一段是 0（一直到文档末尾）。
            range.count = (i + 1 < unique.size()) ? unique.at(i + 1).first - unique.at(i).first : 0;
            const QPDFObjectHandle& label = unique.at(i).second;
            if (label.isDictionary()) {
                const QPDFObjectHandle style = label.getKey("/S");
                if (style.isName()) range.style = QString::fromStdString(style.getName());
                const QPDFObjectHandle prefix = label.getKey("/P");
                if (prefix.isString()) {
                    range.prefix = QString::fromUtf8(prefix.getUTF8Value().c_str());
                }
            }
            ranges.append(range);
        }
        return ranges;
    } catch (const std::exception& failure) {
        setError(error, QStringLiteral("读取页标签失败：%1").arg(QString::fromUtf8(failure.what())));
        return {};
    }
#endif
}

QString pageLabelText(const QVector<PageLabelRange>& ranges, int pageIndex, int firstPageNumber) {
    if (ranges.isEmpty() || pageIndex < 0) return QString();
    const QVector<PageLabelRange> sorted = sortedRanges(ranges);

    // 落在没有规则的页上：所有段的起点都比它大 → 没有标签。
    int index = -1;
    for (int i = 0; i < sorted.size(); ++i) {
        if (sorted.at(i).startIndex <= pageIndex) {
            index = i;
        } else {
            break;
        }
    }
    if (index < 0) return QString();

    // 样式沿用规则与写入时一致：空样式沿用上一段；一路沿用下来还是空 = 不带编号。
    const QVector<QString> styles = resolveStyles(sorted);
    const QString style = styles.at(index);
    const QString prefix = sorted.at(index).prefix;
    if (style.isEmpty()) return prefix;

    // 号码：与写进文件的 /St 用同一套算法（同一套样式就接着数，换了样式就从头数）。
    const QVector<int> numbers = startNumbers(sorted, styles, firstPageNumber);
    const int number = numbers.at(index) + (pageIndex - sorted.at(index).startIndex);
    return prefix + formatPageNumber(style, number);
}

QString formatPageNumber(const QString& style, int number) {
    // PDF 的页码必须 ≥ 1。
    if (number < 1) return QString();
    const QString normalized = normalizeStyle(style);
    if (normalized == QLatin1String("/R")) {
        const QString roman = toRoman(number);
        return roman.isEmpty() ? QString::number(number) : roman;
    }
    if (normalized == QLatin1String("/r")) {
        const QString roman = toRoman(number);
        return roman.isEmpty() ? QString::number(number) : roman.toLower();
    }
    if (normalized == QLatin1String("/A")) return toLetters(number, QLatin1Char('A'));
    if (normalized == QLatin1String("/a")) return toLetters(number, QLatin1Char('a'));
    // "/D" 以及没给样式：都按阿拉伯数字处理。
    return QString::number(number);
}

QVector<QString> formatPageNumbers(const QString& style, int firstNumber, int count) {
    QVector<QString> result;
    if (count <= 0) return result;
    const int start = qMax(1, firstNumber);
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.append(formatPageNumber(style, start + i));
    }
    return result;
}

}  // namespace aipdf::pdf
