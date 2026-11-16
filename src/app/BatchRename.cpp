#include "app/BatchRename.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>

namespace aipdf::app {
namespace {

QString normalized(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

Qt::CaseSensitivity pathCaseSensitivity() {
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool samePath(const QString& left, const QString& right) {
    return normalized(left).compare(normalized(right), pathCaseSensitivity()) == 0;
}

struct RenameItem {
    QString source;
    QString temporary;
    QString target;
};

}  // namespace

BatchRenameResult batchRenameFiles(const QStringList& filePaths,
                                   const QString& prefix,
                                   int firstNumber) {
    BatchRenameResult result;
    const QString cleanPrefix = prefix.trimmed();
    if (filePaths.isEmpty()) {
        result.error = QStringLiteral("没有选择文件");
        return result;
    }
    if (cleanPrefix.isEmpty() || firstNumber < 0 || cleanPrefix.contains('/') ||
        cleanPrefix.contains('\\')) {
        result.error = QStringLiteral("文件名前缀无效");
        return result;
    }

    const qint64 lastNumber = qint64(firstNumber) + filePaths.size() - 1;
    const int width = std::max(3, int(QString::number(lastNumber).size()));
    QVector<RenameItem> items;
    items.reserve(filePaths.size());
    QStringList sources;
    QStringList targets;
    for (qsizetype index = 0; index < filePaths.size(); ++index) {
        const QFileInfo info(filePaths.at(index));
        if (!info.exists() || !info.isFile()) {
            result.error = QStringLiteral("文件不存在：%1").arg(filePaths.at(index));
            return result;
        }
        const QString source = normalized(info.absoluteFilePath());
        for (const QString& existing : sources) {
            if (samePath(existing, source)) {
                result.error = QStringLiteral("重复选择了文件：%1").arg(source);
                return result;
            }
        }
        const QString number = QString::number(firstNumber + index).rightJustified(width, '0');
        const QString suffix = info.completeSuffix();
        const QString fileName = cleanPrefix + QStringLiteral("_") + number +
                                 (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix);
        const QString target = normalized(info.dir().filePath(fileName));
        for (const QString& existing : targets) {
            if (samePath(existing, target)) {
                result.error = QStringLiteral("生成了重复目标名：%1").arg(target);
                return result;
            }
        }
        sources.append(source);
        targets.append(target);
        items.append({source, {}, target});
    }

    for (const RenameItem& item : items) {
        if (!QFileInfo::exists(item.target) || samePath(item.source, item.target)) continue;
        bool targetIsSource = false;
        for (const QString& source : sources) {
            if (samePath(source, item.target)) {
                targetIsSource = true;
                break;
            }
        }
        if (!targetIsSource) {
            result.error = QStringLiteral("目标文件已存在：%1").arg(item.target);
            return result;
        }
    }

    QVector<int> staged;
    for (int index = 0; index < items.size(); ++index) {
        RenameItem& item = items[index];
        if (samePath(item.source, item.target)) continue;
        const QFileInfo sourceInfo(item.source);
        do {
            item.temporary = sourceInfo.dir().filePath(
                QStringLiteral(".folixa-rename-%1.tmp").arg(QUuid::createUuid().toString(QUuid::Id128)));
        } while (QFileInfo::exists(item.temporary));
        if (!QFile::rename(item.source, item.temporary)) {
            for (auto it = staged.crbegin(); it != staged.crend(); ++it) {
                QFile::rename(items[*it].temporary, items[*it].source);
            }
            result.error = QStringLiteral("无法重命名：%1").arg(item.source);
            return result;
        }
        staged.append(index);
    }

    QVector<int> completed;
    for (int index : staged) {
        RenameItem& item = items[index];
        if (!QFile::rename(item.temporary, item.target)) {
            for (auto it = completed.crbegin(); it != completed.crend(); ++it) {
                QFile::rename(items[*it].target, items[*it].source);
            }
            for (int pending : staged) {
                if (!completed.contains(pending)) {
                    QFile::rename(items[pending].temporary, items[pending].source);
                }
            }
            result.error = QStringLiteral("无法写入目标文件：%1").arg(item.target);
            return result;
        }
        completed.append(index);
    }

    result.ok = true;
    result.renamedPaths = targets;
    return result;
}

}  // namespace aipdf::app
