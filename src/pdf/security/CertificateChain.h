#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// SignatureInspector.h 里的签名结构。这里只做前置声明，
// 免得本模块的头文件把另一个模块的实现细节也拖进来。
struct SignatureInfo;

// 证书的撤销状态（三态，缺一不可）。
//
// 为什么是三态而不是布尔：CRL/OCSP 查不到时**不能**报「未被吊销」——
// 那是把「没查出来」说成「查过了、没问题」。界面必须能区分这两件事，
// 否则用户会以为签名证书是干净的，而实际上我们根本不知道。
enum class RevocationState {
    NotRevoked,  // 未被吊销（确实取到了吊销信息，证书不在吊销列表里）
    Revoked,     // 已被吊销
    Unknown,     // 状态未知（没有 CRL/OCSP，或吊销服务不可达）
};

// 链检查失败的原因分类。
//
// 三种原因必须能分辨，因为它们对应完全不同的处置：
//   * 证书解析不了 —— 签名里带的证书不是合法 X.509，属于数据/文件问题；
//   * 链建不起来 —— 证书本身合法，但中间证书缺失或链结构不完整；
//   * 不受信任 —— 链是完整的，只是根证书不在这台机器的信任列表里。
enum class ChainFailure {
    None,                  // 没有失败
    CertificateUnparsable, // 证书根本身解析不了
    ChainNotBuilt,         // 链建不起来
    NotTrusted,            // 链建起来了但不受本机信任
};

// 证书链上的一张证书。
struct ChainCertificateInfo {
    QString subject;       // 主体（CN / O / C 等）
    QString issuer;        // 颁发者
    QString serialNumber;  // 序列号（十六进制大写）
    QString notBefore;     // 生效时间（本地时间）
    QString notAfter;      // 失效时间（本地时间）
    bool selfSigned = false;
    bool isRoot = false;
    bool trustedBySystem = false; // 这张证书是否受本机证书存储信任（根看根，链上证书看自身）
    RevocationState revocationState = RevocationState::Unknown;
    QString revocationStatus;     // 三态中文：未被吊销 / 已被吊销 / 状态未知（没有 CRL/OCSP）
    QString revocationDetail;     // CRL 分发点 / OCSP 地址等
    QByteArray der;               // 这张证书的 DER，便于界面展示
};

// 一次证书链检查的完整结论。
//
// 三个结论**分开**给，不要混成一个「失败」：
//   * trusted —— 整条链是否受本机信任；
//   * revoked —— 链上有没有被吊销的证书；
//   * expired —— 链上有没有不在有效期内的证书。
// 一条链完全可能「受信任但其中一张已过期」，也可能「不受信任但完全没被吊销」。
struct CertificateChainReport {
    bool ok = false;      // 链能不能建起来
    bool trusted = false; // 整条链是否受信任
    bool revoked = false; // 链上有没有被吊销的证书
    bool expired = false; // 链上有没有不在有效期内的证书
    ChainFailure failure = ChainFailure::None;
    QString error;        // 中文失败原因（failure == None 时为空）
    QVector<ChainCertificateInfo> certificates; // 从叶子（签名证书）到根
    QString summary;      // 一行中文结论
};

// ------------------------------------------------------------------ 纯逻辑 ----
//
// 下面这几个函数不碰任何系统 API，只做「结构体 → 文案/枚举」的映射。
// 之所以单独拆出来：撤销状态三态、时间格式、结论三态这些规则最容易在
// 改动中被悄悄改坏，而它们又必须能被**不依赖真证书**地测到。

// 撤销状态 → 中文文案。
QString revocationStateText(RevocationState state);

// 失败原因 → 中文文案。
QString chainFailureText(ChainFailure failure);

// 一行结论。四个输入就是四个独立结论，函数负责排出优先级。
QString chainVerdictSummary(bool ok, bool trusted, bool revoked, bool expired);

// Unix 毫秒 → 本地时间「yyyy-MM-dd HH:mm:ss」。
QString formatCertificateTime(qint64 utcMilliseconds);

// Crypt32 的 CERT_TRUST_STATUS 两个字段 → 撤销状态。
//
// errorStatus / infoStatus 是 wincrypt.h 里 CERT_TRUST_STATUS 的原始位。
// 这里只取与撤销有关的位：
//   * CERT_TRUST_IS_REVOKED(0x4) —— 已吊销；
//   * CERT_TRUST_REVOCATION_STATUS_UNKNOWN(0x40) /
//     CERT_TRUST_IS_OFFLINE_REVOCATION(0x1000000) —— 状态未知；
//   * CERT_TRUST_AUTO_UPDATE_CA_REVOCATION(0x10) /
//     CERT_TRUST_AUTO_UPDATE_END_REVOCATION(0x20)（在 infoStatus 里）——
//     说明引擎确实取到了吊销信息，此时「没被吊销」才是有证据的结论。
// 没有正向证据的一律判「状态未知」，绝不假装「未被吊销」。
RevocationState revocationStateFromTrustFlags(quint32 errorStatus, quint32 infoStatus);

// ------------------------------------------------------------------ 检查 ----

// 当前进程里还活着的 Crypt32 句柄数量（自测用）。
//
// 存在的理由：Crypt32 的句柄全靠人肉释放，早退分支最容易漏掉
// CertFreeCertificateChain。这个计数让「漏释放」变成可断言的事实，
// 而不是只能靠 code review 发现。
int certificateChainLiveHandleCount();

// 查一张叶子证书的链与撤销状态。
//
// leafDer：签名证书的 DER；
// intermediates：额外的中间证书 DER（PKCS#7 里自带的那些）；
// extraStore：额外要放进链引擎的证书库（HCERTSTORE，可空）。
//
// 返回值里的三个结论是分开的：trusted / revoked / expired。
CertificateChainReport inspectCertificateChain(const QByteArray& leafDer,
                                                const QVector<QByteArray>& intermediates =
                                                    QVector<QByteArray>(),
                                                void* extraStore = nullptr);

// ------------------------------------------------------------------ 桥接 ----

// 从一份签名里取出证书的 DER：叶子（签名者证书）在前，其余中间证书在后。
//
// 为什么需要它：SignatureInfo 只导出证书的元信息（主体 / 签发者 / 序列号 /
// 指纹 / 有效期 / trusted），**没有 DER 字节**，而 inspectCertificateChain
// 要的正是 DER。这座桥放在本模块里，app 层就不必再实现一遍 PKCS#7 解析。
//
// 取不到就返回空数组（不报错、不抛异常）。
//
// 调用方式：inspectCertificateChain(ders.first(), ders.mid(1))。
QVector<QByteArray> signatureCertificateDer(const SignatureInfo& info);

}  // namespace aipdf::pdf
