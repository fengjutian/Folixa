// 智能朗读（分段 + 停顿）的规则测试。
//
// 这一层的产物直接喂给系统语音合成：分段错了会念错顺序，
// 停顿错了会让人觉得机器没在思考。所以这里把「怎么切、怎么停」钉死。
#include "ai/content/AiNarration.h"

#include <QtTest>

#include <cmath>

using namespace aipdf::ai;

namespace {

// 有没有中文字符：用来验「失败原因必须是中文」。
bool containsChinese(const QString& text) {
    for (const QChar& unit : text) {
        if (unit.unicode() >= 0x4E00 && unit.unicode() <= 0x9FFF) {
            return true;
        }
    }
    return false;
}

}  // namespace

class AiNarrationTests : public QObject {
    Q_OBJECT

private slots:
    void normalTextYieldsAtLeastOneSegment();
    void emptyTextFailsWithChineseReason();
    void whitespaceOnlyFails();
    void shortHeadingLineIsHeading();
    void dashLineIsListItem();
    void quoteLineIsQuote();
    void consecutiveBlankLinesDoNotCreateEmptySegments();
    void sentenceEndingPauses300ms();
    void questionEndingPausesLonger();
    void cleanRemovesChinesePageNumberLine();
    void cleanRemovesDashedPageNumberLine();
    void cleanCollapsesBlankLinesAndSpaces();
    void totalCharsAndEstimateAreConsistent();
    void listMarkerIsNotSpoken();
    void pageNumberLineDoesNotBecomeASegment();
};

void AiNarrationTests::normalTextYieldsAtLeastOneSegment() {
    const NarrationPlan plan = planNarration(QStringLiteral("这是一段用于测试的普通文字内容。"));

    QVERIFY(plan.ok);
    QVERIFY(plan.error.isEmpty());
    QVERIFY(!plan.segments.isEmpty());
    QCOMPARE(plan.segments.size(), 1);
    QCOMPARE(plan.segments.at(0).kind, QStringLiteral("正文"));
    QCOMPARE(plan.segments.at(0).index, 0);
    QVERIFY(!plan.segments.at(0).text.isEmpty());
}

void AiNarrationTests::emptyTextFailsWithChineseReason() {
    const NarrationPlan plan = planNarration(QString());

    QVERIFY(!plan.ok);
    QVERIFY(plan.segments.isEmpty());
    QVERIFY(!plan.error.isEmpty());
    QVERIFY(containsChinese(plan.error));
}

void AiNarrationTests::whitespaceOnlyFails() {
    const NarrationPlan plan = planNarration(QStringLiteral("   \n\t\r\n  "));

    QVERIFY(!plan.ok);
    QVERIFY(!plan.error.isEmpty());
    QVERIFY(containsChinese(plan.error));
}

void AiNarrationTests::shortHeadingLineIsHeading() {
    // 短行 + 不以标点结尾 → 当标题念，并且要重音。
    const NarrationPlan plan =
        planNarration(QStringLiteral("第一章 概述\n\n这一段是正文内容，用来验证切分是否正确。"));

    QVERIFY(plan.ok);
    QVERIFY(plan.segments.size() >= 2);
    QCOMPARE(plan.segments.at(0).kind, QStringLiteral("标题"));
    QCOMPARE(plan.segments.at(0).text, QStringLiteral("第一章 概述"));
    QVERIFY(plan.segments.at(0).emphasize);
    QCOMPARE(plan.segments.at(1).kind, QStringLiteral("正文"));
}

void AiNarrationTests::dashLineIsListItem() {
    const NarrationPlan plan = planNarration(QStringLiteral("- 第一个要点\n- 第二个要点"));

    QVERIFY(plan.ok);
    QCOMPARE(plan.segments.size(), 2);
    QCOMPARE(plan.segments.at(0).kind, QStringLiteral("列表项"));
    QCOMPARE(plan.segments.at(1).kind, QStringLiteral("列表项"));
    // 要点是重点，要重音。
    QVERIFY(plan.segments.at(0).emphasize);
}

void AiNarrationTests::quoteLineIsQuote() {
    const NarrationPlan plan = planNarration(QStringLiteral("> 这是一段引用。"));

    QVERIFY(plan.ok);
    QCOMPARE(plan.segments.size(), 1);
    QCOMPARE(plan.segments.at(0).kind, QStringLiteral("引用"));
    // 引用符号本身不念。
    QCOMPARE(plan.segments.at(0).text, QStringLiteral("这是一段引用。"));
}

void AiNarrationTests::consecutiveBlankLinesDoNotCreateEmptySegments() {
    const NarrationPlan plan =
        planNarration(QStringLiteral("第一段内容在这里结束。\n\n\n\n第二段内容在这里结束。"));

    QVERIFY(plan.ok);
    QCOMPARE(plan.segments.size(), 2);
    for (const NarrationSegment& segment : plan.segments) {
        QVERIFY(!segment.text.isEmpty());
    }
    QCOMPARE(plan.segments.at(0).index, 0);
    QCOMPARE(plan.segments.at(1).index, 1);
}

void AiNarrationTests::sentenceEndingPauses300ms() {
    const NarrationPlan plan =
        planNarration(QStringLiteral("第一句话在这里结束。\n\n第二句话也结束了。"));

    QVERIFY(plan.ok);
    QCOMPARE(plan.segments.size(), 2);
    // 段中的句末停 300ms。
    QCOMPARE(plan.segments.at(0).pauseAfterMs, 300);
    // 整篇最后一段是段末：多停 300ms，正好 600ms。
    QCOMPARE(plan.segments.at(1).pauseAfterMs, 600);
}

void AiNarrationTests::questionEndingPausesLonger() {
    const NarrationPlan plan = planNarration(
        QStringLiteral("这是第一个疑问句吗？\n\n这是陈述句。\n\n还有一句陈述。"));

    QVERIFY(plan.ok);
    QCOMPARE(plan.segments.size(), 3);
    // 问句比陈述句停得久：听的人要反应过来。
    QCOMPARE(plan.segments.at(0).pauseAfterMs, 500);
    QCOMPARE(plan.segments.at(1).pauseAfterMs, 300);
    QVERIFY(plan.segments.at(0).pauseAfterMs > plan.segments.at(1).pauseAfterMs);
}

void AiNarrationTests::cleanRemovesChinesePageNumberLine() {
    const QString cleaned =
        cleanTextForNarration(QStringLiteral("正文第一段。\n第 3 页\n正文第二段。"));

    QVERIFY(!cleaned.contains(QStringLiteral("第 3 页")));
    QVERIFY(cleaned.contains(QStringLiteral("正文第一段。")));
    QVERIFY(cleaned.contains(QStringLiteral("正文第二段。")));
}

void AiNarrationTests::cleanRemovesDashedPageNumberLine() {
    const QString cleaned =
        cleanTextForNarration(QStringLiteral("正文第一段。\n- 3 -\n正文第二段。"));

    QVERIFY(!cleaned.contains(QStringLiteral("- 3 -")));
    QVERIFY(cleaned.contains(QStringLiteral("正文第一段。")));
    QVERIFY(cleaned.contains(QStringLiteral("正文第二段。")));
}

void AiNarrationTests::cleanCollapsesBlankLinesAndSpaces() {
    const QString cleaned =
        cleanTextForNarration(QStringLiteral("  正文   第一行。  \n\n\n  正文第二行。  "));

    // 连续空行压成一个：一个空行在拼接结果里就是两个换行符，
    // 所以这里要盯的是「不再出现三个换行」。
    QVERIFY(!cleaned.contains(QStringLiteral("\n\n\n")));
    QCOMPARE(cleaned, QStringLiteral("正文 第一行。\n\n正文第二行。"));
}

void AiNarrationTests::totalCharsAndEstimateAreConsistent() {
    const NarrationPlan plan =
        planNarration(QStringLiteral("第一段有十个字左右。\n\n第二段也差不多长。"));

    QVERIFY(plan.ok);
    QVERIFY(plan.totalChars > 0);
    // 估算值不能少于按 4.5 字/秒算出来的结果。
    QVERIFY(plan.estimatedSeconds >= plan.totalChars / 4.5);
    // 向上取整：宁可多估一秒，不可少估。
    QCOMPARE(plan.estimatedSeconds, static_cast<int>(std::ceil(plan.totalChars / 4.5)));
}

void AiNarrationTests::listMarkerIsNotSpoken() {
    const NarrationPlan plan = planNarration(QStringLiteral("- 第一个要点"));

    QVERIFY(plan.ok);
    QCOMPARE(plan.segments.size(), 1);
    // 「- 」是排版符号，不该被念出来。
    QVERIFY(!plan.segments.at(0).text.startsWith(QStringLiteral("-")));
    QCOMPARE(plan.segments.at(0).text, QStringLiteral("第一个要点"));
}

void AiNarrationTests::pageNumberLineDoesNotBecomeASegment() {
    const NarrationPlan plan =
        planNarration(QStringLiteral("正文第一段。\n第 3 页\n正文第二段。"));

    QVERIFY(plan.ok);
    QVERIFY(!plan.segments.isEmpty());
    for (const NarrationSegment& segment : plan.segments) {
        QVERIFY(!segment.text.contains(QStringLiteral("第 3 页")));
    }
}

QTEST_GUILESS_MAIN(AiNarrationTests)
#include "AiNarrationTests.moc"
