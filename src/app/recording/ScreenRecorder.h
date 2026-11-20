#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>

#include <functional>
#include <memory>

class QFile;
class QScreen;
class QTimer;

namespace aipdf::app {

class MotionJpegAviWriter;

// 屏幕 / 窗口 / 放映录制：定时抓帧 → JPEG → 写成一个 AVI 文件。
//
// 为什么不用系统录屏（Windows 游戏栏）：
//   * 游戏栏录的是**游戏**，窗口要另外选，PDF 放映这种全屏场景经常选不中；
//   * 录完的文件落在「视频\捕获」里，本程序拿不到它的路径，就没法登记进
//     录制列表，也没法在列表里管理；
//   * 录屏过程中用户还要切出去按 Win+Alt+R。
// 自己抓帧录制才能做到「点了就开始、停了就有文件、文件在列表里」。
//
// 为什么用可注入的帧源：真机抓屏在单元测试里拿不到稳定的画面，
// 注入一个假帧源就能把「抓帧 → 编码 → 写 AVI → 收尾」整条链在 CI 上跑通；
// 真机路径（真的去抓屏幕）仍然只能在有显示器的机器上验证，这条边界不糊。
class ScreenRecorder : public QObject {
    Q_OBJECT

public:
    struct Options {
        // 要录的区域（**全局屏幕坐标**）。空矩形表示整块目标屏。
        //
        // 注入帧源时（单元测试）没有屏幕坐标，区域按「图像自身的坐标」解释，
        // 也就是从图片左上角算起。
        QRect region;
        // 帧率。屏幕内容变化慢，默认 10 帧足够，帧率越高文件越大。
        int framesPerSecond = 10;
        // 录制的最大宽度（像素）。超过就等比缩小。
        //
        // 为什么限宽：整屏 1080p 的 JPEG 一帧要一两百 KB，不限宽的话
        // 一分钟的录像就是几百 MB，用户不会愿意用。
        int maxWidth = 1280;
        // JPEG 质量（1-100）。
        int jpegQuality = 70;
    };

    // 帧源：返回一张当前画面。测试注入假的，真机用抓屏。
    using FrameProvider = std::function<QImage()>;

    explicit ScreenRecorder(QObject* parent = nullptr);
    ~ScreenRecorder() override;

    // 注入帧源（不持有所有权）。不注入时默认抓主屏。
    void setFrameProvider(FrameProvider provider);

    // 开始录制。outputPath 要是 .avi；目录不存在会自动建。
    //
    // 第一帧在 start() 里就抓好并写进去：AVI 的头要写画面宽高，
    // 没有第一帧就不知道该写多少。
    bool start(const Options& options, const QString& outputPath, QString* error = nullptr);

    // 停止并收尾（写索引、回填长度、关闭文件）。
    bool stop(QString* error = nullptr);

    // 放弃：删掉半成品文件。录废了的时候用它，不要留下一个打不开的 AVI。
    void cancel();

    bool isRecording() const { return recording_; }
    int frameCount() const { return frameCount_; }
    QString outputPath() const { return outputPath_; }
    int width() const { return width_; }
    int height() const { return height_; }
    qint64 elapsedMs() const;

    // 抓一帧写进文件。定时器与单元测试共用这一条路径。
    bool captureFrame(QString* error = nullptr);

signals:
    // 每写进一帧发一次，参数是当前总帧数。
    void frameAppended(int frameCount);
    // 录制结束（stop 或 cancel 之后）。ok 为 false 时 error 里有原因。
    void finished(bool ok, const QString& outputPath, const QString& error);

private:
    void onTimeout();
    QImage grabFrame() const;
    QImage prepareFrame() const;
    // 抓到的这一张图对应哪块逻辑区域（全局屏幕坐标），用来把选区换算成图内坐标。
    QRect sourceRectFor(const QImage& image) const;
    QByteArray encodeJpeg(const QImage& image) const;
    void cleanup();

    Options options_;
    FrameProvider provider_;
    QTimer* timer_ = nullptr;
    QFile* file_ = nullptr;
    std::unique_ptr<MotionJpegAviWriter> writer_;
    QString outputPath_;
    // 录哪块屏（多屏时按选区落在哪块屏上决定）。不持有所有权。
    QScreen* screen_ = nullptr;
    // 要录的区域（**全局屏幕坐标**）。空矩形表示整块目标屏。
    QRect cropRect_;
    int frameCount_ = 0;
    int width_ = 0;
    int height_ = 0;
    qint64 startedAtMs_ = 0;
    int consecutiveFailures_ = 0;
    bool recording_ = false;
};

}  // namespace aipdf::app
