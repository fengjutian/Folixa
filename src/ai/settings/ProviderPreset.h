#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace aipdf::ai {

// 一个服务商的「开箱预设」。
//
// 为什么单独一份表，而不是把地址写在界面里：
// - 界面下拉框要按它排（顺序、每个服务商的默认地址与模型）；
// - 「这个服务商要不要密钥」「地址默认填什么」是同一个事实，
//   在界面里写一份、在 ProviderSelection 里再写一份，迟早会对不上；
// - 主程序与 Worker 都读同一份表。
//
// 地址与模型以各服务商官方文档为准（2026-11 核对）：
// - DeepSeek：OpenAI 兼容，https://api.deepseek.com
// - Kimi（月之暗面）：OpenAI 兼容，https://api.moonshot.cn/v1
// - Qwen（阿里云百炼）：兼容模式，https://dashscope.aliyuncs.com/compatible-mode/v1
// - MiniMax：OpenAI 兼容，国内 https://api.minimaxi.com/v1，海外 https://api.minimax.io/v1
// - Ollama：本机跑，OpenAI 兼容层 http://127.0.0.1:11434/v1
struct ProviderPreset {
    // 服务商标识（写进配置文件的那个 id）。
    QString id;
    // 默认服务地址（到 /v1 为止）。
    QString baseUrl;
    // 默认模型名。留空表示由用户自己填（本地模型取决于装了什么）。
    QString model;
    // 要不要 API 密钥。本机跑的模型不要。
    bool requiresApiKey = true;
    // 是不是在本机跑（本机跑的模型：文档内容不出本机，也不需要密钥）。
    bool runsLocally = false;
};

// 全部预设。顺序就是界面下拉框的顺序。
QVector<ProviderPreset> providerPresets();

// 按 id 找预设。找不到返回空。
std::optional<ProviderPreset> findProviderPreset(const QString& id);

// 服务地址是不是指向本机（127.0.0.1 / localhost / ::1）。
//
// 指向本机时，文档内容根本没离开这台机器——
// 「本地模式不许发出去」那条规则不该把它拦下来。
bool isLoopbackUrl(const QString& baseUrl);

// 这个配置（服务商 + 地址）要不要 API 密钥。
//
// 两条规则：
// - 地址指向本机 → 不要密钥。本机跑的模型（Ollama 这类）没有密钥概念，
//   硬要用户填一个只会挡住「本机模式」。
// - 地址在云端 → 要密钥。没密钥就不联网：宁可退回演示模型，
//   也不要因为配置没填全就把文档发出去。
bool providerRequiresApiKey(const QString& providerId, const QString& baseUrl);

// 服务商 id → 显示名。
//
// 设置下拉框、AI 面板的状态消息、设置页摘要显示的是同一个服务商，
// 名字必须一致：名字散在各处就会各说各话。
QString providerDisplayName(const QString& id);

}  // namespace aipdf::ai
