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
// 边界（写在这里，别在别处重复实现）
// --------------------------------
// 1. 只处理文档级嵌入文件（/Names /EmbeddedFiles）。页面上的附件批注
//    （/FileAttachment）走本程序自己写流的那条路，本来就带 /Subtype。
// 2. MIME 按文件名后缀推断；认不出来的走 application/octet-stream。
// 3. **加密文档不处理**：qpdf 读加密字节要密码，而写回还要保住原加密设置
//    （本仓库拿不到原所有者口令）。所以加密文档的 MIME 要等「重新设置加密」
//    那条路——那条路本来就要解密再加密，顺手就能补上。
// 4. 补 MIME 是「锦上添花」，补不上不算保存失败：调用方应当照实汇报，
//    不能因为补个类型就拦住用户保存。
QByteArray applyEmbeddedFileMimeTypes(const QByteArray& bytes,
                                       EmbeddedFileMimeReport* report = nullptr,
                                       QString* error = nullptr);

}  // namespace aipdf::pdf
