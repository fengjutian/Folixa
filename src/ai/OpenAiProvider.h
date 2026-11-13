#pragma once

#include "ai/ModelProvider.h"
#include "ai/SseParser.h"

#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QTimer;

namespace aipdf::ai {

// OpenAI 兼容的云端 Provider。
//
// 「兼容」的意思是：凡是提供 `/v1/chat/completions` 且支持 SSE 流式响应的
// 服务都能用（OpenAI、DeepSeek、Qwen 的兼容端点、Ollama 的 OpenAI 兼容层……），
// 换一家只改 baseUrl 与 model。
//
// 三件容易做错的事，这里都按规矩来：
//   1. 密钥只放在 Authorization 头里，不进 URL、不进日志、不回传主程序；
//   2. 重定向不自动跟随：跟随会把 Authorization 头带到另一个主机上；
//   3. 429/5xx/网络抖动会重试，4xx（密钥错、参数错）不重试——重试也没用。
class OpenAiProvider final : public ModelProvider {
    Q_OBJECT

public:
    struct Settings {
        // 服务地址（到 /v1 为止）。
        QString baseUrl = QStringLiteral("https://api.openai.com/v1");
        // 模型名。
        QString model = QStringLiteral("gpt-4o-mini");
        // API 密钥。
        QString apiKey;
        // 单次请求的超时（毫秒）。
        int timeoutMs = 60'000;
        // 最多重试几次（不含第一次）。
        int maxRetries = 2;
    };

    explicit OpenAiProvider(Settings settings, QObject* parent = nullptr);
    ~OpenAiProvider() override;

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;

    void startChat(const ChatRequest& request) override;
    void cancel() override;
    bool isRunning() const override;

private:
    void sendRequest(const ChatRequest& request, int attempt);
    void handleReadyRead();
    void handleFinished();
    void handleStreamEvent(const SseParser::Event& event);
    void handlePayload(const QJsonObject& payload);
    void failWith(const QString& code, const QString& message);
    void finishWith(const QString& finishReason);
    void scheduleRetry(const ChatRequest& request, int attempt, const QString& code,
                       const QString& message);
    void resetConnection();

    Settings settings_;
    // 上一次的请求：重试时要用它再发一遍。
    ChatRequest lastRequest_;
    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QTimer* retryTimer_ = nullptr;
    QPointer<QTimer> timeoutTimer_;
    SseParser parser_;
    QString finishReason_;
    int inputTokens_ = 0;
    int outputTokens_ = 0;
    bool running_ = false;
    bool cancelled_ = false;
    int attempt_ = 0;
};

}  // namespace aipdf::ai
