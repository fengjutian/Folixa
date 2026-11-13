#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::doc {

// 一条恢复记录：某个文档的未保存工作。
struct RecoveryEntry {
    // 文档的绝对路径。
    QString documentPath;
    // 记录的最后更新时间（ISO 字符串）。
    QString updatedAt;
    // 编辑命令（序列化后的），按执行顺序。
    QVector<QByteArray> commands;

    bool isValid() const { return !documentPath.isEmpty() && !commands.isEmpty(); }
};

// 恢复日志：把「还没保存的编辑」记在磁盘上。
//
// 程序崩了、机器断电了，下次打开还能看到「上次在改这个文档，改了什么」，
// 重放一遍命令就能把未保存的工作恢复出来。
//
// 记的是**命令序列**而不是文件副本：
// 文档可能几百兆，命令只有几十字节；
// 命令本身可序列化，重放就是再执行一遍。
//
// 记录放在应用数据目录下的 recovery 子目录里，
// 可以用环境变量 AIPDF_RECOVERY_DIR 改位置（测试用）。
class RecoveryLog {
public:
    // 恢复目录。
    static QString directoryPath();

    // 记一次：把某个文档的当前命令序列写进恢复目录。
    static bool write(const QString& documentPath, const QVector<QByteArray>& commands);

    // 读回某个文档的记录。没有记录时返回 false。
    static bool read(const QString& documentPath, RecoveryEntry* entry);

    // 列出所有记录。
    static QVector<RecoveryEntry> list();

    // 删掉某个文档的记录（保存成功之后）。
    static bool remove(const QString& documentPath);

    // 记录文件名用的键：文档绝对路径的哈希。
    static QString keyFor(const QString& documentPath);
};

}  // namespace aipdf::doc
