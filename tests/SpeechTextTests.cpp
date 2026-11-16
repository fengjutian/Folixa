// 朗读分词的规则测试。
//
// 这一层是纯逻辑：切词结果决定界面把高亮画在原文的哪个位置，
// 所以「不丢字符、不改顺序、位置对得上」是硬要求。
#include "speech/SpeechText.h"

#include <QtTest>

using namespace aipdf::speech;

namespace {

QString stripWhitespace(const QString& text) {
    QString out;
    for (const QChar& unit : text) {
        if (!unit.isSpace()) {
            out += unit;
        }
    }
    return out;
}

}  // namespace

class SpeechTextTests : public QObject {
    Q_OBJECT

private slots:
    void splitsEnglishSentenceByWord();
    void splitsMixedChineseAndEnglish();
    void punctuationBecomesItsOwnWord();
    void repeatedSpacesDoNotCreateEmptyWords();
    void emptyTextYieldsNoWords();
    void joinWordsRestoresTextWithoutWhitespace();
    void splitsChinesePerCharacter();
    void digitsAndLettersFormOneWord();
    void coversEveryNonWhitespaceCharacter();
    void wordOffsetsPointBackIntoOriginalText();
    void treatsTabsAndNewlinesAsWhitespace();
    void countsSurrogatePairAsOneWord();
    void treatsApostropheAsSeparateSymbol();
};

void SpeechTextTests::splitsEnglishSentenceByWord() {
    const QVector<SpeechWord> words = splitIntoWords(QStringLiteral("Hello world"));
    QCOMPARE(words.size(), 2);
    QCOMPARE(words.at(0).text, QStringLiteral("Hello"));
    QCOMPARE(words.at(0).start, 0);
    QCOMPARE(words.at(0).length, 5);
    QCOMPARE(words.at(1).text, QStringLiteral("world"));
    QCOMPARE(words.at(1).start, 6);
    QCOMPARE(words.at(1).length, 5);
}

void SpeechTextTests::splitsMixedChineseAndEnglish() {
    const QVector<SpeechWord> words = splitIntoWords(QStringLiteral("PDF 文档"));
    QCOMPARE(words.size(), 3);
    QCOMPARE(words.at(0).text, QStringLiteral("PDF"));
    QCOMPARE(words.at(0).start, 0);
    QCOMPARE(words.at(0).length, 3);
    QCOMPARE(words.at(1).text, QStringLiteral("文"));
    QCOMPARE(words.at(1).start, 4);
    QCOMPARE(words.at(1).length, 1);
    QCOMPARE(words.at(2).text, QStringLiteral("档"));
    QCOMPARE(words.at(2).start, 5);
    QCOMPARE(words.at(2).length, 1);
}

void SpeechTextTests::punctuationBecomesItsOwnWord() {
    const QVector<SpeechWord> words = splitIntoWords(QStringLiteral("Hi, there!"));
    QCOMPARE(words.size(), 4);
    QCOMPARE(words.at(0).text, QStringLiteral("Hi"));
    QCOMPARE(words.at(1).text, QStringLiteral(","));
    QCOMPARE(words.at(1).start, 2);
    QCOMPARE(words.at(1).length, 1);
    QCOMPARE(words.at(2).text, QStringLiteral("there"));
    QCOMPARE(words.at(2).start, 4);
    QCOMPARE(words.at(3).text, QStringLiteral("!"));
    QCOMPARE(words.at(3).start, 9);
}

void SpeechTextTests::repeatedSpacesDoNotCreateEmptyWords() {
    const QVector<SpeechWord> words = splitIntoWords(QStringLiteral("a   b"));
    QCOMPARE(words.size(), 2);
    QCOMPARE(words.at(0).text, QStringLiteral("a"));
    QCOMPARE(words.at(0).start, 0);
    QCOMPARE(words.at(1).text, QStringLiteral("b"));
    QCOMPARE(words.at(1).start, 4);
    for (const SpeechWord& word : words) {
        QVERIFY(!word.text.isEmpty());
    }
}

void SpeechTextTests::emptyTextYieldsNoWords() {
    QVERIFY(splitIntoWords(QString()).isEmpty());
    QVERIFY(splitIntoWords(QStringLiteral("   ")).isEmpty());
    QVERIFY(joinWords(QVector<SpeechWord>()).isEmpty());
}

void SpeechTextTests::joinWordsRestoresTextWithoutWhitespace() {
    const QStringList samples{QStringLiteral("Hello world"),
                              QStringLiteral("PDF 文档 mixed 内容"),
                              QStringLiteral("Hi, there!"),
                              QStringLiteral("a   b\tc\nd"),
                              QStringLiteral("PDF2024 版本 1.0")};
    for (const QString& sample : samples) {
        const QVector<SpeechWord> words = splitIntoWords(sample);
        QCOMPARE(stripWhitespace(joinWords(words)), stripWhitespace(sample));
    }
}

void SpeechTextTests::splitsChinesePerCharacter() {
    const QVector<SpeechWord> words = splitIntoWords(QStringLiteral("中文测试"));
    QCOMPARE(words.size(), 4);
    for (int index = 0; index < words.size(); ++index) {
        QCOMPARE(words.at(index).length, 1);
        QCOMPARE(words.at(index).start, index);
    }
    QCOMPARE(words.at(0).text, QStringLiteral("中"));
    QCOMPARE(words.at(3).text, QStringLiteral("试"));
}

void SpeechTextTests::digitsAndLettersFormOneWord() {
    const QVector<SpeechWord> words = splitIntoWords(QStringLiteral("PDF2024"));
    QCOMPARE(words.size(), 1);
    QCOMPARE(words.at(0).text, QStringLiteral("PDF2024"));
    QCOMPARE(words.at(0).length, 7);
}

void SpeechTextTests::coversEveryNonWhitespaceCharacter() {
    const QString text = QStringLiteral("PDF 文档 v1.0，测试 ok!");
    const QVector<SpeechWord> words = splitIntoWords(text);
    QCOMPARE(stripWhitespace(joinWords(words)), stripWhitespace(text));

    // 词必须按顺序覆盖原文，不能重叠也不能跳字。
    int cursor = 0;
    for (const SpeechWord& word : words) {
        QVERIFY(word.start >= cursor);
        QCOMPARE(text.mid(word.start, word.length), word.text);
        cursor = word.start + word.length;
    }
}

void SpeechTextTests::wordOffsetsPointBackIntoOriginalText() {
    const QString text = QStringLiteral("The quick 棕色狐狸 jumps");
    const QVector<SpeechWord> words = splitIntoWords(text);
    for (const SpeechWord& word : words) {
        QVERIFY(word.start >= 0);
        QVERIFY(word.length > 0);
        QVERIFY(word.start + word.length <= text.size());
        QCOMPARE(text.mid(word.start, word.length), word.text);
    }
}

void SpeechTextTests::treatsTabsAndNewlinesAsWhitespace() {
    const QVector<SpeechWord> words = splitIntoWords(QStringLiteral("a\tb\nc"));
    QCOMPARE(words.size(), 3);
    QCOMPARE(words.at(0).text, QStringLiteral("a"));
    QCOMPARE(words.at(1).text, QStringLiteral("b"));
    QCOMPARE(words.at(1).start, 2);
    QCOMPARE(words.at(2).text, QStringLiteral("c"));
    QCOMPARE(words.at(2).start, 4);
}

void SpeechTextTests::countsSurrogatePairAsOneWord() {
    const QString text = QStringLiteral("a😀b");
    const QVector<SpeechWord> words = splitIntoWords(text);
    QCOMPARE(words.size(), 3);
    QCOMPARE(words.at(0).text, QStringLiteral("a"));
    QCOMPARE(words.at(1).text, QStringLiteral("😀"));
    QCOMPARE(words.at(1).length, 2);
    QCOMPARE(words.at(1).start, 1);
    QCOMPARE(words.at(2).text, QStringLiteral("b"));
    QCOMPARE(words.at(2).start, 3);
}

void SpeechTextTests::treatsApostropheAsSeparateSymbol() {
    // 规则是「拉丁字母/数字连成一个词，其余符号单独成词」：
    // 撇号属于其余符号，所以 don't 会被切成 don + ' + t。
    const QVector<SpeechWord> words = splitIntoWords(QStringLiteral("don't"));
    QCOMPARE(words.size(), 3);
    QCOMPARE(words.at(0).text, QStringLiteral("don"));
    QCOMPARE(words.at(1).text, QStringLiteral("'"));
    QCOMPARE(words.at(2).text, QStringLiteral("t"));
}

QTEST_MAIN(SpeechTextTests)
#include "SpeechTextTests.moc"
