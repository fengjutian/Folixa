#include "ai/AuditLog.h"

#include "ai/ProviderConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace aipdf::ai {

QString auditLogPath() {
    return QDir(ProviderConfig::sharedDataDirectory()).filePath(QStringLiteral("ai/audit.jsonl"));
}

bool appendAudit(const AuditEntry& entry) {
    const QString path = auditLogPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }

    QJsonObject row;
    row.insert(QStringLiteral("time"), entry.time.toUTC().toString(Qt::ISODate));
    row.insert(QStringLiteral("mode"), entry.mode);
    row.insert(QStringLiteral("task"), entry.task);
    row.insert(QStringLiteral("chunks"), entry.chunkCount);
    row.insert(QStringLiteral("chars"), entry.charCount);
    row.insert(QStringLiteral("host"), entry.host);
    row.insert(QStringLiteral("allowed"), entry.allowed);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    const QByteArray line =
        QJsonDocument(row).toJson(QJsonDocument::Compact) + QByteArrayLiteral("\n");
    const bool written = file.write(line) == line.size();
    file.close();
    return written;
}

bool appendProposalAudit(const ProposalAuditEntry& entry) {
    QJsonObject object;
    object.insert(QStringLiteral("kind"), QStringLiteral("proposal"));
    object.insert(QStringLiteral("time"),
                  (entry.time.isValid() ? entry.time : QDateTime::currentDateTime())
                      .toString(Qt::ISODateWithMs));
    // 只落文件名，不落绝对路径：路径里的用户名与目录名属于「内容之外、但仍不该扩散」
    // 的信息，审计文件本身不能成为新的泄露面（见 docs/PRIVACY_AUDIT.md）。
    const QString documentLabel = entry.documentPath.isEmpty()
                                      ? QString()
                                      : QFileInfo(entry.documentPath).fileName();
    object.insert(QStringLiteral("document"), documentLabel);
    object.insert(QStringLiteral("operations"), entry.operationCount);
    object.insert(QStringLiteral("applied"), entry.appliedCount);
    object.insert(QStringLiteral("confirmed"), entry.confirmed);
    object.insert(QStringLiteral("result"), entry.result);
    QJsonArray tools;
    for (const QString& tool : entry.tools) {
        tools.append(tool);
    }
    object.insert(QStringLiteral("tools"), tools);

    const QString path = auditLogPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.write("\n");
    return true;
}
}  // namespace aipdf::ai
