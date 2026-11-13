#include "ai/ChatTypes.h"

namespace aipdf::ai {

QString formatContextBlocks(const QVector<ContextBlock>& blocks) {
    QStringList parts;
    for (const ContextBlock& block : blocks) {
        parts.append(QStringLiteral("[%1]（第 %2 页）\n%3")
                         .arg(block.number)
                         .arg(block.pageNumber)
                         .arg(block.text.trimmed()));
    }
    return parts.join(QStringLiteral("\n\n"));
}

QString qaSystemPrompt() {
    // 引用格式是**硬要求**，不是建议：
    // 应用侧会逐字核对模型引的原文，格式不对的引用会被标成「无法核实」。
    return QStringLiteral(
        "你是一个 PDF 阅读助手。请只依据用户给出的材料回答问题，材料里没有的内容就直说「这些材料里没有提到」。\n"
        "回答要求：\n"
        "1. 用中文回答，尽量简短；\n"
        "2. 每一个事实性句子后面要加上来源编号，格式是方括号里的数字，例如 [1]；\n"
        "3. 回答的最后另起一段，以「引用：」开头，每条引用单独一行，"
        "格式是 [编号] 第 N 页 | 引文：原文片段；\n"
        "4. 「引文」必须**逐字照抄**材料里的原文片段（可以截取一段，不要改写、不要加省略号）；\n"
        "5. 只能引用材料里出现过的编号，不能编造编号。");
}

ChatMessage ChatMessage::system(const QString& text) {
    ChatMessage message;
    message.role = Role::System;
    message.content = text;
    return message;
}

ChatMessage ChatMessage::user(const QString& text) {
    ChatMessage message;
    message.role = Role::User;
    message.content = text;
    return message;
}

ChatMessage ChatMessage::assistant(const QString& text) {
    ChatMessage message;
    message.role = Role::Assistant;
    message.content = text;
    return message;
}

QString ChatMessage::roleName() const {
    switch (role) {
        case Role::System:
            return QStringLiteral("system");
        case Role::Assistant:
            return QStringLiteral("assistant");
        case Role::User:
            break;
    }
    return QStringLiteral("user");
}

double TokenUsage::estimatedCost(double inputPricePerMillion,
                                 double outputPricePerMillion) const {
    const double input = (inputTokens / 1'000'000.0) * inputPricePerMillion;
    const double output = (outputTokens / 1'000'000.0) * outputPricePerMillion;
    return input + output;
}

QString systemPromptForTask(const QString& task) {
    if (task == Task::DocumentQa) {
        return qaSystemPrompt();
    }
    if (task == Task::Summary) {
        return QStringLiteral(
            "你是一个 PDF 阅读助手。请用中文总结用户给出的这一页文本。"
            "要求：只依据给出的文本，不要补充文档里没有的信息；"
            "输出 3 到 6 条要点，每条一行；"
            "最后一行写「出处：第 N 页」，N 是页码。");
    }
    if (task == Task::Explain) {
        return QStringLiteral(
            "你是一个 PDF 阅读助手。请用中文解释用户给出的选中文字。"
            "要求：只依据给出的文字与上下文，不要凭空补充；"
            "先给一句话结论，再展开解释；"
            "最后一行写「出处：第 N 页」，N 是页码。");
    }
    if (task == Task::Translate) {
        return QStringLiteral(
            "你是一个 PDF 阅读助手。请把用户给出的选中文字翻译成中文。"
            "要求：只依据给出的文字翻译，不要解释、不要评论；"
            "保留原有的段落与列表结构；"
            "最后一行写「出处：第 N 页」，N 是页码。");
    }
    return QStringLiteral(
        "你是一个 PDF 阅读助手。请只依据用户给出的这一页文本回答问题。"
        "材料里没有的内容就直说「这一页没有提到」；"
        "回答用中文，尽量简短。");
}

QString userPromptForTask(const QString& task,
                          const QString& material,
                          const QString& question) {
    if (task == Task::Summary) {
        return QStringLiteral("以下是这一页的文本，请总结：\n\n") + material;
    }
    if (task == Task::Explain) {
        return QStringLiteral("以下是选中的文字，请解释：\n\n") + material;
    }
    if (task == Task::Translate) {
        return QStringLiteral("以下是选中的文字，请翻译成中文：\n\n") + material;
    }
    const QString trimmed = question.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("以下是这一页的文本：\n\n") + material;
    }
    return QStringLiteral("以下是这一页的文本：\n\n") + material +
           QStringLiteral("\n\n问题：") + trimmed;
}

}  // namespace aipdf::ai
