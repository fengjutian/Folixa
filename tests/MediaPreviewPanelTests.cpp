// 媒体预览面板与 PCM 播放器的测试。
//
// 硬规矩（别破坏）：
//   1) **不许抢用户的桌面**。所有要建窗口的用例都必须先
//      setAttribute(Qt::WA_DontShowOnScreen, true) 再 show()——
//      布局照算、几何是真的，但不弹窗、不置顶、不合成鼠标点击。
//   2) **不许依赖网络**，也不许依赖本机一定有某个媒体文件：
//      用系统示例文件（如 C:\Windows\Media\Alarm01.wav）时先判断存在，
//      不存在就 QSKIP（跳过不算失败）。
//   3) 能用假数据覆盖的分支一律用假数据，别去碰真实解码器。
#include "app/controllers/PcmPlayer.h"
#include "app/panels/MediaPreviewPanel.h"

#include "pdf/media/MediaSource.h"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

using aipdf::app::MediaFrameProvider;
using aipdf::app::MediaPreviewPanel;
using aipdf::app::PcmPlayer;
using aipdf::app::PcmPlayerErrorKind;

// 领域层的命名空间别名：媒体信息、错误分类、播放状态都在这儿。
namespace pdf = aipdf::pdf;

namespace {

// 假的取帧来源：自己喂帧，不碰真实媒体文件。
//
// 为什么要有它：面板的时序逻辑（该出帧时出帧、播完就停、拖动定位）
// 必须在没有媒体文件的地方也能测。
class FakeProvider final : public MediaFrameProvider {
public:
    // open() 成功时返回的媒体信息（测试自己填）。
    pdf::MediaInfo infoToReturn;
    // open() 是否成功。
    bool openSucceeds = true;
    // open() 失败时的错误分类与原因。
    pdf::MediaErrorKind failKind = pdf::MediaErrorKind::None;
    QString failReason;
    // 取够多少帧之后给「结束帧」。
    int videoFrames = 1;
    // 每帧的时间戳间隔（微秒）。
    qint64 frameIntervalUs = 500000;
    // 已经取过多少帧（测试用来断言「不再出帧」）。
    int frameCount = 0;
    // open() 被调了几次。
    int openCount = 0;
    // close() 被调了几次。
    int closeCount = 0;

    bool open(const QString& path, QString* error) override {
        ++openCount;
        frameCount = 0;
        if (!openSucceeds) {
            infoToReturn.ok = false;
            infoToReturn.errorKind = failKind;
            infoToReturn.error = failReason;
            if (error != nullptr) *error = failReason;
            return false;
        }
        infoToReturn.ok = true;
        infoToReturn.errorKind = pdf::MediaErrorKind::None;
        infoToReturn.error.clear();
        infoToReturn.filePath = path;
        return true;
    }

    bool nextFrame(pdf::MediaFrame* frame, QString* error) override {
        Q_UNUSED(error);
        if (frame == nullptr) return false;
        *frame = pdf::MediaFrame();
        ++frameCount;
        if (frameCount > videoFrames) {
            frame->endOfStream = true;
            return true;
        }
        frame->timestampUs = frameCount * frameIntervalUs;
        frame->image = QImage(320, 240, QImage::Format_RGB32);
        frame->image.fill(Qt::black);
        return true;
    }

    void close() override { ++closeCount; }

    pdf::MediaInfo info() const override { return infoToReturn; }
};

// 建一个不弹窗的面板。
//
// WA_DontShowOnScreen：窗口照常布局、照常算几何，但**不会真的出现在桌面上**。
// 这是「界面测试不许抢用户桌面」这条规矩的落地方式。
MediaPreviewPanel* makeOffscreenPanel(QWidget* parent = nullptr) {
    auto* panel = new MediaPreviewPanel(parent);
    panel->setAttribute(Qt::WA_DontShowOnScreen, true);
    panel->resize(480, 360);
    panel->show();
    return panel;
}

pdf::MediaInfo fakeVideoInfo() {
    pdf::MediaInfo info;
    info.ok = true;
    info.mediaType = QStringLiteral("video");
    info.durationMs = 10000;
    info.width = 320;
    info.height = 240;
    info.frameRate = 25.0;
    info.codecName = QStringLiteral("H.264");
    info.sampleRate = 0;
    info.channelCount = 0;
    return info;
}pdf::MediaInfo fakeAudioInfo() {
    pdf::MediaInfo info;
    info.ok = true;
    info.mediaType = QStringLiteral("audio");
    info.durationMs = 65000;
    info.width = 0;
    info.height = 0;
    info.frameRate = 0.0;
    info.codecName = QStringLiteral("PCM");
    info.sampleRate = 44100;
    info.channelCount = 2;
    return info;
}

// 强制返回源文案的翻译器。
//
// 为什么需要：这里的断言写的是**中文源文案**，而 Qt 会先问翻译器——
// 要是这台机器的界面语言是英文，拿到的就是英文，断言就跟界面语言挂钩了。
// 装一个「原样返回源文案」的翻译器，断言就只测代码，不受环境设置影响。
class SourceTextTranslator final : public QTranslator {
public:
    bool isEmpty() const override { return false; }

    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation = nullptr, int n = -1) const override {
        Q_UNUSED(context);
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        return QString::fromUtf8(sourceText);
    }
};


}  // namespace

class MediaPreviewPanelTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 媒体信息文案
    void infoTextForVideoListsEveryField();
    void infoTextForAudioListsSampleRateAndChannels();
    void timeTextFormatsMinutesAndHours();

    // 三类错误
    void threeErrorKindsHaveThreeDifferentTexts();
    void missingFileIsReportedAsOpenFailed();
    void textFileIsReportedAsNotMedia();

    // 画面区
    void audioMediaSaysThereIsNoPicture();
    void videoMediaHasNoPictureHint();

    // 播放时序
    void playbackReachesFinishedAndStopsPullingFrames();
    void pausedPlaybackDoesNotAdvanceOrPullFrames();
    void switchingSourceResetsState();
    void seekMovesPosition();

    // PCM 播放器
    void pcmPlayerErrorTextsAreThreeDifferent();
    void pcmPlayerWriteWithoutOpenFails();
    void pcmPlayerRejectsInvalidFormat();
    void pcmPlayerOpensAndReleasesDevice();

    // 真实媒体文件（有就测，没有就跳过）
    void systemSampleOpensAsAudio();

private:
    // 让界面文案永远是中文源文案，不受本机界面语言影响。
    SourceTextTranslator sourceTextTranslator_;
};

void MediaPreviewPanelTests::initTestCase() {
    // 断言的是中文源文案：装一个原样返回源文案的翻译器，
    // 免得测试结果取决于这台机器的界面语言设置。
    QCoreApplication::installTranslator(&sourceTextTranslator_);
}

void MediaPreviewPanelTests::cleanupTestCase() {
    QCoreApplication::removeTranslator(&sourceTextTranslator_);
}

void MediaPreviewPanelTests::infoTextForVideoListsEveryField() {
    const QString text = aipdf::app::mediaPreviewInfoText(fakeVideoInfo());
    QVERIFY(text.contains(QStringLiteral("视频")));
    QVERIFY(text.contains(QStringLiteral("0:10")));
    QVERIFY(text.contains(QStringLiteral("320")));
    QVERIFY(text.contains(QStringLiteral("240")));
    QVERIFY(text.contains(QStringLiteral("25.00")));
    QVERIFY(text.contains(QStringLiteral("H.264")));
    // 视频没有采样率与声道数：这两项不该出现。
    QVERIFY(!text.contains(QStringLiteral("采样率")));
    QVERIFY(!text.contains(QStringLiteral("声道数")));
}

void MediaPreviewPanelTests::infoTextForAudioListsSampleRateAndChannels() {
    const QString text = aipdf::app::mediaPreviewInfoText(fakeAudioInfo());
    QVERIFY(text.contains(QStringLiteral("音频")));
    QVERIFY(text.contains(QStringLiteral("1:05")));
    QVERIFY(text.contains(QStringLiteral("44100")));
    QVERIFY(text.contains(QStringLiteral("2")));
    // 音频没有画面尺寸。
    QVERIFY(!text.contains(QStringLiteral("画面尺寸")));
}

void MediaPreviewPanelTests::timeTextFormatsMinutesAndHours() {
    QCOMPARE(aipdf::app::mediaPreviewTimeText(0), QStringLiteral("0:00"));
    QCOMPARE(aipdf::app::mediaPreviewTimeText(65000), QStringLiteral("1:05"));
    QCOMPARE(aipdf::app::mediaPreviewTimeText(59999), QStringLiteral("0:59"));
    QCOMPARE(aipdf::app::mediaPreviewTimeText(3661000), QStringLiteral("1:01:01"));
    // 负数按 0 算，不能显示成 "-1:00"。
    QCOMPARE(aipdf::app::mediaPreviewTimeText(-5), QStringLiteral("0:00"));
}

void MediaPreviewPanelTests::threeErrorKindsHaveThreeDifferentTexts() {
    const QString openFailed = aipdf::app::mediaPreviewErrorText(pdf::MediaErrorKind::OpenFailed);
    const QString notMedia = aipdf::app::mediaPreviewErrorText(pdf::MediaErrorKind::NotMedia);
    const QString decodeFailed = aipdf::app::mediaPreviewErrorText(pdf::MediaErrorKind::DecodeFailed);

    QVERIFY(!openFailed.isEmpty());
    QVERIFY(!notMedia.isEmpty());
    QVERIFY(!decodeFailed.isEmpty());
    // 三类必须给三句不同的话：要修的地方完全不一样。
    QVERIFY(openFailed != notMedia);
    QVERIFY(openFailed != decodeFailed);
    QVERIFY(notMedia != decodeFailed);
    // 没有错误时不说话。
    QVERIFY(aipdf::app::mediaPreviewErrorText(pdf::MediaErrorKind::None).isEmpty());
}

void MediaPreviewPanelTests::missingFileIsReportedAsOpenFailed() {
    MediaPreviewPanel* panel = makeOffscreenPanel();
    panel->setSourcePath(QStringLiteral("D:/aipdf-does-not-exist/nothing.mp4"));

    QCOMPARE(panel->errorKind(), pdf::MediaErrorKind::OpenFailed);
    QCOMPARE(panel->errorText(), aipdf::app::mediaPreviewErrorText(pdf::MediaErrorKind::OpenFailed));
    QVERIFY(panel->errorText() != aipdf::app::mediaPreviewErrorText(pdf::MediaErrorKind::NotMedia));
    delete panel;
}

void MediaPreviewPanelTests::textFileIsReportedAsNotMedia() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("not-media.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not a media file at all");
    file.close();

    MediaPreviewPanel* panel = makeOffscreenPanel();
    panel->setSourcePath(path);

    QCOMPARE(panel->errorKind(), pdf::MediaErrorKind::NotMedia);
    QCOMPARE(panel->errorText(), aipdf::app::mediaPreviewErrorText(pdf::MediaErrorKind::NotMedia));
    QVERIFY(panel->errorText() != aipdf::app::mediaPreviewErrorText(pdf::MediaErrorKind::OpenFailed));
    delete panel;
}

void MediaPreviewPanelTests::audioMediaSaysThereIsNoPicture() {
    FakeProvider provider;
    provider.infoToReturn = fakeAudioInfo();

    MediaPreviewPanel* panel = makeOffscreenPanel();
    panel->setFrameProvider(&provider);
    panel->setSourcePath(QStringLiteral("fake.wav"));

    QCOMPARE(panel->errorKind(), pdf::MediaErrorKind::None);
    QVERIFY(panel->videoHintText().contains(QStringLiteral("没有画面")));
    // 画面区上要真的写着这句话，不能留一块空白。
    QCOMPARE(panel->videoAreaText(), panel->videoHintText());
    delete panel;
}

void MediaPreviewPanelTests::videoMediaHasNoPictureHint() {
    FakeProvider provider;
    provider.infoToReturn = fakeVideoInfo();

    MediaPreviewPanel* panel = makeOffscreenPanel();
    panel->setFrameProvider(&provider);
    panel->setSourcePath(QStringLiteral("fake.mp4"));

    QVERIFY(panel->videoHintText().isEmpty());
    QVERIFY(panel->videoAreaText().isEmpty());
    delete panel;
}

void MediaPreviewPanelTests::playbackReachesFinishedAndStopsPullingFrames() {
    FakeProvider provider;
    provider.infoToReturn = fakeVideoInfo();
    provider.videoFrames = 2;

    MediaPreviewPanel* panel = makeOffscreenPanel();
    panel->setFrameProvider(&provider);
    panel->setSourcePath(QStringLiteral("fake.mp4"));
    panel->play();

    panel->advancePlayback(0);
    QCOMPARE(provider.frameCount, 1);
    panel->advancePlayback(40);
    QCOMPARE(provider.frameCount, 2);
    // 第三拍拿到的是结束帧：播完。
    panel->advancePlayback(80);
    QCOMPARE(provider.frameCount, 3);
    QCOMPARE(panel->playbackState(), pdf::MediaPlaybackState::Finished);
    QCOMPARE(panel->positionMs(), panel->durationMs());

    // 播完之后**不能再出帧**：否则就是空转，界面会一直闪。
    panel->advancePlayback(200);
    panel->advancePlayback(400);
    QCOMPARE(provider.frameCount, 3);
    QCOMPARE(panel->playbackState(), pdf::MediaPlaybackState::Finished);
    delete panel;
}

void MediaPreviewPanelTests::pausedPlaybackDoesNotAdvanceOrPullFrames() {
    FakeProvider provider;
    provider.infoToReturn = fakeVideoInfo();
    provider.videoFrames = 100;

    MediaPreviewPanel* panel = makeOffscreenPanel();
    panel->setFrameProvider(&provider);
    panel->setSourcePath(QStringLiteral("fake.mp4"));
    panel->play();
    panel->advancePlayback(0);
    panel->advancePlayback(50);
    QVERIFY(panel->positionMs() > 0);

    const int framesBeforePause = provider.frameCount;
    const qint64 positionBeforePause = panel->positionMs();
    panel->pause();

    panel->advancePlayback(500);
    panel->advancePlayback(900);
    QCOMPARE(provider.frameCount, framesBeforePause);
    QCOMPARE(panel->positionMs(), positionBeforePause);
    QCOMPARE(panel->playbackState(), pdf::MediaPlaybackState::Paused);
    delete panel;
}

void MediaPreviewPanelTests::switchingSourceResetsState() {
    FakeProvider provider;
    provider.infoToReturn = fakeVideoInfo();
    provider.videoFrames = 100;

    MediaPreviewPanel* panel = makeOffscreenPanel();
    panel->setFrameProvider(&provider);
    panel->setSourcePath(QStringLiteral("first.mp4"));
    panel->play();
    panel->advancePlayback(0);
    panel->advancePlayback(60);
    QVERIFY(panel->positionMs() > 0);

    panel->setSourcePath(QStringLiteral("second.mp4"));

    QCOMPARE(panel->positionMs(), 0);
    QCOMPARE(panel->playbackState(), pdf::MediaPlaybackState::Stopped);
    QVERIFY(panel->errorText().isEmpty());
    QCOMPARE(panel->errorKind(), pdf::MediaErrorKind::None);
    // 换文件必须把上一个文件的解码器关掉。
    QVERIFY(provider.closeCount >= 1);
    delete panel;
}

void MediaPreviewPanelTests::seekMovesPosition() {
    FakeProvider provider;
    provider.infoToReturn = fakeVideoInfo();
    provider.videoFrames = 100;

    MediaPreviewPanel* panel = makeOffscreenPanel();
    panel->setFrameProvider(&provider);
    panel->setSourcePath(QStringLiteral("fake.mp4"));

    panel->seekTo(1500);
    QCOMPARE(panel->positionMs(), 1500);
    // 领域层的语义：seek 只改位置，状态只有从「已播完」回到「暂停」这一种变化。
    // 这里是从停止态定位，状态仍然是停止。
    QCOMPARE(panel->playbackState(), pdf::MediaPlaybackState::Stopped);

    // 播完之后往回拖：回到「暂停在中间」，等用户继续。
    panel->play();
    panel->advancePlayback(0);
    panel->advancePlayback(40);
    panel->seekTo(panel->durationMs());
    QCOMPARE(panel->playbackState(), pdf::MediaPlaybackState::Finished);
    panel->seekTo(2000);
    QCOMPARE(panel->positionMs(), 2000);
    QCOMPARE(panel->playbackState(), pdf::MediaPlaybackState::Paused);
    delete panel;
}

void MediaPreviewPanelTests::pcmPlayerErrorTextsAreThreeDifferent() {
    const QString noDevice = aipdf::app::pcmPlayerErrorText(PcmPlayerErrorKind::NoDevice);
    const QString openFailed = aipdf::app::pcmPlayerErrorText(PcmPlayerErrorKind::OpenFailed);
    const QString writeFailed = aipdf::app::pcmPlayerErrorText(PcmPlayerErrorKind::WriteFailed);

    QVERIFY(!noDevice.isEmpty());
    QVERIFY(!openFailed.isEmpty());
    QVERIFY(!writeFailed.isEmpty());
    QVERIFY(noDevice != openFailed);
    QVERIFY(noDevice != writeFailed);
    QVERIFY(openFailed != writeFailed);
    QVERIFY(aipdf::app::pcmPlayerErrorText(PcmPlayerErrorKind::None).isEmpty());
}

void MediaPreviewPanelTests::pcmPlayerWriteWithoutOpenFails() {
    PcmPlayer player;
    QString error;
    // 没打开就写：这是「写 PCM 失败」，不是「没设备」。
    QVERIFY(!player.pushPcm(QByteArray(64, 0), &error));
    QCOMPARE(error, aipdf::app::pcmPlayerErrorText(PcmPlayerErrorKind::WriteFailed));
    QVERIFY(!player.isOpen());
}

void MediaPreviewPanelTests::pcmPlayerRejectsInvalidFormat() {
    const int devicesBefore = PcmPlayer::liveDeviceCount();
    PcmPlayer player;
    QString error;
    // 采样率不合法：属于「打不开设备」，和「没有设备」不是一回事。
    QVERIFY(!player.open(0, 2, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!player.isOpen());
    QCOMPARE(PcmPlayer::liveDeviceCount(), devicesBefore);
}

void MediaPreviewPanelTests::pcmPlayerOpensAndReleasesDevice() {
    const int devicesBefore = PcmPlayer::liveDeviceCount();
    PcmPlayer player;
    QString error;
    if (!player.open(44100, 2, &error)) {
        QSKIP(qPrintable(QStringLiteral("这台机器没有可用的音频输出设备：") + error));
    }
    QVERIFY(player.isOpen());
    QCOMPARE(PcmPlayer::liveDeviceCount(), devicesBefore + 1);

    // 推 0.5 秒静音：听不见，但确实排在声卡队列里。
    const QByteArray pcm(44100 * 2 * 2 / 2, 0);
    QVERIFY(player.pushPcm(pcm, &error));
    QVERIFY(player.queuedBytes() > 0);

    player.close();
    // 关掉之后：句柄没了、排队清空、设备计数回到原值。
    QVERIFY(!player.isOpen());
    QCOMPARE(player.queuedBytes(), 0);
    QCOMPARE(PcmPlayer::liveDeviceCount(), devicesBefore);
    // 重复关闭是安全的。
    player.close();
    QCOMPARE(PcmPlayer::liveDeviceCount(), devicesBefore);
}

void MediaPreviewPanelTests::systemSampleOpensAsAudio() {
    const QString path = QStringLiteral("C:/Windows/Media/Alarm01.wav");
    if (!QFile::exists(path)) {
        QSKIP("本机没有系统示例音频文件，跳过。");
    }

    MediaPreviewPanel* panel = makeOffscreenPanel();
    panel->setSourcePath(path);

    QCOMPARE(panel->errorKind(), pdf::MediaErrorKind::None);
    QVERIFY(panel->errorText().isEmpty());
    QVERIFY(panel->mediaInfoText().contains(QStringLiteral("音频")));
    QVERIFY(panel->videoHintText().contains(QStringLiteral("没有画面")));
    QVERIFY(panel->durationMs() > 0);
    delete panel;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    MediaPreviewPanelTests testCase;
    return QTest::qExec(&testCase, argc, argv);
}

#include "MediaPreviewPanelTests.moc"
