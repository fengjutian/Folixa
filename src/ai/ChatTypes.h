#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::ai {

// 一条对话消息。
struct ChatMessage {
    enum class Role {
        System,
        User,
        Assistant,
    };

    Role role = Role::User;
    QString content;

    static ChatMessage system(const QString& text);
    static ChatMessage user(const QString& text);
    static ChatMessage assistant(const QString& text);

    // 协议里的角色名（"system" / "user" / "assistant"）。
    QString roleName() const;
};

// 四个受限任务。
//
// 「受限」的意思有两层：
//   1. 能送出去的内容是有限的——只有当前页文本或当前选区，
//      绝不默认送整份文档；
//   2. 每个任务有固定的系统提示词，用户问什么都不会越出这个范围。
namespace Task {
inline constexpr auto Summary = "summary";    // 总结当前页
inline constexpr auto Explain = "explain";    // 解释当前选区
inline constexpr auto Translate = "translate";// 翻译当前选区
inline constexpr auto Chat = "chat";          // 就当前页提问
}  // namespace Task

// 一次对话请求。
struct ChatRequest {
    // 任务类型（Task 里的四个之一）。
    QString task = Task::Chat;
    QVector<ChatMessage> messages;
    // 送给模型的模型名（留空表示用 Provider 自己的默认模型）。
    QString model;
    double temperature = 0.2;
    int maxTokens = 800;
    // 用户可见的「本次发送范围」说明，例如「第 3 页 · 1,240 字」。
    // 界面在发出去之前先把它显示出来。
    QString dataScope;
};

// token 用量。
struct TokenUsage {
    int inputTokens = 0;
    int outputTokens = 0;

    int totalTokens() const { return inputTokens + outputTokens; }

    // 估算费用（美元）。
    //
    // 价格是「每百万 token 多少美元」，写在配置里，不写死在代码里：
    // 服务商的定价会变，写死就会悄悄算错。
    double estimatedCost(double inputPricePerMillion, double outputPricePerMillion) const;
};

// 某个任务的系统提示词。
//
// 提示词里明确「只依据给出的材料回答」，避免模型自己编造文档里没有的内容；
// 摘要与解释要求给出出处（页码），为后面的引用定位留好接口。
QString systemPromptForTask(const QString& task);

// 构造某个任务的用户消息：材料 + 问题。
//
// material 是这次真正要送出去的文本（当前页文本或当前选区），
// question 是用户的问题（摘要任务可以为空）。
QString userPromptForTask(const QString& task,
                          const QString& material,
                          const QString& question);

}  // namespace aipdf::ai
