#include "WorkerServer.h"

#include "ai/ChatTypes.h"
#include "ai/ProviderConfig.h"
#include "ai/ProviderFactory.h"
#include "ipc/IpcProtocol.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

#include <utility>

namespace aipdf::worker {

WorkerServer::WorkerServer(QString serverName, QObject* parent)
    : QObject(parent), serverName_(std::move(serverName)) {
    connect(&server_, &QLocalServer::newConnection, this, &WorkerServer::acceptConnection);
}

WorkerServer::~WorkerServer() = default;

bool WorkerServer::start() {
    QLocalServer::removeServer(serverName_);
    return server_.listen(serverName_);
}

void WorkerServer::acceptConnection() {
    while (server_.hasPendingConnections()) {
        QLocalSocket* candidate = server_.nextPendingConnection();
        if (client_ != nullptr) {
            candidate->disconnectFromServer();
            candidate->deleteLater();
            continue;
        }
        client_ = candidate;
        connect(client_, &QLocalSocket::readyRead, this, &WorkerServer::readFrames);
        connect(client_, &QLocalSocket::disconnected, this, [this] {
            // 客户端没了，正在跑的流也要停：不然会一直挂着等一个不会来的回复。
            if (provider_ != nullptr && provider_->isRunning()) {
                provider_->cancel();
            }
            provider_.reset();
            activeStreamId_.clear();
            client_->deleteLater();
            client_ = nullptr;
            receiveBuffer_.clear();
        });
    }
}

void WorkerServer::readFrames() {
    if (client_ == nullptr) {
        return;
    }
    receiveBuffer_.append(client_->readAll());
    if (receiveBuffer_.size() > ipc::kMaximumFrameBytes) {
        client_->write(ipc::encodeFrame(ipc::makeError(
            {}, "frame_too_large", "IPC frame exceeds the 1 MiB limit")));
        client_->disconnectFromServer();
        receiveBuffer_.clear();
        return;
    }

    qsizetype newline = -1;
    while ((newline = receiveBuffer_.indexOf('\n')) >= 0) {
        const QByteArray frame = receiveBuffer_.left(newline);
        receiveBuffer_.remove(0, newline + 1);
        if (frame.isEmpty()) {
            continue;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(frame, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            client_->write(ipc::encodeFrame(ipc::makeError(
                {}, "invalid_json", "Request must be a JSON object")));
            continue;
        }
        handleMessage(client_, document.object());
    }
}

void WorkerServer::writeFrame(const QJsonObject& frame) {
    if (client_ == nullptr) {
        return;
    }
    client_->write(ipc::encodeFrame(frame));
}

void WorkerServer::handleMessage(QLocalSocket* socket, const QJsonObject& request) {
    const QString requestId = request.value("request_id").toString();
    if (request.value("protocol_version").toInt(-1) != ipc::kProtocolVersion) {
        socket->write(ipc::encodeFrame(ipc::makeError(
            requestId, "protocol_version_unsupported", "Unsupported IPC protocol version")));
        return;
    }

    const QJsonObject payload = request.value("payload").toObject();
    const QString operation = request.value("operation").toString();
    if (operation == "system.health") {
        socket->write(ipc::encodeFrame(ipc::makeResult(
            requestId,
            {{"status", "ready"},
             {"protocol_version", ipc::kProtocolVersion},
             {"worker_version", QCoreApplication::applicationVersion()}})));
        return;
    }
    if (operation == "ai.echo") {
        const QString text = payload.value("text").toString();
        socket->write(ipc::encodeFrame(ipc::makeResult(
            requestId, {{"text", text}, {"placeholder", true}})));
        return;
    }
    if (operation == "ai.chat.stream") {
        startChat(requestId, payload);
        return;
    }
    if (operation == "ai.chat.cancel") {
        const QString target = payload.value("target").toString();
        if (provider_ != nullptr && provider_->isRunning() && target == activeStreamId_) {
            provider_->cancel();
        }
        socket->write(ipc::encodeFrame(ipc::makeResult(
            requestId, {{"status", "cancelling"}, {"target", target}})));
        return;
    }
    if (operation == "ai.provider.status") {
        // 报给界面用：现在用的是哪个 Provider、是不是演示模式、单价多少。
        const aipdf::ai::ProviderConfig config = aipdf::ai::ProviderConfig::load();
        const bool demo = config.providerId != QLatin1String("openai") ||
                          !config.hasCredential();
        socket->write(ipc::encodeFrame(ipc::makeResult(
            requestId,
            {{"provider", config.providerId},
             {"model", config.model},
             {"demo", demo},
             {"input_price_per_million", config.inputPricePerMillion},
             {"output_price_per_million", config.outputPricePerMillion}})));
        return;
    }
    if (operation == "system.shutdown") {
        socket->write(ipc::encodeFrame(ipc::makeResult(
            requestId, {{"status", "stopping"}})));
        socket->flush();
        // 先让客户端读到确认帧，再结束事件循环。
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        return;
    }
    socket->write(ipc::encodeFrame(ipc::makeError(
        requestId, "operation_not_supported", "The requested operation is not available")));
}

void WorkerServer::startChat(const QString& requestId, const QJsonObject& payload) {
    if (provider_ != nullptr && provider_->isRunning()) {
        // 消息是给用户看的：Worker 的错误帧会直接显示在 AI 面板里。
        writeFrame(ipc::makeError(requestId, "busy",
                                  "上一次生成还没结束，请等它结束或点「停止」。"));
        return;
    }

    aipdf::ai::ChatRequest chat;
    chat.task = payload.value("task").toString();
    chat.dataScope = payload.value("data_scope").toString();
    chat.model = payload.value("model").toString();
    chat.temperature = payload.value("temperature").toDouble(0.2);
    chat.maxTokens = payload.value("max_tokens").toInt(800);

    const QJsonArray messages = payload.value("messages").toArray();
    for (const QJsonValue& value : messages) {
        const QJsonObject object = value.toObject();
        aipdf::ai::ChatMessage message;
        const QString role = object.value("role").toString();
        if (role == QLatin1String("system")) {
            message.role = aipdf::ai::ChatMessage::Role::System;
        } else if (role == QLatin1String("assistant")) {
            message.role = aipdf::ai::ChatMessage::Role::Assistant;
        } else {
            message.role = aipdf::ai::ChatMessage::Role::User;
        }
        message.content = object.value("content").toString();
        chat.messages.append(message);
    }

    // 整篇问答的上下文块（带编号）。
    //
    // 这一份是**结构化**的：提示词里那份是给模型看的文本，
    // 这里这份是给 Provider 用的——演示模式要靠它引用真实的块，
    // 而不是编一段像样的样子。
    const QJsonArray context = payload.value("context").toArray();
    for (const QJsonValue& value : context) {
        const QJsonObject object = value.toObject();
        aipdf::ai::ContextBlock block;
        block.number = object.value("number").toInt();
        block.pageNumber = object.value("page").toInt();
        block.text = object.value("text").toString();
        block.chunkId = object.value("chunk_id").toString();
        if (block.text.trimmed().isEmpty()) {
            continue;
        }
        chat.context.append(block);
    }

    const aipdf::ai::ProviderConfig config = aipdf::ai::ProviderConfig::load();
    provider_ = aipdf::ai::createProvider(config);
    activeStreamId_ = requestId;

    connect(provider_.get(), &aipdf::ai::ModelProvider::streamDelta, this,
            [this, requestId](const QString& text) {
                writeFrame(ipc::makeEvent(requestId, "ai.stream.delta",
                                           {{"text", text}}));
            });
    connect(provider_.get(), &aipdf::ai::ModelProvider::streamFinished, this,
            [this, requestId](const QString& reason, int inputTokens, int outputTokens) {
                const aipdf::ai::ProviderConfig current = aipdf::ai::ProviderConfig::load();
                aipdf::ai::TokenUsage usage;
                usage.inputTokens = inputTokens;
                usage.outputTokens = outputTokens;
                finishStream(requestId, "ai.stream.done",
                             {{"finish_reason", reason},
                              {"input_tokens", usage.inputTokens},
                              {"output_tokens", usage.outputTokens},
                              {"estimated_cost",
                               usage.estimatedCost(current.inputPricePerMillion,
                                                   current.outputPricePerMillion)}});
            });
    connect(provider_.get(), &aipdf::ai::ModelProvider::streamFailed, this,
            [this, requestId](const QString& code, const QString& message) {
                finishStream(requestId, "ai.stream.error",
                             {{"code", code}, {"message", message}});
            });
    connect(provider_.get(), &aipdf::ai::ModelProvider::streamCancelled, this,
            [this, requestId]() {
                finishStream(requestId, "ai.stream.cancelled", {{"cancelled", true}});
            });

    // 先回一个「开始了」：客户端据此把界面切到「生成中」。
    writeFrame(ipc::makeResult(requestId,
                               {{"status", "streaming"},
                                {"provider", provider_->id()},
                                {"model", config.model},
                                {"demo", provider_->isDemo()}}));
    provider_->startChat(chat);
}

void WorkerServer::finishStream(const QString& requestId,
                                 const QString& event,
                                 const QJsonObject& payload) {
    writeFrame(ipc::makeEvent(requestId, event, payload));
    // 每个请求都要以**一个最终结果帧**收尾。
    // 客户端靠它清掉流式状态（activeStreams_）；只发事件不发结果，
    // 客户端那边就会一直以为这次流还活着。
    writeFrame(ipc::makeResult(requestId, {{"status", "finished"}}));
    if (activeStreamId_ == requestId) {
        activeStreamId_.clear();
    }
}

}  // namespace aipdf::worker
