#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 签名者证书。
//
// 字段名与上层「电子签章」页签的证书链面板一一对应：面板要显示
// 主体、签发者、序列号、指纹、生效/失效时间，以及「是否自签」「是否受信任」。
struct SignerCertificate {
    QString subject;           // 证书主体（CN=..., O=..., C=... 形式）
    QString issuer;            // 签发者
    QString commonName;        // 主体 CN
    QString issuerCommonName;  // 签发者 CN
    QString serialNumber;      // 十六进制大写
    QString thumbprintSha1;    // 证书 SHA-1 指纹（无连字符大写）
    QString notBefore;         // 生效时间（本地可读格式）
    QString notAfter;          // 失效时间（本地可读格式）
    bool selfSigned = false;   // 主体 == 签发者
    bool trusted = false;      // 系统证书链校验通过
    // 系统链引擎有没有真的把链建起来。
    //
    // 为什么单独给一个标志：trusted 为假有两种原因 ——
    // 「链建起来了但根证书不受本机信任」与「链根本就没建起来」。
    // 只看 trusted 分不出来这两件事，而它们的处置完全不同。
    bool chainBuilt = false;
};

// 一个签名域的完整信息。
struct SignatureInfo {
    QString fieldName;      // 签名域名字（/T 或域对象名）
    QString signerName;     // 签名者名字（/Name），可选
    QString subFilter;      // 如 adbe.pkcs7.detached
    QString reason;         // /Reason
    QString signingTime;    // /M（若 PKCS#7 里也有则优先取 PKCS#7 的）
    QString coverError;     // 覆盖范围检查的结论（空 = 覆盖正常）
    QVector<qint64> byteRange;    // 签名覆盖的字节区间（成对：offset, length）
    bool coversDocument = false;  // 覆盖范围是否覆盖整份文档（/Contents 空洞除外）
    QVector<SignerCertificate> chain;  // 证书链：叶子在前
    QByteArray pkcs7;        // 原始 PKCS#7 DER
};

// 本次构建是否带签名解析能力。
//
// 解析依赖 qpdf（可选依赖，没启用时 inspectSignatures 只会返回错误）。
// 上层用它决定「电子签章」页签要不要显示「本版本不支持验签」的空态。
bool signatureInspectionAvailable();

// 读一份 PDF 的全部签名。
//
// pdfBytes 为空 / 不是 PDF / 解析失败时返回空数组，
// error 要写明是「无签名」还是「解析失败」。
//
// 本模块只返回结构化数据，不含任何 tr() 文案：中文措辞由 app 层生成。
QVector<SignatureInfo> inspectSignatures(const QByteArray& pdfBytes, QString* error = nullptr);

}  // namespace aipdf::pdf
