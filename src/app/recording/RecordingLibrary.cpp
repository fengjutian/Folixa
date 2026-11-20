#include "app/recording/RecordingLibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace aipdf::app {
namespace {

QString kindToString(RecordingKind kind) {
    switch (kind) {
        case RecordingKind::Screen: return QStringLiteral("screen");
        case RecordingKind::Presentation: return QStringLiteral("presentation");
        case RecordingKind::Audio: return QStringLiteral("audio");
        case RecordingKind::Screenshot: return QStringLiteral("screenshot");
    }
    return QStringLiteral("screen");
}

bool kindFromString(const QString& text, RecordingKind* kind) {
    if (text == QLatin1String("screen")) { *kind = RecordingKind::Screen; return true; }
    if (text == QLatin1String("presentation")) { *kind = RecordingKind::Presentation; return true; }
    if (text == QLatin1String("audio")) { *kind = RecordingKind::Audio; return true; }
    if (text == QLatin1String("screenshot")) { *kind = RecordingKind::Screenshot; return true; }
    return false;
}

}  // namespace

QString RecordingEntry::displayName() const {
    const QFileInfo info(path);
    const QString base = info.completeBaseName();
    return base.isEmpty() ? path : base;
}

RecordingLibrary::RecordingLibrary(const QString& indexPath) : indexPath_(indexPath) {}

QString RecordingLibrary::defaultDirectory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
        .filePath(QStringLiteral("Captures"));
}

QString RecordingLibrary::defaultIndexPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("recordings.json"));
}

QString RecordingLibrary::fileNameFor(RecordingKind kind, const QDateTime& at) {
    const QString stamp = at.toString(QStringLiteral("yyyyMMdd-HHmmss"));
    switch (kind) {
        case RecordingKind::Screen:
            return QStringLiteral("Folixa-screen-%1.avi").arg(stamp);
        case RecordingKind::Presentation:
            return QStringLiteral("Folixa-presentation-%1.avi").arg(stamp);
        case RecordingKind::Audio:
            return QStringLiteral("Folixa-audio-%1.wav").arg(stamp);
        case RecordingKind::Screenshot:
            return QStringLiteral("Folixa-shot-%1.png").arg(stamp);
    }
    return QStringLiteral("Folixa-%1.avi").arg(stamp);
}

QString RecordingLibrary::kindLabel(RecordingKind kind) {
    switch (kind) {
        case RecordingKind::Screen: return QStringLiteral("屏幕录制");
        case RecordingKind::Presentation: return QStringLiteral("PDF 录制");
        case RecordingKind::Audio: return QStringLiteral("声音录制");
        case RecordingKind::Screenshot: return QStringLiteral("截图");
    }
    return QStringLiteral("录制");
}

bool RecordingLibrary::kindFromFileName(const QString& fileName, RecordingKind* kind) {
    const QString lower = fileName.toLower();
    if (lower.contains(QLatin1String("presentation"))) { *kind = RecordingKind::Presentation; return true; }
    if (lower.contains(QLatin1String("audio"))) { *kind = RecordingKind::Audio; return true; }
    if (lower.contains(QLatin1String("shot"))) { *kind = RecordingKind::Screenshot; return true; }
    if (lower.contains(QLatin1String("screen"))) { *kind = RecordingKind::Screen; return true; }
    return false;
}

bool RecordingLibrary::load(QString* error) {
    entries_.clear();
    QFile file(indexPath_);
    if (!file.exists()) {
        // 索引不存在 = 还没录过任何东西，不是错误。
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) *error = QStringLiteral("打不开录制索引：%1").arg(file.errorString());
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        if (error != nullptr) {
            *error = QStringLiteral("录制索引坏了（%1），这次先当空清单处理。")
                         .arg(parseError.errorString());
        }
        return false;
    }

    const QJsonArray array = doc.array();
    for (const QJsonValue& value : array) {
        const QJsonObject object = value.toObject();
        RecordingEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        RecordingKind kind = RecordingKind::Screen;
        kindFromString(object.value(QStringLiteral("kind")).toString(), &kind);
        entry.kind = kind;
        entry.path = object.value(QStringLiteral("path")).toString();
        entry.createdAt = QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(),
                                                Qt::ISODate);
        entry.sizeBytes = static_cast<qint64>(object.value(QStringLiteral("sizeBytes")).toDouble());
        entry.durationMs = object.value(QStringLiteral("durationMs")).toInt();
        entry.width = object.value(QStringLiteral("width")).toInt();
        entry.height = object.value(QStringLiteral("height")).toInt();
        if (entry.path.isEmpty()) continue;
        if (entry.id.isEmpty()) entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entries_.append(entry);
    }
    return true;
}

bool RecordingLibrary::save(QString* error) {
    QJsonArray array;
    for (const RecordingEntry& entry : entries_) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), entry.id);
        object.insert(QStringLiteral("kind"), kindToString(entry.kind));
        object.insert(QStringLiteral("path"), entry.path);
        object.insert(QStringLiteral("createdAt"), entry.createdAt.toString(Qt::ISODate));
        object.insert(QStringLiteral("sizeBytes"), static_cast<double>(entry.sizeBytes));
        object.insert(QStringLiteral("durationMs"), entry.durationMs);
        object.insert(QStringLiteral("width"), entry.width);
        object.insert(QStringLiteral("height"), entry.height);
        array.append(object);
    }

    QDir().mkpath(QFileInfo(indexPath_).absolutePath());
    QFile file(indexPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) *error = QStringLiteral("写不进录制索引：%1").arg(file.errorString());
        return false;
    }
    const QByteArray raw = QJsonDocument(array).toJson(QJsonDocument::Indented);
    const bool ok = file.write(raw) == raw.size();
    file.close();
    if (!ok && error != nullptr) {
        *error = QStringLiteral("写不进录制索引：磁盘写入失败。");
    }
    return ok;
}

QVector<RecordingEntry> RecordingLibrary::entries() const {
    QVector<RecordingEntry> sorted = entries_;
    std::sort(sorted.begin(), sorted.end(), [](const RecordingEntry& a, const RecordingEntry& b) {
        if (a.createdAt != b.createdAt) return a.createdAt > b.createdAt;
        return a.path < b.path;
    });
    return sorted;
}

bool RecordingLibrary::contains(const QString& path) const {
    for (const RecordingEntry& entry : entries_) {
        if (entry.path.compare(path, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool RecordingLibrary::add(const RecordingEntry& entry, QString* error) {
    if (entry.path.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("这条录制没有文件路径。");
        return false;
    }
    RecordingEntry copy = entry;
    if (copy.id.isEmpty()) copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!copy.createdAt.isValid()) copy.createdAt = QDateTime::currentDateTime();

    // 同一路径只留一条：重新录一遍同名文件时更新原条目。
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_.at(i).path.compare(copy.path, Qt::CaseInsensitive) == 0) {
            entries_[i] = copy;
            return save(error);
        }
    }
    entries_.append(copy);
    return save(error);
}

bool RecordingLibrary::remove(const QString& id, bool deleteFile, QString* error) {
    int found = -1;
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_.at(i).id == id) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        if (error != nullptr) *error = QStringLiteral("清单里没有这一条录制。");
        return false;
    }
    const RecordingEntry entry = entries_.at(found);
    entries_.remove(found);

    if (deleteFile) {
        QFile file(entry.path);
        if (file.exists() && !file.remove()) {
            if (error != nullptr) {
                *error = QStringLiteral("删不掉文件：%1").arg(QDir::toNativeSeparators(entry.path));
            }
            // 清单照样更新：文件删不掉不该让整条录制留在清单里假装还在。
        }
    }
    return save(error);
}

int RecordingLibrary::syncWithDirectory(QString* error) {
    const QDir directory(defaultDirectory());
    QStringList removed;
    for (int i = entries_.size() - 1; i >= 0; --i) {
        if (!QFileInfo::exists(entries_.at(i).path)) {
            removed.append(entries_.at(i).displayName());
            entries_.remove(i);
        }
    }

    // 把目录里还没登记的文件补进来。
    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("Folixa-*.avi"), QStringLiteral("Folixa-*.wav"), QStringLiteral("Folixa-*.png")},
        QDir::Files, QDir::Time);
    for (const QFileInfo& info : files) {
        if (contains(info.absoluteFilePath())) continue;
        RecordingEntry entry;
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.path = info.absoluteFilePath();
        entry.createdAt = info.lastModified();
        entry.sizeBytes = info.size();
        RecordingKind kind = RecordingKind::Screen;
        kindFromFileName(info.fileName(), &kind);
        entry.kind = kind;
        entries_.append(entry);
    }

    if (!removed.isEmpty() && error != nullptr) {
        *error = QStringLiteral("以下录制文件已不存在，已从清单移除：%1")
                     .arg(removed.join(QStringLiteral("、")));
    }
    save(nullptr);
    return entries_.size();
}

}  // namespace aipdf::app
