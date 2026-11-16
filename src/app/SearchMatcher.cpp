#include "app/SearchMatcher.h"

#include <QChar>
#include <QRegularExpression>

namespace aipdf::app {

bool mapTextMatchToCharacterRange(const QVector<int>& utf16UnitsPerCharacter,
                                  const TextMatch& match,
                                  int* startCharacter,
                                  int* endCharacter) {
    if (match.at < 0 || match.length <= 0 || startCharacter == nullptr ||
        endCharacter == nullptr) {
        return false;
    }

    int position = 0;
    int start = -1;
    int end = -1;
    const int matchEnd = match.at + match.length;
    for (int character = 0; character < utf16UnitsPerCharacter.size(); ++character) {
        const int units = qMax(1, utf16UnitsPerCharacter.at(character));
        if (position == match.at) {
            start = character;
        }
        position += units;
        if (position == matchEnd) {
            end = character + 1;
            break;
        }
        if ((start < 0 && position > match.at) || (start >= 0 && position > matchEnd)) {
            return false;
        }
    }
    if (start < 0 || end <= start) {
        return false;
    }
    *startCharacter = start;
    *endCharacter = end;
    return true;
}

namespace {

// 全字匹配的边界判定。
//
// 只把 ASCII 侧的字母数字当"词字符"。中文没有空格分词，如果按 Unicode 的
// isLetterOrNumber 判，'与合同' 里的 '与' 会把 '合同' 判成词的一部分，
// 结果是中文全字匹配几乎永远搜不到东西。代价是 "合同法" 里的 "合同" 也会命中——
// 要做对需要分词，那是另一个量级的工程，界面上写清楚"全字匹配面向西文"即可。
bool blocksWordBoundary(const QString& plain, int position) {
    if (position < 0 || position >= plain.size()) {
        return false;  // 文本首尾算边界
    }
    const QChar character = plain.at(position);
    if (character.unicode() >= 0x2E80) {
        return false;  // CJK 及更靠后的区段不参与边界判定
    }
    return character.isLetterOrNumber() || character == QLatin1Char('_');
}

}  // namespace

QVector<TextMatch> findTextMatches(const QString& plain,
                                   const QString& query,
                                   const SearchOptions& options) {
    QVector<TextMatch> matches;
    if (plain.isEmpty() || query.isEmpty()) {
        return matches;
    }

    const Qt::CaseSensitivity sensitivity =
        options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    if (options.regex) {
        QRegularExpression expression(
            query, options.caseSensitive ? QRegularExpression::NoPatternOption
                                         : QRegularExpression::CaseInsensitiveOption);
        if (!expression.isValid()) {
            return matches;  // 写坏的正则不给命中，也不弹窗：搜索框里最忌讳打断输入
        }
        int from = 0;
        while (from <= plain.size()) {
            const QRegularExpressionMatch match = expression.match(plain, from);
            if (!match.hasMatch()) {
                break;
            }
            if (match.capturedLength() > 0) {
                matches.append(TextMatch{ static_cast<int>(match.capturedStart()),
                                          static_cast<int>(match.capturedLength()) });
            }
            // 零长度匹配（如 "a*"）不前进就会死循环，强制至少走一格。
            from = static_cast<int>(match.capturedEnd()) + (match.capturedLength() == 0 ? 1 : 0);
        }
        return matches;
    }

    int from = 0;
    while (from <= plain.size() - query.size()) {
        const int at = plain.indexOf(query, from, sensitivity);
        if (at < 0) {
            break;
        }

        const bool startsAtBoundary = !blocksWordBoundary(plain, at - 1);
        const bool endsAtBoundary = !blocksWordBoundary(plain, at + query.size());
        if (!options.wholeWord || (startsAtBoundary && endsAtBoundary)) {
            matches.append(TextMatch{ at, query.size() });
            from = at + qMax(1, query.size());
        } else {
            // 不满足全字匹配时只前进一个字符：像 "aa" 搜 "a" 这种重叠情形
            // 不能直接跳过整个关键词，否则会漏掉后面的合法命中。
            from = at + 1;
        }
    }
    return matches;
}

}  // namespace aipdf::app
