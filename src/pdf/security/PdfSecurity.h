#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QVector>

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
//
// password 是打开密码：加密文档给了密码才能清，清理后**仍然加密**。
QByteArray sanitizeHiddenData(const QByteArray& input,
                              QString* error = nullptr,
                              const QString& password = QString());

// 把文档级嵌入文件的**本体**也清掉。
//
// 为什么还需要这个函数：sanitizeHiddenData() 只删了 /Names 里的 /EmbeddedFiles
// **索引**。文件规格（/Filespec）与 EmbeddedFile 流本体还会被别的引用点拉活——
// 页面上的 /FileAttachment 批注、catalog 的 /AF 关联文件——于是照样写进输出文件。
// 实测（qpdf 12.4.2）：只删索引时，附件本体仍在输出字节里。
//
// 这个函数把所有指向嵌入文件的引用点都断开（名称树索引、catalog 的 /AF、
// 页面上的 /FileAttachment 批注），并要求写出时不保留不可达对象，
// 被断开的对象因此不会再进到输出字节里。
//
// 需要 qpdf；未启用 qpdf 时返回空 QByteArray 并写中文错误。
//
// password 是打开密码：加密文档给了密码才能清，清理后**仍然加密**。
QByteArray removeEmbeddedFiles(const QByteArray& input,
                               QString* error = nullptr,
                               const QString& password = QString());

// 「清理隐藏信息」按钮真正要走的那条路：先清嵌入文件本体，再清隐藏信息。
//
// 为什么单独提供组合入口，而不是让界面自己串两个函数：
//   顺序有讲究——removeEmbeddedFiles() 要靠 /Names 里的名称树把嵌入文件一个个
//   找出来并「掏空」，所以必须**先**跑；sanitizeHiddenData() 随后把
//   /Metadata、/Info、JavaScript 这些一并删掉。
//   这个顺序一旦写反（例如先删索引再找文件），文件就找不到了。
//   组合放在领域层，界面只调一个函数，顺序就永远不会被写错。
//
// 语义：sanitizeHiddenData() 的清理项 + 嵌入文件本体（/Filespec 与 EmbeddedFile 流）
// 一起清掉。挂附件的 /FileAttachment 批注会连同附件一起删除（附件没了，批注没有意义）。
// 页面正文、可见批注与表单值不受影响。
QByteArray sanitizeHiddenDataFully(const QByteArray& input,
                                  QString* error = nullptr,
                                  const QString& password = QString());

// 一类隐藏数据的清单项。
//
// 为什么要有这个结构：界面要逐项勾选，领域层不认识中文，只给 ASCII 的 id 和
// 数字；中文标题由 app 层按 id 拼（见交付报告里的 i18n 清单）。
struct HiddenDataCategory {
    QString id;         // ASCII 稳定标识，例如 "metadata" / "embedded-files"
    int itemCount = 0;  // 这一类有几项（0 = 这份文档里没有）
    QString detail;     // ASCII 技术细节：这一类凭什么被算进来
};

// 这份文档是不是加密的。
//
// 「加密但用户密码为空」也算加密：这种文档能直接打开，可它仍然带着 /Encrypt，
// 清理之后必须继续带着——否则用户会拿到一份「密码保护没了」的文件。
bool isEncryptedDocument(const QByteArray& bytes, QString* error = nullptr);

// 打开这份文档是不是必须要密码。
//
// 与 isEncryptedDocument 的区别：加密文档的用户密码为空时，它不需要密码就能打开。
// 界面据此决定要不要弹密码框——不该为一个空密码的加密文档挡住用户。
bool requiresPasswordToOpen(const QByteArray& bytes, QString* error = nullptr);

// 枚举一份文档里各类隐藏数据。
//
// 只读：只解析不写盘，调用前后文档字节不变。
// 每一类都有可验证的判断依据（依据写在 PdfSecurity.cpp 里），不是猜的。
// 返回的清单总是包含全部受支持的类别，固定顺序；没有的类别 itemCount = 0，
// 这样界面可以直接按顺序渲染勾选列表。
//
// password 给的是**打开密码**：加密文档不给对密码就枚举不了（qpdf 直接拒绝解密），
// 这时返回空清单并写明原因，而不是给一份「什么都没查出来」的假清单。
QVector<HiddenDataCategory> enumerateHiddenData(const QByteArray& bytes,
                                                 QString* error = nullptr,
                                                 const QString& password = QString());

// 只清理勾选的类；没勾的类必须原样保留。
//
// 与 sanitizeHiddenDataFully() 的区别：那个是「一键组合」，会把嵌入文件本体
// 一起删掉；这个是逐项勾选，勾了哪一类才动哪一类。
//   - categoryIds 为空 → 什么都不做，原样返回（**不是**「全清」），并写明原因。
//   - 未知 id → 明确失败，不静默忽略。
//   - 写盘之前会用同一套枚举复验：勾选的类必须归零，没勾的类必须不变
//     （增量历史除外——任何重写都会把它拍平，这是写出器的固有行为）。
//
// 加密文档：password 是打开密码。清理完之后**仍然加密**（沿用原来的密码与
// 权限位），绝不清成明文——把一份加密文件「清理」成明文，等于替用户拆了锁。
QByteArray sanitizeHiddenDataSelective(const QByteArray& bytes,
                                       const QSet<QString>& categoryIds,
                                       QString* error = nullptr,
                                       const QString& password = QString());

// 把一份加密 PDF 解开成明文（用打开密码）。
//
// 什么时候需要：用户在「加密与权限」里重新设置了加密，而源文档本身是加密的——
// 必须先把旧的解开，才能按新设置加密。直接对加密字节调用 encryptPdf() 是错的：
// qpdf 连打开都打不开。
//
// 本来就没加密的文档原样返回（不改一个字节）。
QByteArray decryptPdf(const QByteArray& input,
                      const QString& password,
                      QString* error = nullptr);

// 用 qpdf 区分用户密码和所有者密码。仅“能打开”不足以解除权限限制。
bool verifyOwnerPassword(const QByteArray& input,
                         const QString& password,
                         QString* error = nullptr);

}  // namespace aipdf::pdf
