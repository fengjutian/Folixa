#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace aipdf::tools {

// 一条通过校验的提案：工具 + 参数 + 给人看的一句话。
struct ProposedOperation {
    QString toolId;
    QString displayName;
    QJsonObject args;
    QString summary;  // 预览对话框里的那一行
};

struct ProposalIssue {
    int index = -1;   // 第几条操作（从 0 开始；-1 表示整份提案）
    QString message;
};

struct Proposal {
    QVector<ProposedOperation> operations;
    QVector<ProposalIssue> issues;
    bool ok() const { return issues.isEmpty() && !operations.isEmpty(); }
};

// 校验时的文档事实：页数与修订号。
struct DocumentSnapshot {
    int pageCount = 0;
    // 文档修订号：提案生成时的版本。执行前若文档已变，必须重新确认。
    int revision = 0;
};

// 校验模型给出的提案（JSON 数组）。
//
// 这一步是「AI 无法绕过确认」的第一道闸：
//   - 未知工具、明确禁止的工具、只读工具混进变更提案 → 直接判失败；
//   - 参数必须符合 schema（必填、类型、范围、数组长度）；
//   - 页码必须落在当前文档范围内，修订号必须与生成提案时一致。
// 校验只产出「可预览的提案」，**不执行任何操作**。
Proposal validateProposal(const QJsonArray& rawOperations, const DocumentSnapshot& document);

}  // namespace aipdf::tools
