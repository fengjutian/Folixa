#include "doc/export/ZipWriter.h"

#include <QHash>

namespace aipdf::doc::zip {

namespace {

// ZIP 里所有整数都是小端。
void appendU16(QByteArray& out, quint16 value) {
    out.append(static_cast<char>(value & 0xFFU));
    out.append(static_cast<char>((value >> 8) & 0xFFU));
}

void appendU32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>(value & 0xFFU));
    out.append(static_cast<char>((value >> 8) & 0xFFU));
    out.append(static_cast<char>((value >> 16) & 0xFFU));
    out.append(static_cast<char>((value >> 24) & 0xFFU));
}

quint16 readU16(const QByteArray& data, int offset) {
    if (offset < 0 || offset + 1 >= data.size()) {
        return 0;
    }
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
    return static_cast<quint16>(bytes[0] | (bytes[1] << 8));
}

quint32 readU32(const QByteArray& data, int offset) {
    if (offset < 0 || offset + 3 >= data.size()) {
        return 0;
    }
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
    return static_cast<quint32>(bytes[0]) | (static_cast<quint32>(bytes[1]) << 8) |
           (static_cast<quint32>(bytes[2]) << 16) | (static_cast<quint32>(bytes[3]) << 24);
}

// DOS 时间/日期。
//
// 固定成 1980-01-01 00:00:00（ZIP 格式能表示的最早时刻）：
// 导出结果必须是**可复现**的——同一份 PDF 导两次，字节必须一样，
// 否则没法做"导出结果有没有变"的回归判断，也没法把导出物当缓存用。
constexpr quint16 kDosTime = 0;
constexpr quint16 kDosDate = 0x0021;  // 1980-01-01

// 一般标志位的第 11 位：文件名的编码是 UTF-8。
constexpr quint16 kFlagUtf8Names = 0x0800;

// 文件名是不是纯 ASCII。
//
// 不用 QString::isAscii()：它在 Qt 里不是公开接口（QChar::isAscii 才是），
// 自己数一遍更省事，名字也就两三个字符。
bool isAsciiName(const QString& name) {
    for (const QChar ch : name) {
        if (ch.unicode() >= 128) {
            return false;
        }
    }
    return true;
}

}  // namespace

quint32 crc32(const QByteArray& data) {
    // 查表法：表是纯函数算出来的，进程内缓存一份就行。
    static const quint32* table = [] {
        static quint32 values[256];
        for (quint32 i = 0; i < 256; ++i) {
            quint32 value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1U) ? (0xEDB88320U ^ (value >> 1)) : (value >> 1);
            }
            values[i] = value;
        }
        return values;
    }();

    quint32 crc = 0xFFFFFFFFU;
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData());
    for (int i = 0; i < data.size(); ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

void Writer::addFile(const QString& name, const QByteArray& data) {
    if (name.isEmpty()) {
        return;
    }
    Entry entry;
    entry.name = name;
    entry.data = data;
    entry.checksum = crc32(data);
    for (Entry& existing : entries_) {
        if (existing.name == name) {
            existing = entry;
            return;
        }
    }
    entries_.append(entry);
}

bool Writer::contains(const QString& name) const {
    for (const Entry& entry : entries_) {
        if (entry.name == name) {
            return true;
        }
    }
    return false;
}

QByteArray Writer::toByteArray() const {
    if (entries_.isEmpty()) {
        return QByteArray();
    }

    QByteArray out;
    QVector<quint32> localOffsets;
    localOffsets.reserve(entries_.size());

    // ---------------------------------------------------------- 文件数据 ----
    for (const Entry& entry : entries_) {
        localOffsets.append(static_cast<quint32>(out.size()));

        const QByteArray name = entry.name.toUtf8();
        // 文件名是纯 ASCII 时不用置 UTF-8 标志：置了也无害，但保持"该置才置"。
        const quint16 flags = isAsciiName(entry.name) ? 0 : kFlagUtf8Names;

        appendU32(out, 0x04034B50U);  // 本地文件头签名
        appendU16(out, 20);           // 解压所需版本：2.0（store 不需要更高）
        appendU16(out, flags);
        appendU16(out, 0);            // 压缩方法：0 = store
        appendU16(out, kDosTime);
        appendU16(out, kDosDate);
        appendU32(out, entry.checksum);
        appendU32(out, static_cast<quint32>(entry.data.size()));  // 压缩后大小
        appendU32(out, static_cast<quint32>(entry.data.size()));  // 原始大小
        appendU16(out, static_cast<quint16>(name.size()));
        appendU16(out, 0);            // 扩展字段长度
        out.append(name);
        out.append(entry.data);
    }

    // ------------------------------------------------------------ 中央目录 ----
    const quint32 centralDirectoryOffset = static_cast<quint32>(out.size());
    for (int i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_.at(i);
        const QByteArray name = entry.name.toUtf8();
        const quint16 flags = isAsciiName(entry.name) ? 0 : kFlagUtf8Names;

        appendU32(out, 0x02014B50U);  // 中央目录头签名
        appendU16(out, 20);           // 生成版本
        appendU16(out, 20);           // 解压所需版本
        appendU16(out, flags);
        appendU16(out, 0);            // 压缩方法
        appendU16(out, kDosTime);
        appendU16(out, kDosDate);
        appendU32(out, entry.checksum);
        appendU32(out, static_cast<quint32>(entry.data.size()));
        appendU32(out, static_cast<quint32>(entry.data.size()));
        appendU16(out, static_cast<quint16>(name.size()));
        appendU16(out, 0);            // 扩展字段长度
        appendU16(out, 0);            // 注释长度
        appendU16(out, 0);            // 起始磁盘号
        appendU16(out, 0);            // 内部属性
        appendU32(out, 0);            // 外部属性
        appendU32(out, localOffsets.at(i));
        out.append(name);
    }
    const quint32 centralDirectorySize =
        static_cast<quint32>(out.size()) - centralDirectoryOffset;

    // ---------------------------------------------------- 中央目录结束记录 ----
    appendU32(out, 0x06054B50U);
    appendU16(out, 0);  // 本磁盘号
    appendU16(out, 0);  // 中央目录所在磁盘号
    appendU16(out, static_cast<quint16>(entries_.size()));
    appendU16(out, static_cast<quint16>(entries_.size()));
    appendU32(out, centralDirectorySize);
    appendU32(out, centralDirectoryOffset);
    appendU16(out, 0);  // 注释长度

    return out;
}

QStringList listEntryNames(const QByteArray& zipBytes) {
    QStringList names;
    if (zipBytes.size() < 22) {
        return names;
    }

    // 从尾部往前找中央目录结束记录（EOCD）：它的签名可能带一个注释，
    // 所以不能直接按长度定位末尾。
    int eocd = -1;
    const int lowest = qMax(0, zipBytes.size() - 22 - 0xFFFF);
    for (int i = zipBytes.size() - 22; i >= lowest; --i) {
        if (readU32(zipBytes, i) == 0x06054B50U) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        return names;
    }

    const quint16 entryCount = readU16(zipBytes, eocd + 10);
    quint32 offset = readU32(zipBytes, eocd + 16);
    for (quint16 i = 0; i < entryCount; ++i) {
        if (readU32(zipBytes, static_cast<int>(offset)) != 0x02014B50U) {
            break;
        }
        const quint16 nameLength = readU16(zipBytes, static_cast<int>(offset) + 28);
        const quint16 extraLength = readU16(zipBytes, static_cast<int>(offset) + 30);
        const quint16 commentLength = readU16(zipBytes, static_cast<int>(offset) + 32);
        const int nameStart = static_cast<int>(offset) + 46;
        if (nameStart + nameLength > zipBytes.size()) {
            break;
        }
        names.append(QString::fromUtf8(zipBytes.constData() + nameStart, nameLength));
        offset = static_cast<quint32>(nameStart + nameLength + extraLength + commentLength);
    }
    return names;
}

QByteArray readEntry(const QByteArray& zipBytes, const QString& name) {
    if (zipBytes.size() < 22) {
        return QByteArray();
    }

    int eocd = -1;
    const int lowest = qMax(0, zipBytes.size() - 22 - 0xFFFF);
    for (int i = zipBytes.size() - 22; i >= lowest; --i) {
        if (readU32(zipBytes, i) == 0x06054B50U) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        return QByteArray();
    }

    const quint16 entryCount = readU16(zipBytes, eocd + 10);
    quint32 offset = readU32(zipBytes, eocd + 16);
    for (quint16 i = 0; i < entryCount; ++i) {
        if (readU32(zipBytes, static_cast<int>(offset)) != 0x02014B50U) {
            break;
        }
        const quint16 method = readU16(zipBytes, static_cast<int>(offset) + 10);
        const quint32 compressedSize = readU32(zipBytes, static_cast<int>(offset) + 20);
        const quint32 uncompressedSize = readU32(zipBytes, static_cast<int>(offset) + 24);
        const quint16 nameLength = readU16(zipBytes, static_cast<int>(offset) + 28);
        const quint16 extraLength = readU16(zipBytes, static_cast<int>(offset) + 30);
        const quint16 commentLength = readU16(zipBytes, static_cast<int>(offset) + 32);
        const quint32 localOffset = readU32(zipBytes, static_cast<int>(offset) + 42);
        const int nameStart = static_cast<int>(offset) + 46;
        if (nameStart + nameLength > zipBytes.size()) {
            break;
        }
        const QString entryName =
            QString::fromUtf8(zipBytes.constData() + nameStart, nameLength);

        offset = static_cast<quint32>(nameStart + nameLength + extraLength + commentLength);

        if (entryName != name) {
            continue;
        }
        if (method != 0) {
            // 只支持 store：压缩过的条目需要解压实现。
            return QByteArray();
        }
        if (compressedSize != uncompressedSize) {
            return QByteArray();
        }
        // 本地文件头里的文件名长度可能与中央目录不同（可以有扩展字段），
        // 所以数据起点要从本地头重新读。
        if (readU32(zipBytes, static_cast<int>(localOffset)) != 0x04034B50U) {
            return QByteArray();
        }
        const quint16 localNameLength = readU16(zipBytes, static_cast<int>(localOffset) + 26);
        const quint16 localExtraLength = readU16(zipBytes, static_cast<int>(localOffset) + 28);
        const int dataStart = static_cast<int>(localOffset) + 30 + localNameLength + localExtraLength;
        if (dataStart + static_cast<int>(compressedSize) > zipBytes.size()) {
            return QByteArray();
        }
        return zipBytes.mid(dataStart, static_cast<int>(compressedSize));
    }
    return QByteArray();
}

}  // namespace aipdf::doc::zip
