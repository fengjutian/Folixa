// 检索匹配规则的单元测试。
//
// 这两条规则（区分大小写、全字匹配）看着简单，实际最容易在两种地方出错：
//   1) 全字匹配把中文当成"非单词字符"，于是搜"合同"会在"合同法"里命中也算过；
//   2) 不满足条件时直接跳过整个关键词，漏掉重叠命中（"aa" 里搜 "a"）。

#include "app/SearchMatcher.h"

#include <QTest>

using aipdf::app::findTextMatches;
using aipdf::app::SearchOptions;
using aipdf::app::TextMatch;

namespace {

QVector<TextMatch> run(const QString& text, const QString& query, bool caseSensitive = false,
                       bool wholeWord = false) {
    SearchOptions options;
    options.caseSensitive = caseSensitive;
    options.wholeWord = wholeWord;
    return findTextMatches(text, query, options);
}

}  // namespace

class SearchMatcherTests : public QObject {
    Q_OBJECT

private slots:
    void emptyInputsProduceNothing();
    void defaultIsCaseInsensitive();
    void caseSensitiveHonoursCase();
    void wholeWordSkipsSubstrings();
    void wholeWordTreatsCjkAsWordCharacters();
    void wholeWordMatchesAtTextEdges();
    void wholeWordRejectsInnerLetters();
    void countsEveryOccurrence();
    void regexMatchesPatterns();
    void invalidRegexYieldsNothing();
};

void SearchMatcherTests::emptyInputsProduceNothing() {
    QVERIFY(run(QString(), QStringLiteral("a")).isEmpty());
    QVERIFY(run(QStringLiteral("abc"), QString()).isEmpty());
}

void SearchMatcherTests::defaultIsCaseInsensitive() {
    const auto matches = run(QStringLiteral("PDF pdf Pdf"), QStringLiteral("pdf"));
    QCOMPARE(matches.size(), 3);
    QCOMPARE(matches.first().at, 0);
    QCOMPARE(matches.first().length, 3);
}

void SearchMatcherTests::caseSensitiveHonoursCase() {
    const auto matches = run(QStringLiteral("PDF pdf Pdf"), QStringLiteral("pdf"), true, false);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().at, 4);
}

void SearchMatcherTests::wholeWordSkipsSubstrings() {
    const auto matches = run(QStringLiteral("cat concatenate cat"), QStringLiteral("cat"), false, true);
    QCOMPARE(matches.size(), 2);
    QCOMPARE(matches.at(0).at, 0);
    QCOMPARE(matches.at(1).at, 16);
}

void SearchMatcherTests::wholeWordTreatsCjkAsWordCharacters() {
    // 中文没有空格分词：全字匹配只按 ASCII 词字符判边界，所以两处都会命中。
    // 这是刻意的取舍（要做对需要分词），界面上写明"全字匹配面向西文"。
    const auto matches = run(QStringLiteral("合同法与合同"), QStringLiteral("合同"), false, true);
    QCOMPARE(matches.size(), 2);
}

void SearchMatcherTests::wholeWordMatchesAtTextEdges() {
    const auto matches = run(QStringLiteral("cat"), QStringLiteral("cat"), false, true);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().at, 0);
}

void SearchMatcherTests::wholeWordRejectsInnerLetters() {
    // "aa" 里的 'a' 不是独立的词，两侧都被字母夹住——全字匹配应当一个都不给。
    const auto matches = run(QStringLiteral("aa"), QStringLiteral("a"), false, true);
    QVERIFY(matches.isEmpty());

    // 用连字符隔开才算两个词，顺带验证"失败后前进一个字符"不会漏掉后一个。
    const auto separated = run(QStringLiteral("a-a"), QStringLiteral("a"), false, true);
    QCOMPARE(separated.size(), 2);
}

void SearchMatcherTests::countsEveryOccurrence() {
    const auto matches = run(QStringLiteral("aaaa"), QStringLiteral("aa"));
    QCOMPARE(matches.size(), 2);  // 不重叠：0 和 2
    QCOMPARE(matches.at(0).at, 0);
    QCOMPARE(matches.at(1).at, 2);
}

void SearchMatcherTests::regexMatchesPatterns() {
    SearchOptions options;
    options.regex = true;
    // 两条命中：两个数字串
    const auto digits = findTextMatches(QStringLiteral("a1 b22 c333"), QStringLiteral("\\d+"), options);
    QCOMPARE(digits.size(), 3);
    QCOMPARE(digits.at(1).at, 4);  // "a1 b22 c333" 里第二个数字串从偏移 4 开始
    QCOMPARE(digits.at(1).length, 2);

    options.caseSensitive = true;
    const auto upper = findTextMatches(QStringLiteral("Ab ab"), QStringLiteral("Ab"), options);
    QCOMPARE(upper.size(), 1);
}

void SearchMatcherTests::invalidRegexYieldsNothing() {
    SearchOptions options;
    options.regex = true;
    // 写坏的正则不给命中、也不抛：搜索框里最忌讳打断输入
    QVERIFY(findTextMatches(QStringLiteral("abc"), QStringLiteral("(["), options).isEmpty());
}

QTEST_MAIN(SearchMatcherTests)
#include "SearchMatcherTests.moc"
