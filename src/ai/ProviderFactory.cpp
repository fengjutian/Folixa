#include "ai/ProviderFactory.h"

#include "ai/CredentialStore.h"
#include "ai/MockProvider.h"
#include "ai/OpenAiProvider.h"

namespace aipdf::ai {

std::unique_ptr<ModelProvider> createProvider(const ProviderConfig& config) {
    const bool hasCredential =
        CredentialStore::exists(CredentialStore::targetNameFor(config.providerId));
    if (resolveProviderKind(config, hasCredential) == ProviderKind::Demo) {
        return std::make_unique<MockProvider>();
    }
    // 密钥只在这里读一次，读完就交给 Provider；
    // 它不会出现在任何日志、轨迹或 IPC 帧里。
    const QString apiKey =
        CredentialStore::load(CredentialStore::targetNameFor(config.providerId));
    if (!apiKey.isEmpty()) {
        OpenAiProvider::Settings settings;
        settings.baseUrl = config.baseUrl;
        settings.model = config.model;
        settings.apiKey = apiKey;
        settings.timeoutMs = config.timeoutMs;
        return std::make_unique<OpenAiProvider>(settings);
    }
    // exists() 与 load() 之间凭据被删了：宁可退回演示，也不要拿空密钥去联网。
    return std::make_unique<MockProvider>();
}

}  // namespace aipdf::ai
