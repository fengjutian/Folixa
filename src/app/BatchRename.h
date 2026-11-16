#pragma once

#include <QString>
#include <QStringList>

namespace aipdf::app {

struct BatchRenameResult {
    bool ok = false;
    QString error;
    QStringList renamedPaths;
};

// 按输入顺序把文件改成「前缀_001.ext」。操作先统一移到临时名，再落到目标名，
// 因而源文件名与目标文件名互相占用时也不会覆盖文件。
BatchRenameResult batchRenameFiles(const QStringList& filePaths,
                                   const QString& prefix,
                                   int firstNumber = 1);

}  // namespace aipdf::app
