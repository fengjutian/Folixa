#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一份字体的竖排度量（vhea / vmtx）。
//
// 为什么要有它：竖排不是「把横排的字转 90 度」，而是每个字在自己的字身框里
// 站好。字身框的高度（竖排 advance）来自字体的 vmtx 表；没有这张表，
// 排版就只能拿横排宽度凑合，标点与全角字的框宽就会错。
//
// 单位一律是**字体单位**（font units）；要换算成 PDF 的 1/1000 em，
// 用 1000 / unitsPerEm。
struct TrueTypeVerticalMetrics {
    // 字体里到底有没有竖排度量：vhea 与 vmtx 都在、且能读出竖排 advance 才为真。
    //
    // **没有就是没有**：这个字段为假时，调用方应当退回「竖排默认宽度」，
    // 不许拿横排宽度冒充，也不许编一个数出来。
    bool hasVerticalMetrics = false;
    // head 表里的 unitsPerEm。
    int unitsPerEm = 0;
    // vhea 的 ascender / descender（descender 是负数）。
    int ascent = 0;
    int descent = 0;
    // vhea 的 lineGap。
    int lineGap = 0;
    // vhea 的 numberOfVMetrics：vmtx 里单独记录了多少个字形的竖排度量。
    int numberOfVMetrics = 0;
    // 字体里的字形总数（与 maxp 一致）。
    int glyphCount = 0;
    // 每个字形的竖排 advance（字体单位）。
    //
    // vmtx 里没有单独记录的字形沿用最后一条记录的 advance（TrueType 规范如此），
    // 所以这里给的是**展开后**的完整表，调用方按字形号直接查。
    QVector<int> advanceHeights;
    // 失败原因（中文）。成功时为空。
    QString error;

    // 某个字形的竖排 advance（字体单位）。字形号越界时返回 0。
    int advanceHeightForGlyph(int gid) const {
        if (gid < 0 || gid >= advanceHeights.size()) {
            return 0;
        }
        return advanceHeights.at(gid);
    }
};

// 读一份 TrueType 字体的竖排度量（vhea / vmtx）。
//
// 字体里没有 vhea / vmtx 时返回的 hasVerticalMetrics 为假，error 里写明原因；
// **不编数**：调用方据此退回默认宽度。
TrueTypeVerticalMetrics readTrueTypeVerticalMetrics(const QByteArray& fontBytes);

// 一份 TrueType 字体子集化之后的结果。
struct TrueTypeSubsetResult {
    // 成功为真；失败时 bytes 为空，error 写明原因（中文）。
    bool ok = false;
    // 子集字体的字节。
    QByteArray bytes;
    // 字体里的字形总数（**编号没有变**，与原字体一致）。
    int glyphCount = 0;
    // 实际保留的字形数（含 .notdef 与复合字形的组件）。
    int keptGlyphCount = 0;
    // 原始字体的字节数（用来对比体积）。
    int originalBytes = 0;
    // 失败原因（中文）。成功时为空。
    QString error;
};

// 把一份 TrueType 字体子集化成「只含用到的那些字形」的字体。
//
// 为什么需要它：嵌入字体时如果把整份 .ttf 塞进 PDF，一份中文 TTF 动辄 10 MB，
// 生成一页中文的 PDF 也有 10 MB——邮件发不出去、打印排队、存盘占地方。
// 子集化只保留真正用到的字形，体积能降两个数量级。
//
// 三条约定（改之前先想清楚）：
//
//  1. **字形号保持不变**。PDF 里用的是 Type0 / Identity-H + /CIDToGIDMap /Identity，
//     也就是「CID 就是字形号」；字形号一变，内容流里的十六进制串与 ToUnicode
//     全得跟着改。所以这里保留原编号，没用到的字形在 loca 里变成空字形。
//  2. **复合字形的组件要一并带上**。一个汉字常常由几个部件组合而成，
//     组件字形不在子集里，那个字就画不出来（PDFium 会画出空白）。
//  3. **子集化失败不算错误**。调用方拿到 ok=false 时应当退回「整份嵌入」：
//     文件大一点，但中文照样显示得出来。不静默丢字。
//
// 支持：单字体 TrueType（.ttf）与字体集合（.ttc，取第一份）。
// 不支持：OpenType/CFF（.otf）、位图字体、压缩过的 WOFF——它们没有 glyf/loca。
TrueTypeSubsetResult subsetTrueTypeFont(const QByteArray& fontBytes,
                                         const QSet<int>& usedGids);

}  // namespace aipdf::pdf
