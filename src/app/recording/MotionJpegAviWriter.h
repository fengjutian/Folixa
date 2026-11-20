#pragma once

#include <QIODevice>
#include <QString>
#include <QVector>

namespace aipdf::app {

// Motion-JPEG AVI（MJPG）封装器：把一帧帧 JPEG 写成标准 AVI 文件。
//
// 为什么自己写而不引 FFmpeg：本工程的依赖政策是「不把第三方商业/重型二进制
// 拉进产品」，而 MJPEG 容器本身就是一个几百字节的 RIFF 结构。Windows 自带的
// JPEG 编码能力由 Qt 提供（QtGui 内建 qjpeg 处理器），所以整条链零外部依赖。
//
// 为什么 MJPEG 而不是 H.264/MP4：H.264 要走 Media Foundation 的 MFT 软硬件
// 编码器，可用性随机器与驱动变化，很难写出「一定跑得通」的验收；MJPEG 的字节
// 完全由本文件决定，可以用独立解析器逐字节核对。代价是文件大（同画质下约为
// H.264 的十倍量级），这一点在界面与文档里都要说清。
//
// 线程模型：只在主线程调用（写文件 + 定时器都在主线程）。
class MotionJpegAviWriter {
public:
    // device 必须**可寻址**（QFile / QBuffer）：AVI 的头部与索引要在写完帧之后
    // 回填总帧数与长度，写完再回填就要求能 seek。
    //
    // 调用方负责 open()，但要求是 ReadWrite（要回填）。
    MotionJpegAviWriter(QIODevice* device, int width, int height, int framesPerSecond);

    // 写文件头。失败时返回 false 并把原因写进 error。
    bool begin(QString* error = nullptr);

    // 追加一帧。data 必须是完整的 JPEG（SOI 开头、EOI 结尾），否则拒绝。
    //
    // 为什么要自己验 JPEG：写进去的字节如果不完整，播放器会在中途花屏，
    // 而这种坏文件往往在几小时后才被用户打开——那时已经没法定位是哪一帧坏了。
    bool appendFrame(const QByteArray& data, QString* error = nullptr);

    // 收尾：写 idx1 索引，回填 RIFF / avih / strh / movi 的长度字段。
    //
    // 没写过任何一帧也允许收尾（产出一个 0 帧的合法 AVI），但调用方通常不该
    // 走到这一步——零帧的录像对用户没有意义。
    bool finish(QString* error = nullptr);

    int frameCount() const { return frameCount_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int framesPerSecond() const { return fps_; }
    bool isOpen() const { return opened_; }
    bool isFinished() const { return finished_; }
    QString errorString() const { return error_; }

    // 这段字节是不是一段完整的 JPEG。
    static bool isJpeg(const QByteArray& data);

private:
    struct Chunk {
        quint32 id = 0;       // '00dc'
        quint32 offset = 0;   // 相对 movi 四字节码的偏移
        quint32 length = 0;   // JPEG 数据长度
    };

    void putU16(char* target, quint16 value) const;
    void putU32(char* target, quint32 value) const;
    bool patchU32(qint64 position, quint32 value);

    QIODevice* device_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int fps_ = 0;
    int frameCount_ = 0;
    quint32 maxFrameSize_ = 0;
    bool opened_ = false;
    bool finished_ = false;
    QString error_;

    // 需要回填的位置（begin() 时记下）。
    qint64 riffSizePos_ = -1;
    qint64 avihTotalFramesPos_ = -1;
    qint64 avihMaxBytesPos_ = -1;
    qint64 avihSuggestedPos_ = -1;
    qint64 strhLengthPos_ = -1;
    qint64 strhSuggestedPos_ = -1;
    qint64 moviListSizePos_ = -1;
    qint64 moviFourccPos_ = -1;

    QVector<Chunk> chunks_;
};

}  // namespace aipdf::app
