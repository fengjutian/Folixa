#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::pdf {

// 补 MIME 的结果（写进报告，界面照实说这次补了几个）。
struct EmbeddedFileMimeReport {
    int scanned = 0;   // 走过的文档级嵌入文件条目数
    int patched = 0;   // 实际补上 /Subtype 的条目数
    int skipped = 0;   // 补不了的条目数（没有文件名 / 指向的不是流）
    bool rewritten = false;  // 整份文件是否被重写过（没要补的时候不会重写）
    bool encrypted = false;  // 这份文档是加密文档（写回时保留了原加密）
};

// 给文档级嵌入文件（/Names -> /EmbeddedFiles）补上 /Subtype（MIME 类型）。
//
// 为什么需要这件事
// ----------------
// PDF 的嵌入文件流上有一个 /Subtype，写的是 MIME 类型。阅读器靠它决定
// 「点开这个附件用什么程序打开」——缺了这一项，挂进去的视频只会被当成一坨二进制。
//
// PDFium 的嵌入文件接口（FPDFAttachment_SetFile）**写不进这个键**：
// 它只把键写进 /Params（写出来是 /Params<</#2FSubtype(text/plain)...>> 这种
// 非标准位置），够不到嵌入文件流的字典；它自己的 FPDFAttachment_GetSubtype
// 也读不回来。这是实测出来的限制，不是漏写。
//
// 做法
// ----
// 用 qpdf 打开序列化后的字节 -> 走 /Names /EmbeddedFiles 名字树 -> 给每个
// EmbeddedFile 流补 /Subtype -> 写回。**没有要补的就原样返回**，不做无谓的
// 整份重写（重写会改文件结构，虽然 qpdf 保内容，但没必要就不动）。
//
// 加密文档
// --------
// 加密文档也能补：PDF 的加密只作用在**流数据与字符串**上，流**字典**是明文，
// 所以带着打开口令把文档解析开之后，字典项照常能改。
//
// 写回时**保留原加密**（QPDFWriter::setPreserveEncryption(true) +
// copyEncryptionParameters）：原所有者口令、原 /Perms 权限位、原打开口令一律不动。
// 绝不能走「解密 -> 用用户口令当所有者口令重新加密」那条路——那会改掉用户的
// 权限位与所有者口令，是用户数据损失（实测确认，见交付报告）。
//
// 边界（写在这里，别在别处重复实现）
// --------------------------------
// 1. 只处理文档级嵌入文件（/Names /EmbeddedFiles）。页面上的附件批注
//    （/FileAttachment）走本程序自己写流的那条路，本来就带 /Subtype。
// 2. MIME 按文件名后缀推断；认不出来的走 application/octet-stream。
// 3. 加密文档要给出**打开口令**（用户口令）。给对了才动；给错了或者没给，
//    一律明确失败并说明原因，**不静默跳过**，也**不产出半改的文件**
//    （返回的是原封不动的输入字节）。
// 4. 补 MIME 是「锦上添花」，补不上不算保存失败：调用方应当照实汇报，
//    不能因为补个类型就拦住用户保存。
//
// 两个入口
// --------
// 不带口令的那个保留下来，是为了不破坏既有调用方：它对**未加密**文档的行为
// 与以前完全一样（加密文档走「需要打开口令」的失败分支）。
QByteArray applyEmbeddedFileMimeTypes(const QByteArray& bytes,
                                       EmbeddedFileMimeReport* report = nullptr,
                                       QString* error = nullptr);

// 带打开口令的入口：加密文档靠它才能补。
//
// password 给的是**打开口令**（用户口令）。它同时适用于加密与未加密文档：
// 未加密文档忽略这个口令，走普通路径；加密文档用它打开，写回时保留原加密。
QByteArray applyEmbeddedFileMimeTypes(const QByteArray& bytes,
                                       const QString& password,
                                       EmbeddedFileMimeReport* report = nullptr,
                                       QString* error = nullptr);

}  // namespace aipdf::pdf
