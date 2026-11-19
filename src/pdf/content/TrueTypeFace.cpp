#include "pdf/content/TrueTypeFace.h"

#include <QStringList>

#include <algorithm>

namespace aipdf::pdf {
namespace {

quint16 readU16(const QByteArray& bytes, int offset) {
    if (offset < 0 || offset + 2 > bytes.size()) return 0;
    return static_cast<quint16>((static_cast<quint8>(bytes.at(offset)) << 8) |
                                static_cast<quint8>(bytes.at(offset + 1)));
}

quint32 readU32(const QByteArray& bytes, int offset) {
    if (offset < 0 || offset + 4 > bytes.size()) return 0;
    return (static_cast<quint32>(readU16(bytes, offset)) << 16) |
           static_cast<quint32>(readU16(bytes, offset + 2));
}

QString readTag(const QByteArray& bytes, int offset) {
    if (offset < 0 || offset + 4 > bytes.size()) return QString();
    return QString::fromLatin1(bytes.mid(offset, 4));
}

// 表目录：tag → (offset, length)。
struct Table {
    int offset = 0;
    int length = 0;
};

QHash<QString, Table> readTableDirectory(const QByteArray& bytes, int base, QString* error) {
    QHash<QString, Table> tables;
    const int tableCount = readU16(bytes, base + 4);
    if (tableCount <= 0 || tableCount > 512) {
        *error = QStringLiteral("字体表目录损坏（表数量 %1）").arg(tableCount);
        return tables;
    }
    const int records = base + 12;
    if (records + tableCount * 16 > bytes.size()) {
        *error = QStringLiteral("字体表目录越界");
        return tables;
    }
    for (int i = 0; i < tableCount; ++i) {
        const int record = records + i * 16;
        const QString tag = readTag(bytes, record);
        Table table;
        table.offset = static_cast<int>(readU32(bytes, record + 8));
        table.length = static_cast<int>(readU32(bytes, record + 12));
        if (table.offset < 0 || table.length < 0 || table.offset + table.length > bytes.size()) {
            continue;
        }
        tables.insert(tag, table);
    }
    return tables;
}

void parseCmapFormat4(const QByteArray& bytes, int subtable, QHash<uint32_t, quint16>* out) {
    const int segCount = readU16(bytes, subtable + 6) / 2;
    if (segCount <= 0 || segCount > 4096) return;
    const int endCodes = subtable + 14;
    const int startCodes = endCodes + segCount * 2 + 2;
    const int idDeltas = startCodes + segCount * 2;
    const int idRangeOffsets = idDeltas + segCount * 2;
    if (idRangeOffsets + segCount * 2 > bytes.size()) return;

    for (int segment = 0; segment < segCount; ++segment) {
        const quint16 end = readU16(bytes, endCodes + segment * 2);
        const quint16 start = readU16(bytes, startCodes + segment * 2);
        const quint16 delta = readU16(bytes, idDeltas + segment * 2);
        const quint16 rangeOffset = readU16(bytes, idRangeOffsets + segment * 2);
        if (start > end) continue;
        for (quint32 code = start; code <= end && code - start < 65536; ++code) {
            quint16 gid = 0;
            if (rangeOffset == 0) {
                gid = static_cast<quint16>(code + delta);
            } else {
                const int glyphOffset = idRangeOffsets + segment * 2 + rangeOffset +
                                        static_cast<int>(code - start) * 2;
                gid = readU16(bytes, glyphOffset);
                if (gid != 0) gid = static_cast<quint16>(gid + delta);
            }
            if (gid != 0) out->insert(code, gid);
        }
    }
}

void parseCmapFormat12(const QByteArray& bytes, int subtable, QHash<uint32_t, quint16>* out) {
    const quint32 groupCount = readU32(bytes, subtable + 12);
    if (groupCount == 0 || groupCount > 200000) return;
    const int groups = subtable + 16;
    if (static_cast<qint64>(groups) + static_cast<qint64>(groupCount) * 12 > bytes.size()) return;
    for (quint32 i = 0; i < groupCount; ++i) {
        const int group = groups + static_cast<int>(i) * 12;
        const quint32 start = readU32(bytes, group);
        const quint32 end = readU32(bytes, group + 4);
        const quint32 startGid = readU32(bytes, group + 8);
        if (start > end || end - start > 0x10000) continue;
        for (quint32 code = start; code <= end; ++code) {
            const quint32 gid = startGid + (code - start);
            if (gid != 0 && gid < 65535) out->insert(code, static_cast<quint16>(gid));
        }
    }
}

}  // namespace

uint16_t TrueTypeFace::gidFor(char32_t code) const {
    return unicodeToGid.value(static_cast<uint32_t>(code), 0);
}

double TrueTypeFace::widthForGid(uint16_t gid) const {
    if (gid >= advanceWidths.size()) return 1000.0;
    return static_cast<double>(advanceWidths.at(gid)) * 1000.0 / qMax(1, unitsPerEm);
}

TrueTypeFace TrueTypeFace::load(const QByteArray& fontBytes) {
    TrueTypeFace face;
    if (fontBytes.size() < 12) {
        face.error = QStringLiteral("字体字节太短");
        return face;
    }

    int base = 0;
    if (readTag(fontBytes, 0) == QLatin1String("ttcf")) {
        // TTC：取第一份子字体。
        const quint32 first = readU32(fontBytes, 12);
        if (first == 0 || first >= static_cast<quint32>(fontBytes.size())) {
            face.error = QStringLiteral("TTC 里没有可用的子字体");
            return face;
        }
        base = static_cast<int>(first);
    }

    QString tableError;
    const QHash<QString, Table> tables = readTableDirectory(fontBytes, base, &tableError);
    if (!tableError.isEmpty()) {
        face.error = tableError;
        return face;
    }
    if (!tables.contains(QStringLiteral("head")) || !tables.contains(QStringLiteral("maxp")) ||
        !tables.contains(QStringLiteral("cmap")) || !tables.contains(QStringLiteral("hmtx"))) {
        face.error = QStringLiteral("字体缺少 head / maxp / cmap / hmtx");
        return face;
    }

    const Table head = tables.value(QStringLiteral("head"));
    face.unitsPerEm = readU16(fontBytes, head.offset + 18);
    if (face.unitsPerEm <= 0) face.unitsPerEm = 1000;

    const Table maxp = tables.value(QStringLiteral("maxp"));
    face.numGlyphs = readU16(fontBytes, maxp.offset + 4);

    int numberOfHMetrics = 0;
    if (tables.contains(QStringLiteral("hhea"))) {
        const Table hhea = tables.value(QStringLiteral("hhea"));
        numberOfHMetrics = readU16(fontBytes, hhea.offset + 34);
        face.ascent = readU16(fontBytes, hhea.offset + 4);
        face.descent = -static_cast<int>(readU16(fontBytes, hhea.offset + 6));
    }
    if (numberOfHMetrics <= 0) numberOfHMetrics = face.numGlyphs;

    const Table hmtx = tables.value(QStringLiteral("hmtx"));
    face.advanceWidths.resize(qMax(0, face.numGlyphs));
    quint16 lastAdvance = 0;
    for (int gid = 0; gid < face.numGlyphs; ++gid) {
        const int record = hmtx.offset + gid * 4;
        if (gid < numberOfHMetrics && record + 2 <= fontBytes.size()) {
            lastAdvance = readU16(fontBytes, record);
        }
        face.advanceWidths[gid] = lastAdvance;
    }

    // cmap：优先 (3,10) 格式 12，其次 (3,1) 格式 4，再退到任意 Unicode 子表。
    const Table cmap = tables.value(QStringLiteral("cmap"));
    const int subtableCount = readU16(fontBytes, cmap.offset + 2);
    int best = -1;
    int bestScore = -1;
    for (int i = 0; i < subtableCount && i < 64; ++i) {
        const int record = cmap.offset + 4 + i * 8;
        const quint16 platform = readU16(fontBytes, record);
        const quint16 encoding = readU16(fontBytes, record + 2);
        const quint32 offset = readU32(fontBytes, record + 4);
        const int subtable = cmap.offset + static_cast<int>(offset);
        if (subtable + 2 > fontBytes.size()) continue;
        const quint16 format = readU16(fontBytes, subtable);
        int score = -1;
        if (format == 12 && (platform == 3 || platform == 0)) score = 100 + encoding;
        else if (format == 4 && platform == 3 && encoding == 1) score = 90;
        else if (format == 4 && platform == 0) score = 80;
        else if (format == 12) score = 70;
        if (score > bestScore) {
            bestScore = score;
            best = subtable;
        }
    }
    if (best < 0) {
        face.error = QStringLiteral("cmap 里没有可用的 Unicode 子表");
        return face;
    }
    const quint16 format = readU16(fontBytes, best);
    if (format == 12) parseCmapFormat12(fontBytes, best, &face.unicodeToGid);
    else if (format == 4) parseCmapFormat4(fontBytes, best, &face.unicodeToGid);
    if (face.unicodeToGid.isEmpty()) {
        face.error = QStringLiteral("cmap 里没有可映射的字符");
        return face;
    }
    face.ok = true;
    return face;
}

}  // namespace aipdf::pdf
