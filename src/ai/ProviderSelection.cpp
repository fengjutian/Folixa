#include "ai/ProviderSelection.h"

namespace aipdf::ai {

ProviderKind resolveProviderKind(const ProviderConfig& config, bool hasCredential) {
    // 本地模式：云端分支直接短路——即使配了密钥也只能走演示。
    // 这是「文档内容不出本机」的机械保证，不是界面上一个变灰的按钮。
    if (!mayUseCloud(config.effectiveDataMode())) {
        return ProviderKind::Demo;
    }
    if (config.providerId == QLatin1String("openai") && hasCredential) {
        return ProviderKind::OpenAi;
    }
    return ProviderKind::Demo;
}

}  // namespace aipdf::ai
