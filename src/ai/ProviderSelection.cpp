#include "ai/ProviderSelection.h"

namespace aipdf::ai {

ProviderKind resolveProviderKind(const ProviderConfig& config, bool hasCredential) {
    // 本地模式：云端分支直接短路——即使配了密钥也只能走演示。
    // 这是「文档内容不出本机」的机械保证，不是界面上一个变灰的按钮。
    if (!mayUseCloud(config.effectiveDataMode())) {
        return ProviderKind::Demo;
    }
    // MiniMax、DeepSeek、Qwen、Kimi 与自定义服务都走同一套 OpenAI 兼容实现。
    const bool compatible = config.providerId == QLatin1String("minimax") ||
                            config.providerId == QLatin1String("deepseek") ||
                            config.providerId == QLatin1String("qwen") ||
                            config.providerId == QLatin1String("kimi") ||
                            config.providerId == QLatin1String("openai");
    if (compatible && hasCredential) {
        return ProviderKind::OpenAi;
    }
    return ProviderKind::Demo;
}

}  // namespace aipdf::ai
