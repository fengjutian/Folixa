// 拼写检查的单元测试。
//
// 断言的重点是「位置对得上」：start/length 必须真的指向原文里的那个词，
// 否则界面上的波浪线会画在错的地方，单元测试也就没意义了。

#include "pdf/SpellChecker.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTest>

#include <algorithm>

using aipdf::pdf::SpellChecker;
using aipdf::pdf::SpellIssue;

namespace {

// 编辑距离（Levenshtein）：用来独立验一遍建议到底差多少。
int editDistance(const QString& a, const QString& b) {
    if (a == b) return 0;
    if (a.isEmpty()) return b.size();
    if (b.isEmpty()) return a.size();
    QVector<int> previous(b.size() + 1);
    QVector<int> current(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) previous[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
        }
        previous = current;
    }
    return previous[b.size()];
}

// 断言一条问题确实指向原文里的那个词。
void checkIssueMatches(const SpellIssue& issue, const QString& text, const QString& word) {
    QCOMPARE(issue.word, word);
    QCOMPARE(issue.length, word.size());
    QVERIFY(issue.start >= 0);
    QVERIFY(issue.start + issue.length <= text.size());
    QCOMPARE(text.mid(issue.start, issue.length), word);
}

}  // namespace

class SpellCheckerTests : public QObject {
    Q_OBJECT

private slots:
    void correctSentenceHasNoIssues();
    void commonMisspellingIsReportedWithSuggestion();
    void repeatedWordIsReported();
    void shortWordsAreIgnored();
    void wordsWithDigitsAreIgnored();
    void abbreviationsAreIgnored();
    void chineseTextIsIgnored();
    void emptyTextHasNoIssues();
    void userDictionaryRemovesIssue();
    void missingUserDictionaryReportsError();
    void suggestionsAreOneEditAway();
    void addedWordBecomesKnown();
    void knownWordLookupIsCaseInsensitive();
    void commonParagraphHasNoIssues();
};

void SpellCheckerTests::correctSentenceHasNoIssues() {
    const SpellChecker checker = SpellChecker::builtin();
    const QString text = QStringLiteral("This is a simple sentence about a page of text.");
    const QVector<SpellIssue> issues = checker.check(text);
    for (const SpellIssue& issue : issues)
        qWarning("FALSEPOS: %s", issue.word.toUtf8().constData());
    QCOMPARE(issues.size(), 0);
}

// 一整段正常文字不该有波浪线。
//
// 这条是误报率的守门人：内置词表只覆盖常用词，一旦词表漏了词、
// 或者近邻规则太激进，整段话就会到处冒红线。
void SpellCheckerTests::commonParagraphHasNoIssues() {
    const SpellChecker checker = SpellChecker::builtin();
    const QString text = QStringLiteral(
        "The quick brown fox jumps over the lazy dog. This is a simple sentence about a page of text. "
        "Documents may contain words like workflow, metadata, bookmark, annotation, signature, hyperlink, "
        "checkbox, dropdown, template, and other technical terms that are not in the built-in dictionary. "
        "We have been working on this for three weeks and the result is quite good.");
    const QVector<SpellIssue> issues = checker.check(text);
    for (const SpellIssue& issue : issues)
        qWarning("FALSEPOS: %s", issue.word.toUtf8().constData());
    QCOMPARE(issues.size(), 0);
}

// 常见拼错：recieve → receive，位置要精确。
void SpellCheckerTests::commonMisspellingIsReportedWithSuggestion() {
    const SpellChecker checker = SpellChecker::builtin();
    const QString text = QStringLiteral("I will recieve the package.");
    const QVector<SpellIssue> issues = checker.check(text);
    QCOMPARE(issues.size(), 1);
    checkIssueMatches(issues.first(), text, QStringLiteral("recieve"));
    QCOMPARE(issues.first().start, text.indexOf(QStringLiteral("recieve")));
    QVERIFY(issues.first().suggestions.contains(QStringLiteral("receive")));
}

// 连续重复的词：the the → 报第二处。
void SpellCheckerTests::repeatedWordIsReported() {
    const SpellChecker checker = SpellChecker::builtin();
    const QString text = QStringLiteral("This is the the problem.");
    const QVector<SpellIssue> issues = checker.check(text);
    QCOMPARE(issues.size(), 1);
    checkIssueMatches(issues.first(), text, QStringLiteral("the"));
    // 报的是第二处：位置必须落在第二个 the 上。
    const int first = text.indexOf(QStringLiteral("the"));
    QCOMPARE(issues.first().start, text.indexOf(QStringLiteral("the"), first + 1));
}

// 少于三个字母的词不查。
void SpellCheckerTests::shortWordsAreIgnored() {
    const SpellChecker checker = SpellChecker::builtin();
    const QVector<SpellIssue> issues = checker.check(QStringLiteral("I am to be it"));
    QCOMPARE(issues.size(), 0);
}

// 含数字的词不查（版本号、编号、金额这些）。
void SpellCheckerTests::wordsWithDigitsAreIgnored() {
    const SpellChecker checker = SpellChecker::builtin();
    const QVector<SpellIssue> issues = checker.check(QStringLiteral("The build 12345 works"));
    QCOMPARE(issues.size(), 0);
}

// 缩写不查：e.g.、Mr. 这类带点的短词。
void SpellCheckerTests::abbreviationsAreIgnored() {
    const SpellChecker checker = SpellChecker::builtin();
    const QVector<SpellIssue> issues = checker.check(QStringLiteral("e.g. text and file."));
    QCOMPARE(issues.size(), 0);
}

// 中文不做拼写检查：连续汉字整段跳过。
void SpellCheckerTests::chineseTextIsIgnored() {
    const SpellChecker checker = SpellChecker::builtin();
    const QVector<SpellIssue> issues = checker.check(QStringLiteral("这是一段中文句子，没有问题。"));
    QCOMPARE(issues.size(), 0);
}

// 空文本 → 空结果。
void SpellCheckerTests::emptyTextHasNoIssues() {
    const SpellChecker checker = SpellChecker::builtin();
    QCOMPARE(checker.check(QString()).size(), 0);
    QCOMPARE(checker.check(QStringLiteral("   ")).size(), 0);
}

// 用户词典：装进去之后，原来报错的词不再报。
void SpellCheckerTests::userDictionaryRemovesIssue() {
    const QString path = QDir::tempPath() + QStringLiteral("/aipdf-spell-user.dic");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    // Hunspell 的 .dic 第一行是词条数；这里用 2。
    file.write("# 用户词典\n2\nrecieve\nzzword/ABC\n");
    file.close();

    SpellChecker checker = SpellChecker::builtin();
    const QString text = QStringLiteral("I will recieve the package.");
    QCOMPARE(checker.check(text).size(), 1);

    QString error;
    const int loaded = checker.loadUserDictionary(path, &error);
    QVERIFY2(!error.isEmpty() || loaded == 2, qPrintable(error));
    // 注释行与数字行不算词，带 /FLAGS 的词条按词本身算一个。
    QCOMPARE(loaded, 2);
    QVERIFY(checker.isKnownWord(QStringLiteral("recieve")));
    QVERIFY(checker.isKnownWord(QStringLiteral("ZZWORD")));

    // 原来报错的词现在不报了。
    QCOMPARE(checker.check(text).size(), 0);
    QFile::remove(path);
}

// 词典文件不存在：返回 0，并给出原因。
void SpellCheckerTests::missingUserDictionaryReportsError() {
    SpellChecker checker = SpellChecker::builtin();
    QString error;
    const int loaded = checker.loadUserDictionary(QDir::tempPath() + QStringLiteral("/aipdf-not-exist.dic"), &error);
    QCOMPARE(loaded, 0);
    QVERIFY(!error.isEmpty());
}

// 建议必须真的只差一个字符（增/删/改一个）。
//
// 注意口径：编辑距离 1 指「增/删/改一个字符」，
// 换位（recieve → receive）要动两个字符，不在这个口径里——
// recieve 的建议走的是「常见拼错对」那条路，见上面那条用例。
void SpellCheckerTests::suggestionsAreOneEditAway() {
    const SpellChecker checker = SpellChecker::builtin();
    const QStringList suggestions = checker.suggestions(QStringLiteral("documant"), 3);
    QVERIFY(!suggestions.isEmpty());
    QVERIFY(suggestions.contains(QStringLiteral("document")));
    for (const QString& suggestion : suggestions)
        QCOMPARE(editDistance(QStringLiteral("documant"), suggestion), 1);
}

// 追加自定义词（不落盘）之后，这个词就认识了。
void SpellCheckerTests::addedWordBecomesKnown() {
    SpellChecker checker = SpellChecker::builtin();
    QVERIFY(!checker.isKnownWord(QStringLiteral("zzyzx")));
    checker.addWord(QStringLiteral("zzyzx"));
    QVERIFY(checker.isKnownWord(QStringLiteral("zzyzx")));
    QCOMPARE(checker.check(QStringLiteral("The zzyzx file is here.")).size(), 0);
}

// 词典查询大小写不敏感。
void SpellCheckerTests::knownWordLookupIsCaseInsensitive() {
    const SpellChecker checker = SpellChecker::builtin();
    QVERIFY(checker.isKnownWord(QStringLiteral("document")));
    QVERIFY(checker.isKnownWord(QStringLiteral("Document")));
    QVERIFY(checker.isKnownWord(QStringLiteral("DOCUMENT")));
    QVERIFY(!checker.isKnownWord(QStringLiteral("qqqqqq")));
}

QTEST_MAIN(SpellCheckerTests)
#include "SpellCheckerTests.moc"
