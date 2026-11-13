#include "ai/MockProvider.h"

#include <QTimer>

namespace aipdf::ai {

MockProvider::MockProvider(int chunkDelayMs, QObject* parent)
    : ModelProvider(parent), chunkDelayMs_(chunkDelayMs > 0 ? chunkDelayMs : 30) {
    // 环境变量可以放慢演示模式的吐字速度。
    //
    // 取消功能的验收需要一条**足够长**的流：默认 30ms 一段太快，
    // 「点停止」还没点到就结束了，测不出「取消真的能停下来」。
    // 验收脚本把 AIPDF_AI_MOCK_DELAY_MS 调大，这条流就变长，
    // 取消的时序才是确定的。
    const int override =
        qEnvironmentVariableIntValue("AIPDF_AI_MOCK_DELAY_MS");
    if (override > 0) {
        chunkDelayMs_ = override;
    }
    timer_ = new QTimer(this);
    timer_->setInterval(chunkDelayMs_);
    connect(timer_, &QTimer::timeout, this, &MockProvider::emitNextChunk);
}

QString MockProvider::id() const {
    return QStringLiteral("demo");
}

QString MockProvider::displayName() const {
    return QStringLiteral("演示模式（本地应答，未接入模型）");
}

bool MockProvider::isConfigured() const {
    // 演示模式永远「可用」：它不需要密钥。
    return true;
}

bool MockProvider::isRunning() const {
    return running_;
}

void MockProvider::startChat(const ChatRequest& request) {
    if (running_) {
        emit streamFailed(QStringLiteral("busy"), QStringLiteral("上一次请求还没结束。"));
        return;
    }
    cancelled_ = false;
    pending_ = buildAnswer(request);
    offset_ = 0;
    running_ = true;
    emit streamStarted();
    timer_->start();
}

void MockProvider::cancel() {
    if (!running_) {
        return;
    }
    timer_->stop();
    running_ = false;
    pending_.clear();
    emit streamCancelled();
}

void MockProvider::emitNextChunk() {
    if (!running_) {
        return;
    }
    if (offset_ >= pending_.size()) {
        timer_->stop();
        running_ = false;
        // 用量按「材料长度」估算，量级对得上真实模型就行。
        const int inputTokens = pending_.size() / 2;
        const int outputTokens = pending_.size() / 2;
        emit streamFinished(QStringLiteral("stop"), inputTokens, outputTokens);
        return;
    }
    // 每次吐几个字：真流式不是一个字一个字蹦，否则界面看起来像卡住。
    const int step = qMin(4, pending_.size() - offset_);
    emit streamDelta(pending_.mid(offset_, step));
    offset_ += step;
}

QString MockProvider::buildAnswer(const ChatRequest& request) const {
    // 从最后一条用户消息里把材料抠出来，好让演示答复跟真实内容对得上。
    QString material;
    for (int index = request.messages.size() - 1; index >= 0; --index) {
        const ChatMessage& message = request.messages.at(index);
        if (message.role != ChatMessage::Role::User) {
            continue;
        }
        material = message.content;
        break;
    }
    // 去掉提示词前缀，只留材料。
    const int marker = material.indexOf(QStringLiteral("\n\n"));
    if (marker >= 0) {
        material = material.mid(marker + 2);
    }
    const QString excerpt = material.left(80).trimmed();

    if (request.task == Task::Summary) {
        return QStringLiteral(
                   "【演示模式】下面是按当前页文本拼出的摘要示例（没有真正调用模型）。\n"
                   "· 材料开头：%1\n"
                   "· 材料长度：%2 字\n"
                   "· 要真正的模型摘要，请在设置里填 API 密钥。")
            .arg(excerpt.isEmpty() ? QStringLiteral("（空）") : excerpt)
            .arg(material.size());
    }
    if (request.task == Task::Explain) {
        return QStringLiteral(
                   "【演示模式】下面是按选中文字拼出的解释示例（没有真正调用模型）。\n"
                   "· 选中内容：%1\n"
                   "· 要真正的模型解释，请在设置里填 API 密钥。")
            .arg(excerpt.isEmpty() ? QStringLiteral("（空）") : excerpt);
    }
    if (request.task == Task::Translate) {
        return QStringLiteral(
                   "【演示模式】下面是按选中文字拼出的翻译示例（没有真正调用模型）。\n"
                   "· 原文：%1\n"
                   "· 要真正的模型翻译，请在设置里填 API 密钥。")
            .arg(excerpt.isEmpty() ? QStringLiteral("（空）") : excerpt);
    }
    return QStringLiteral(
               "【演示模式】收到你的问题，下面是本地拼出的示例回答（没有真正调用模型）。\n"
               "· 材料开头：%1\n"
               "· 材料长度：%2 字")
        .arg(excerpt.isEmpty() ? QStringLiteral("（空）") : excerpt)
        .arg(material.size());
}

}  // namespace aipdf::ai
