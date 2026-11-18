#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>

namespace aipdf::pdf {

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
