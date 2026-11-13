#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::doc {

// 安全保存的结果。
struct SaveResult {
    bool ok = false;
    // 失败原因（成功时为空）。
    QString errorMessage;
    // 原文件备份的路径（没有备份时为空）。
    QString backupPath;
};

// 安全保存：把内容原子地写到目标路径。
//
// 顺序是「写临时文件 → 重新打开校验 → 备份原文件 → 原子替换」：
//   - 临时文件写在**同一个目录**：跨卷的移动不是原子操作，
//     写到系统临时目录再挪过来，中途断电会留下半个文件；
//   - 校验的是**重新打开的临时文件**：写坏了的文件不会顶掉原文件；
//   - 替换用系统的原子替换：失败时原文件原封不动，
//     不会出现「保存到一半，原文件也坏了」。
//
// 覆盖不了的几种情况（只读文件、被别的程序独占、磁盘满），
// 都会在这里变成一句能看懂的中文提示，而不是半个坏文件。
class SafeSave {
public:
    // 把 bytes 原子地写到 targetPath。
    //
    // expectedPageCount 大于 0 时会校验页数：写出来的文件必须能打开，
    // 而且页数要对得上。createBackup 为真且原文件存在时，
    // 先把原文件备份成「<原名>.folixa.bak」。
    static SaveResult writeAtomically(const QString& targetPath,
                                      const QByteArray& bytes,
                                      int expectedPageCount,
                                      bool createBackup);

    // 校验字节：能不能当 PDF 打开、页数对不对。
    static bool verify(const QByteArray& bytes, int expectedPageCount, QString* error);

    // 备份文件路径：<原名>.folixa.bak。
    static QString backupPathFor(const QString& targetPath);

    // 临时文件路径：<原名>.folixa.tmp。
    static QString temporaryPathFor(const QString& targetPath);
};

}  // namespace aipdf::doc
