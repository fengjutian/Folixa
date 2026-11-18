#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <QtGlobal>

namespace aipdf::doc::zip {

// CRC-32（IEEE 802.3，与 zlib / PKZIP 用的是同一套多项式）。
//
// 为什么自己算：ZIP 的每个文件头都要写 CRC-32，Qt 没有公开的 CRC-32 接口
//（QCryptographicHash 只有 MD5/SHA 系列）。自己实现这 20 行，比为了一个
//校验和引入 zlib 或 Qt 私有 API 都划算。
quint32 crc32(const QByteArray& data);

// 最小 ZIP 写入器（store 模式，不压缩）。
//
// 为什么自己写而不是用现成库：
//   1. Qt 6 的公开 API 里**没有 ZIP 写**。QZipWriter 在 Qt Gui 的 private
//      头里（qzipwriter_p.h），不是公开接口，依赖它等于依赖 Qt 私有实现，
//      升级 Qt 就会断。
//   2. 调外部命令（PowerShell 的 Compress-Archive、7z、Python 的 zipfile）
//      会把「纯字节生成」这件事变成「依赖本机装了什么」：CI 机器、
//      用户的精简版 Windows 都会不一样，而且导出结果的字节不可复现。
//   3. 导出的产物是 .docx / .ofd 这类**必须能被别的软件打开**的包，
//      所以格式必须严格对，不能"差不多像 ZIP"。
//
// 只做 store（不压缩）：ZIP 允许每个条目单独选择压缩方式，store 是其中一种，
// 完全合法。代价是包比压缩版大，收益是实现只有 100 行、没有压缩算法，
// 也没有"压缩后反而更大"的边界情况。docx / ofd 的内容以 XML 为主，
// 大头是页面位图，本来也压不动。
class Writer {
public:
    // 加一个文件。同名条目会被覆盖（而不是产生两份）。
    void addFile(const QString& name, const QByteArray& data);

    bool contains(const QString& name) const;
    bool isEmpty() const { return entries_.isEmpty(); }
    int count() const { return entries_.size(); }

    // 生成完整的 ZIP 字节。没有条目时返回空数组。
    QByteArray toByteArray() const;

private:
    struct Entry {
        QString name;
        QByteArray data;
        quint32 checksum = 0;
    };

    QVector<Entry> entries_;
};

// 读回一个 ZIP 包的条目名（按中央目录里的顺序）。
//
// 只支持 store 模式的包（也就是本文件写出来的那种）：解 store 只需要按长度
// 原样取字节，不需要任何解压实现。
QStringList listEntryNames(const QByteArray& zipBytes);

// 从 ZIP 包里取出一个条目的内容。条目不存在或包损坏时返回空数组。
QByteArray readEntry(const QByteArray& zipBytes, const QString& name);

}  // namespace aipdf::doc::zip
