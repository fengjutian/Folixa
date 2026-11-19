#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

#include <cstdint>

namespace aipdf::pdf {

// 页面上的一个字体资源：把内容流里的字节还原成文字、把文字编码回字节。
//
// 为什么单独立这一层：
//   * 内容编辑要回答「这个字在原字体里有没有」——没有就得换字体，
//     有就必须沿用原来的字符码与字宽，否则改一个字整行都会重新排；
//   * 语义标签、无障碍、OCR 回写都要把字形码还原成文字，以前各自抄了一份；
//   * 表单外观流要按 /DA 里的字体算字宽，也是同一件事。
// 一份实现，三个用途；口径写在这里，不在调用方各写各的。
//
// 已知边界：
//   * 只解析 /ToUnicode 与 /Encoding /Differences；没有 ToUnicode 的复合字体
//     （Type0）还原不出文字，decode 会给出 U+FFFD，covers() 一律为假；
//   * 不解析 Type3 的 /CharProcs、不解析 CFF 字形表；
//   * 宽度取自 /Widths、/W、/DW、/MissingWidth；都缺时按字体类型的默认值，
//     并用 `widthsApproximate` 标出来，不假装是精确值。
enum class FontKind { Unknown, Type1, TrueType, Type0, Type3 };

// 一个字符在字体里的编码结果。
struct FontGlyph {
    char32_t unicode = 0;
    // 字体编码里的字符码（简单字体 1 字节，复合字体 2 字节）。
    uint32_t code = 0;
    int codeBytes = 1;
    // 字宽，1/1000 em。
    double width = 0.0;
    // 这个字在这个字体里有没有可用的字符码。
    bool covered = false;
};

struct FontResource {
    // 页面 /Resources /Font 里的资源名，例如 "F1"。
    QString resourceName;
    // /BaseFont，去掉子集前缀（"ABCDEF+"）。
    QString baseFont;
    FontKind kind = FontKind::Unknown;
    // 是不是复合字体（Type0）。
    bool composite = false;
    // 一个字符码占几个字节：简单字体 1，复合字体 2。
    int codeBytes = 1;
    // 字体文件是不是嵌在 PDF 里（/FontFile、/FontFile2、/FontFile3）。
    bool embedded = false;
    // 竖排（/Encoding /Identity-V 或其它 -V 的 CMap）。
    bool vertical = false;
    // 有没有 /ToUnicode：没有时 decode 只能按 WinAnsi 猜，coverage 不可信。
    bool toUnicodePresent = false;
    // 字宽是不是靠默认值估的（/Widths、/W 都没有）。
    bool widthsApproximate = false;
    // 缺省字宽（1/1000 em）：复合字体取 /DW，简单字体取 500。
    double defaultWidth = 500.0;
    // /FontDescriptor /MissingWidth（1/1000 em）。
    double missingWidth = 0.0;

    // 字符码 → 文字（/ToUnicode 的 bfchar / bfrange）。
    QHash<uint32_t, QString> codeToUnicode;
    // 文字（单个码点）→ 字符码。只收单码点映射，连字不收。
    QHash<char32_t, uint32_t> unicodeToCode;
    // 字符码 → 字宽（1/1000 em）。
    QHash<uint32_t, double> widths;

    // 内容流里的字节 → 文字。无映射的码按 WinAnsi 兜底（单字节），
    // 多字节又无 ToUnicode 时给 U+FFFD。
    QString decode(const QByteArray& bytes) const;

    // 一个码点的编码结果。covered=false 表示这个字体写不出这个字。
    FontGlyph glyphFor(char32_t unicode) const;

    // 整段文字的编码结果，逐码点。
    QVector<FontGlyph> encode(const QString& text) const;

    // 这段文字在这个字体下占多宽（点）。
    double measure(const QString& text, double fontSize) const;

    // 这段文字是不是每个字都有字符码。
    bool covers(const QString& text) const;
};

struct PageFontList {
    QVector<FontResource> fonts;
    bool ok = false;
    // 失败原因（中文）。成功时为空。
    QString error;
};

// 读一页的字体资源。页 /Resources 沿 /Parent 继承链查找。
//
// 失败（ok=false）：PDF 解析不了、页码越界。
// 「这一页没有字体」不是失败：ok=true 且 fonts 为空。
PageFontList readPageFonts(const QByteArray& pdfBytes, int pageIndex);

// 按资源名找字体（"F1"）。找不到返回 nullptr。
const FontResource* findFontResource(const PageFontList& list, const QString& resourceName);

}  // namespace aipdf::pdf
