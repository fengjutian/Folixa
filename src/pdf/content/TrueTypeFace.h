#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

#include <cstdint>

namespace aipdf::pdf {

// 一份 TrueType 字体里表单嵌入需要的那几样东西：
// 码位 → 字形号、每个字形的横向 advance、以及 head/hhea 的度量。
//
// 为什么单独一层：表单外观流要写中文，必须把系统字体子集化后嵌进 PDF。
// 子集化要「用到哪些字形号」，写 /W 要「每个字形的宽度」，
// 这两样都得先把 cmap 与 hmtx 读出来。
//
// 支持：单字体 TTF、字体集合 TTC（取第一份）、cmap 格式 4 与 12。
// 不支持：CFF/OpenType 轮廓、位图字体、WOFF。
struct TrueTypeFace {
    bool ok = false;
    QString error;

    int unitsPerEm = 1000;
    int ascent = 800;
    int descent = -200;
    int numGlyphs = 0;

    // 码位 → 字形号。只收 cmap 里真正有映射的码位。
    QHash<uint32_t, uint16_t> unicodeToGid;
    // 字形号 → 横向 advance（字体单位）。
    QVector<uint16_t> advanceWidths;

    uint16_t gidFor(char32_t code) const;
    // advance，已换算成 1/1000 em（PDF 的字宽口径）。
    double widthForGid(uint16_t gid) const;

    static TrueTypeFace load(const QByteArray& fontBytes);
};

}  // namespace aipdf::pdf
