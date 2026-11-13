#pragma once

#include "ai/ModelProvider.h"

#include <QString>

class QTimer;

namespace aipdf::ai {

// 演示 Provider：没有真实密钥时用它。
//
// 它不是「假的成功」——它确实按任务生成一段像样的中文答复，
// 并且**逐字流式吐出来**，所以流式链路（增量、取消、用量）在演示模式下
// 也是真的跑通的，只是回答的内容由本地拼出来，不来自任何模型。
//
// 界面必须明确标出这是演示模式：不能让人以为模型真的回答了。
class MockProvider final : public ModelProvider {
    Q_OBJECT

public:
    // 每个字之间的间隔（毫秒）：用来模拟真实的逐字输出。
    explicit MockProvider(int chunkDelayMs = 30, QObject* parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    bool isDemo() const override { return true; }

    void startChat(const ChatRequest& request) override;
    void cancel() override;
    bool isRunning() const override;

private:
    void emitNextChunk();
    QString buildAnswer(const ChatRequest& request) const;
    // 整篇问答的演示回答：引用**真实的**上下文块。
    //
    // 为什么不用编一段像样的样子：引文要能在原文里逐字核对到，
    // 编出来的引文必然核验不过，演示模式就跑不通整条链路。
    QString buildQaAnswer(const ChatRequest& request) const;

    int chunkDelayMs_;
    QTimer* timer_ = nullptr;
    QString pending_;
    int offset_ = 0;
    bool running_ = false;
    bool cancelled_ = false;
};

}  // namespace aipdf::ai
