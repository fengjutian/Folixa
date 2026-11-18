#pragma once

#include <QString>
#include <QVector>

namespace aipdf::speech {

// 系统里可用的一个音色（语音合成器）。
struct SpeechVoice {
    // 系统里的语音名，例如 "Microsoft Huihui Desktop"。传给引擎时用它。
    QString name;
    // 语言标签，例如 "zh-CN"。界面上按它分组，用户好找。
    QString culture;
};

// 朗读设置：音色、语速、音量。
//
// 为什么放在领域层而不是对话框里：设置要跨会话记住，
// 而且"语速 7 到底算多快"这种事得有一个地方说了算，
// 对话框只负责收数字。
struct SpeechSettings {
    // 音色名。空 = 由引擎按文本语言自动挑一个。
    QString voice;
    // 语速：-10（最慢）到 10（最快），0 是系统默认。
    int rate = 0;
    // 音量：0 到 100。
    int volume = 100;
};

// 语速的合法区间。
inline constexpr int kSpeechRateMin = -10;
inline constexpr int kSpeechRateMax = 10;

// 把设置夹到合法范围：语速夹到 [-10, 10]，音量夹到 [0, 100]。
//
// 为什么要夹而不是拒绝：设置来自界面上的滑块与输入框，
// 越界只可能是程序自己的问题，静默夹住比让朗读整个罢工好。
SpeechSettings normalizeSpeechSettings(const SpeechSettings& settings);

}  // namespace aipdf::speech
