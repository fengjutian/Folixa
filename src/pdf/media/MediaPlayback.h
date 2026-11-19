#pragma once

#include <QString>

namespace aipdf::pdf {

// MediaFrame 定义在 MediaSource.h 里；这里只用到指针，前向声明即可。
struct MediaFrame;

// ============================================================================
// 播放状态机（纯逻辑，不依赖 Media Foundation）
// ============================================================================
//
// 这个状态机只管**时序**：什么时候该出一帧、位置走到哪、暂停的时候
// 位置会不会偷偷往前跑。它不碰解码器，也不碰界面，因此可以脱离
// MF 单独测——「暂停了还在出帧」「暂停时位置还在走」这类毛病，
// 在这里能一眼看出来。
//
// 状态转换：
//
//   Stopped --play()--> Playing --pause()--> Paused --play()--> Playing
//                          |                   |
//                          |                   +--seekTo()--> Paused（位置变了）
//                          +--播放到结尾------> Finished
//   Finished --play()--> Playing（从头重放）
//   任意状态 --stop()--> Stopped（位置回到 0）
//
// 几条硬规则（都有对应用例）：
//   * 暂停时位置**不动**，而且**不再出帧**；
//   * 停止后位置回到 0；
//   * 播放到结尾变成 Finished，位置停在总时长上；
//   * seekTo 之后位置就是给定的值（自动夹在 [0, 总时长] 内）。

// 播放状态。
enum class MediaPlaybackState {
    // 没在播（初始状态，或者刚 stop 过）。
    Stopped,
    // 在播。
    Playing,
    // 暂停中：位置记住，出帧停住。
    Paused,
    // 播完了：位置停在总时长上。
    Finished,
};

// 播放控制器。
//
// 典型用法（界面层）：
//
//     MediaPlaybackController controller(durationMs);
//     controller.setFrameRate(25.0);
//     controller.play();
//     // 定时器每 33 毫秒调一次：
//     MediaFrame frame;
//     if (controller.advanceTo(elapsedMs, &frame)) {
//         decoder.nextFrame(&frame);   // 这一拍要出帧，去解码器取
//         render(frame);
//     }
class MediaPlaybackController {
public:
    // durationMs 是媒体总时长（毫秒）。0 表示还不知道总长。
    explicit MediaPlaybackController(qint64 durationMs = 0);

    // 设置总时长。已经在播的话，按新时长重新判断是不是播完了。
    void setDurationMs(qint64 durationMs);

    // 设置帧率，用来算「多久该出一帧」。<= 0 表示不知道，按默认间隔走。
    void setFrameRate(double frameRate);

    // 从当前位置继续播。
    // 已经播完（Finished）时从头重放——否则「继续」会立刻又播完，等于按不动。
    void play();

    // 暂停：位置记住，之后不再出帧。
    // 本来就没在播的话，什么也不做。
    void pause();

    // 停止：位置回到 0，状态回到 Stopped。
    void stop();

    // 拖到指定位置（毫秒）。自动夹在 [0, 总时长] 内。
    // 拖到总时长上 → Finished；拖到中间 → 回到 Paused，等着继续播。
    void seekTo(qint64 positionMs);

    // 当前状态。
    MediaPlaybackState state() const;

    // 当前播放位置（毫秒）。
    qint64 positionMs() const;

    // 媒体总时长（毫秒）。
    qint64 durationMs() const;

    // 走一帧：把墙钟时间推进到现在这个时刻。
    //
    // 返回 true 表示「这一拍该出一帧」，此时 frameOut 会被填上
    // 这一帧的目标时间戳（微秒），调用方拿它去解码器取真正的画面。
    // 返回 false 表示这一拍不用出帧（还没到下一帧的时间，
    // 或者已经暂停 / 停止 / 播完）。
    //
    // wallClockMs 是**单调递增**的墙钟毫秒数（QElapsedTimer::elapsed 那种）。
    // 暂停期间调用它不会推进位置，也不会出帧。
    bool advanceTo(qint64 wallClockMs, MediaFrame* frameOut);

    // 一帧多久出一次（毫秒）。由帧率算出来。
    qint64 frameIntervalMs() const;

private:
    // 状态。
    MediaPlaybackState state_ = MediaPlaybackState::Stopped;
    // 当前位置（毫秒）。
    qint64 positionMs_ = 0;
    // 总时长（毫秒）。
    qint64 durationMs_ = 0;
    // 帧率（帧 / 秒）。
    double frameRate_ = 0.0;
    // 上一次 advanceTo 看到的墙钟时间。
    qint64 lastWallClockMs_ = 0;
    // 有没有见过墙钟时间。第一次见到时只记基准，不推进位置。
    bool hasClock_ = false;
    // 下一帧该在什么位置出（毫秒，媒体时间轴）。
    qint64 nextFrameDueMs_ = 0;
};

}  // namespace aipdf::pdf
