#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "doc/BlankDocument.h"

namespace aipdf::doc {

// ============================================================ 任意文件 → PDF ==
//
// 领域层模块：把常见的**文本类**文件（.txt / .md / .csv / .json / .html）
// 直接变成一份可分发的 PDF。
//
// 为什么不调 LibreOffice：那条路依赖本机装了没有转换引擎，
// 「缺引擎」本身就是一个需要弹窗提示的失败态；本模块不依赖任何外部程序，
// 输入字节进去、PDF 字节出来，行为完全确定（同样输入永远同样输出）。
// 代价是只覆盖文本类文件，且排版是「能读」而不是「像原版」——
// 见下面「已知边界」。
//
// ---------------------------------------------------------------------------
// 中文怎么显示（这一段是本模块最重要的约定，别改坏了）：
//
//   * **默认路径用标准 14 字体**（Helvetica / Helvetica-Bold / Courier）。
//     标准 14 字体没有中文字形，所以走这条路时，中文等非 WinAnsi 字符
//     **会被替换成 '?'**，并在结果里给出替换字符数（replacedCharacterCount）
//     与一条中文警告（warning）。不静默丢字：调用方能一眼看出这份 PDF
//     的中文已经掉了。
//   * **要正确显示中文，调用方必须自备字体**：把一份 TrueType（.ttf）
//     字体的字节放进 options.regularFont / options.boldFont，模块会把它
//     按 Type0 / Identity-H / CIDFontType2 嵌进 PDF（带 ToUnicode），
//     此时中文既**显示得出来**，也**能被抽取回原文**。
//   * 仓库里**没有**随附的中文字体（tools/ 与 assets/ 下都没有），
//     所以默认路径做不到「中文直接可读」——这是资源缺失，不是实现偷懒。
//     调用方拿到用户机器上的字体路径后再传进来即可。
//   * 嵌入的是**整份字体文件**，不做子集化：一份中文 TTF 动辄 10 MB，
//     生成的 PDF 也会跟着大。子集化需要完整的字形重写，不在本模块范围内。
//
// ---------------------------------------------------------------------------
// 复用了仓库里的哪些东西（别再另写一套）：
//
//   * **页面几何**用 doc/BlankDocument 的 pageSizeInPoints() 与那套
//     PageSize / PageOrientation 枚举：纸张尺寸是几何事实，只该有一张表。
//   * **内容流自检**用 pdf/ContentStream::tokenize()：BT/ET 配对坏掉的话
//     PDFium 可能照收不误，别的阅读器却打不开；写之前先过一遍它。
//   * **pdf/AppearanceStream 用不上**：它画的是**批注外观流**（Form XObject），
//     页面正文的内容流不是一回事，套不上。它那套 WinAnsi 转义的做法本模块
//     借了思路（转义括号与反斜杠），代码没法直接复用。
//
// ---------------------------------------------------------------------------
// 已知边界（调用方需要知道的部分）：
//
//   * 只做**文本**。图片、表格线、样式都不保留。
//   * .html 只做「去标签留文字」：不还原布局，也不执行脚本；
//     <script> / <style> 里的内容会被整段丢掉。
//   * .json 会先按 JSON 解析再缩进输出；解析不了就按纯文本处理。
//   * .csv 只按逗号分列（支持双引号包裹与 "" 转义），不做 CSV 方言的全套。
//   * 折行按**估算字宽**进行（标准 14 用内置字宽表，嵌入字体用字体自己的
//     hmtx），不是排版引擎级的精确断行。

// 支持转换的源文件类型。
enum class UniversalFileKind {
    Unknown,
    PlainText,
    Markdown,
    Csv,
    Json,
    Html,
};

// 源字节不是合法 UTF-8 时怎么办。
enum class InvalidUtf8Policy {
    // 非法字节序列替换成 U+FFFD，转换继续。
    Replace,
    // 直接失败，并把原因写进结果的 error。
    Fail,
};

// 调用方自备的字体资源。
//
// 为什么留成「字节 + 名字」而不是一个路径：领域层不碰文件系统，
// 读文件是调用方的事；顺带也让测试可以用内存里的字体字节。
struct FontResource {
    // TrueType（.ttf）字体的字节。空表示「没有自备字体」。
    QByteArray bytes;
    // PDF 里用的 PostScript 名。空则由模块按用途生成（AipdfEmbedded-…）。
    QString postScriptName;
    bool isEmpty() const { return bytes.isEmpty(); }
};

// 转换参数。
struct UniversalConverterOptions {
    // 纸张与方向（复用 BlankDocument 的那套枚举，别再开一张新表）。
    PageSize pageSize = PageSize::A4;
    PageOrientation orientation = PageOrientation::Portrait;
    // 页边距（点）。默认 56 ≈ 2 cm。
    double marginPt = 56.0;
    // 正文字号（点）。
    double fontSizePt = 11.0;
    // 行距倍数（乘字号得行高）。
    double lineSpacing = 1.35;
    // 列表项每级的缩进（点）。
    double listIndentPt = 18.0;
    // 非法 UTF-8 的处理方式。
    InvalidUtf8Policy invalidUtf8 = InvalidUtf8Policy::Replace;
    // 正文字体。空 = 标准 14 字体（中文会变成替代字形）。
    FontResource regularFont;
    // 标题字体。空 = 标准 14 粗体；给了正文嵌入字体但没给粗体时，
    // 标题用正文那份嵌入字体（字号更大）。
    FontResource boldFont;
};

// 解析出来的一行（**折行前**）。
struct UniversalTextLine {
    // 行文字（列表项已去掉项目符号，缩进记在 indentLevel 里）。
    QString text;
    // 标题层级：0 是正文，1 起是标题（1 最大）。
    int headingLevel = 0;
    // 是不是列表项。
    bool listItem = false;
    // 缩进层级（列表嵌套、引用块都算）。
    int indentLevel = 0;
    // 是不是等宽内容（CSV 表格、代码块）。
    bool monospace = false;
    // 要不要加粗（Markdown 标题、CSV 表头）。
    bool bold = false;
};

// 转换结果。
struct UniversalConversionResult {
    // 成功为真；失败时 bytes 为空、error 写明原因（中文）。
    bool ok = false;
    // 生成的 PDF 字节。
    QByteArray bytes;
    // 建议的文件名（源名 + ".pdf"，例如 "报告.md.pdf"）。
    QString suggestedFileName;
    // 失败原因（中文）。成功时为空。
    QString error;
    // 警告（中文）。成功但有损时非空，例如中文被替换成替代字形。
    QString warning;
    // 生成的页数。
    int pageCount = 0;
    // 被替换成替代字形的字符数（中文等标准 14 字体显示不了的字符）。
    int replacedCharacterCount = 0;
    // 有没有真的用上嵌入字体。
    bool usedEmbeddedFont = false;
};

// 把一个文本类文件转成 PDF。
//
// fileName 决定文件类型（按扩展名判断）；调用方已经知道类型时用下面那个重载。
// 失败返回 ok=false，并把原因（中文）写进 result.error。
UniversalConversionResult convertToPdf(const QByteArray& content,
                                        const QString& fileName,
                                        const UniversalConverterOptions& options = UniversalConverterOptions());

// 已知文件类型时的重载。suggestedFileName 为空时按 "文档.pdf" 给建议名。
UniversalConversionResult convertToPdf(const QByteArray& content,
                                        UniversalFileKind kind,
                                        const QString& fileName = QString(),
                                        const UniversalConverterOptions& options = UniversalConverterOptions());

// 把源字节解析成排版行（**不折行、不分页**）。
//
// 单独暴露出来是为了能直接测「Markdown 标题识别」「HTML 去标签」
// 「CSV 列对齐」这些规则，不必先绕一圈 PDF。
//
// 非法 UTF-8 在这里一律替换成 U+FFFD（这个函数没有策略参数；
// 要「遇到非法字节就失败」请走 convertToPdf 的 InvalidUtf8Policy::Fail）。
// kind 是 Unknown 时返回空列表并写明原因。
QVector<UniversalTextLine> parseUniversalText(const QByteArray& content,
                                               UniversalFileKind kind,
                                               QString* error = nullptr);

// 文件名 → 文件类型。扩展名不认识时是 Unknown。
UniversalFileKind universalFileKindFor(const QString& fileName);

// 这个文件名是不是本模块能转的类型。
bool isSupportedUniversalFile(const QString& fileName);

// 源文件名 → 建议的 PDF 文件名（"报告.md" → "报告.md.pdf"）。
//
// 为什么不是"报告.pdf"：同一次批量转换里 "报告.md" 与 "报告.txt" 都在，
// 去掉扩展名会让两份结果互相覆盖。
QString suggestedPdfFileName(const QString& sourceFileName);

// 文件类型的中文名（"Markdown" / "CSV" / …），用于报错与提示。
QString universalFileKindLabel(UniversalFileKind kind);

// 当前参数下，一页能放多少行正文。
//
// 暴露出来是因为调用方常要「先估页数再决定要不要转换」，
// 而页数是排版规则算出来的，不该让上层再猜一遍。
int linesPerPageFor(const UniversalConverterOptions& options);

}  // namespace aipdf::doc
