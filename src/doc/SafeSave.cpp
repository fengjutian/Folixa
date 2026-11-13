#include "doc/SafeSave.h"

#include "pdf/PdfiumAdapter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace aipdf::doc {

namespace {

// 系统的错误码 → 一句能看懂的中文。
QString systemErrorMessage(unsigned long code) {
#ifdef Q_OS_WIN
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);
    QString text;
    if (length > 0 && buffer != nullptr) {
        text = QString::fromWCharArray(buffer, int(length)).trimmed();
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    if (text.isEmpty()) {
        text = QStringLiteral("系统错误 %1").arg(code);
    }
    return text;
#else
    return QStringLiteral("系统错误 %1").arg(code);
#endif
}

// 原子替换：把临时文件换成目标文件。
//
// Windows 上用 MoveFileEx 的 REPLACE_EXISTING：
// 这是同卷内的原子替换，目标被占用（别的程序开着）或只读时会失败，
// 失败时目标文件原封不动——这正是「保存失败不破坏原文件」要的。
//
// MOVEFILE_WRITE_THROUGH 让替换在落盘后才返回：
// 断电也不会出现「替换做了，内容还在缓存里」。
bool replaceFileAtomically(const QString& temporaryPath, const QString& targetPath, QString* error) {
#ifdef Q_OS_WIN
    const std::wstring source = temporaryPath.toStdWString();
    const std::wstring target = targetPath.toStdWString();
    if (MoveFileExW(source.c_str(),
                    target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("无法替换原文件：%1").arg(systemErrorMessage(GetLastError()));
    }
    return false;
#else
    QFile::remove(targetPath);
    if (QFile::rename(temporaryPath, targetPath)) {
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("无法替换原文件");
    }
    return false;
#endif
}

}  // namespace

QString SafeSave::backupPathFor(const QString& targetPath) {
    return targetPath + QStringLiteral(".folixa.bak");
}

QString SafeSave::temporaryPathFor(const QString& targetPath) {
    return targetPath + QStringLiteral(".folixa.tmp");
}

bool SafeSave::verify(const QByteArray& bytes, int expectedPageCount, QString* error) {
    if (bytes.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("内容为空");
        }
        return false;
    }
    const pdf::PdfOpenResult result = pdf::openDocumentFromBytes(bytes);
    if (!result.document.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("写出的文件打不开：%1").arg(result.errorMessage);
        }
        return false;
    }
    const int pages = result.document.pageCount();
    if (expectedPageCount > 0 && pages != expectedPageCount) {
        if (error != nullptr) {
            *error = QStringLiteral("写出的文件页数不对：期望 %1 页，实际 %2 页")
                         .arg(expectedPageCount)
                         .arg(pages);
        }
        return false;
    }
    return true;
}

SaveResult SafeSave::writeAtomically(const QString& targetPath,
                                     const QByteArray& bytes,
                                     int expectedPageCount,
                                     bool createBackup) {
    SaveResult result;

    if (targetPath.isEmpty()) {
        result.errorMessage = QStringLiteral("没有目标路径");
        return result;
    }

    const QFileInfo targetInfo(targetPath);
    const QDir directory = targetInfo.absoluteDir();
    if (!directory.exists()) {
        result.errorMessage = QStringLiteral("目标目录不存在：%1").arg(directory.path());
        return result;
    }

    // 1. 写临时文件（与目标同一个目录）。
    const QString temporaryPath = temporaryPathFor(targetPath);
    {
        QFile temporary(temporaryPath);
        if (!temporary.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.errorMessage =
                QStringLiteral("无法写入临时文件：%1").arg(temporary.errorString());
            return result;
        }
        const qint64 written = temporary.write(bytes);
        if (written != bytes.size()) {
            temporary.close();
            QFile::remove(temporaryPath);
            result.errorMessage = QStringLiteral("写入临时文件失败：磁盘可能已满");
            return result;
        }
        if (!temporary.flush()) {
            temporary.close();
            QFile::remove(temporaryPath);
            result.errorMessage = QStringLiteral("写入临时文件失败：%1").arg(temporary.errorString());
            return result;
        }
        temporary.close();
    }

    // 2. 重新打开临时文件校验：坏文件不许顶掉原文件。
    QString verifyError;
    {
        QFile temporary(temporaryPath);
        if (!temporary.open(QIODevice::ReadOnly)) {
            QFile::remove(temporaryPath);
            result.errorMessage =
                QStringLiteral("无法读取临时文件：%1").arg(temporary.errorString());
            return result;
        }
        const QByteArray written = temporary.readAll();
        temporary.close();
        if (!verify(written, expectedPageCount, &verifyError)) {
            QFile::remove(temporaryPath);
            result.errorMessage = verifyError;
            return result;
        }
    }

    // 3. 备份原文件（可选）。备份写不成不算失败：备份是保险，不是主流程。
    if (createBackup && targetInfo.exists() && targetInfo.isFile()) {
        const QString backupPath = backupPathFor(targetPath);
        QFile backup(backupPath);
        if (backup.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QFile original(targetPath);
            if (original.open(QIODevice::ReadOnly)) {
                backup.write(original.readAll());
                original.close();
            }
            backup.close();
            result.backupPath = backupPath;
        }
    }

    // 4. 原子替换。
    QString replaceError;
    if (!replaceFileAtomically(temporaryPath, targetPath, &replaceError)) {
        QFile::remove(temporaryPath);
        result.errorMessage = replaceError;
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace aipdf::doc
