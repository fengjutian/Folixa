// PPT 生成（三种来源 × 同一套结构）的领域层单元测试。
//
// 这一层不联网：模型调用是注入进来的假函数，返回固定的 JSON。
// 所以这些用例跑得快、结果确定，也不会碰到任何真实服务商的限流。
//
// 覆盖四块：
//   1. 主题生成（模型给内容）；
//   2. 文档生成（有模型走模型、没模型走本地提炼）；
//   3. 大纲生成（纯本地排版）；
//   4. 模型回答的解析与 Markdown 导出。
//
// 有一条贯穿全篇的底线：**失败绝不能长得像成功**。
// 凡是出错的地方都断言 error 非空、slides 为空、isValid() 为假。
#include "ai/content/PptGeneration.h"

#include <QtTest>

using namespace aipdf::ai;

namespace {

// 假模型：原样回一段准备好的 JSON。
//
// 为什么用 std::function 而不是真的 Provider：Provider 是异步流式的，
// 单测要等它就得先转一个事件循环；这一层要验的是「拿到 JSON 之后干什么」，
// 与网络无关，所以直接注入一个函数。
DeckModelCall fakeModel(const QByteArray& response) {
    return [response](const QString& prompt, QString* error) -> QByteArray {
        Q_UNUSED(prompt);
        if (error != nullptr) {
            *error = QString();
        }
        return response;
    };
}

// 假模型：报一个错。
DeckModelCall failingModel(const QString& reason) {
    return [reason](const QString& prompt, QString* error) -> QByteArray {
        Q_UNUSED(prompt);
        if (error != nullptr) {
            *error = reason;
        }
        return QByteArray();
    };
}

// 假模型：什么都不回。
DeckModelCall silentModel() {
    return [](const QString& prompt, QString* error) -> QByteArray {
        Q_UNUSED(prompt);
        if (error != nullptr) {
            *error = QString();
        }
        return QByteArray();
    };
}

// 两页的固定回答。
const QByteArray kTwoSlideJson =
    R"({"title":"自动保存","slides":[{"title":"自动保存","bullets":["关闭文档时提示","可以配置延迟"]},{"title":"批注","subtitle":"标注与核对","bullets":["支持多种形状"],"notes":"演示用"}]})";

// 三页的固定回答。
const QByteArray kThreeSlideJson =
    R"({"title":"季度回顾","slides":[{"title":"第一页","bullets":["要点一"]},{"title":"第二页","bullets":["要点二"]},{"title":"第三页","bullets":["要点三"]}]})";

}  // namespace

class PptGenerationTests : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------- 主题生成 ----
    void topicGeneratesExpectedSlideCount();
    void topicRejectsEmptyTopic();
    void topicWithoutModelCallFails();
    void topicFallsBackToTopicAsTitle();
    void topicPropagatesModelFailure();
    void topicRejectsModelGarbage();
    void topicRejectsEmptyModelResponse();

    // ------------------------------------------------------- 文档生成 ----
    void documentGeneratesExpectedSlideCount();
    void documentRejectsEmptyText();
    void documentUsesModelWhenAvailable();
    void documentPrefersExplicitTitleOverModelTitle();
    void documentFallsBackToLocalExtraction();
    void documentDerivesTitleFromText();

    // ------------------------------------------------------- 大纲生成 ----
    void outlinePageCountMatchesTitleLines();
    void outlineKeepsIndentedBullets();
    void outlineRejectsEmptyText();
    void outlineWithoutBulletsKeepsPage();
    void outlineAcceptsNumberedTitles();
    void outlineIgnoresFourDigitNumberedLines();
    void outlineIgnoresUnmarkedTopLevelLines();

    // ---------------------------------------------------------- 页数 ----
    void maxSlidesTruncatesToLimit();
    void maxSlidesZeroClampsToOne();
    void maxSlidesAboveMaximumClampsToThirty();

    // ---------------------------------------------------------- 解析 ----
    void parseReadsTitleAndBullets();
    void parseRejectsInvalidJson();
    void parseRejectsMissingSlides();
    void parseRejectsEmptySlides();
    void parseRejectsNonStringBullet();

    // ---------------------------------------------------------- 导出 ----
    void markdownContainsTitleAndPageHeadings();
    void markdownOnEmptyDeckIsSafe();
    void markdownContainsBulletsAndNotes();
};

// ------------------------------------------------------------ 主题生成 ----

void PptGenerationTests::topicGeneratesExpectedSlideCount() {
    const PptDeck deck =
        generateDeckFromTopic(QStringLiteral("自动保存"), QString(), 12, fakeModel(kTwoSlideJson));

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    QVERIFY(deck.isValid());
    QCOMPARE(deck.slides.size(), 2);
    QCOMPARE(deck.source, PptSource::Topic);
    // 主题就是这套 PPT 讲什么。
    QCOMPARE(deck.theme, QStringLiteral("自动保存"));
    // 页码从 1 起、连续。
    QCOMPARE(deck.slides.at(0).index, 1);
    QCOMPARE(deck.slides.at(1).index, 2);
    QCOMPARE(deck.slides.at(0).title, QStringLiteral("自动保存"));
    QCOMPARE(deck.slides.at(0).bullets.size(), 2);
    QCOMPARE(deck.slides.at(0).bullets.at(0), QStringLiteral("关闭文档时提示"));
}

void PptGenerationTests::topicRejectsEmptyTopic() {
    const PptDeck deck = generateDeckFromTopic(
        QStringLiteral("   "), QString(), 12, fakeModel(kTwoSlideJson));

    // 空主题必须失败，而且原因得让人看得懂。
    QVERIFY(!deck.error.isEmpty());
    QVERIFY(deck.slides.isEmpty());
    QVERIFY(!deck.isValid());
    QVERIFY2(deck.error.contains(QStringLiteral("主题")),
             qPrintable(QStringLiteral("失败原因里没有「主题」：") + deck.error));
}

void PptGenerationTests::topicWithoutModelCallFails() {
    const PptDeck deck = generateDeckFromTopic(QStringLiteral("自动保存"), QString(), 12);

    // 没有模型就没有内容来源：不能返回一份空 deck 冒充成功。
    QVERIFY(!deck.error.isEmpty());
    QVERIFY(deck.slides.isEmpty());
    QVERIFY(!deck.isValid());
}

void PptGenerationTests::topicFallsBackToTopicAsTitle() {
    // 模型只给了 slides、没给整套标题：标题用主题顶上，PPT 总得有标题。
    const QByteArray json =
        R"({"slides":[{"title":"自动保存","bullets":["关闭文档时提示"]}]})";
    const PptDeck deck =
        generateDeckFromTopic(QStringLiteral("自动保存"), QString(), 12, fakeModel(json));

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    QCOMPARE(deck.title, QStringLiteral("自动保存"));
    QCOMPARE(deck.slides.size(), 1);
}

void PptGenerationTests::topicPropagatesModelFailure() {
    const PptDeck deck = generateDeckFromTopic(QStringLiteral("自动保存"), QString(), 12,
                                                failingModel(QStringLiteral("密钥无效")));

    QVERIFY(!deck.error.isEmpty());
    QVERIFY(deck.slides.isEmpty());
    // 模型给的原因要透出来，不能被吞掉。
    QVERIFY2(deck.error.contains(QStringLiteral("密钥无效")),
             qPrintable(QStringLiteral("模型原因没有透出：") + deck.error));
}

void PptGenerationTests::topicRejectsModelGarbage() {
    const PptDeck deck = generateDeckFromTopic(QStringLiteral("自动保存"), QString(), 12,
                                                fakeModel(QByteArray("这不是 JSON")));

    // 模型没按格式回：整份 PPT 作废，绝不返回空 deck 冒充成功。
    QVERIFY(!deck.error.isEmpty());
    QVERIFY(deck.slides.isEmpty());
    QVERIFY(!deck.isValid());
}

void PptGenerationTests::topicRejectsEmptyModelResponse() {
    const PptDeck deck =
        generateDeckFromTopic(QStringLiteral("自动保存"), QString(), 12, silentModel());

    // 模型一个字节都没回：当成失败，不当成「生成了 0 页的 PPT」。
    QVERIFY(!deck.error.isEmpty());
    QVERIFY(deck.slides.isEmpty());
    QVERIFY(!deck.isValid());
}

// ------------------------------------------------------------ 文档生成 ----

void PptGenerationTests::documentGeneratesExpectedSlideCount() {
    // 正文按空行分成两段 → 两页；标题没给，从正文里推。
    const QString document = QStringLiteral(
        "产品介绍\n"
        "自动保存会在关闭文档时提示用户。\n"
        "\n"
        "搜索支持中文全文检索。\n");
    const PptDeck deck =
        generateDeckFromDocument(document, QString(), 12, DeckModelCall());

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    QCOMPARE(deck.slides.size(), 2);
    QCOMPARE(deck.source, PptSource::Document);
    // 标题来自文档：第一行。
    QCOMPARE(deck.title, QStringLiteral("产品介绍"));
    QCOMPARE(deck.slides.at(0).title, QStringLiteral("产品介绍"));
    QCOMPARE(deck.slides.at(0).bullets.size(), 1);
    QCOMPARE(deck.slides.at(1).title, QStringLiteral("搜索支持中文全文检索。"));
}

void PptGenerationTests::documentRejectsEmptyText() {
    const PptDeck deck =
        generateDeckFromDocument(QStringLiteral("  \n  "), QStringLiteral("产品介绍"), 12,
                                 fakeModel(kThreeSlideJson));

    QVERIFY(!deck.error.isEmpty());
    QVERIFY(deck.slides.isEmpty());
    QVERIFY(!deck.isValid());
    QVERIFY2(deck.error.contains(QStringLiteral("文档")),
             qPrintable(QStringLiteral("失败原因里没有「文档」：") + deck.error));
}

void PptGenerationTests::documentUsesModelWhenAvailable() {
    const PptDeck deck =
        generateDeckFromDocument(QStringLiteral("正文"), QStringLiteral("产品介绍"), 12,
                                  fakeModel(kTwoSlideJson));

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    // 有模型时页数由模型给的内容决定。
    QCOMPARE(deck.slides.size(), 2);
    QCOMPARE(deck.slides.at(1).title, QStringLiteral("批注"));
}

void PptGenerationTests::documentPrefersExplicitTitleOverModelTitle() {
    // 调用方显式给了标题，就用它：那是用户自己写的，比模型拟的更权威。
    const PptDeck deck =
        generateDeckFromDocument(QStringLiteral("正文"), QStringLiteral("产品介绍"), 12,
                                  fakeModel(kTwoSlideJson));

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    QCOMPARE(deck.title, QStringLiteral("产品介绍"));
    // 模型拟的标题（「自动保存」）只在这一层被丢掉，页内容仍是模型的。
    QCOMPARE(deck.slides.size(), 2);
    QCOMPARE(deck.slides.at(0).title, QStringLiteral("自动保存"));
}

void PptGenerationTests::documentFallsBackToLocalExtraction() {
    // 没有模型也要能用：本地提炼按空行分段，每段一页。
    const QString document = QStringLiteral(
        "自动保存\n"
        "关闭文档时提示用户\n"
        "\n"
        "批注\n"
        "支持导出与撤销\n");
    const PptDeck deck =
        generateDeckFromDocument(document, QStringLiteral("产品介绍"), 12, DeckModelCall());

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    QCOMPARE(deck.slides.size(), 2);
    QCOMPARE(deck.slides.at(0).title, QStringLiteral("自动保存"));
    QCOMPARE(deck.slides.at(0).bullets.size(), 1);
    QCOMPARE(deck.slides.at(0).bullets.at(0), QStringLiteral("关闭文档时提示用户"));
    QCOMPARE(deck.slides.at(1).title, QStringLiteral("批注"));
    QCOMPARE(deck.slides.at(1).bullets.size(), 1);
}

void PptGenerationTests::documentDerivesTitleFromText() {
    const PptDeck deck = generateDeckFromDocument(
        QStringLiteral("产品介绍\n正文一行\n"), QString(), 12, DeckModelCall());

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    // 没给标题时从正文里推一个。
    QCOMPARE(deck.title, QStringLiteral("产品介绍"));
}

// ------------------------------------------------------------ 大纲生成 ----

void PptGenerationTests::outlinePageCountMatchesTitleLines() {
    const QString outline = QStringLiteral(
        "- 背景\n"
        "- 现状\n"
        "- 结论\n");
    const PptDeck deck = generateDeckFromOutline(outline, QStringLiteral("汇报"), 12);

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    QCOMPARE(deck.slides.size(), 3);
    QCOMPARE(deck.source, PptSource::Outline);
    QCOMPARE(deck.slides.at(0).title, QStringLiteral("背景"));
    QCOMPARE(deck.slides.at(1).title, QStringLiteral("现状"));
    QCOMPARE(deck.slides.at(2).title, QStringLiteral("结论"));
    // 页码从 1 起、连续。
    QCOMPARE(deck.slides.at(0).index, 1);
    QCOMPARE(deck.slides.at(2).index, 3);
}

void PptGenerationTests::outlineKeepsIndentedBullets() {
    const QString outline = QStringLiteral(
        "- 背景\n"
        "  - 市场在增长\n"
        "  - 竞争也在加剧\n"
        "- 结论\n"
        "  - 要动手了\n");
    const PptDeck deck = generateDeckFromOutline(outline, QStringLiteral("汇报"), 12);

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    QCOMPARE(deck.slides.size(), 2);
    QCOMPARE(deck.slides.at(0).bullets.size(), 2);
    QCOMPARE(deck.slides.at(0).bullets.at(0), QStringLiteral("市场在增长"));
    QCOMPARE(deck.slides.at(0).bullets.at(1), QStringLiteral("竞争也在加剧"));
    QCOMPARE(deck.slides.at(1).bullets.size(), 1);
    QCOMPARE(deck.slides.at(1).bullets.at(0), QStringLiteral("要动手了"));
}

void PptGenerationTests::outlineRejectsEmptyText() {
    const PptDeck deck = generateDeckFromOutline(QStringLiteral("  \n "), QStringLiteral("汇报"), 12);

    QVERIFY(!deck.error.isEmpty());
    QVERIFY(deck.slides.isEmpty());
    QVERIFY(!deck.isValid());
    QVERIFY2(deck.error.contains(QStringLiteral("大纲")),
             qPrintable(QStringLiteral("失败原因里没有「大纲」：") + deck.error));
}

void PptGenerationTests::outlineWithoutBulletsKeepsPage() {
    const QString outline = QStringLiteral(
        "- 背景\n"
        "- 结论\n");
    const PptDeck deck = generateDeckFromOutline(outline, QStringLiteral("汇报"), 12);

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    // 没有要点不等于没有这一页：页还在，只是要点是空的。
    QCOMPARE(deck.slides.size(), 2);
    QCOMPARE(deck.slides.at(0).bullets.size(), 0);
    QCOMPARE(deck.slides.at(1).bullets.size(), 0);
}

void PptGenerationTests::outlineAcceptsNumberedTitles() {
    const QString outline = QStringLiteral(
        "1. 背景\n"
        "   1) 市场在增长\n"
        "2、结论\n"
        "3.收尾\n");
    const PptDeck deck = generateDeckFromOutline(outline, QStringLiteral("汇报"), 12);

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    QCOMPARE(deck.slides.size(), 3);
    QCOMPARE(deck.slides.at(0).title, QStringLiteral("背景"));
    QCOMPARE(deck.slides.at(0).bullets.size(), 1);
    QCOMPARE(deck.slides.at(0).bullets.at(0), QStringLiteral("市场在增长"));
    // 编号后面不带空格也要认：中文大纲里这种写法很常见。
    QCOMPARE(deck.slides.at(2).title, QStringLiteral("收尾"));
}

void PptGenerationTests::outlineIgnoresFourDigitNumberedLines() {
    // 「2024. 年的数据」里的点不是编号标记（编号最多三位），
    // 它既不是标题也不是要点，整行忽略。
    const QString outline = QStringLiteral(
        "2024. 年的数据\n"
        "- 真正的标题\n");
    const PptDeck deck = generateDeckFromOutline(outline, QStringLiteral("汇报"), 12);

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    QCOMPARE(deck.slides.size(), 1);
    QCOMPARE(deck.slides.at(0).title, QStringLiteral("真正的标题"));
}

void PptGenerationTests::outlineIgnoresUnmarkedTopLevelLines() {
    // 顶格又没有标记的行既不是标题也不是要点：忽略掉，但不能因此算失败。
    const QString outline = QStringLiteral(
        "这段是正文，不是标题\n"
        "- 真正的标题\n");
    const PptDeck deck = generateDeckFromOutline(outline, QStringLiteral("汇报"), 12);

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    QCOMPARE(deck.slides.size(), 1);
    QCOMPARE(deck.slides.at(0).title, QStringLiteral("真正的标题"));
}

// -------------------------------------------------------------- 页数 ----

void PptGenerationTests::maxSlidesTruncatesToLimit() {
    const PptDeck deck =
        generateDeckFromTopic(QStringLiteral("季度回顾"), QString(), 2, fakeModel(kThreeSlideJson));

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    // 超过上限要截断，而不是报错。
    QCOMPARE(deck.slides.size(), 2);
    QCOMPARE(deck.slides.at(0).title, QStringLiteral("第一页"));
    QCOMPARE(deck.slides.at(1).title, QStringLiteral("第二页"));
    // 截断后页码仍然连续。
    QCOMPARE(deck.slides.at(1).index, 2);
}

void PptGenerationTests::maxSlidesZeroClampsToOne() {
    const PptDeck deck =
        generateDeckFromTopic(QStringLiteral("季度回顾"), QString(), 0, fakeModel(kThreeSlideJson));

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    // 0 被夹到 1：不是报错，是只给一页。
    QCOMPARE(deck.slides.size(), 1);
}

void PptGenerationTests::maxSlidesAboveMaximumClampsToThirty() {
    // 上限是 30：给一个更大的数，最多也只能有 30 页（截断，不报错）。
    QString outline;
    for (int index = 0; index < 40; ++index) {
        outline += QStringLiteral("- 第 %1 页\n").arg(index + 1);
    }
    const PptDeck clamped = generateDeckFromOutline(outline, QStringLiteral("汇报"), 1000);

    QVERIFY2(clamped.error.isEmpty(), qPrintable(clamped.error));
    QCOMPARE(clamped.slides.size(), 30);
    // 截断后页码仍然连续。
    QCOMPARE(clamped.slides.at(0).index, 1);
    QCOMPARE(clamped.slides.at(29).index, 30);
}

// -------------------------------------------------------------- 解析 ----

void PptGenerationTests::parseReadsTitleAndBullets() {
    const PptDeck deck = parseDeckResponse(kTwoSlideJson);

    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));
    QCOMPARE(deck.title, QStringLiteral("自动保存"));
    QCOMPARE(deck.slides.size(), 2);
    QCOMPARE(deck.slides.at(0).title, QStringLiteral("自动保存"));
    QCOMPARE(deck.slides.at(0).bullets.size(), 2);
    QCOMPARE(deck.slides.at(0).bullets.at(1), QStringLiteral("可以配置延迟"));
    QCOMPARE(deck.slides.at(1).subtitle, QStringLiteral("标注与核对"));
    QCOMPARE(deck.slides.at(1).notes, QStringLiteral("演示用"));
    // 页码 1 起。
    QCOMPARE(deck.slides.at(0).index, 1);
}

void PptGenerationTests::parseRejectsInvalidJson() {
    const PptDeck deck = parseDeckResponse(QByteArray("{不是 JSON"));

    QVERIFY(!deck.error.isEmpty());
    // 坏 JSON 绝不能被吞成一份空的成功结果。
    QVERIFY(deck.slides.isEmpty());
    QVERIFY(!deck.isValid());
}

void PptGenerationTests::parseRejectsMissingSlides() {
    const PptDeck deck = parseDeckResponse(QByteArray(R"({"title":"自动保存"})"));

    QVERIFY(!deck.error.isEmpty());
    QVERIFY(deck.slides.isEmpty());
    QVERIFY(!deck.isValid());
    QVERIFY2(deck.error.contains(QStringLiteral("slides")),
             qPrintable(QStringLiteral("失败原因里没有「slides」：") + deck.error));
}

void PptGenerationTests::parseRejectsEmptySlides() {
    const PptDeck deck = parseDeckResponse(QByteArray(R"({"title":"自动保存","slides":[]})"));

    QVERIFY(!deck.error.isEmpty());
    QVERIFY(deck.slides.isEmpty());
    QVERIFY(!deck.isValid());
}

void PptGenerationTests::parseRejectsNonStringBullet() {
    const PptDeck deck = parseDeckResponse(
        QByteArray(R"({"title":"自动保存","slides":[{"title":"自动保存","bullets":["好的",5]}]})"));

    // 类型不对就算失败：宁可让用户重试，也不要给一份内容悄悄缺一块的 PPT。
    QVERIFY(!deck.error.isEmpty());
    QVERIFY(deck.slides.isEmpty());
    QVERIFY(!deck.isValid());
}

// -------------------------------------------------------------- 导出 ----

void PptGenerationTests::markdownContainsTitleAndPageHeadings() {
    const PptDeck deck = parseDeckResponse(kTwoSlideJson);
    QVERIFY2(deck.error.isEmpty(), qPrintable(deck.error));

    const QString markdown = deckToMarkdown(deck);

    QVERIFY2(markdown.contains(QStringLiteral("# 自动保存")),
             qPrintable(QStringLiteral("没有整套标题：") + markdown));
    QVERIFY2(markdown.contains(QStringLiteral("## 第 1 页：自动保存")),
             qPrintable(QStringLiteral("没有第 1 页标题：") + markdown));
    QVERIFY2(markdown.contains(QStringLiteral("## 第 2 页：批注")),
             qPrintable(QStringLiteral("没有第 2 页标题：") + markdown));
    // 要点用列表项。
    QVERIFY2(markdown.contains(QStringLiteral("- 关闭文档时提示")),
             qPrintable(QStringLiteral("没有要点：") + markdown));
}

void PptGenerationTests::markdownOnEmptyDeckIsSafe() {
    // 空 deck 不能崩：返回空字符串就是「没有什么可导出的」。
    const QString markdown = deckToMarkdown(PptDeck());
    QVERIFY(markdown.isEmpty());

    // 有标题但一页都没有：只出标题那一行。
    PptDeck titleOnly;
    titleOnly.title = QStringLiteral("自动保存");
    const QString partial = deckToMarkdown(titleOnly);
    QVERIFY(partial.contains(QStringLiteral("# 自动保存")));
    QVERIFY(!partial.contains(QStringLiteral("## 第")));
}

void PptGenerationTests::markdownContainsBulletsAndNotes() {
    PptDeck deck;
    deck.title = QStringLiteral("自动保存");
    Slide slide;
    slide.index = 1;
    slide.title = QStringLiteral("自动保存");
    slide.subtitle = QStringLiteral("什么时候提示");
    slide.bullets.append(QStringLiteral("关闭文档时提示"));
    slide.notes.append(QStringLiteral("演示时可以现场关一次文档"));
    deck.slides.append(slide);

    const QString markdown = deckToMarkdown(deck);

    QVERIFY(markdown.contains(QStringLiteral("# 自动保存")));
    QVERIFY(markdown.contains(QStringLiteral("## 第 1 页：自动保存")));
    QVERIFY(markdown.contains(QStringLiteral("什么时候提示")));
    QVERIFY(markdown.contains(QStringLiteral("- 关闭文档时提示")));
    QVERIFY(markdown.contains(QStringLiteral("演示时可以现场关一次文档")));
}

QTEST_GUILESS_MAIN(PptGenerationTests)
#include "PptGenerationTests.moc"
