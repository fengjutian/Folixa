#pragma once

#include "ai/ChatTypes.h"

#include <QObject>
#include <QString>

namespace aipdf::ai {

// 模型 Provider 抽象。
//
// 界面只认这个接口，不认具体服务商：换一家只换一个实现。
// 对标：Open WebUI 与 Cherry Studio 都把 provider 抽象成一层，
// 聊天界面与具体后端解耦；Open WebUI 的 provider 客户端还是「按请求新建」，
// 避免共享可变状态——本类同样按请求新建（见 ProviderFactory）。
class ModelProvider : public QObject {
    Q_OBJECT

public:
    explicit ModelProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~ModelProvider() override = default;

    // Provider 标识（"openai" / "demo"）。
    virtual QString id() const = 0;
    // 界面显示的名字。
    virtual QString displayName() const = 0;
    // 能不能真的发起调用（有没有密钥、有没有地址）。
    virtual bool isConfigured() const = 0;
    // 是不是演示模式：没有真实密钥，用本地假模型应答。
    virtual bool isDemo() const { return false; }

    // 开始一次流式对话。结果通过下面的信号陆续给出。
    virtual void startChat(const ChatRequest& request) = 0;
    // 取消当前这次请求。
    virtual void cancel() = 0;
    // 当前是否正在跑。
    virtual bool isRunning() const = 0;

signals:
    void streamStarted();
    // 一段增量文本。
    void streamDelta(const QString& text);
    // 正常结束：结束原因与 token 用量。
    void streamFinished(const QString& finishReason, int inputTokens, int outputTokens);
    // 出错：稳定的错误码 + 给用户看的提示。
    void streamFailed(const QString& code, const QString& message);
    // 被用户取消。
    void streamCancelled();
};

}  // namespace aipdf::ai
