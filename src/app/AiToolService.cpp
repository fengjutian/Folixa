#include "app/AiToolService.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include "ai/AiImageGeneration.h"
#include "ai/ChatTypes.h"
#include "ai/CredentialStore.h"
#include "ai/ProviderConfig.h"
#include "i18n/Translate.h"
#include "ipc/WorkerClient.h"

namespace aipdf::app {

namespace {

// 图像生成接口的路径（相对 baseUrl）。
constexpr char kImageGenerationPath[] = "/images/generations";

// 同步等模型回答的上限（毫秒）。
constexpr int kBlockingAskTimeoutMs = 120'000;

}  // namespace

AiToolService::AiToolService(QObject* parent) : QObject(parent) {
    // 这里不接任何信号回环：后台线程按槽名投递（invokeMethod），
    // 回环会让图片结果自己套自己。
}

AiToolService::~AiToolService() {
    // 后台线程要先收工：它还握着 this。
    if (imageThread_ != nullptr) {
        imageThread_->wait();
        delete imageThread_;
        imageThread_ = nullptr;
    }
}

void AiToolService::setWorkerClient(ipc::WorkerClient* client) {
    if (workerClient_ == client) {
        return;
    }
    if (workerClient_ != nullptr) {
        disconnect(workerClient_, nullptr, this, nullptr);
    }
    workerClient_ = client;
    if (workerClient_ != nullptr) {
        connect(workerClient_, &ipc::WorkerClient::eventReceived, this,
                &AiToolService::handleWorkerEvent);
    }
}

bool AiToolService::busy() const {
    return busy_;
}

void AiToolService::requestCompletion(const QString& systemPrompt, const QString& userPrompt) {
    if (workerClient_ == nullptr) {
        emit failed(i18n::tr("aipdf::app::MainWindow", "AI 服务还没连上。"));
        emit finished();
        return;
    }
    QJsonArray messages;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"), systemPrompt);
    messages.append(system);
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"), userPrompt);
    messages.append(user);

    QJsonObject payload;
    payload.insert(QStringLiteral("task"), QStringLiteral("chat"));
    payload.insert(QStringLiteral("data_scope"), i18n::tr("aipdf::app::MainWindow", "本次发送：一次提问"));
    payload.insert(QStringLiteral("messages"), messages);

    collected_.clear();
    busy_ = true;
    activeRequestId_ = workerClient_->sendStreamRequest(QStringLiteral("ai.chat.stream"), payload);
}

QByteArray AiToolService::askModelBlocking(const QString& systemPrompt,
                                            const QString& userPrompt, QString* error) {
    if (workerClient_ == nullptr) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::MainWindow", "AI 服务还没连上。");
        }
        return QByteArray();
    }
    QEventLoop loop;
    QString answer;
    QString reason;
    bool failedFlag = false;

    const QMetaObject::Connection readyConnection =
        connect(this, &AiToolService::completionReady, &loop, [&](const QString& value) {
            answer = value;
            loop.quit();
        });
    const QMetaObject::Connection failedConnection =
        connect(this, &AiToolService::failed, &loop, [&](const QString& message) {
            failedFlag = true;
            reason = message;
            loop.quit();
        });
    // 模型不回也不能一直挂着：超时算失败，把原因说清楚。
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(kBlockingAskTimeoutMs);

    requestCompletion(systemPrompt, userPrompt);
    if (busy_) {
        loop.exec();
    }
    disconnect(readyConnection);
    disconnect(failedConnection);

    if (failedFlag || answer.isEmpty()) {
        if (error != nullptr) {
            *error = reason.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "模型没有返回内容。") : reason;
        }
        return QByteArray();
    }
    return answer.toUtf8();
}

void AiToolService::requestImageGeneration(const QByteArray& payload) {
    if (imageThread_ != nullptr) {
        imageThread_->wait();
        delete imageThread_;
        imageThread_ = nullptr;
    }
    const ai::ProviderConfig config = ai::ProviderConfig::load();
    const QString apiKey =
        ai::CredentialStore::load(ai::CredentialStore::targetNameFor(config.providerId));
    if (apiKey.isEmpty()) {
        emit failed(i18n::tr("aipdf::app::MainWindow", "没有配置 API 密钥，生成不了图片。请先在「AI 服务设置」里配置。"));
        emit finished();
        return;
    }
    const QString url = config.baseUrl + QLatin1String(kImageGenerationPath);
    busy_ = true;

    // 整条链路（发请求 + 解析 + 下载图片）都在后台线程上：
    // 领域层的图片下载是阻塞的，放到界面线程会把窗口卡住。
    imageThread_ = QThread::create([this, payload, url, apiKey]() {
        QNetworkAccessManager manager;
        QNetworkRequest request{QUrl(url)};
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());

        QNetworkReply* reply = manager.post(request, payload);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const QByteArray body = reply->readAll();
        const QVariant statusValue =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int status = statusValue.isValid() ? statusValue.toInt() : 0;
        const QString errorText = reply->errorString();
        reply->deleteLater();

        if (status < 200 || status >= 300) {
            const QString message = i18n::tr("aipdf::app::MainWindow", "图像服务返回了 %1：%2").arg(status).arg(errorText);
            QMetaObject::invokeMethod(this, "deliverFailure", Qt::QueuedConnection,
                                      Q_ARG(QString, message));
            QMetaObject::invokeMethod(this, "deliverFinished", Qt::QueuedConnection);
            return;
        }

        ai::ImageBytesFetcher fetcher =
            [&manager](const QString& imageUrl, QByteArray* out, QString* error) {
                return ai::downloadImageBytes(&manager, imageUrl, out, error);
            };
        const ai::ImageGenerationOutcome outcome = ai::parseImageGenerationResponse(body, fetcher);
        if (!outcome.ok) {
            // 解析失败就是失败：不能当成「生成成功但没有图」。
            QMetaObject::invokeMethod(this, "deliverFailure", Qt::QueuedConnection,
                                      Q_ARG(QString, outcome.error));
            QMetaObject::invokeMethod(this, "deliverFinished", Qt::QueuedConnection);
            return;
        }
        for (const ai::GeneratedImage& image : outcome.images) {
            QMetaObject::invokeMethod(this, "deliverImage", Qt::QueuedConnection,
                                      Q_ARG(QByteArray, image.bytes),
                                      Q_ARG(QString, image.mimeType),
                                      Q_ARG(QString, image.revisedPrompt));
        }
        QMetaObject::invokeMethod(this, "deliverFinished", Qt::QueuedConnection);
    });
    imageThread_->start();
}

void AiToolService::handleWorkerEvent(const QString& requestId, const QString& event,
                                      const QJsonObject& payload) {
    if (activeRequestId_.isEmpty() || requestId != activeRequestId_) {
        return;
    }
    if (event == QLatin1String("ai.stream.delta")) {
        collected_ += payload.value(QStringLiteral("text")).toString();
        return;
    }
    if (event == QLatin1String("ai.stream.done")) {
        const QString answer = collected_;
        collected_.clear();
        activeRequestId_.clear();
        busy_ = false;
        emit completionReady(answer);
        emit finished();
        return;
    }
    if (event == QLatin1String("ai.stream.error")) {
        const QString message = payload.value(QStringLiteral("message")).toString();
        collected_.clear();
        activeRequestId_.clear();
        busy_ = false;
        emit failed(message.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "模型调用失败。") : message);
        emit finished();
        return;
    }
    if (event == QLatin1String("ai.stream.cancelled")) {
        collected_.clear();
        activeRequestId_.clear();
        busy_ = false;
        emit failed(i18n::tr("aipdf::app::MainWindow", "这次请求被取消了。"));
        emit finished();
    }
}

void AiToolService::deliverImage(const QByteArray& bytes, const QString& mimeType,
                                 const QString& revisedPrompt) {
    emit imageReady(bytes, mimeType, revisedPrompt);
}

void AiToolService::deliverFailure(const QString& reason) {
    busy_ = false;
    emit failed(reason);
}

void AiToolService::deliverFinished() {
    busy_ = false;
    emit finished();
}

}  // namespace aipdf::app
