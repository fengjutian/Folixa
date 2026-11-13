#pragma once

#include "ai/ModelProvider.h"

#include <QByteArray>
#include <QLocalServer>
#include <QObject>
#include <QString>

#include <memory>

class QLocalSocket;
class QJsonObject;

namespace aipdf::worker {

class WorkerServer final : public QObject {
    Q_OBJECT

public:
    explicit WorkerServer(QString serverName, QObject* parent = nullptr);
    ~WorkerServer() override;

    bool start();

private:
    void acceptConnection();
    void readFrames();
    void handleMessage(QLocalSocket* socket, const QJsonObject& request);
    // 开始一次流式对话：先回一个 result 说「开始了」，再陆续发事件帧。
    void startChat(const QString& requestId, const QJsonObject& payload);
    // 结束当前这次流（正常结束、出错、取消都走这里）。
    void finishStream(const QString& requestId, const QString& event,
                      const QJsonObject& payload);
    void writeFrame(const QJsonObject& frame);

    QString serverName_;
    QLocalServer server_;
    QLocalSocket* client_ = nullptr;
    QByteArray receiveBuffer_;
    // 当前的模型 Provider（按请求新建，见 ai/ProviderFactory 的说明）。
    std::unique_ptr<aipdf::ai::ModelProvider> provider_;
    // 正在进行的流式请求 id。
    QString activeStreamId_;
};

}  // namespace aipdf::worker
