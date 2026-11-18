#pragma once

#include "ai/settings/ProviderConfig.h"

namespace aipdf::ai {

// 实际会被造出来的 Provider 种类。
enum class ProviderKind {
    Demo,    // 本地演示：不联网
    OpenAi,  // 云端 OpenAI 兼容
};

// 纯函数：给定配置与「凭据是否存在」，决定实际会用哪个 Provider。
//
// 单独放在一个只依赖 ProviderConfig 的编译单元里：
// createProvider 要读 Windows 凭据管理器，而「本地模式即使配了密钥
// 也只能走演示」这条机械保证，不该只能靠一台恰好存了密钥的机器来验证。
//
// 两个例外（都在实现里写死了，不靠界面）：
// - 地址指向本机（Ollama 这类本机模型）：内容不出本机，
//   「本地模式」不该把它拦下来；
// - 本机模型不需要密钥：没有密钥也能用。
ProviderKind resolveProviderKind(const ProviderConfig& config, bool hasCredential);

}  // namespace aipdf::ai
