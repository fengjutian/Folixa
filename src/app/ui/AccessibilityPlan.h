#pragma once

#include <QString>
#include <QStringList>

#include "app/ReadingScheme.h"
#include "speech/SpeechSettings.h"

namespace aipdf::app {

// 无障碍配置向导的**结果**：一次配好「看得清 / 听得清 / 找得到」。
//
// 为什么要有这个结构：向导是界面层的东西，而「字号夹到多少」「配色是哪一档」
// 「朗读参数要不要归一」这些是**规则**，得有一个不依赖控件的地方说了算。
// 把规则收在这里，向导只负责收集数字，规则有单测可钉。
struct AccessibilityPlan {
    // 界面字号（磅）。太小看不清，太大装不下，所以要夹。
    int interfaceFontSize = 10;
    // 阅读配色（页面周围的底色）。
    theme::ReadingScheme readingScheme = theme::ReadingScheme::Light;
    // 左侧导航面板要不要显示。
    bool navigationVisible = true;
    // 朗读设置：音色、语速、音量。
    speech::SpeechSettings speech;
};

// 界面字号的合法区间（磅）。
inline constexpr int kAccessibilityFontSizeMin = 8;
inline constexpr int kAccessibilityFontSizeMax = 24;

// 把字号夹到合法区间。
//
// 为什么要夹而不是拒绝：字号来自界面上的输入框，
// 越界只可能是程序自己的问题（或者设置文件被改坏了），
// 静默夹住比让整个界面变成不可用好。
inline int clampAccessibilityFontSize(int size) {
    if (size < kAccessibilityFontSizeMin) {
        return kAccessibilityFontSizeMin;
    }
    if (size > kAccessibilityFontSizeMax) {
        return kAccessibilityFontSizeMax;
    }
    return size;
}

// 把计划里的各项收拾干净：字号夹范围，朗读参数走归一化。
//
// 这样"计划"从任何地方来（向导、设置文件、别的调用方）都是同一套规则。
inline AccessibilityPlan normalizeAccessibilityPlan(const AccessibilityPlan& plan) {
    AccessibilityPlan result = plan;
    result.interfaceFontSize = clampAccessibilityFontSize(plan.interfaceFontSize);
    result.speech = speech::normalizeSpeechSettings(plan.speech);
    return result;
}

// 计划相对「默认」有没有改动。
//
// 用在哪：向导最后一页要列「改了哪些」，得先知道哪些真的变了 ——
// 不然每次打开都把没动过的项也列一遍，用户以为改过了。
inline bool accessibilityPlanDiffers(const AccessibilityPlan& plan,
                                     const AccessibilityPlan& baseline) {
    const AccessibilityPlan normalized = normalizeAccessibilityPlan(plan);
    const AccessibilityPlan base = normalizeAccessibilityPlan(baseline);
    if (normalized.interfaceFontSize != base.interfaceFontSize) {
        return true;
    }
    if (normalized.readingScheme != base.readingScheme) {
        return true;
    }
    if (normalized.navigationVisible != base.navigationVisible) {
        return true;
    }
    return normalized.speech.voice != base.speech.voice ||
           normalized.speech.rate != base.speech.rate ||
           normalized.speech.volume != base.speech.volume;
}

// 计划的一份**机器可读**签名（ASCII 键值对，空格分隔）。
//
// 为什么不是给人看的文字：给人看的摘要由向导自己拼（那要翻译），
// 这里给的是"这个计划长什么样"的唯一标识 —— 单测拿它比对两份计划
// 是不是同一份，不用逐字段写断言。
inline QString accessibilityPlanSignature(const AccessibilityPlan& plan) {
    const AccessibilityPlan normalized = normalizeAccessibilityPlan(plan);
    QStringList parts;
    parts << QStringLiteral("font=%1").arg(normalized.interfaceFontSize);
    parts << QStringLiteral("scheme=%1").arg(static_cast<int>(normalized.readingScheme));
    parts << QStringLiteral("navigation=%1").arg(normalized.navigationVisible ? 1 : 0);
    parts << QStringLiteral("voice=%1").arg(normalized.speech.voice);
    parts << QStringLiteral("rate=%1").arg(normalized.speech.rate);
    parts << QStringLiteral("volume=%1").arg(normalized.speech.volume);
    return parts.join(QLatin1Char(' '));
}

}  // namespace aipdf::app
