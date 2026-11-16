#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::pdf {

// PDF 权限位。默认值按「只读分发」的常见预期给：
// 允许阅读、打印、无障碍提取；不允许改写、复制正文、组装页面。
struct PdfPermissions {
    bool allowPrinting = true;
    // 高质量打印（高分辨率位图）。关掉它只留下低分辨率打印。
    bool allowHighQualityPrinting = true;
    // 复制/提取正文（文本与图片）。
    bool allowCopying = false;
    // 为无障碍工具提取内容。它比 allowCopying 宽，且不应被一并关掉。
    bool allowAccessibility = true;
    bool allowAnnotations = true;
    bool allowFormFilling = true;
    // 插入/删除/旋转页面、创建书签缩略图。
    bool allowAssembly = false;
    // 除上面几项以外的修改（例如内容编辑）。
    bool allowModifying = false;
};

// AES-256（PDF 2.0 标准安全处理器，V5/R6）加密参数。
struct PdfEncryptionOptions {
    // 空密码表示「打开不需要密码」——此时只在文档上写权限位，
    // 但任何阅读器都可以无密码打开，权限位只是君子协定。
    QString userPassword;
    // 空表示自动生成一个随机所有者密码：用户无法用「所有者身份」解除限制。
    QString ownerPassword;
    PdfPermissions permissions;
    // 是否加密元数据流（/Metadata）。默认加密。
    bool encryptMetadata = true;
};

// 本次构建是否带 qpdf。没有 qpdf 时加密入口会返回 false 并给出原因。
bool encryptionAvailable();

// 把一份明文 PDF 重写成 AES-256 加密的 PDF。成功返回新字节；失败返回空数组并写 error。
//
// 输入必须是完整 PDF 字节（不是增量片段）。函数会做一次「用用户密码重新打开」
// 的自检：打不开就当失败，避免写出一个谁也读不了的文件。
QByteArray encryptPdf(const QByteArray& input,
                      const PdfEncryptionOptions& options,
                      QString* error = nullptr);

// 用给定密码打开加密 PDF，读回 /Encrypt 字典里的版本与权限位。
// 仅用于测试与诊断；生产路径不需要它。
bool inspectEncryption(const QByteArray& input,
                       const QString& password,
                       int* revision,
                       int* permissionBits,
                       QString* error = nullptr);

// 删除常见的隐藏信息载体：文档信息字典、XMP、文档级 JavaScript、自动打开动作、
// 附加动作、嵌入文件名称树和页面级附加动作。页面正文、可见批注和表单值不受影响。
// 结果由 qpdf 完整重写，失败时返回空数组，绝不回退成“原样保存却声称已清理”。
QByteArray sanitizeHiddenData(const QByteArray& input, QString* error = nullptr);

// 用 qpdf 区分用户密码和所有者密码。仅“能打开”不足以解除权限限制。
bool verifyOwnerPassword(const QByteArray& input,
                         const QString& password,
                         QString* error = nullptr);

}  // namespace aipdf::pdf
