#include "ai/settings/ProviderPreset.h"

#include "i18n/Translate.h"

#include <QUrl>

namespace aipdf::ai {

QVector<ProviderPreset> providerPresets() {
    return {
        // 演示模式：不联网，地址与模型都不需要。
        {QStringLiteral("demo"), QString(), QString(), false, false},
        // MiniMax：OpenAI 兼容。国内账号走 api.minimaxi.com，海外账号走 api.minimax.io。
        {QStringLiteral("minimax"), QStringLiteral("https://api.minimaxi.com/v1"),
         QStringLiteral("MiniMax-M3.1-Flash"), true, false},
        // DeepSeek：OpenAI 兼容。
        {QStringLiteral("deepseek"), QStringLiteral("https://api.deepseek.com"),
         QStringLiteral("deepseek-chat"), true, false},
        // Qwen：阿里云百炼的 OpenAI 兼容模式。
        {QStringLiteral("qwen"), QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1"),
         QStringLiteral("qwen-plus"), true, false},
        // Kimi（月之暗面）：OpenAI 兼容。
        {QStringLiteral("kimi"), QStringLiteral("https://api.moonshot.cn/v1"),
         QStringLiteral("moonshot-v1-8k"), true, false},
        // Ollama：本机跑的模型。OpenAI 兼容层不需要密钥，模型名由用户按 ollama list 填。
        {QStringLiteral("ollama"), QStringLiteral("http://127.0.0.1:11434/v1"), QString(), false, true},
        // 自定义 OpenAI 兼容服务：地址与模型都由用户填。
        {QStringLiteral("openai"), QString(), QString(), true, false},
    };
}

std::optional<ProviderPreset> findProviderPreset(const QString& id) {
    const QVector<ProviderPreset> presets = providerPresets();
    for (const ProviderPreset& preset : presets) {
        if (preset.id == id) {
            return preset;
        }
    }
    return std::nullopt;
}

bool isLoopbackUrl(const QString& baseUrl) {
    const QUrl url(baseUrl.trimmed());
    const QString host = url.host().toLower();
    return host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost") ||
           host == QLatin1String("::1") || host == QLatin1String("[::1]");
}

bool providerRequiresApiKey(const QString& providerId, const QString& baseUrl) {
    // 本机地址：不需要密钥（内容根本没出本机）。
    if (isLoopbackUrl(baseUrl)) {
        return false;
    }
    const std::optional<ProviderPreset> preset = findProviderPreset(providerId);
    if (!preset.has_value()) {
        // 认不出来的服务商按最严处理：要密钥。
        return true;
    }
    return preset->requiresApiKey;
}

QString providerDisplayName(const QString& id) {
    if (id == QLatin1String("demo")) {
        return i18n::tr("aipdf::ai::ProviderPreset", "演示模式（本地生成，不联网）");
    }
    if (id == QLatin1String("minimax")) {
        return i18n::tr("aipdf::ai::ProviderPreset", "MiniMax");
    }
    if (id == QLatin1String("deepseek")) {
        return i18n::tr("aipdf::ai::ProviderPreset", "DeepSeek");
    }
    if (id == QLatin1String("qwen")) {
        return i18n::tr("aipdf::ai::ProviderPreset", "Qwen（阿里云百炼）");
    }
    if (id == QLatin1String("kimi")) {
        return i18n::tr("aipdf::ai::ProviderPreset", "Kimi（月之暗面）");
    }
    if (id == QLatin1String("ollama")) {
        return i18n::tr("aipdf::ai::ProviderPreset", "Ollama（本机模型）");
    }
    if (id == QLatin1String("openai")) {
        return i18n::tr("aipdf::ai::ProviderPreset", "自定义 OpenAI 兼容服务");
    }
    // 认不出来的服务商：显示原样的 id，比显示一个猜出来的名字诚实。
    if (!id.isEmpty()) {
        return id;
    }
    return i18n::tr("aipdf::ai::ProviderPreset", "未知的服务商");
}

}  // namespace aipdf::ai
