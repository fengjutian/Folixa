#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::app {

// 录音：Win32 waveIn，输出一段完整的 WAV 字节（含 RIFF 头）。
//
// 为什么不用 Qt Multimedia：这个工程固定工具链里没有 Qt Multimedia 模块，
// 引进来会破坏"装好工具链就能构建"的前提。waveIn 是系统自带的，零依赖。
//
// 线程模型：必须在主线程调用；回调里只做数据搬运，不碰界面。
class AudioRecorder {
public:
    AudioRecorder() = default;
    ~AudioRecorder();
    AudioRecorder(const AudioRecorder&) = delete;
    AudioRecorder& operator=(const AudioRecorder&) = delete;

    // 开始录音。没有输入设备时返回 false 并给出原因（不能崩）。
    bool start(QString* error = nullptr);
    // 停止并返回 WAV 字节；没在录时返回空。
    QByteArray stop();
    bool isRecording() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace aipdf::app
