#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::pdf {

// 用证书里的 RSA 公钥验一个 PKCS#1 v1.5 签名。
//
// 为什么不用 BCryptVerifySignature / CryptVerifySignature：
// 这两个 API 在 MinGW 的头文件里是**旧参数表**，与系统 DLL 导出的新参数表不一致
// （CryptImportKey 少两个参数、BCryptVerifySignature 的 padding 信息位置也不一样）。
// 按头文件调用不会编译报错，但参数会被错位地传进 DLL，
// 结果是「验签必然失败」而且报的是 INVALID_PARAMETER——极难定位。
// BCrypt 的 RSA 公钥 blob 布局也踩过坑：头是 24 字节，
// 后面依次是模数、指数（按 MS SDK 的 sizeof 算会多出 16 字节指针区，导致导入直接失败）。
//
// 所以这里自己算：模幂用大整数实现，PKCS#1 v1.5 的填充与 DigestInfo 逐字节比对。
// 好处是完全自洽、没有 ABI 陷阱，而且可以单独写单测。
//
// 参数：
//   modulusBigEndian —— 模数（DER 整数去掉前导 0 后的字节，大端）
//   exponentBigEndian —— 指数（DER 整数去掉前导 0 后的字节，大端）
//   digest —— 被签内容的摘要字节
//   digestAlgorithmOid —— 摘要算法的点分十进制 OID（例如 2.16.840.1.101.3.4.2.1 = SHA-256），
//                        用来拼出期望的 DigestInfo
//   signature —— RSA 签名值（长度应与模数一致）
//
// 失败时返回 false，并把中文原因写进 reason。
bool verifyRsaPkcs1Signature(const QByteArray& modulusBigEndian,
                              const QByteArray& exponentBigEndian,
                              const QByteArray& digest,
                              const QString& digestAlgorithmOid,
                              const QByteArray& signature,
                              QString* reason = nullptr);

}  // namespace aipdf::pdf
