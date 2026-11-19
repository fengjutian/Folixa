#pragma once

#include <QJsonObject>
#include <QString>

#include "pdf/ops/TranslationWriter.h"

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

// 把 Worker 的 AI 接口包成 pdf::TranslationProvider。
//
// 为什么要有这一层：领域层（pdf::translateDocument）**不该知道网络是怎么走的**，
// 它只认「给我一段原文，我还你一段译文」这一个接口。
// 界面层把 Worker 的流式聊天包成这个接口，领域层就能整篇翻译并写回。
//
// **演示模式一律拒绝写回**：演示 Provider 的输出是拼出来的示例，
// 把示例写进用户的文档是最坏的一种失败。这里直接按 Worker 报上来的
// provider 状态判断，不靠「有没有密钥」猜。
class WorkerTranslationProvider final : public pdf::TranslationProvider {
public:
    explicit WorkerTranslationProvider(ipc::WorkerClient* client);

    QString id() const override;
    bool isConfigured() const override;
    bool isDemo() const override;
    bool translate(const QString& sourceText, const QString& targetLanguage, QString* translation,
                   QString* error) override;

private:
    // 问 Worker 现在用的是哪个 Provider、是不是演示模式。
    bool queryProviderStatus(QJsonObject* status, QString* error) const;
    // 等一次同步请求的结果（内部跑事件循环）。
    bool waitForResponse(const QString& requestId, int timeoutMs, QJsonObject* response,
                         QString* error) const;

    ipc::WorkerClient* client_ = nullptr;
};

}  // namespace aipdf::app
