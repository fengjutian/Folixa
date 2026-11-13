#include "doc/RecoveryLog.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace aipdf::doc {

namespace {

constexpr auto kKeyVersion = "version";
constexpr auto kKeyDocumentPath = "documentPath";
constexpr auto kKeyUpdatedAt = "updatedAt";
constexpr auto kKeyCommands = "commands";

constexpr int kFormatVersion = 1;

QString normalizedPath(const QString& documentPath) {
    return QFileInfo(documentPath).absoluteFilePath();
}

}  // namespace

QString RecoveryLog::directoryPath() {
    // 测试与便携模式可以用环境变量改位置。
    const QString override = qEnvironmentVariable("AIPDF_RECOVERY_DIR");
    if (!override.isEmpty()) {
        return override;
    }
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("recovery"));
}

QString RecoveryLog::keyFor(const QString& documentPath) {
    const QByteArray hash =
        QCryptographicHash::hash(normalizedPath(documentPath).toUtf8(),
                                 QCryptographicHash::Sha1);
    return QString::fromLatin1(hash.toHex());
}

bool RecoveryLog::write(const QString& documentPath, const QVector<QByteArray>& commands) {
    if (documentPath.isEmpty() || commands.isEmpty()) {
        return false;
    }

    const QString directory = directoryPath();
    if (!QDir().mkpath(directory)) {
        return false;
    }

    QJsonArray commandArray;
    for (const QByteArray& command : commands) {
        const QJsonDocument document = QJsonDocument::fromJson(command);
        if (!document.isObject()) {
            // 认不出来的命令不记：重放时跳过它比整条记录作废好。
            continue;
        }
        commandArray.append(document.object());
    }
    if (commandArray.isEmpty()) {
        return false;
    }

    QJsonObject object;
    object.insert(QString::fromLatin1(kKeyVersion), kFormatVersion);
    object.insert(QString::fromLatin1(kKeyDocumentPath), normalizedPath(documentPath));
    object.insert(QString::fromLatin1(kKeyUpdatedAt),
                  QDateTime::currentDateTime().toString(Qt::ISODate));
    object.insert(QString::fromLatin1(kKeyCommands), commandArray);

    const QString path =
        QDir(directory).filePath(keyFor(documentPath) + QStringLiteral(".json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const bool ok = file.write(data) == data.size();
    file.close();
    return ok;
}

bool RecoveryLog::read(const QString& documentPath, RecoveryEntry* entry) {
    if (entry == nullptr || documentPath.isEmpty()) {
        return false;
    }
    const QString path =
        QDir(directoryPath()).filePath(keyFor(documentPath) + QStringLiteral(".json"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject object = document.object();
    const QJsonArray commands = object.value(QString::fromLatin1(kKeyCommands)).toArray();
    if (commands.isEmpty()) {
        return false;
    }

    entry->documentPath =
        object.value(QString::fromLatin1(kKeyDocumentPath)).toString(normalizedPath(documentPath));
    entry->updatedAt = object.value(QString::fromLatin1(kKeyUpdatedAt)).toString();
    entry->commands.clear();
    for (const QJsonValue& value : commands) {
        if (!value.isObject()) {
            continue;
        }
        entry->commands.append(
            QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return !entry->commands.isEmpty();
}

QVector<RecoveryEntry> RecoveryLog::list() {
    QVector<RecoveryEntry> entries;
    const QString directory = directoryPath();
    QDir dir(directory);
    if (!dir.exists()) {
        return entries;
    }
    const QStringList files =
        dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString& name : files) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray data = file.readAll();
        file.close();
        const QJsonDocument document = QJsonDocument::fromJson(data);
        if (!document.isObject()) {
            continue;
        }
        const QJsonObject object = document.object();
        const QJsonArray commands = object.value(QString::fromLatin1(kKeyCommands)).toArray();
        if (commands.isEmpty()) {
            continue;
        }
        RecoveryEntry entry;
        entry.documentPath = object.value(QString::fromLatin1(kKeyDocumentPath)).toString();
        entry.updatedAt = object.value(QString::fromLatin1(kKeyUpdatedAt)).toString();
        for (const QJsonValue& value : commands) {
            if (!value.isObject()) {
                continue;
            }
            entry.commands.append(
                QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
        }
        if (entry.isValid()) {
            entries.append(entry);
        }
    }
    return entries;
}

bool RecoveryLog::remove(const QString& documentPath) {
    if (documentPath.isEmpty()) {
        return false;
    }
    const QString path =
        QDir(directoryPath()).filePath(keyFor(documentPath) + QStringLiteral(".json"));
    if (!QFile::exists(path)) {
        return true;
    }
    return QFile::remove(path);
}

}  // namespace aipdf::doc
