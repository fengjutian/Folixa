#include "ai/AuditLog.h"

#include "ai/ProviderConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
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

}  // namespace aipdf::ai
