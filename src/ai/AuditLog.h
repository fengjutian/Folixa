#pragma once

#include <QDateTime>
#include <QString>

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

}  // namespace aipdf::ai
