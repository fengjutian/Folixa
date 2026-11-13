#include "ai/OpenAiProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace aipdf::ai {

namespace {

// 错误码 → 给用户看的中文提示。
//
// 提示要能指导下一步动作：密钥错要去改密钥，限流要等一会儿，
// 网络错要检查网络。笼统的「出错了」等于没提示。
QString messageForCode(const QString& code, const QString& detail) {
    if (code == QLatin1String("invalid_key")) {
        return QStringLiteral("API 密钥无效或已过期，请在设置里重新填写。");
    }
    if (code == QLatin1String("forbidden")) {
        return QStringLiteral("这个密钥没有权限访问该模型。");
    }
    if (code == QLatin1String("rate_limited")) {
        return QStringLiteral("请求太频繁，服务商限流了，请稍后再试。");
    }
    if (code == QLatin1String("timeout")) {
        return QStringLiteral("请求超时，服务商没有在时限内回应。");
    }
    if (code == QLatin1String("network_error")) {
        return QStringLiteral("连不上模型服务，请检查网络。");
    }
    if (code == QLatin1String("server_error")) {
        return QStringLiteral("模型服务出错了，请稍后再试。");
    }
    if (code == QLatin1String("bad_request")) {
        return QStringLiteral("请求被模型服务拒绝：%1").arg(detail);
    }
    return detail.isEmpty() ? QStringLiteral("模型请求失败。") : detail;
}

// HTTP 状态码 → 错误码。
QString codeForHttpStatus(int status) {
    if (status == 401) {
        return QStringLiteral("invalid_key");
    }
    if (status == 403) {
        return QStringLiteral("forbidden");
    }
    if (status == 429) {
        return QStringLiteral("rate_limited");
    }
    if (status >= 500) {
        return QStringLiteral("server_error");
    }
    if (status >= 400) {
        return QStringLiteral("bad_request");
    }
    return QString();
}

// 这个状态码值不值得重试。
//
// 4xx（密钥错、参数错）重试多少次都没用；429 与 5xx 是服务端的问题，等一下就好。
bool isRetryable(const QString& code) {
    return code == QLatin1String("rate_limited") ||
           code == QLatin1String("server_error") ||
           code == QLatin1String("network_error") ||
           code == QLatin1String("timeout");
}

}  // namespace

OpenAiProvider::OpenAiProvider(Settings settings, QObject* parent)
    : ModelProvider(parent), settings_(std::move(settings)) {
    network_ = new QNetworkAccessManager(this);
    retryTimer_ = new QTimer(this);
    retryTimer_->setSingleShot(true);
    connect(retryTimer_, &QTimer::timeout, this, [this] {
        if (cancelled_) {
            return;
        }
        // 重试时用回上一次的请求参数。
        sendRequest(lastRequest_, attempt_ + 1);
    });
}

OpenAiProvider::~OpenAiProvider() {
    resetConnection();
}

QString OpenAiProvider::id() const {
    return QStringLiteral("openai");
}

QString OpenAiProvider::displayName() const {
    return QStringLiteral("云端模型（OpenAI 兼容）");
}

bool OpenAiProvider::isConfigured() const {
    return !settings_.apiKey.isEmpty() && !settings_.baseUrl.isEmpty();
}

bool OpenAiProvider::isRunning() const {
    return running_;
}

void OpenAiProvider::startChat(const ChatRequest& request) {
    if (running_) {
        failWith(QStringLiteral("busy"), QStringLiteral("上一次请求还没结束。"));
        return;
    }
    if (!isConfigured()) {
        failWith(QStringLiteral("not_configured"),
                 QStringLiteral("没有配置 API 密钥，无法调用云端模型。"));
        return;
    }
    cancelled_ = false;
    attempt_ = 0;
    finishReason_.clear();
    inputTokens_ = 0;
    outputTokens_ = 0;
    parser_.reset();
    lastRequest_ = request;
    running_ = true;
    emit streamStarted();
    sendRequest(request, 0);
}

void OpenAiProvider::cancel() {
    if (!running_) {
        return;
    }
    cancelled_ = true;
    resetConnection();
    retryTimer_->stop();
    running_ = false;
    emit streamCancelled();
}

void OpenAiProvider::resetConnection() {
    if (reply_ != nullptr) {
        QNetworkReply* reply = reply_;
        reply_ = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void OpenAiProvider::sendRequest(const ChatRequest& request, int attempt) {
    if (cancelled_) {
        return;
    }
    attempt_ = attempt;

    QJsonArray messages;
    for (const ChatMessage& message : request.messages) {
        messages.append(QJsonObject{{"role", message.roleName()},
                                    {"content", message.content}});
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"),
                 request.model.isEmpty() ? settings_.model : request.model);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), true);
    // 让服务端在流结束时把 token 用量一并带回来（费用指标要用）。
    body.insert(QStringLiteral("stream_options"),
                QJsonObject{{"include_usage", true}});
    body.insert(QStringLiteral("temperature"), request.temperature);
    body.insert(QStringLiteral("max_tokens"), request.maxTokens);

    QNetworkRequest networkRequest(
        QUrl(settings_.baseUrl + QStringLiteral("/chat/completions")));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/json"));
    networkRequest.setRawHeader("Accept", "text/event-stream");
    if (!settings_.apiKey.isEmpty()) {
        networkRequest.setRawHeader("Authorization",
                                    QByteArrayLiteral("Bearer ") +
                                        settings_.apiKey.toUtf8());
    }
    // 不自动跟随重定向：跟随会把 Authorization 头带到另一个主机上，
    // 密钥就跟着出去了。服务端地址配错了应该直接报错。
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::ManualRedirectPolicy);
    if (settings_.timeoutMs > 0) {
        networkRequest.setTransferTimeout(settings_.timeoutMs);
    }

    resetConnection();
    reply_ = network_->post(networkRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply_, &QNetworkReply::readyRead, this, &OpenAiProvider::handleReadyRead);
    connect(reply_, &QNetworkReply::finished, this, &OpenAiProvider::handleFinished);
}

void OpenAiProvider::handleReadyRead() {
    if (reply_ == nullptr) {
        return;
    }
    const QVector<SseParser::Event> events =
        parser_.feed(reply_->readAll());
    for (const SseParser::Event& event : events) {
        handleStreamEvent(event);
        if (cancelled_) {
            return;
        }
    }
}

void OpenAiProvider::handleStreamEvent(const SseParser::Event& event) {
    const QString data = event.data.trimmed();
    if (data.isEmpty()) {
        return;
    }
    // [DONE] 是流结束的标记。
    if (data == QLatin1String("[DONE]")) {
        finishWith(finishReason_.isEmpty() ? QStringLiteral("stop") : finishReason_);
        return;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        // 一行坏 JSON 不该让整条流报废：跳过它，继续读下一条。
        return;
    }
    handlePayload(document.object());
}

void OpenAiProvider::handlePayload(const QJsonObject& payload) {
    // 错误事件：{"error": {"message": "...", "type": "...", "code": "..."}}
    if (payload.contains(QStringLiteral("error"))) {
        const QJsonObject error = payload.value(QStringLiteral("error")).toObject();
        const QString detail = error.value(QStringLiteral("message")).toString();
        const QString type = error.value(QStringLiteral("type")).toString();
        QString code = QStringLiteral("bad_request");
        if (type.contains(QStringLiteral("rate_limit"))) {
            code = QStringLiteral("rate_limited");
        } else if (type.contains(QStringLiteral("auth")) ||
                   type.contains(QStringLiteral("invalid_api_key"))) {
            code = QStringLiteral("invalid_key");
        } else if (type.contains(QStringLiteral("server"))) {
            code = QStringLiteral("server_error");
        }
        failWith(code, detail);
        return;
    }

    // token 用量：最后一个 chunk 里带（要 stream_options.include_usage）。
    if (payload.contains(QStringLiteral("usage"))) {
        const QJsonObject usage = payload.value(QStringLiteral("usage")).toObject();
        inputTokens_ = usage.value(QStringLiteral("prompt_tokens")).toInt(inputTokens_);
        outputTokens_ = usage.value(QStringLiteral("completion_tokens")).toInt(outputTokens_);
    }

    const QJsonArray choices = payload.value(QStringLiteral("choices")).toArray();
    for (const QJsonValue& value : choices) {
        const QJsonObject choice = value.toObject();
        const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
        const QString content = delta.value(QStringLiteral("content")).toString();
        if (!content.isEmpty()) {
            emit streamDelta(content);
        }
        const QString reason = choice.value(QStringLiteral("finish_reason")).toString();
        if (!reason.isNull() && !reason.isEmpty()) {
            finishReason_ = reason;
        }
    }
}

void OpenAiProvider::handleFinished() {
    if (reply_ == nullptr) {
        return;
    }
    QNetworkReply* reply = reply_;
    reply_ = nullptr;

    if (cancelled_) {
        reply->deleteLater();
        return;
    }

    const QVariant statusValue = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusValue.isValid() ? statusValue.toInt() : 0;
    const QNetworkReply::NetworkError error = reply->error();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (error == QNetworkReply::OperationCanceledError) {
        // 取消：cancel() 已经发过 streamCancelled。
        running_ = false;
        return;
    }

    if (error == QNetworkReply::TimeoutError) {
        scheduleRetry(lastRequest_, attempt_, QStringLiteral("timeout"),
                      QStringLiteral("请求超时"));
        return;
    }

    if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
        const QString code = status > 0 ? codeForHttpStatus(status)
                                         : QStringLiteral("network_error");
        QString detail = QString::fromUtf8(body).simplified();
        if (detail.size() > 300) {
            detail = detail.left(300) + QStringLiteral("…");
        }
        scheduleRetry(lastRequest_, attempt_, code, detail);
        return;
    }

    // 正常结束：如果服务端没有发 [DONE]，把缓冲里剩下的事件也读掉。
    const QVector<SseParser::Event> tail = parser_.feed(QByteArray());
    for (const SseParser::Event& event : tail) {
        handleStreamEvent(event);
    }
    const SseParser::Event last = parser_.flush();
    if (!last.data.trimmed().isEmpty()) {
        handleStreamEvent(last);
    }

    running_ = false;
    finishWith(finishReason_.isEmpty() ? QStringLiteral("stop") : finishReason_);
}

void OpenAiProvider::scheduleRetry(const ChatRequest& request,
                                   int attempt,
                                   const QString& code,
                                   const QString& message) {
    if (cancelled_) {
        return;
    }
    if (!isRetryable(code) || attempt >= settings_.maxRetries) {
        failWith(code, message);
        return;
    }
    // 退避：1 秒、2 秒、4 秒……
    const int delay = 1000 * (1 << attempt);
    retryTimer_->start(delay);
}

void OpenAiProvider::failWith(const QString& code, const QString& message) {
    resetConnection();
    running_ = false;
    emit streamFailed(code, messageForCode(code, message));
}

void OpenAiProvider::finishWith(const QString& finishReason) {
    if (!running_) {
        return;
    }
    resetConnection();
    running_ = false;
    emit streamFinished(finishReason, inputTokens_, outputTokens_);
}

}  // namespace aipdf::ai
