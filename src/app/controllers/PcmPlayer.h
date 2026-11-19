#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

namespace aipdf::app {

// ============================================================================
// PCM 播放失败的三类
// ============================================================================
//
// 为什么必须分开：这三件事要修的地方完全不同——
//   * 没有输出设备：换机器 / 插声卡，重试没有用；
//   * 设备在但打不开：可能被别的程序占着，重启一下可能就好了；
//   * 设备开着但写不进去：要怀疑句柄或数据本身。
// 一律说「播放失败」，用户只会反复点重试，而重试并不会变好。
enum class PcmPlayerErrorKind {
    // 没有错误。
    None = 0,
    // 这台机器没有音频输出设备（没插声卡、驱动没装、输出被禁用）。
    NoDevice,
    // 设备在，但打不开（格式不支持、设备被占用、权限不够）。
    OpenFailed,
    // 设备开着，但 PCM 数据写不进去。
    WriteFailed,
};

// 三类失败各自的中文原因。
//
// code 是 Win32 的错误码（MMRESULT）；传 0 表示没有具体错误码。
// 三种分类给出的文案**互不相同**，界面要靠这个区分「是没设备」
// 「是打不开」还是「是写不进去」。
QString pcmPlayerErrorText(PcmPlayerErrorKind kind, int code = 0);

// ============================================================================
// 把 PCM 块播到声卡
// ============================================================================
//
// 为什么用 Win32 waveOut 而不是 Qt Multimedia：这个工程固定工具链里没有
// Qt Multimedia 模块，引进来会破坏「装好工具链就能构建」的前提。
// waveOut 是系统自带的，零依赖——和 AudioRecorder 的 waveIn 正好是一对。
//
// PCM 格式固定 **16 位小端**（和 Media Foundation 的 MFAudioFormat_PCM 一致），
// 采样率与声道数由调用方给（从媒体信息里读出来）。
//
// 线程模型：open / pushPcm / pause 这些都要在**主线程**调用；
// waveOut 的回调跑在驱动自己的线程上，回调里只搬数据、不碰界面。
//
// 生命周期：open() 之后必须 close()（析构里也会 close，多调一次安全）。
class PcmPlayer {
public:
    PcmPlayer();
    ~PcmPlayer();
    PcmPlayer(const PcmPlayer&) = delete;
    PcmPlayer& operator=(const PcmPlayer&) = delete;

    // 打开声卡。
    //
    // 没有输出设备时返回 false，kind 是 NoDevice；
    // 设备在但打不开时 kind 是 OpenFailed，错误码在 code 里。
    bool open(int sampleRate, int channelCount, QString* error = nullptr);

    // 推入一块 PCM（16 位小端）。
    //
    // 这是**异步**的：函数返回时数据只是排进了声卡队列，还没有播。
    // 排队量用 queuedBytes() 查。
    // 播放器没打开时返回 false，kind 是 WriteFailed。
    bool pushPcm(const QByteArray& pcm, QString* error = nullptr);

    // 暂停：已经排队的 PCM 放完之后就不再出声。
    // 返回 true 表示暂停成功（本来就在暂停里也算成功）。
    bool pause();

    // 继续播放。返回 true 表示现在在播。
    bool resume();

    // 停止：丢掉排队的 PCM，设备留着（还能接着用）。
    void stop();

    // 关闭：释放 waveOut 句柄与所有缓冲。可以重复调用。
    void close();

    // 声卡是不是开着。
    bool isOpen() const;

    // 现在是不是暂停着。
    bool isPaused() const;

    // 还有多少 PCM 排在声卡里（字节）。
    qint64 queuedBytes() const;

    // 排队的 PCM 折算成多少毫秒（按当前采样率与声道数）。
    qint64 queuedDurationMs() const;

    // 泄漏自检：本类当前还开着的 waveOut 设备数。
    //
    // 正常情况下 close() 之后这个数必须回到原值——只增不减就说明句柄漏了。
    // 这是给「关掉之后到底释没释放」一个能测的判据，而不是靠人记。
    static int liveDeviceCount();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aipdf::app
