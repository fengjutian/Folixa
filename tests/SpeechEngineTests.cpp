// 朗读引擎的行为测试。
//
// 前提：测试机不一定装了语音（Windows Server / CI 常见），所以这里
// 不假设「能出声」。能朗读就断言逐词下标对得上，不能朗读就断言失败原因
// 非空——两条路都必须能收场，不许挂死。
#include "speech/SpeechEngine.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

using namespace aipdf::speech;

namespace {

// 有上限的等待：绝不能无限等，否则没有语音的 CI 上会挂死。
bool waitForAny(QSignalSpy& first, QSignalSpy& second, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (!first.isEmpty() || !second.isEmpty()) {
            return true;
        }
        QTest::qWait(50);
    }
    return !first.isEmpty() || !second.isEmpty();
}

}  // namespace

class SpeechEngineTests : public QObject {
    Q_OBJECT

private slots:
    void isSupportedDoesNotCrash();
    void unavailableReasonIsNotEmptyWhenUnsupported();
    void emptyTextDoesNotStartSpeaking();
    void speakingShortTextFinishesOrFails();
    void consecutiveSpeakDoesNotCrash();
    void stopPreventsFurtherWordEvents();
    void wordIndexesMatchSplitIntoWords();
    void destroyingWhileSpeakingIsSafe();
    void settingsAreStoredAndNormalized();
    void normalizeClampsRateAndVolume();
    void availableVoicesDoesNotCrash();
};

void SpeechEngineTests::isSupportedDoesNotCrash() {
    const bool supported = SpeechEngine::isSupported();
    // 有语音与没语音都不算失败：这里只保证探测本身不会崩。
    QVERIFY(supported || !supported);
}

void SpeechEngineTests::unavailableReasonIsNotEmptyWhenUnsupported() {
    SpeechEngine engine;
    if (SpeechEngine::isSupported()) {
        QVERIFY(engine.unavailableReason().isEmpty());
    } else {
        QVERIFY(!engine.unavailableReason().isEmpty());
    }
}

void SpeechEngineTests::emptyTextDoesNotStartSpeaking() {
    SpeechEngine engine;
    QSignalSpy wordSpy(&engine, &SpeechEngine::wordStarted);
    QSignalSpy failedSpy(&engine, &SpeechEngine::failed);

    engine.speak(QString());
    QVERIFY(wordSpy.isEmpty());
    QVERIFY(!engine.isSpeaking());

    // 也不会过一会儿冒出一个迟到的词事件。
    QTest::qWait(300);
    QVERIFY(wordSpy.isEmpty());
    QVERIFY(failedSpy.isEmpty());
}

void SpeechEngineTests::speakingShortTextFinishesOrFails() {
    SpeechEngine engine;
    QSignalSpy finishedSpy(&engine, &SpeechEngine::finished);
    QSignalSpy failedSpy(&engine, &SpeechEngine::failed);

    engine.speak(QStringLiteral("hello"));

    // 两种结局都算通过：读完（finished）或这台机器读不了（failed）。
    QVERIFY(waitForAny(finishedSpy, failedSpy, 8000));
    if (!failedSpy.isEmpty()) {
        const QString reason = failedSpy.first().at(0).toString();
        QVERIFY2(!reason.isEmpty(), "失败必须给出原因，界面才有东西可显示");
    }
}

void SpeechEngineTests::consecutiveSpeakDoesNotCrash() {
    SpeechEngine engine;
    QSignalSpy failedSpy(&engine, &SpeechEngine::failed);

    engine.speak(QStringLiteral("first"));
    engine.speak(QStringLiteral("second"));
    engine.speak(QStringLiteral("third"));

    QTest::qWait(500);
    engine.stop();
    QVERIFY(!engine.isSpeaking());
}

void SpeechEngineTests::stopPreventsFurtherWordEvents() {
    SpeechEngine engine;
    QSignalSpy wordSpy(&engine, &SpeechEngine::wordStarted);

    engine.speak(QStringLiteral("one two three four five six seven eight"));
    QTest::qWait(400);
    engine.stop();

    const int countAfterStop = wordSpy.count();
    QVERIFY(!engine.isSpeaking());
    QTest::qWait(800);
    // 停止之后不能再冒出词事件（子进程已被杀掉）。
    QCOMPARE(wordSpy.count(), countAfterStop);
}

void SpeechEngineTests::wordIndexesMatchSplitIntoWords() {
    SpeechEngine engine;
    QSignalSpy wordSpy(&engine, &SpeechEngine::wordStarted);
    QSignalSpy finishedSpy(&engine, &SpeechEngine::finished);
    QSignalSpy failedSpy(&engine, &SpeechEngine::failed);

    const QString text = QStringLiteral("hello world");
    const QVector<SpeechWord> words = splitIntoWords(text);

    engine.speak(text);
    // 只等第一个词事件：单词的起点下标对得上就够了，不必等整段读完。
    waitForAny(wordSpy, failedSpy, 5000);

    for (const QList<QVariant>& args : wordSpy) {
        const int index = args.at(0).toInt();
        QVERIFY(index >= 0);
        QVERIFY(index < words.size());
        // 下标必须和 splitIntoWords 对得上，界面才知道该高亮哪个词。
        QCOMPARE(args.at(1).toString(), words.at(index).text);
    }
}

void SpeechEngineTests::destroyingWhileSpeakingIsSafe() {
    QElapsedTimer timer;
    timer.start();
    {
        SpeechEngine engine;
        engine.speak(QStringLiteral("this should be interrupted by destruction"));
        QTest::qWait(200);
        // 引擎在这里析构：子进程必须被收掉，不能留下孤儿进程。
    }
    QVERIFY2(timer.elapsed() < 5000, "析构不能挂住");
}

void SpeechEngineTests::settingsAreStoredAndNormalized() {
    SpeechEngine engine;
    SpeechSettings settings;
    settings.voice = QStringLiteral("  Microsoft Test Voice  ");
    settings.rate = 3;
    settings.volume = 80;
    engine.setSettings(settings);

    const SpeechSettings stored = engine.settings();
    QCOMPARE(stored.voice, QStringLiteral("Microsoft Test Voice"));
    QCOMPARE(stored.rate, 3);
    QCOMPARE(stored.volume, 80);
}

void SpeechEngineTests::normalizeClampsRateAndVolume() {
    SpeechSettings settings;
    settings.rate = 99;
    settings.volume = 250;
    SpeechSettings normalized = normalizeSpeechSettings(settings);
    QCOMPARE(normalized.rate, kSpeechRateMax);
    QCOMPARE(normalized.volume, 100);

    settings.rate = -99;
    settings.volume = -10;
    normalized = normalizeSpeechSettings(settings);
    QCOMPARE(normalized.rate, kSpeechRateMin);
    QCOMPARE(normalized.volume, 0);

    // 合法值不能被改动。
    settings.rate = 0;
    settings.volume = 50;
    normalized = normalizeSpeechSettings(settings);
    QCOMPARE(normalized.rate, 0);
    QCOMPARE(normalized.volume, 50);
}

void SpeechEngineTests::availableVoicesDoesNotCrash() {
    // 系统没装语音时列表是空的，这里只保证探测本身不会崩。
    const QVector<SpeechVoice> voices = SpeechEngine::availableVoices();
    for (const SpeechVoice& voice : voices) {
        QVERIFY(!voice.name.isEmpty());
    }
}

QTEST_MAIN(SpeechEngineTests)
#include "SpeechEngineTests.moc"
