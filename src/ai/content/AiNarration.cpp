#include "ai/content/AiNarration.h"

#include "speech/SpeechText.h"

#include <QRegularExpression>
#include <QStringList>

#include <cmath>

namespace aipdf::ai {

namespace {

// 段的类型（界面上直接显示的中文文案）。
const QString kKindHeading = QStringLiteral("标题");
const QString kKindBody = QStringLiteral("正文");
const QString kKindListItem = QStringLiteral("列表项");
const QString kKindQuote = QStringLiteral("引用");

// 语速：中文播报大约每秒 4.5 个字。
constexpr double kCharsPerSecond = 4.5;

// 停顿（毫秒）。问句比陈述句长：听的人要反应过来。
constexpr int kPauseQuestion = 500;
constexpr int kPauseSentence = 300;
constexpr int kPauseComma = 200;
// 整篇念完最后一段再多停这么久（于是「段末」正好是 600）。
constexpr int kPauseEndOfPlan = 300;

// 短于这个字数的行才有资格当标题。
constexpr int kHeadingMaxChars = 24;

// 全角空格：PDF 抽出来的文本里很常见。
constexpr ushort kIdeographicSpace = 0x3000;

// 清洗与分类用的正则：放在一处，避免每次调用都重新编译一遍。
struct Patterns {
    QRegularExpression chinesePage;
    QRegularExpression englishPage;
    QRegularExpression dashedPage;
    QRegularExpression urlLine;
    QRegularExpression mailLine;
    QRegularExpression copyrightLine;
    QRegularExpression bulletLine;
    QRegularExpression orderedListLine;
    QRegularExpression separatorLine;
    QRegularExpression markdownHeading;
};

const Patterns& patterns() {
    static const Patterns value{
        // 「第 3 页」「第3页」「第 3 页（共 10 页）」
        QRegularExpression(QStringLiteral("^第\\s*\\d+\\s*页(?:\\s*(?:共|/|,|，)\\s*\\d+\\s*页)?$")),
        // 「Page 3」「Page 3 of 10」
        QRegularExpression(QStringLiteral("^page\\s*\\d+(?:\\s*(?:of|/)\\s*\\d+)?$"),
                           QRegularExpression::CaseInsensitiveOption),
        // 行首页码点号：「- 3 -」「– 3 –」
        QRegularExpression(QStringLiteral("^[-–—]\\s*\\d+\\s*[-–—]$")),
        // 整行网址 / 整行邮箱：页眉页脚最常见的两种残留
        QRegularExpression(QStringLiteral("^(?:https?://|www\\.)\\S+$"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("^\\S+@\\S+\\.\\S+$")),
        // 短版权行
        QRegularExpression(QStringLiteral("copyright|©|版权所有"),
                           QRegularExpression::CaseInsensitiveOption),
        // 无序列表项：- * + • ·
        QRegularExpression(QStringLiteral("^[-*+•·]\\s*")),
        // 有序列表项：1. 2) 3、
        // 为什么要留白：没有空格的话「3.5 版本」会被当成第 3 条，
        // 反而把正文切坏。
        QRegularExpression(QStringLiteral("^\\d+\\s*(?:[.)]\\s+|、\\s*)")),
        // 分隔线：----- ===== ____（PDF 里常见的表格框线）
        QRegularExpression(QStringLiteral("^[=\\-*_#]{3,}$")),
        // Markdown 标题：## 标题
        QRegularExpression(QStringLiteral("^#{1,6}\\s+"))};
    return value;
}

// 把一行里的连续空白压成一个空格。
//
// 为什么压：PDF 抽出来的文本里常有大段空格与全角空格，直接念会一顿一顿。
QString collapseSpaces(const QString& line) {
    QString out;
    out.reserve(line.size());
    bool pendingSpace = false;
    for (const QChar& unit : line) {
        const ushort code = unit.unicode();
        const bool isSpace = code == ' ' || code == '\t' || code == kIdeographicSpace;
        if (isSpace) {
            // 行首的空白直接丢掉，行中间的一律压成一个空格。
            pendingSpace = !out.isEmpty();
            continue;
        }
        if (pendingSpace) {
            out += QLatin1Char(' ');
            pendingSpace = false;
        }
        out += unit;
    }
    return out;
}

// 这一行是页码吗？
bool isPageNumberLine(const QString& line) {
    const Patterns& rules = patterns();
    return rules.chinesePage.match(line).hasMatch() ||
           rules.englishPage.match(line).hasMatch() ||
           rules.dashedPage.match(line).hasMatch();
}

// 这一行是页眉页脚的残留吗？
//
// 只认三种最典型的：整行网址、整行邮箱、短版权行。
// 认得越宽，越容易把正文里的真话当成页脚删掉，所以这里刻意保守。
bool isRunningHeaderLine(const QString& line) {
    const Patterns& rules = patterns();
    if (rules.urlLine.match(line).hasMatch()) {
        return true;
    }
    if (rules.mailLine.match(line).hasMatch()) {
        return true;
    }
    return line.size() <= 40 && rules.copyrightLine.match(line).hasMatch();
}

// 这一行只是分隔线吗（表格框线之类）？
bool isSeparatorLine(const QString& line) {
    return patterns().separatorLine.match(line).hasMatch();
}

// 这一行以标点结尾吗？
//
// 标题的启发式规则是「短行 + 不以标点结尾」；所以这里要把常见的
// 句末标点、逗号、括号、引号都算成「标点」。
bool endsWithPunctuation(const QString& text) {
    if (text.isEmpty()) {
        return false;
    }
    const ushort code = text.at(text.size() - 1).unicode();
    switch (code) {
    case 0x3002:  // 。
    case 0xFF01:  // ！
    case 0xFF1F:  // ？
    case 0xFF1B:  // ；
    case 0xFF1A:  // ：
    case 0xFF0C:  // ，
    case 0x3001:  // 、
    case 0x2026:  // …
    case 0x300D:  // 」
    case 0x300F:  // 』
    case 0x201D:  // ”
    case 0x2019:  // ’
    case '.':
    case '!':
    case '?':
    case ';':
    case ':':
    case ',':
    case ')':
    case '"':
    case '\'':
        return true;
    default:
        return false;
    }
}

// 一行的分类结果。
struct LineInfo {
    QString kind = kKindBody;
    QString text;
    bool emphasize = false;
};

// 判断一行是什么类型，并去掉不该念出来的标记。
//
// 顺序有讲究：先认引用、再认列表、再认 Markdown 标题，最后才用
// 「短行且不以标点结尾」这条启发式去猜标题——否则 "1. 要点" 会被
// 猜成标题，"- 要点" 也会被猜成标题。
LineInfo classifyLine(const QString& line) {
    LineInfo info;

    // 引用行：> 开头，或者中文里常见的破折号引文（—— 引文）。
    // 为什么认破折号：中文排版里的引用块常常就用破折号起头，
    // 认不出来就只会被当成一句普通正文。
    if (line.startsWith(QLatin1Char('>')) || line.startsWith(QStringLiteral("—")) ||
        line.startsWith(QStringLiteral("–"))) {
        info.kind = kKindQuote;
        QString quoteText = line.mid(1).trimmed();
        // 破折号常常成对出现（——），把剩下的破折号也去掉。
        while (quoteText.startsWith(QStringLiteral("—")) ||
               quoteText.startsWith(QStringLiteral("–"))) {
            quoteText = quoteText.mid(1).trimmed();
        }
        info.text = quoteText;
        return info;
    }

    // 列表项：- * + • · 或 1. 2) 3、
    const QRegularExpressionMatch bullet = patterns().bulletLine.match(line);
    if (bullet.hasMatch()) {
        info.kind = kKindListItem;
        info.text = line.mid(bullet.capturedLength()).trimmed();
        info.emphasize = true;
        return info;
    }
    const QRegularExpressionMatch ordered = patterns().orderedListLine.match(line);
    if (ordered.hasMatch()) {
        info.kind = kKindListItem;
        info.text = line.mid(ordered.capturedLength()).trimmed();
        info.emphasize = true;
        return info;
    }

    // Markdown 标题：## 标题
    const QRegularExpressionMatch heading = patterns().markdownHeading.match(line);
    if (heading.hasMatch()) {
        info.kind = kKindHeading;
        info.text = line.mid(heading.capturedLength()).trimmed();
        info.emphasize = true;
        return info;
    }

    // 启发式标题：短行且不以标点结尾。
    if (line.size() <= kHeadingMaxChars && !endsWithPunctuation(line)) {
        info.kind = kKindHeading;
        info.text = line;
        info.emphasize = true;
        return info;
    }

    info.kind = kKindBody;
    info.text = line;
    return info;
}

// 一段念完该停多久。
//
// 问句比陈述句长（听的人要反应过来）；以逗号收尾的段落还没说完，停一下继续。
int pauseFor(const QString& text) {
    if (text.isEmpty()) {
        return kPauseSentence;
    }
    const ushort code = text.at(text.size() - 1).unicode();
    if (code == '?' || code == 0xFF1F) {
        return kPauseQuestion;
    }
    if (code == ',' || code == 0xFF0C || code == 0x3001 || code == ';' || code == 0xFF1B ||
        code == ':' || code == 0xFF1A) {
        return kPauseComma;
    }
    return kPauseSentence;
}

}  // namespace

QString cleanTextForNarration(const QString& text) {
    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QStringList rawLines = normalized.split(QLatin1Char('\n'));
    QStringList kept;
    kept.reserve(rawLines.size());
    for (const QString& rawLine : rawLines) {
        const QString line = collapseSpaces(rawLine).trimmed();
        if (line.isEmpty()) {
            // 空行要留着：它决定段落的边界。
            kept.append(QString());
            continue;
        }
        if (isPageNumberLine(line) || isRunningHeaderLine(line) || isSeparatorLine(line)) {
            // 整行丢掉，不留空行占位：页码行被删掉之后不应该把段落切开。
            continue;
        }
        kept.append(line);
    }

    // 连续空行压成一个。
    QStringList compacted;
    compacted.reserve(kept.size());
    bool previousBlank = false;
    for (const QString& line : kept) {
        const bool blank = line.isEmpty();
        if (blank && previousBlank) {
            continue;
        }
        compacted.append(line);
        previousBlank = blank;
    }
    while (!compacted.isEmpty() && compacted.first().isEmpty()) {
        compacted.removeFirst();
    }
    while (!compacted.isEmpty() && compacted.last().isEmpty()) {
        compacted.removeLast();
    }
    return compacted.join(QLatin1Char('\n'));
}

NarrationPlan planNarration(const QString& text) {
    NarrationPlan plan;

    if (text.trimmed().isEmpty()) {
        plan.error = QStringLiteral("没有可朗读的文本");
        return plan;
    }

    const QString cleaned = cleanTextForNarration(text);
    if (cleaned.trimmed().isEmpty()) {
        // 例如整页只有一个页码：洗完什么都不剩。
        plan.error = QStringLiteral("清洗后没有可朗读的文本");
        return plan;
    }

    const QStringList lines = cleaned.split(QLatin1Char('\n'));
    QVector<NarrationSegment> segments;

    const auto appendSegment = [&segments](const QString& text, const QString& kind,
                                           bool emphasize) {
        if (text.isEmpty()) {
            return;
        }
        NarrationSegment segment;
        segment.index = segments.size();
        segment.text = text;
        segment.kind = kind;
        segment.emphasize = emphasize;
        segments.append(segment);
    };

    QStringList buffer;
    const auto flushBuffer = [&]() {
        if (buffer.isEmpty()) {
            return;
        }
        // 段内多行用空格连起来：英文单词不能被换行粘成一个词，
        // 而空白在朗读时本来就不出声（分词时会跳过）。
        appendSegment(buffer.join(QLatin1Char(' ')), kKindBody, false);
        buffer.clear();
    };

    for (const QString& line : lines) {
        if (line.trimmed().isEmpty()) {
            flushBuffer();
            continue;
        }
        const LineInfo info = classifyLine(line);
        if (info.kind == kKindBody) {
            buffer.append(line);
            continue;
        }
        // 标题、列表项、引用各自独立成段。
        flushBuffer();
        appendSegment(info.text, info.kind, info.emphasize);
    }
    flushBuffer();

    if (segments.isEmpty()) {
        plan.error = QStringLiteral("清洗后没有可朗读的文本");
        return plan;
    }

    // 停顿：按这一段怎么收尾决定；整篇最后一段再多停一会儿。
    for (int index = 0; index < segments.size(); ++index) {
        NarrationSegment& segment = segments[index];
        segment.pauseAfterMs = pauseFor(segment.text);
        if (index == segments.size() - 1) {
            segment.pauseAfterMs += kPauseEndOfPlan;
        }
    }

    // 总字数按「真正会念出来的字符」算：空白不念。
    // 复用朗读分词（splitIntoWords），它保证不丢字符也不改顺序，
    // 词长加起来就是实打实要念的字数。
    int totalChars = 0;
    for (const NarrationSegment& segment : segments) {
        const QVector<speech::SpeechWord> words = speech::splitIntoWords(segment.text);
        for (const speech::SpeechWord& word : words) {
            totalChars += word.length;
        }
    }

    plan.ok = true;
    plan.segments = segments;
    plan.totalChars = totalChars;
    plan.estimatedSeconds = static_cast<int>(std::ceil(totalChars / kCharsPerSecond));
    return plan;
}

}  // namespace aipdf::ai
