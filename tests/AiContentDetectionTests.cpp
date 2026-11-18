// AI 内容检测的规则测试。
//
// 这一层最容易出的错是「把解析不出来当成不是 AI 写的」：
// 界面上一旦显示「完全人工」，用户就会拿它当一个确定结论用。
// 所以这组测试里有一半在盯失败路径。
#include "ai/content/AiContentDetection.h"

#include <QtTest>

using namespace aipdf::ai;

namespace {

// 有没有中文字符：用来验「错误原因必须是中文」。
bool containsChinese(const QString& text) {
    for (const QChar& unit : text) {
        if (unit.unicode() >= 0x4E00 && unit.unicode() <= 0x9FFF) {
            return true;
        }
    }
    return false;
}

}  // namespace

class AiContentDetectionTests : public QObject {
    Q_OBJECT

private slots:
    void promptContainsInputText();
    void emptyTextFailsWithChineseReason();
    void parsesAiProbabilityShape();
    void parsesLikelyAiShape();
    void likelyAiFalseMeansHuman();
    void invalidJsonFails();
    void missingVerdictFieldFails();
    void missingReasonFails();
    void confidenceIsClamped();
    void categoryMapsThreeTiers();
    void halfBoundaryGoesToSuspectSide();
    void rawResponseIsPreserved();
    void outOfRangeProbabilityIsClamped();
    void fencedJsonIsAccepted();
    void probabilityAsStringIsAccepted();
};

void AiContentDetectionTests::promptContainsInputText() {
    const QString text = QStringLiteral("这是一段用来检测的中文文本，内容平淡。");
    QString error;
    const QByteArray prompt = buildDetectionPrompt(text, &error);

    QVERIFY(!prompt.isEmpty());
    QVERIFY(error.isEmpty());
    // 待检文本必须原样出现在提示词里。
    QVERIFY(prompt.contains(text.toUtf8()));
    // 提示词还得说清输出格式，否则模型回的不是 JSON，解析必然失败。
    QVERIFY(prompt.contains("ai_probability"));
}

void AiContentDetectionTests::emptyTextFailsWithChineseReason() {
    QString error;
    const QByteArray prompt = buildDetectionPrompt(QString(), &error);

    QVERIFY(prompt.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(containsChinese(error));

    // 只有空白也一样不行。
    QString blankError;
    QVERIFY(buildDetectionPrompt(QStringLiteral("   \n\t "), &blankError).isEmpty());
    QVERIFY(containsChinese(blankError));
}

void AiContentDetectionTests::parsesAiProbabilityShape() {
    const QByteArray response = QByteArrayLiteral(
        "{\"ai_probability\": 0.82, \"reason\": \"句式高度一致\", \"category\": \"疑似 AI 生成\"}");
    const DetectionVerdict verdict = parseDetectionResponse(response);

    QVERIFY(verdict.ok);
    QCOMPARE(verdict.confidence, 82);
    QVERIFY(verdict.likelyAiGenerated);
    QCOMPARE(verdict.category, QStringLiteral("疑似 AI 生成"));
    QCOMPARE(verdict.reason, QStringLiteral("句式高度一致"));
}

void AiContentDetectionTests::parsesLikelyAiShape() {
    const QByteArray response =
        QByteArrayLiteral("{\"likely_ai\": true, \"confidence\": 75, \"reason\": \"用词过于规整\"}");
    const DetectionVerdict verdict = parseDetectionResponse(response);

    QVERIFY(verdict.ok);
    QVERIFY(verdict.likelyAiGenerated);
    QCOMPARE(verdict.confidence, 75);
    QCOMPARE(verdict.category, QStringLiteral("疑似 AI 生成"));
    QCOMPARE(verdict.reason, QStringLiteral("用词过于规整"));
}

void AiContentDetectionTests::likelyAiFalseMeansHuman() {
    const QByteArray response = QByteArrayLiteral(
        "{\"likely_ai\": false, \"confidence\": 90, \"reason\": \"有个人写的痕迹\"}");
    const DetectionVerdict verdict = parseDetectionResponse(response);

    QVERIFY(verdict.ok);
    QVERIFY(!verdict.likelyAiGenerated);
    QCOMPARE(verdict.confidence, 90);
    QCOMPARE(verdict.category, QStringLiteral("完全人工"));
}

void AiContentDetectionTests::invalidJsonFails() {
    const DetectionVerdict verdict = parseDetectionResponse(QByteArrayLiteral("这不是 JSON"));
    QVERIFY(!verdict.ok);
    // 关键：失败时不能显示成「完全人工」——那等于给了一个假的「不是 AI 写的」结论。
    QCOMPARE(verdict.category, QStringLiteral("无法判断"));
    QVERIFY(containsChinese(verdict.reason));

    // 花括号在、但语法坏掉，同样不能解析。
    const DetectionVerdict broken =
        parseDetectionResponse(QByteArrayLiteral("{ai_probability: 0.8, reason: 缺引号}"));
    QVERIFY(!broken.ok);
    QCOMPARE(broken.category, QStringLiteral("无法判断"));
}

void AiContentDetectionTests::missingVerdictFieldFails() {
    // 只有理由、没有判定字段：不能当成「不是 AI 写的」。
    const DetectionVerdict verdict =
        parseDetectionResponse(QByteArrayLiteral("{\"reason\": \"看不太出来\"}"));
    QVERIFY(!verdict.ok);
    QCOMPARE(verdict.category, QStringLiteral("无法判断"));
    QVERIFY(containsChinese(verdict.reason));
}

void AiContentDetectionTests::missingReasonFails() {
    // 字段缺失即失败：没有理由的判定没人能信，也不该显示成结论。
    const DetectionVerdict verdict =
        parseDetectionResponse(QByteArrayLiteral("{\"ai_probability\": 0.9}"));
    QVERIFY(!verdict.ok);
    QCOMPARE(verdict.category, QStringLiteral("无法判断"));
    QVERIFY(containsChinese(verdict.reason));

    // 理由是空字符串，同样算缺失。
    const DetectionVerdict blank =
        parseDetectionResponse(QByteArrayLiteral("{\"ai_probability\": 0.9, \"reason\": \"  \"}"));
    QVERIFY(!blank.ok);
    QCOMPARE(blank.category, QStringLiteral("无法判断"));
}

void AiContentDetectionTests::confidenceIsClamped() {
    const DetectionVerdict high = parseDetectionResponse(
        QByteArrayLiteral("{\"likely_ai\": true, \"confidence\": 150, \"reason\": \"像\"}"));
    QVERIFY(high.ok);
    QCOMPARE(high.confidence, 100);

    const DetectionVerdict low = parseDetectionResponse(
        QByteArrayLiteral("{\"likely_ai\": true, \"confidence\": -20, \"reason\": \"像\"}"));
    QVERIFY(low.ok);
    QCOMPARE(low.confidence, 0);
}

void AiContentDetectionTests::categoryMapsThreeTiers() {
    QCOMPARE(detectionCategory(0.95), QStringLiteral("疑似 AI 生成"));
    QCOMPARE(detectionCategory(0.60), QStringLiteral("疑似 AI 改写"));
    QCOMPARE(detectionCategory(0.10), QStringLiteral("完全人工"));
}

void AiContentDetectionTests::halfBoundaryGoesToSuspectSide() {
    // 0.5 归到「疑似」那一侧：漏判（把 AI 写的判成人写的）代价更高。
    QCOMPARE(detectionCategory(0.50), QStringLiteral("疑似 AI 改写"));
    QCOMPARE(detectionCategory(0.4999), QStringLiteral("完全人工"));
    QCOMPARE(detectionCategory(0.75), QStringLiteral("疑似 AI 生成"));
    QCOMPARE(detectionCategory(0.7499), QStringLiteral("疑似 AI 改写"));
}

void AiContentDetectionTests::rawResponseIsPreserved() {
    const QByteArray response =
        QByteArrayLiteral("{\"ai_probability\": 0.3, \"reason\": \"像人写的\"}");
    const DetectionVerdict verdict = parseDetectionResponse(response);
    QVERIFY(verdict.ok);
    QCOMPARE(verdict.rawResponse, QString::fromUtf8(response));

    // 解析失败时也要留着原文：排查模型到底返回了什么，全靠它。
    const QByteArray broken = QByteArrayLiteral("这不是 JSON");
    const DetectionVerdict failed = parseDetectionResponse(broken);
    QVERIFY(!failed.ok);
    QCOMPARE(failed.rawResponse, QString::fromUtf8(broken));
}

void AiContentDetectionTests::outOfRangeProbabilityIsClamped() {
    const DetectionVerdict over = parseDetectionResponse(
        QByteArrayLiteral("{\"ai_probability\": 1.8, \"reason\": \"非常像\"}"));
    QVERIFY(over.ok);
    QCOMPARE(over.confidence, 100);
    QCOMPARE(over.category, QStringLiteral("疑似 AI 生成"));
    QVERIFY(over.likelyAiGenerated);

    const DetectionVerdict under = parseDetectionResponse(
        QByteArrayLiteral("{\"ai_probability\": -0.4, \"reason\": \"不像\"}"));
    QVERIFY(under.ok);
    QCOMPARE(under.confidence, 0);
    QCOMPARE(under.category, QStringLiteral("完全人工"));
    QVERIFY(!under.likelyAiGenerated);
}

void AiContentDetectionTests::fencedJsonIsAccepted() {
    // 模型很爱把 JSON 包在 ```json 代码块里，直接当 JSON 解析必然失败。
    const QByteArray response = QByteArrayLiteral(
        "```json\n{\"ai_probability\": 0.7, \"reason\": \"像\"}\n```");
    const DetectionVerdict verdict = parseDetectionResponse(response);

    QVERIFY(verdict.ok);
    QCOMPARE(verdict.confidence, 70);
}

void AiContentDetectionTests::probabilityAsStringIsAccepted() {
    // 有些模型把数字写成字符串："0.62"。
    const QByteArray response =
        QByteArrayLiteral("{\"ai_probability\": \"0.62\", \"reason\": \"像\"}");
    const DetectionVerdict verdict = parseDetectionResponse(response);

    QVERIFY(verdict.ok);
    QCOMPARE(verdict.confidence, 62);
}

QTEST_GUILESS_MAIN(AiContentDetectionTests)
#include "AiContentDetectionTests.moc"
