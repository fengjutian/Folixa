#include "ai/ProviderFactory.h"

#include "ai/CredentialStore.h"
#include "ai/MockProvider.h"
#include "ai/OpenAiProvider.h"

namespace aipdf::ai {

std::unique_ptr<ModelProvider> createProvider(const ProviderConfig& config) {
    if (config.providerId == QLatin1String("openai")) {
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
    }
    // 没配密钥（或配的不是云端）：演示模式。
    return std::make_unique<MockProvider>();
}

}  // namespace aipdf::ai
