// 录音器的单元测试。
//
// 这台机器上可能没有麦克风（CI 就没有），所以测试不强制"一定要录到音"：
// 有设备时验证 WAV 头与数据，没有设备时验证**优雅失败**——不崩、给出原因。

#include "app/AudioRecorder.h"

#include <QTest>

using aipdf::app::AudioRecorder;

class AudioRecorderTests : public QObject {
    Q_OBJECT

private slots:
    void startEitherSucceedsOrExplainsWhy();
    void stopWithoutStartReturnsNothing();
};

void AudioRecorderTests::startEitherSucceedsOrExplainsWhy() {
    AudioRecorder recorder;
    QString error;
    const bool started = recorder.start(&error);
    if (!started) {
        QVERIFY2(!error.isEmpty(), "失败时必须说明原因");
        QVERIFY(!recorder.isRecording());
        return;
    }
    QVERIFY(recorder.isRecording());
    QTest::qWait(250);
    const QByteArray wav = recorder.stop();
    QVERIFY(!recorder.isRecording());
    // 有设备时至少要有合法的 RIFF/WAVE 头。
    QVERIFY(wav.startsWith("RIFF"));
    QVERIFY(wav.contains("WAVE"));
}

void AudioRecorderTests::stopWithoutStartReturnsNothing() {
    AudioRecorder recorder;
    QVERIFY(recorder.stop().isEmpty());
}

QTEST_MAIN(AudioRecorderTests)
#include "AudioRecorderTests.moc"
