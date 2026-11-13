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

QString MockProvider::buildQaAnswer(const ChatRequest& request) const {
    // 引文取块的第一行：逐字照抄，不加省略号，不改写。
    //
    // 这样引文一定能在原文里核对到——演示模式也能跑通完整的核验链路。
    constexpr int kMaximumQuoteCharacters = 200;

    // 负例开关：故意引用一句原文里没有的话。
    //
    // 验收要能证明「伪造的引用会被挡住」，
    // 而演示模式本来永远引真块，这条路走不到——
    // 所以留一个只在演示模式生效的开关，让验收能真的走一遍负例。
    const bool fabricate = qEnvironmentVariableIsSet("AIPDF_AI_MOCK_FAKE_CITATION");
    const QString fabricatedQuote = QStringLiteral("这一句是演示模式伪造的，原文里并没有它。");

    QStringList body;
    body.append(QStringLiteral(
        "【演示模式】下面是按检索到的材料拼出的回答示例（没有真正调用模型）。"));

    QStringList citationLines;
    for (const ContextBlock& block : request.context) {
        QString quote = block.text.split(QLatin1Char('\n')).first().trimmed();
        if (quote.size() > kMaximumQuoteCharacters) {
            // 截断仍然是原文的**前缀**，逐字核对照样过；
            // 但不能加省略号——加了就不是原文了。
            quote = quote.left(kMaximumQuoteCharacters);
        }
        if (fabricate) {
            quote = fabricatedQuote;
        }
        body.append(QStringLiteral("· 第 %1 页的材料说：%2 [%3]")
                        .arg(block.pageNumber)
                        .arg(quote)
                        .arg(block.number));
        citationLines.append(QStringLiteral("[%1] 第 %2 页 | 引文：%3")
                                 .arg(block.number)
                                 .arg(block.pageNumber)
                                 .arg(quote));
    }

    if (citationLines.isEmpty()) {
        body.append(QStringLiteral("· 没有检索到可引用的材料。"));
    }

    return body.join(QLatin1Char('\n')) + QStringLiteral("\n\n引用：\n") +
           citationLines.join(QLatin1Char('\n'));
}

QString MockProvider::buildAnswer(const ChatRequest& request) const {
    if (request.task == Task::DocumentQa) {
        return buildQaAnswer(request);
    }
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
