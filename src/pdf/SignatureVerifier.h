#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一个签名的密码学判定结果。
//
// 语义约定（很重要，界面层照着显示即可）：
//   * signatureValid 只有在「RSA 验签成立」**且**「被签名的字节摘要与签名里的一致」
//     时才为 true。验不出来（摘要算法不认得、证书取不到公钥、覆盖范围不对）
//     一律判成不成立，并在 failureReason 里写清原因——
//     绝不能把「验不出来」当成「验签通过」。
//   * failureReason 只在不成立时填写，成立时为空字符串。
struct SignatureCryptoVerdict {
    bool signatureValid = false;     // RSA 验签是否成立
    bool hashAlgorithmKnown = false; // 摘要算法是否认得（不认得就不能判）
    bool digestMatched = false;      // 被签名字节的摘要是否与签名里的 messageDigest 一致
    QString algorithm;               // 例如 "SHA-256" / "SHA-1"
    QString signerSubject;           // 签名者证书的 Subject
    QString failureReason;           // 不成立时的中文原因（成立时为空）
    QString timestamp;               // RFC 3161 时间戳（没有则为空）
    QString timestampAuthority;      // TSA 名称（没有时间戳则为空）
};

// 对一份文档里的所有签名做密码学判定。
//
// 返回值与 inspectSignatures 的顺序一致；解析不出来的签名也会占一个位置，
// failureReason 里写明原因。整份文档没有签名时返回空数组，error 里给出中文说明。
//
// 本模块只返回结构化数据，不含任何 tr() 文案：中文措辞由 app 层生成。
QVector<SignatureCryptoVerdict> verifyDocumentSignatures(const QByteArray& pdfBytes,
                                                           QString* error = nullptr);

// 只验一个签名值（给界面「单独复验」用）。
//
// signedBytes 是**被签名的那一段内容**：PDF 里就是 /ByteRange 覆盖到的字节拼起来。
// signerCertificateDer 是签名者证书的 DER；留空时自动从 pkcs7Bytes 里按
// issuer+serial 认出签名者证书。
bool verifyPkcs7Signature(const QByteArray& pkcs7Bytes,
                           const QByteArray& signedBytes,
                           const QByteArray& signerCertificateDer,
                           SignatureCryptoVerdict* verdict,
                           QString* error = nullptr);

// 从 PKCS#7 里取出签名者证书的 DER（界面层要拿它去复验、查吊销状态）。
// 认不出签名者证书时返回空 QByteArray。
QByteArray signerCertificateDerFromPkcs7(const QByteArray& pkcs7Bytes, QString* error = nullptr);

// 解析 RFC 3161 时间戳令牌（ContentInfo，取出里面的 TSTInfo）。
//
// timestamp 拿到时间戳时间，authority 拿到 TSA 名称。
// 令牌结构不对时返回 false，error 里给出中文原因。
bool parseTimestampToken(const QByteArray& tokenDer,
                          QString* timestamp,
                          QString* authority,
                          QString* error = nullptr);

// 把摘要算法 OID 翻译成名字（"SHA-256" / "SHA-1" …）。
// 不认得的 OID 返回空字符串——空字符串意味着「不能判」，不是「判成通过」。
QString digestAlgorithmNameFromOid(const QString& oid);

// ------------------------------------------------------------------ OCSP ----
//
// 本模块只做 OCSP 的**编解码**，不发网络请求：请求由界面层交给网络层发出去。

// 证书在 OCSP 响应里的状态。
enum class OcspStatus {
    Good,      // 未被吊销
    Revoked,   // 已被吊销
    Unknown,   // 响应说「查不到这个证书的状态」
    Malformed, // 响应结构不可解析
};

struct OcspResponseInfo {
    OcspStatus status = OcspStatus::Unknown;
    QString responderName;     // 响应者名称
    QString producedAt;        // 响应产生时间
    QString revocationReason;  // 吊销原因（Revoked 时有）
    bool thisUpdateValid = false;
    QString thisUpdate;
    bool nextUpdateValid = false;
    QString nextUpdate;
};

// 解析 OCSP 响应（DER）。
// 结构不可解析时返回 false，info->status 置为 Malformed，error 里给出中文原因。
bool parseOcspResponse(const QByteArray& der, OcspResponseInfo* info, QString* error = nullptr);

// 构造对一个证书的 OCSP 请求（DER）。
//
// issuerCertificateDer 是签发者证书，subjectCertificateDer 是被查的证书。
// 按 RFC 6960，CertID 用 SHA-1：issuerNameHash 是签发者 Name 的 DER 摘要，
// issuerKeyHash 是签发者公钥 BIT STRING 内容的摘要。
QByteArray buildOcspRequest(const QByteArray& issuerCertificateDer,
                             const QByteArray& subjectCertificateDer,
                             QString* error = nullptr);

// 把一份 OCSP 请求解回结构（自检与调试用）。
struct OcspRequestInfo {
    QString hashAlgorithm;      // 例如 "SHA-1"
    QByteArray issuerNameHash;
    QByteArray issuerKeyHash;
    QByteArray serialNumber;
};

bool parseOcspRequest(const QByteArray& der, OcspRequestInfo* info, QString* error = nullptr);

}  // namespace aipdf::pdf
