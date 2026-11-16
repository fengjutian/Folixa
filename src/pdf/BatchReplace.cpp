// 批量替换：把多条替换规则按顺序应用到 PDF 的文本层上。
//
// 为什么替换语义放在领域层：大小写、全字匹配、正则、捕获组引用这些是
// 「编辑能力」的一部分，不是界面细节。界面只负责把用户的输入收成规则，
// 规则一旦收集完，「怎么改、改了几处」就不该再依赖界面层。
//
// 底层复用 ContentStream 的内容流改写：qpdf 打开文件、取页、合并内容流、
// 写回并重新序列化这一整套只在 ContentStream 里有一份实现，
// 这里只负责「每个文本操作数该怎么改」。

#include "pdf/BatchReplace.h"

#include "pdf/ContentStream.h"

#include <QRegularExpression>
#include <QStringList>

namespace aipdf::pdf {
namespace {

// 全字匹配的边界字符：字母、数字、下划线。
// 命中前后只要有一个这样的字符，就说明命中的是更长单词的一部分。
bool isWordCharacter(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// [start, end) 是否是一个完整单词的边界。
bool hasWholeWordBoundary(const QString& text, int start, int end) {
    if (start > 0 && isWordCharacter(text.at(start - 1))) return false;
    if (end < text.size() && isWordCharacter(text.at(end))) return false;
    return true;
}

// 展开替换文本里的捕获组引用：$1、${name}、\1 都认。
//
// 为什么自己展开而不用正则引擎自带的替换模板：QRegularExpression 的模板语法
// 只认 \1，编辑器里的用户习惯的是 $1；而且 \ 后面跟数字在「替换文本里本来
// 就有反斜杠」时会被误当成组引用。两种写法都支持、都不明确的按字面量输出，
// 行为才是可预期的。
QString expandReplacementTemplate(const QString& tmpl, const QRegularExpressionMatch& match) {
    QString out;
    const int n = tmpl.size();
    int i = 0;
    while (i < n) {
        const QChar c = tmpl.at(i);
        if ((c == QLatin1Char('$') || c == QLatin1Char('\\')) && i + 1 < n) {
            const QChar next = tmpl.at(i + 1);
            // ${name}：带花括号的具名组。
            if (next == QLatin1Char('{')) {
                const int close = tmpl.indexOf(QLatin1Char('}'), i + 2);
                if (close > 0) {
                    out += match.captured(tmpl.mid(i + 2, close - i - 2));
                    i = close + 1;
                    continue;
                }
            }
            // $12 / \12：优先按两位组号解释，组不存在时退回一位。
            if (next.isDigit()) {
                int digits = 0;
                int value = 0;
                while (digits < 2 && i + 1 + digits < n && tmpl.at(i + 1 + digits).isDigit()) {
                    value = value * 10 + tmpl.at(i + 1 + digits).digitValue();
                    ++digits;
                }
                int group = value;
                if (group > match.lastCapturedIndex() && digits == 2) {
                    group = value / 10;
                    digits = 1;
                }
                if (group > 0 && group <= match.lastCapturedIndex()) {
                    out += match.captured(group);
                    i += 1 + digits;
                    continue;
                }
                // 没有这个组：按字面量输出（$1 原样留下）。
                out += c;
                ++i;
                continue;
            }
            // $name：具名组。
            if (c == QLatin1Char('$') && next.isLetter()) {
                int end = i + 1;
                while (end < n && tmpl.at(end).isLetter()) ++end;
                out += match.captured(tmpl.mid(i + 1, end - i - 1));
                i = end;
                continue;
            }
            // \\：转义后的反斜杠，按字面量输出一个。
            if (c == QLatin1Char('\\') && next == QLatin1Char('\\')) {
                out += QLatin1Char('\\');
                i += 2;
                continue;
            }
        }
        out += c;
        ++i;
    }
    return out;
}

// 普通字符串规则：按大小写敏感性找全部命中并替换。
//
// 计数口径：只算「真的改了文本」的命中。替换文本与命中文本相同的命中不算，
// 否则统计出来的次数与文件实际变化对不上。
int applyLiteralRule(const ReplaceRule& rule, const QString& input, QString* output) {
    const Qt::CaseSensitivity sensitivity =
        rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QString result;
    int pos = 0;
    int count = 0;
    while (pos <= input.size()) {
        const int index = input.indexOf(rule.find, pos, sensitivity);
        if (index < 0) break;
        const int end = index + rule.find.size();
        if (rule.wholeWord && !hasWholeWordBoundary(input, index, end)) {
            // 不是完整单词：这一段原文原样留下，继续找下一个候选。
            // 注意这里必须把跳过的原文拷回去——只挪动 pos 会把中间的字符弄丢。
            result += input.mid(pos, index - pos);
            result += input.mid(index, rule.find.size());
            pos = end;
            continue;
        }
        const QString matched = input.mid(index, rule.find.size());
        result += input.mid(pos, index - pos);
        result += rule.replace;
        pos = end;
        if (matched != rule.replace) ++count;
    }
    result += input.mid(pos);
    *output = result;
    return count;
}

// 正则规则：逐个匹配并把替换文本里的捕获组引用展开。
int applyRegexRule(const ReplaceRule& rule, const QRegularExpression& regex, const QString& input,
                    QString* output) {
    QString result;
    int pos = 0;
    int count = 0;
    while (pos <= input.size()) {
        const QRegularExpressionMatch match = regex.match(input, pos);
        if (!match.hasMatch()) break;
        const int start = match.capturedStart(0);
        const int length = match.capturedLength(0);
        if (length <= 0) {
            // 空匹配：往前挪一位，否则会原地打转。
            pos = start + 1;
            continue;
        }
        if (rule.wholeWord && !hasWholeWordBoundary(input, start, start + length)) {
            // 不是完整单词：这一段原文原样留下，继续找下一个候选。
            result += input.mid(pos, start - pos);
            result += input.mid(start, length);
            pos = start + length;
            continue;
        }
        const QString expanded = expandReplacementTemplate(rule.replace, match);
        result += input.mid(pos, start - pos);
        result += expanded;
        pos = start + length;
        if (expanded != match.captured(0)) ++count;
    }
    result += input.mid(pos);
    *output = result;
    return count;
}

}  // namespace

QByteArray applyBatchReplace(const QByteArray& pdfBytes, const QVector<ReplaceRule>& rules,
                              const BatchReplaceOptions& options, BatchReplaceResult* result,
                              QString* error) {
    if (result) *result = BatchReplaceResult();
    if (error) error->clear();

    const auto fail = [&result, error](const QString& message) {
        if (result) {
            result->ok = false;
            result->error = message;
        }
        if (error) *error = message;
        return QByteArray();
    };

    if (pdfBytes.isEmpty()) return fail(QStringLiteral("输入的 PDF 内容为空"));
    if (rules.isEmpty()) return fail(QStringLiteral("没有替换规则"));

    if (result) {
        result->rules = rules.size();
        result->perRule.reserve(rules.size());
    }

    // 每条规则都在上一条的结果上继续改：用户排的顺序就是他想要的顺序。
    QByteArray current = pdfBytes;
    for (const ReplaceRule& rule : rules) {
        ReplaceRuleResult ruleResult;
        ruleResult.find = rule.find;

        // 规则本身是否合法。非法只影响这一条规则。
        QRegularExpression regex;
        if (rule.find.isEmpty()) {
            ruleResult.error = QStringLiteral("查找内容为空");
        } else if (rule.regex) {
            regex.setPattern(rule.find);
            if (!regex.isValid())
                ruleResult.error = QStringLiteral("正则表达式无效：%1").arg(regex.errorString());
        }

        if (!ruleResult.error.isEmpty()) {
            ruleResult.ok = false;
            if (result) result->perRule.append(ruleResult);
            if (result && result->error.isEmpty()) result->error = ruleResult.error;
            if (error && error->isEmpty()) *error = ruleResult.error;
            if (options.stopOnFirstError) return fail(ruleResult.error);
            continue;
        }

        int matched = 0;
        QString rewriteError;
        const QByteArray rewritten = ContentStream::rewritePdfTextWith(
            current, options.pageIndexes,
            [&](const QString& input) {
                QString output;
                const int count = rule.regex ? applyRegexRule(rule, regex, input, &output)
                                              : applyLiteralRule(rule, input, &output);
                matched += count;
                return output;
            },
            nullptr, &rewriteError);

        if (!rewriteError.isEmpty()) {
            ruleResult.ok = false;
            ruleResult.error = rewriteError;
            if (result) result->perRule.append(ruleResult);
            if (result && result->error.isEmpty()) result->error = rewriteError;
            if (error && error->isEmpty()) *error = rewriteError;
            if (options.stopOnFirstError) return fail(rewriteError);
            continue;
        }

        // 什么都没匹配上时底层返回空字节：这时保留上一轮的字节，
        // 「没找到」不是失败，文件内容必须保持等价。
        if (!rewritten.isEmpty()) current = rewritten;
        ruleResult.ok = true;
        ruleResult.replacements = matched;
        if (result) {
            result->perRule.append(ruleResult);
            result->replacements += matched;
        }
    }

    if (result) result->ok = result->error.isEmpty();
    return current;
}

}  // namespace aipdf::pdf
