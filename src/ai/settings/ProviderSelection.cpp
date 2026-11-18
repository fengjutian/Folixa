#include "ai/settings/ProviderSelection.h"

#include "ai/settings/ProviderPreset.h"

namespace aipdf::ai {

ProviderKind resolveProviderKind(const ProviderConfig& config, bool hasCredential) {
    // 「本地模式」＝文档内容一律不出本机。
    //
    // 例外只有一个：地址指向本机（Ollama 这类本机模型）。
    // 内容根本没离开这台机器，「不许发出去」这条规则拦它没有意义，
    // 拦了反而让用户选了本地模型却用不上。
    const bool localEndpoint = isLoopbackUrl(config.baseUrl);
    if (!localEndpoint && !mayUseCloud(config.effectiveDataMode())) {
        // 本地模式：云端分支直接短路——即使配了密钥也只能走演示。
        // 这是「文档内容不出本机」的机械保证，不是界面上一个变灰的按钮。
        return ProviderKind::Demo;
    }

    const std::optional<ProviderPreset> preset = findProviderPreset(config.providerId);
    if (!preset.has_value()) {
        // 认不出来的服务商 id：当成演示，绝不拿空地址去联网。
        return ProviderKind::Demo;
    }
    if (preset->id == QLatin1String("demo")) {
        return ProviderKind::Demo;
    }
    // MiniMax、DeepSeek、Qwen、Kimi、Ollama 与自定义服务都走同一套 OpenAI 兼容实现。
    // 本机模型不需要密钥；云端的没有密钥就不能联网。
    const bool keySatisfied =
        hasCredential || !providerRequiresApiKey(config.providerId, config.baseUrl);
    if (keySatisfied) {
        return ProviderKind::OpenAi;
    }
    return ProviderKind::Demo;
}

}  // namespace aipdf::ai
