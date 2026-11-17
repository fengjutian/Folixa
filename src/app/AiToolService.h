#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QThread;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

// AI 工具的网络层：把领域层准备好的请求发出去，并把结果读回来。
//
// 这一层**不决定发什么**：提示词、请求体、解析规则全部由领域层给
// （ai::buildImageGenerationPayload / ai::buildDetectionPrompt /
// ai::parseDetectionResponse / ai::parseImageGenerationResponse）。
// 界面层只负责把参数收齐、把结果显示出来。
class AiToolService final : public QObject {
    Q_OBJECT

public:
    explicit AiToolService(QObject* parent = nullptr);
    ~AiToolService() override;

    // 绑定 Worker 客户端（聊天类的请求走它，与 AI 面板同一套通路）。
    void setWorkerClient(ipc::WorkerClient* client);

    // 一次性聊天调用：送一段系统提示词与一段用户提示词，拿回模型的原始回答。
    //
    // 回答是**原始文本**：JSON 的解析交给领域层，界面不自己剥括号。
    void requestCompletion(const QString& systemPrompt, const QString& userPrompt);

    // 同步地问模型一句（PPT 生成需要这种形状：领域层要的是一个同步回调）。
    // 返回空字节表示失败，失败原因写进 error。
    QByteArray askModelBlocking(const QString& systemPrompt, const QString& userPrompt,
                                 QString* error);

    // 文生图：payload 由领域层的 buildImageGenerationPayload 拼好，
    // 这里只负责发请求与解析结果。
    void requestImageGeneration(const QByteArray& payload);

    // 当前是不是还有请求在跑。
    bool busy() const;

signals:
    // 模型的一次性回答。
    void completionReady(const QString& text);
    // 生成好的一张图片。
    void imageReady(const QByteArray& bytes, const QString& mimeType, const QString& revisedPrompt);
    // 失败了（中文原因）。
    void failed(const QString& reason);
    // 一次请求收尾（成功或失败都会发）。
    void finished();

private slots:
    void deliverImage(const QByteArray& bytes, const QString& mimeType, const QString& revisedPrompt);
    void deliverFailure(const QString& reason);
    void deliverFinished();

private:
    void handleWorkerEvent(const QString& requestId, const QString& event,
                           const QJsonObject& payload);

    ipc::WorkerClient* workerClient_ = nullptr;
    // 正在跑的聊天请求 id（worker 的流式请求用它对号入座）。
    QString activeRequestId_;
    // 这次请求收到的增量文本。
    QString collected_;
    // 文生图跑在后台线程上：下载图片字节是阻塞的，不能占住界面线程。
    QThread* imageThread_ = nullptr;
    bool busy_ = false;
};

}  // namespace aipdf::app
