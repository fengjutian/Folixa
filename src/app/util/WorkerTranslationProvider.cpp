#include "app/util/WorkerTranslationProvider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QTimer>

#include "ai/chat/ChatTypes.h"
#include "ipc/WorkerClient.h"

namespace aipdf::app {
namespace {

// 一次翻译请求的最长等待时间（毫秒）。
//
// 为什么要有上限：Worker 挂了或网络不通时，没有上限的等待会让界面**卡死**，
// 用户只能强杀进程。宁可等超时后如实报错，也不要把界面拖住。
constexpr int kRequestTimeoutMs = 60'000;
constexpr int kStatusTimeoutMs = 5'000;

}  // namespace

WorkerTranslationProvider::WorkerTranslationProvider(ipc::WorkerClient* client) : client_(client) {}

QString WorkerTranslationProvider::id() const {
    return QStringLiteral("worker");
}

bool WorkerTranslationProvider::isConfigured() const {
    return client_ != nullptr;
}

bool WorkerTranslationProvider::isDemo() const {
    QJsonObject status;
    QString error;
    if (!queryProviderStatus(&status, &error)) {
        // 问不到状态时按演示处理：宁可不做，也不把演示文本写进用户的文档。
        return true;
    }
    return status.value("demo").toBool(true);
}

bool WorkerTranslationProvider::queryProviderStatus(QJsonObject* status, QString* error) const {
    if (client_ == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("AI Worker 没有连上。");
        }
        return false;
    }
    const QString requestId =
        client_->sendRequest(QStringLiteral("ai.provider.status"), QJsonObject(), kStatusTimeoutMs);
    return waitForResponse(requestId, kStatusTimeoutMs, status, error);
}

bool WorkerTranslationProvider::waitForResponse(const QString& requestId, int timeoutMs,
                                                 QJsonObject* response, QString* error) const {
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);

    const auto finish = [&loop, &done](bool ok) {
        if (done) {
            return;
        }
        done = true;
        loop.quit();
    };

    const QMetaObject::Connection responseConnection = QObject::connect(
        client_, &ipc::WorkerClient::responseReceived, &loop,
        [&finish, requestId, response](const QString& id, const QJsonObject& payload) {
            if (id != requestId) {
                return;
            }
            if (response != nullptr) {
                *response = payload;
            }
            finish(true);
        });
    const QMetaObject::Connection failureConnection = QObject::connect(
        client_, &ipc::WorkerClient::requestFailed, &loop,
        [&finish, requestId, error](const QString& id, const QString& message) {
            if (id != requestId) {
                return;
            }
            if (error != nullptr) {
                *error = message;
            }
            finish(false);
        });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&finish, error]() {
        if (error != nullptr) {
            *error = QStringLiteral("AI Worker 请求超时。");
        }
        finish(false);
    });
    timer.start(timeoutMs);
    loop.exec();
    QObject::disconnect(responseConnection);
    QObject::disconnect(failureConnection);
    return done;
}

bool WorkerTranslationProvider::translate(const QString& sourceText, const QString& targetLanguage,
                                          QString* translation, QString* error) {
    if (client_ == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("AI Worker 没有连上。");
        }
        return false;
    }
    if (sourceText.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("这一段没有可翻译的文字。");
        }
        return false;
    }

    // 提示词：沿用 AI 层给「翻译」这个任务写好的系统提示词，
    // 目标语言作为用户消息的一部分送出去。
    QJsonArray messages;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"), ai::systemPromptForTask(ai::Task::Translate));
    messages.append(system);
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"),
                ai::userPromptForTask(ai::Task::Translate, sourceText,
                                      QStringLiteral("translate into %1, output only the translation")
                                          .arg(targetLanguage)));
    messages.append(user);

    QJsonObject payload;
    payload.insert(QStringLiteral("task"), ai::Task::Translate);
    payload.insert(QStringLiteral("data_scope"), QStringLiteral("整篇文档 · 按段翻译"));
    payload.insert(QStringLiteral("messages"), messages);

    const QString requestId = client_->sendStreamRequest(QStringLiteral("ai.chat.stream"), payload);

    QEventLoop loop;
    QString collected;
    bool done = false;
    bool failed = false;
    QString failureText;
    QTimer timer;
    timer.setSingleShot(true);

    const auto finish = [&loop, &done, &failed](bool ok) {
        if (done) {
            return;
        }
        done = true;
        failed = !ok;
        loop.quit();
    };

    const QMetaObject::Connection eventConnection = QObject::connect(
        client_, &ipc::WorkerClient::eventReceived, &loop,
        [&collected, requestId](const QString& id, const QString& event, const QJsonObject& payload) {
            if (id != requestId) {
                return;
            }
            if (event == QLatin1String("ai.stream.delta")) {
                collected += payload.value("text").toString();
            }
        });
    const QMetaObject::Connection responseConnection = QObject::connect(
        client_, &ipc::WorkerClient::responseReceived, &loop,
        [&finish, requestId](const QString& id, const QJsonObject&) {
            if (id != requestId) {
                return;
            }
            finish(true);
        });
    const QMetaObject::Connection failureConnection = QObject::connect(
        client_, &ipc::WorkerClient::requestFailed, &loop,
        [&finish, &failureText, requestId](const QString& id, const QString& message) {
            if (id != requestId) {
                return;
            }
            failureText = message;
            finish(false);
        });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&finish, &failureText]() {
        failureText = QStringLiteral("AI Worker 请求超时。");
        finish(false);
    });
    timer.start(kRequestTimeoutMs);
    loop.exec();
    QObject::disconnect(eventConnection);
    QObject::disconnect(responseConnection);
    QObject::disconnect(failureConnection);

    if (failed) {
        if (error != nullptr) {
            *error = failureText.isEmpty() ? QStringLiteral("翻译调用失败。") : failureText;
        }
        return false;
    }
    if (collected.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("翻译服务没有返回内容。");
        }
        return false;
    }
    if (translation != nullptr) {
        *translation = collected.trimmed();
    }
    return true;
}

}  // namespace aipdf::app
