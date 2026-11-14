#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace aipdf::ai {

// 一条审计记录：只记「发生了什么」，不记「内容是什么」。
//
// 这里刻意没有正文字段。审计能回答「昨天有没有东西发出去、发到哪」，
// 但它自己不能变成一份新的泄露面——所以不写选区、不写块内容、不写密钥。
struct AuditEntry {
    QDateTime time;
    QString mode;   // local / chunks / pages
    QString task;   // summary / explain / translate / chat / qa
    int chunkCount = 0;
    int charCount = 0;
    QString host;   // 目标主机；本地模式为空
    bool allowed = true;
};

// 审计文件路径：与 AI 配置同一个数据目录，文件名 audit.jsonl。
QString auditLogPath();

// 追加一条记录（JSON Lines）。写入失败返回 false，调用方不应因此中断回答。
bool appendAudit(const AuditEntry& entry);


// 一条「文档操作提案」的审计记录（v0.14.0）。
//
// 与云端请求的审计同一条原则：只记元数据，不记提案要写的文字内容。
// 它要能回答的是「谁在什么时候确认了几条修改、涉及哪些工具、结果如何」。
struct ProposalAuditEntry {
    QDateTime time;
    QString documentPath;   // 文档路径（用于定位是哪份文档）
    QStringList tools;      // 涉及的工具 id（去重后）
    int operationCount = 0; // 提案里的操作条数
    int appliedCount = 0;   // 真正执行成功的条数
    bool confirmed = false; // 用户是否点了「执行」
    QString result;         // applied / cancelled / failed
};

bool appendProposalAudit(const ProposalAuditEntry& entry);

}  // namespace aipdf::ai
