// 音视频解码（Media Foundation）与播放状态机的单元测试。
//
// 覆盖三块：
//   1) 播放状态机：播放 → 暂停 → 继续 → 停止 → 播完，
//      全部是纯逻辑，不依赖系统解码器；
//   2) 错误分类：文件打不开 / 不是媒体文件 / 解码失败，三类必须分得开；
//   3) 真机解码：用系统自带的媒体样本跑一遍真的解码。
//
// 真机样本**只在测试里读**，绝不拷进仓库。样本路径可以用环境变量覆盖：
//   AIPDF_MEDIA_AUDIO_SAMPLE  —— WAV 样本
//   AIPDF_MEDIA_MP3_SAMPLE    —— MP3 样本
//   AIPDF_MEDIA_VIDEO_SAMPLE  —— 视频样本
// 找不到样本时用 QSKIP 跳过，**不红**。

#include "pdf/media/MediaPlayback.h"
#include "pdf/media/MediaSource.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using aipdf::pdf::MediaDecoder;
using aipdf::pdf::MediaDecoderOptions;
using aipdf::pdf::MediaErrorKind;
using aipdf::pdf::MediaFrame;
using aipdf::pdf::MediaInfo;
using aipdf::pdf::MediaPlaybackController;
using aipdf::pdf::MediaPlaybackState;
using aipdf::pdf::openMediaFile;
using aipdf::pdf::rgb32BufferToImage;

namespace {

// 找一个可用的媒体样本。
//
// 顺序：环境变量 → 给定的候选目录里第一个能用的文件。
// 找不到就返回空串（调用方 QSKIP）。
QString findSample(const char* envVar, const QStringList& searchDirs, const QStringList& suffixes) {
    const QByteArray override = qgetenv(envVar);
    if (!override.isEmpty()) {
        const QString path = QString::fromLocal8Bit(override);
        if (QFileInfo::exists(path)) return path;
    }
    for (const QString& dir : searchDirs) {
        const QFileInfo dirInfo(dir);
        if (!dirInfo.isDir()) continue;
        QDir directory(dir);
        const QFileInfoList files = directory.entryInfoList(suffixes, QDir::Files | QDir::Readable);
        for (const QFileInfo& file : files) {
            if (file.size() > 0) return file.absoluteFilePath();
        }
    }
    return QString();
}

// 系统里自带的媒体样本目录。
QStringList windowsMediaDirs() { return {QStringLiteral("C:/Windows/Media")}; }

QStringList windowsMp3Dirs() {
    return {QStringLiteral("C:/Windows/ImmersiveControlPanel/SystemSettings/Assets")};
}

void appendU16(QByteArray& out, quint16 value) {
    out.append(static_cast<char>(value & 0xff));
    out.append(static_cast<char>((value >> 8) & 0xff));
}

void appendU32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>(value & 0xff));
    out.append(static_cast<char>((value >> 8) & 0xff));
    out.append(static_cast<char>((value >> 16) & 0xff));
    out.append(static_cast<char>((value >> 24) & 0xff));
}

// 造一个「RIFF 头是好的、里面的格式是坏的」样本。
//
// 为什么要专门造：系统对媒体文件相当宽容，光看「能不能打开」说明不了问题；
// 真正要守住的是：**解码器永远不能把一次失败说成「成功但没有内容」**。
QString writeBrokenMediaFile(const QTemporaryDir& dir) {
    const QString path = dir.filePath(QStringLiteral("broken.wav"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return QString();

    QByteArray bytes;
    bytes.append("RIFF");
    appendU32(bytes, 100);
    bytes.append("WAVE");
    bytes.append("fmt ");
    appendU32(bytes, 16);
    appendU16(bytes, 0x9999);  // 不认识的格式标记
    appendU16(bytes, 1);
    appendU32(bytes, 22050);
    appendU32(bytes, 44100);
    appendU16(bytes, 2);
    appendU16(bytes, 16);
    bytes.append("data");
    appendU32(bytes, 64);
    bytes.append(QByteArray(64, '\x7f'));
    file.write(bytes);
    file.close();
    return path;
}

}  // namespace

class MediaSourceTests : public QObject {
    Q_OBJECT

private slots:
    // ---------------------------------------------------------------------
    // 播放状态机（纯逻辑）
    // ---------------------------------------------------------------------

    // 初始状态：没在播，位置是 0。
    void playbackStartsStopped() {
        MediaPlaybackController controller(5000);
        QCOMPARE(controller.state(), MediaPlaybackState::Stopped);
        QCOMPARE(controller.positionMs(), 0);
        QCOMPARE(controller.durationMs(), 5000);
    }

    // 播放时位置跟着墙钟往前走，并且按帧率出帧。
    void playbackAdvancesPositionAndEmitsFrames() {
        MediaPlaybackController controller(5000);
        controller.setFrameRate(25.0);  // 一帧 40 毫秒
        controller.play();

        MediaFrame frame;
        // 第一次调用只建立时间基准（位置不变），但**会出第一帧**——
        // 播放刚开始就要有画面，不能等第二拍。
        QVERIFY(controller.advanceTo(0, &frame));
        QCOMPARE(controller.positionMs(), 0);

        // 走过 100 毫秒：位置该到 100，并且出了帧。
        QVERIFY(controller.advanceTo(100, &frame));
        QCOMPARE(controller.positionMs(), 100);
        QCOMPARE(frame.timestampUs, 100000);

        // 再走 10 毫秒：还没到下一帧（40 毫秒一帧），不该出帧。
        QVERIFY(!controller.advanceTo(110, &frame));
        QCOMPARE(controller.positionMs(), 110);

        // 走到 140 毫秒：又该出帧了。
        QVERIFY(controller.advanceTo(140, &frame));
        QCOMPARE(controller.positionMs(), 140);
    }

    // 暂停：位置不动，也不再出帧。
    void playbackPauseFreezesPositionAndStopsFrames() {
        MediaPlaybackController controller(5000);
        controller.setFrameRate(25.0);
        controller.play();
        controller.advanceTo(0, nullptr);   // 建立时间基准
        controller.advanceTo(100, nullptr); // 位置到 100
        QCOMPARE(controller.positionMs(), 100);

        controller.pause();
        QCOMPARE(controller.state(), MediaPlaybackState::Paused);

        MediaFrame frame;
        // 暂停之后墙钟继续走，但位置一步都不许动，也不许出帧。
        QVERIFY(!controller.advanceTo(1000, &frame));
        QCOMPARE(controller.positionMs(), 100);
        QVERIFY(!controller.advanceTo(5000, &frame));
        QCOMPARE(controller.positionMs(), 100);
        QCOMPARE(controller.state(), MediaPlaybackState::Paused);
    }

    // 继续：从暂停的位置接着走，不是从头开始。
    void playbackResumeContinuesFromPausedPosition() {
        MediaPlaybackController controller(5000);
        controller.play();
        controller.advanceTo(0, nullptr);
        controller.advanceTo(200, nullptr);
        controller.pause();
        QCOMPARE(controller.positionMs(), 200);

        // 暂停期间墙钟走了一大截，这段不能算进播放进度。
        controller.advanceTo(3000, nullptr);
        QCOMPARE(controller.positionMs(), 200);

        controller.play();
        QCOMPARE(controller.state(), MediaPlaybackState::Playing);
        // 重新开始计时：这一拍只记基准，位置还是 200。
        controller.advanceTo(3000, nullptr);
        QCOMPARE(controller.positionMs(), 200);
        // 再走 50 毫秒：位置到 250。
        controller.advanceTo(3050, nullptr);
        QCOMPARE(controller.positionMs(), 250);
    }

    // 停止：位置回到 0，状态回到 Stopped。
    void playbackStopReturnsToZero() {
        MediaPlaybackController controller(5000);
        controller.play();
        controller.advanceTo(0, nullptr);
        controller.advanceTo(800, nullptr);
        QCOMPARE(controller.positionMs(), 800);

        controller.stop();
        QCOMPARE(controller.state(), MediaPlaybackState::Stopped);
        QCOMPARE(controller.positionMs(), 0);

        // 停着的时候墙钟走，位置也不许动。
        MediaFrame frame;
        QVERIFY(!controller.advanceTo(2000, &frame));
        QCOMPARE(controller.positionMs(), 0);
    }

    // 拖动：位置就是给定的值。
    void playbackSeekMovesPosition() {
        MediaPlaybackController controller(5000);
        controller.seekTo(1200);
        QCOMPARE(controller.positionMs(), 1200);

        // 超出范围要被夹住。
        controller.seekTo(-100);
        QCOMPARE(controller.positionMs(), 0);
        controller.seekTo(99999);
        QCOMPARE(controller.positionMs(), 5000);
    }

    // 播完：状态变成 Finished，位置停在总时长上。
    void playbackReachesFinishedStateAtEnd() {
        MediaPlaybackController controller(1000);
        controller.play();
        MediaFrame frame;
        controller.advanceTo(0, &frame);  // 建立时间基准
        QVERIFY(!controller.advanceTo(2000, &frame));
        QCOMPARE(controller.state(), MediaPlaybackState::Finished);
        QCOMPARE(controller.positionMs(), 1000);

        // 播完之后再走时钟：状态与位置都不动，也不出帧。
        QVERIFY(!controller.advanceTo(5000, &frame));
        QCOMPARE(controller.state(), MediaPlaybackState::Finished);
        QCOMPARE(controller.positionMs(), 1000);
    }

    // 从结尾拖回中间：回到暂停状态，可以接着播。
    void playbackSeekBackFromFinishedPauses() {
        MediaPlaybackController controller(1000);
        controller.play();
        controller.advanceTo(0, nullptr);    // 建立时间基准
        controller.advanceTo(2000, nullptr); // 播到结尾
        QCOMPARE(controller.state(), MediaPlaybackState::Finished);

        controller.seekTo(300);
        QCOMPARE(controller.positionMs(), 300);
        QCOMPARE(controller.state(), MediaPlaybackState::Paused);

        controller.play();
        QCOMPARE(controller.state(), MediaPlaybackState::Playing);
        controller.advanceTo(300, nullptr);
        controller.advanceTo(350, nullptr);
        QCOMPARE(controller.positionMs(), 350);
    }

    // ---------------------------------------------------------------------
    // 错误分类
    // ---------------------------------------------------------------------

    // 路径不存在：属于「文件打不开」。
    void openMediaFileFailsForMissingPath() {
        QString error;
        const MediaInfo info =
            openMediaFile(QStringLiteral("Z:/definitely/not/here/nothing.wav"), &error);
        QVERIFY(!info.ok);
        QCOMPARE(info.errorKind, MediaErrorKind::OpenFailed);
        QVERIFY(!error.isEmpty());
        QVERIFY(!info.error.isEmpty());
    }

    // 空路径：也属于「文件打不开」，不能崩。
    void openMediaFileFailsForEmptyPath() {
        QString error;
        const MediaInfo info = openMediaFile(QString(), &error);
        QVERIFY(!info.ok);
        QCOMPARE(info.errorKind, MediaErrorKind::OpenFailed);
        QVERIFY(!error.isEmpty());
    }

    // 一个 .txt 文件：属于「不是媒体文件」。
    void openMediaFileReportsNotMediaForTextFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("note.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello, this is definitely not a media file at all\n");
        file.close();

        QString error;
        const MediaInfo info = openMediaFile(path, &error);
        QVERIFY(!info.ok);
        QCOMPARE(info.errorKind, MediaErrorKind::NotMedia);
        QVERIFY(!error.isEmpty());
    }

    // 「文件打不开」与「不是媒体文件」必须是两回事：分类不同、说法也不同。
    void errorKindsAndMessagesAreDistinct() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString textPath = dir.filePath(QStringLiteral("note.txt"));
        QFile textFile(textPath);
        QVERIFY(textFile.open(QIODevice::WriteOnly));
        textFile.write("not media\n");
        textFile.close();

        const MediaInfo missing = openMediaFile(QStringLiteral("Z:/nope/none.wav"));
        const MediaInfo notMedia = openMediaFile(textPath);

        QVERIFY(!missing.ok);
        QVERIFY(!notMedia.ok);
        // 分类不同。
        QVERIFY2(missing.errorKind != notMedia.errorKind,
                 qPrintable(QStringLiteral("打不开=%1 不是媒体=%2")
                                .arg(static_cast<int>(missing.errorKind))
                                .arg(static_cast<int>(notMedia.errorKind))));
        QCOMPARE(missing.errorKind, MediaErrorKind::OpenFailed);
        QCOMPARE(notMedia.errorKind, MediaErrorKind::NotMedia);
        // 说法也不同：不然界面上两句提示长得一样，用户分不清。
        QVERIFY(missing.error != notMedia.error);
        QVERIFY(missing.error.contains(QStringLiteral("打不开")));
        QVERIFY(notMedia.error.contains(QStringLiteral("不是媒体文件")));
    }

    // 解码器永远不能把一次失败说成「成功但没有内容」。
    void decoderNeverReturnsEmptySuccessfulFrame() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeBrokenMediaFile(dir);
        QVERIFY(!path.isEmpty());

        MediaDecoder decoder(path);
        MediaDecoderOptions options;
        QString error;
        if (!decoder.open(options, &error)) {
            // 打不开也要有说法，分类不能是「没有错误」。
            QVERIFY(!error.isEmpty());
            QVERIFY(decoder.info().errorKind != MediaErrorKind::None);
            return;
        }

        // 打开了：解出来的每一帧都得有内容，或者明确是结束帧。
        for (int i = 0; i < 1000; ++i) {
            MediaFrame frame;
            if (!decoder.nextFrame(&frame, &error)) {
                QVERIFY(!error.isEmpty());  // 失败必须有原因
                return;
            }
            if (frame.endOfStream) break;
            QVERIFY2(!frame.pcm.isEmpty() || !frame.image.isNull(), "解出来是一帧空的");
        }
    }

    // 解码器打不开：失败要有原因，不许崩。
    void decoderOpenFailsForMissingFile() {
        MediaDecoder decoder(QStringLiteral("Z:/definitely/not/here/nothing.wav"));
        MediaDecoderOptions options;
        QString error;
        QVERIFY(!decoder.open(options, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!decoder.isOpen());
        QCOMPARE(decoder.info().errorKind, MediaErrorKind::OpenFailed);
    }

    // 解码器没打开就取帧：必须报失败，**不许**返回「成功的空帧」。
    void decoderNextFrameFailsWhenNotOpen() {
        MediaDecoder decoder(QStringLiteral("Z:/definitely/not/here/nothing.wav"));
        MediaFrame frame;
        QString error;
        QVERIFY(!decoder.nextFrame(&frame, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!frame.endOfStream);
        QVERIFY(frame.pcm.isEmpty());
        QVERIFY(frame.image.isNull());
    }

    // ---------------------------------------------------------------------
    // 色彩通道顺序
    // ---------------------------------------------------------------------

    // MF 的 RGB32 缓冲是 B, G, R, X；换出来的图必须是同一个颜色。
    //
    // 这是这类代码最常见的坑：红蓝一换，画面看着「有点怪」但很难看出是哪儿错了。
    void rgb32ConversionKeepsChannelOrder() {
        // 一个 2x1 的缓冲：左边是纯红 (R=255)，右边是纯绿 (G=255)。
        uchar buffer[8] = {0x00, 0x00, 0xff, 0x00,   // B, G, R, X
                           0x00, 0xff, 0x00, 0x00};  // B, G, R, X
        const QImage image = rgb32BufferToImage(buffer, 2, 1, 8);
        QVERIFY(!image.isNull());
        QCOMPARE(image.width(), 2);
        QCOMPARE(image.height(), 1);
        QCOMPARE(image.format(), QImage::Format_RGB32);

        const QColor left = image.pixelColor(0, 0);
        const QColor right = image.pixelColor(1, 0);
        QCOMPARE(left.red(), 255);
        QCOMPARE(left.green(), 0);
        QCOMPARE(left.blue(), 0);
        QCOMPARE(right.red(), 0);
        QCOMPARE(right.green(), 255);
        QCOMPARE(right.blue(), 0);
    }

    // 行跨度比画面宽的时候（有对齐填充），不能把填充字节当成像素。
    void rgb32ConversionHonoursStride() {
        // 每行 12 字节，但画面只有 8 字节宽（2 个像素）。
        uchar buffer[24] = {0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00,
                             0xde, 0xad, 0xbe, 0xef,                        // 第 1 行的填充
                             0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00,  // 第 2 行
                             0xde, 0xad, 0xbe, 0xef};                       // 第 2 行的填充
        const QImage image = rgb32BufferToImage(buffer, 2, 2, 12);
        QVERIFY(!image.isNull());
        QCOMPARE(image.width(), 2);
        QCOMPARE(image.height(), 2);
        QCOMPARE(image.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(image.pixelColor(1, 0), QColor(0, 255, 0));
        QCOMPARE(image.pixelColor(0, 1), QColor(0, 0, 255));
        QCOMPARE(image.pixelColor(1, 1), QColor(0, 255, 0));
    }

    // 尺寸不对的输入不许越界读，只返回一个空图。
    void rgb32ConversionRejectsBadInput() {
        QVERIFY(rgb32BufferToImage(nullptr, 2, 2, 8).isNull());
        uchar buffer[8] = {0};
        QVERIFY(rgb32BufferToImage(buffer, 0, 2, 8).isNull());
        QVERIFY(rgb32BufferToImage(buffer, 2, 0, 8).isNull());
        // 行跨度比一行像素还小：这种输入没法读。
        QVERIFY(rgb32BufferToImage(buffer, 2, 1, 4).isNull());
    }

    // ---------------------------------------------------------------------
    // 真机解码
    // ---------------------------------------------------------------------

    // 系统自带的 WAV：读得出时长与采样率，还能解出至少一块 PCM。
    void realWavSampleDecodes() {
        const QString path =
            findSample("AIPDF_MEDIA_AUDIO_SAMPLE", windowsMediaDirs(), {QStringLiteral("*.wav")});
        if (path.isEmpty()) {
            QSKIP("这台机器上找不到 WAV 样本（可用 AIPDF_MEDIA_AUDIO_SAMPLE 指定）");
        }

        QString error;
        const MediaInfo info = openMediaFile(path, &error);
        QVERIFY2(info.ok, qPrintable(error));
        QCOMPARE(info.mediaType, QStringLiteral("audio"));
        QVERIFY2(info.durationMs > 0, "读不出时长");
        QVERIFY2(info.sampleRate > 0, "读不出采样率");
        QVERIFY(info.channelCount > 0);
        QVERIFY(!info.codecName.isEmpty());

        MediaDecoder decoder(path);
        MediaDecoderOptions options;
        QVERIFY2(decoder.open(options, &error), qPrintable(error));

        MediaFrame frame;
        QVERIFY2(decoder.nextFrame(&frame, &error), qPrintable(error));
        QVERIFY(!frame.endOfStream);
        QVERIFY2(!frame.pcm.isEmpty(), "解出来的 PCM 是空的");
        QVERIFY(frame.image.isNull());
        QVERIFY(frame.timestampUs >= 0);
    }

    // 系统自带的 MP3：MF 自带的 MP3 解码器应该能把它解成 PCM。
    void realMp3SampleDecodes() {
        const QString path = findSample("AIPDF_MEDIA_MP3_SAMPLE", windowsMp3Dirs(),
                                        {QStringLiteral("*.mp3")});
        if (path.isEmpty()) {
            QSKIP("这台机器上找不到 MP3 样本（可用 AIPDF_MEDIA_MP3_SAMPLE 指定）");
        }

        QString error;
        const MediaInfo info = openMediaFile(path, &error);
        QVERIFY2(info.ok, qPrintable(error));
        QCOMPARE(info.mediaType, QStringLiteral("audio"));
        QVERIFY2(info.durationMs > 0, "读不出时长");
        QVERIFY2(info.sampleRate > 0, "读不出采样率");

        MediaDecoder decoder(path);
        MediaDecoderOptions options;
        QVERIFY2(decoder.open(options, &error), qPrintable(error));

        MediaFrame frame;
        QVERIFY2(decoder.nextFrame(&frame, &error), qPrintable(error));
        QVERIFY(!frame.endOfStream);
        QVERIFY2(!frame.pcm.isEmpty(), "解出来的 PCM 是空的");
    }

    // 读到底：最后要给一个 endOfStream 的帧，而且不能越读越出帧。
    void realAudioSampleReachesEndOfStream() {
        const QString path =
            findSample("AIPDF_MEDIA_AUDIO_SAMPLE", windowsMediaDirs(), {QStringLiteral("*.wav")});
        if (path.isEmpty()) {
            QSKIP("这台机器上找不到 WAV 样本（可用 AIPDF_MEDIA_AUDIO_SAMPLE 指定）");
        }

        MediaDecoder decoder(path);
        MediaDecoderOptions options;
        QString error;
        QVERIFY2(decoder.open(options, &error), qPrintable(error));

        qint64 frames = 0;
        qint64 lastTimestamp = -1;
        for (int i = 0; i < 20000; ++i) {
            MediaFrame frame;
            QVERIFY2(decoder.nextFrame(&frame, &error), qPrintable(error));
            if (frame.endOfStream) break;
            QVERIFY(!frame.pcm.isEmpty());
            // 时间戳必须单调不减：音画交织靠它排序。
            QVERIFY(frame.timestampUs >= lastTimestamp);
            lastTimestamp = frame.timestampUs;
            ++frames;
        }
        QVERIFY2(frames > 0, "一帧都没解出来");

        // 读完之后再取一次：还是结束帧，不该又冒出一帧。
        MediaFrame extra;
        QVERIFY(decoder.nextFrame(&extra, &error));
        QVERIFY(extra.endOfStream);
    }

    // 视频：解出一帧，画面中心要是红色的。
    //
    // 样本从哪来：
    //   * 环境变量 AIPDF_MEDIA_VIDEO_SAMPLE 指定的文件；
    //   * 或者本机有 ffmpeg 的话，现场生成一段纯红的小视频（**不落进仓库**）；
    //   * 都没有就跳过。
    void realVideoSampleDecodes() {
        const QString path = videoSamplePath();
        if (path.isEmpty()) {
            QSKIP("这台机器上没有视频样本（可用 AIPDF_MEDIA_VIDEO_SAMPLE 指定，或装个 ffmpeg）");
        }

        QString error;
        MediaDecoder decoder(path);
        MediaDecoderOptions options;
        if (!decoder.open(options, &error)) {
            // 这台机器没有对应的解码器（比如没装 WMV）：跳过，不算红。
            QSKIP(qPrintable(QStringLiteral("本机解不了这个视频：%1").arg(error)));
        }

        const MediaInfo info = decoder.info();
        QCOMPARE(info.mediaType, QStringLiteral("video"));
        QVERIFY(info.width > 0);
        QVERIFY(info.height > 0);

        MediaFrame frame;
        QVERIFY2(decoder.nextFrame(&frame, &error), qPrintable(error));
        QVERIFY(!frame.endOfStream);
        QVERIFY2(!frame.image.isNull(), "解出来的画面是空的");

        const QImage image = frame.image;
        QCOMPARE(image.width(), info.width);
        QCOMPARE(image.height(), info.height);

        // 样本是一段纯红画面：中心必须是红的。
        // 红蓝一换就会变成蓝，这里立刻能看出来。
        const QColor center = image.pixelColor(image.width() / 2, image.height() / 2);
        QVERIFY2(center.red() > 120, qPrintable(QStringLiteral("画面中心不是红的：%1,%2,%3")
                                                    .arg(center.red())
                                                    .arg(center.green())
                                                    .arg(center.blue())));
        QVERIFY(center.red() > center.blue() + 40);
        QVERIFY(center.red() > center.green() + 40);
    }

private:
    // 找视频样本：环境变量优先；否则用 ffmpeg 现场生成一段纯红的。
    //
    // 生成物放在测试自己的临时目录里，测试结束就删，绝不落进仓库。
    QString videoSamplePath() {
        const QByteArray override = qgetenv("AIPDF_MEDIA_VIDEO_SAMPLE");
        if (!override.isEmpty()) {
            const QString path = QString::fromLocal8Bit(override);
            if (QFileInfo::exists(path)) return path;
        }
        if (!m_videoDir.isValid()) return QString();

        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty()) return QString();

        const QString output = m_videoDir.filePath(QStringLiteral("red.wmv"));
        if (QFileInfo::exists(output)) return output;

        QProcess process;
        process.start(ffmpeg,
                      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                       QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                       QStringLiteral("-i"), QStringLiteral("color=c=red:s=96x48:r=10"),
                       QStringLiteral("-t"), QStringLiteral("1"), QStringLiteral("-c:v"),
                       QStringLiteral("wmv2"), output});
        if (!process.waitForFinished(30000) || process.exitCode() != 0) {
            return QString();
        }
        if (!QFileInfo::exists(output)) return QString();
        return output;
    }

    // 生成视频样本用的临时目录（测试结束自动清理）。
    QTemporaryDir m_videoDir;
};

QTEST_MAIN(MediaSourceTests)

#include "MediaSourceTests.moc"
