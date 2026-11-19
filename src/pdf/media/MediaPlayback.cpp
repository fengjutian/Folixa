#include "pdf/media/MediaPlayback.h"

#include "pdf/media/MediaSource.h"

namespace aipdf::pdf {

namespace {

// 不知道帧率时，一帧按多久算。
//
// 取 40 毫秒（25 帧 / 秒）是常见默认值：多数视频是 24 / 25 / 30 帧，
// 取中间值不至于让画面跳得太快，也不至于慢得看不出在动。
constexpr qint64 kDefaultFrameIntervalMs = 40;

// 位置夹到 [0, 总时长]。
//
// 总时长还不知道（0）时只保证不小于 0：这时候不能拿 0 当上限，
// 否则还没读到总长度的媒体会被夹死在 0 毫秒。
qint64 clampPosition(qint64 positionMs, qint64 durationMs) {
    if (positionMs < 0) return 0;
    if (durationMs > 0 && positionMs > durationMs) return durationMs;
    return positionMs;
}

}  // namespace

MediaPlaybackController::MediaPlaybackController(qint64 durationMs)
    : durationMs_(durationMs > 0 ? durationMs : 0) {}

void MediaPlaybackController::setDurationMs(qint64 durationMs) {
    durationMs_ = durationMs > 0 ? durationMs : 0;
    positionMs_ = clampPosition(positionMs_, durationMs_);
    // 已经播到末尾而总时长又变长了，回到「暂停在当前位置」的状态，
    // 否则用户会发现进度条卡在结尾、播不了了。
    if (state_ == MediaPlaybackState::Finished && positionMs_ < durationMs_) {
        state_ = MediaPlaybackState::Paused;
    }
    // 下一帧的到期时间不能落在总时长后面，否则最后一段画面永远出不来。
    if (nextFrameDueMs_ > durationMs_ && durationMs_ > 0) {
        nextFrameDueMs_ = durationMs_;
    }
}

void MediaPlaybackController::setFrameRate(double frameRate) {
    frameRate_ = frameRate > 0.0 ? frameRate : 0.0;
}

void MediaPlaybackController::play() {
    // 已经播完：从头重放。
    // 不这么做的话「继续」会立刻又判定为播完，按多少次都没反应。
    if (state_ == MediaPlaybackState::Finished) {
        positionMs_ = 0;
        nextFrameDueMs_ = 0;
    }
    state_ = MediaPlaybackState::Playing;
    // 重新开始计时：暂停期间过掉的那段墙钟时间不算进播放位置。
    hasClock_ = false;
}

void MediaPlaybackController::pause() {
    // 本来就没在播，暂停没有意义，状态也不变。
    if (state_ != MediaPlaybackState::Playing) return;
    state_ = MediaPlaybackState::Paused;
    // 位置原样记住。下次继续时重新取墙钟基准，
    // 暂停的这段时间不会被人算进播放进度。
    hasClock_ = false;
}

void MediaPlaybackController::stop() {
    state_ = MediaPlaybackState::Stopped;
    positionMs_ = 0;
    nextFrameDueMs_ = 0;
    hasClock_ = false;
}

void MediaPlaybackController::seekTo(qint64 positionMs) {
    positionMs_ = clampPosition(positionMs, durationMs_);
    nextFrameDueMs_ = positionMs_;
    hasClock_ = false;
    if (durationMs_ > 0 && positionMs_ >= durationMs_) {
        // 拖到结尾：跟播完一样。
        state_ = MediaPlaybackState::Finished;
        return;
    }
    // 从结尾拖回中间：回到「暂停在当前位置」，等用户点播放。
    if (state_ == MediaPlaybackState::Finished) {
        state_ = MediaPlaybackState::Paused;
    }
}

MediaPlaybackState MediaPlaybackController::state() const { return state_; }

qint64 MediaPlaybackController::positionMs() const { return positionMs_; }

qint64 MediaPlaybackController::durationMs() const { return durationMs_; }

qint64 MediaPlaybackController::frameIntervalMs() const {
    if (frameRate_ <= 0.0) return kDefaultFrameIntervalMs;
    const qint64 interval = static_cast<qint64>(1000.0 / frameRate_ + 0.5);
    return interval > 0 ? interval : 1;
}

bool MediaPlaybackController::advanceTo(qint64 wallClockMs, MediaFrame* frameOut) {
    // 关键规则：**只有真的在播的时候才推进位置、才出帧**。
    // 暂停 / 停止 / 播完时直接返回，位置一动不动。
    if (state_ != MediaPlaybackState::Playing) return false;

    if (!hasClock_) {
        // 第一次见到墙钟：只记基准。这一拍不推进位置，
        // 否则 play() 之后的第一拍会把「从上次到现在」的一大段时间
        // 全算进播放位置。
        lastWallClockMs_ = wallClockMs;
        hasClock_ = true;
    }

    const qint64 delta = wallClockMs - lastWallClockMs_;
    lastWallClockMs_ = wallClockMs;
    if (delta > 0) {
        positionMs_ += delta;
    }

    if (durationMs_ > 0 && positionMs_ >= durationMs_) {
        // 播完了：位置停在总时长上，状态变成 Finished，不再出帧。
        positionMs_ = durationMs_;
        state_ = MediaPlaybackState::Finished;
        hasClock_ = false;
        return false;
    }

    if (positionMs_ < nextFrameDueMs_) {
        // 还没到出帧的时间点。
        return false;
    }

    // 到点了：这一拍出一帧。
    // 下一帧的到期时间按**当前位置**往后推一个间隔，而不是在上一次到期
    // 时间上累加——累加会让误差越攒越多，画面越播越偏。
    nextFrameDueMs_ = positionMs_ + frameIntervalMs();
    if (frameOut != nullptr) {
        frameOut->timestampUs = positionMs_ * 1000;
        frameOut->endOfStream = false;
    }
    return true;
}

}  // namespace aipdf::pdf
