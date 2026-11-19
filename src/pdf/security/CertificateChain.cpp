// 用 Windows 自带的证书存储（Crypt32）查证书链与撤销状态。
//
// 为什么要有这个模块：验签结果一直由随包的 pyHanko 给出，
// 但主程序并没有接管「证书链」与「撤销状态」这两件事——
// 用户只看到「验签通过 / 失败」，看不到：
//   * 这张证书是谁签发的、链上还有哪些中间证书；
//   * 根证书在不在本机受信任的证书存储里；
//   * 每张证书的撤销状态（CRL / OCSP）以及有没有过期。
// 这里用 Crypt32 把这三件事一次性问清楚，并且把
// 「受信任」「被吊销」「已过期」三个结论**分开**给出。
//
// ---------------------------------------------- 本模块踩过的系统坑 -------------
//
// 1) CertGetCertificateChain 的 pChainPara **不能传 nullptr**：
//    传了直接返回 ERROR_INVALID_PARAMETER（0x80070057），链根本建不起来。
//    （仓库里 SignatureInspector.cpp 的建链调用就传了 nullptr，
//      所以那里的信任判断实际上从来没有生效过——本模块不依赖那条路径。）
//
// 2) CERT_SIMPLE_CHAIN::rgpElement 的真实布局是「元素指针数组」
//    （PCERT_CHAIN_ELEMENT**），而 SDK 声明的是 PCERT_CHAIN_ELEMENT*。
//    照声明去解会读到错位的内存。本模块两种布局都试，并用
//    cbSize/pCertContext 校验，取到可信的那个（见 chainElementAt）。
//
// 3) MinGW 的 CertCreateCertificateContext 只声明了 3 个参数，
//    而 crypt32.dll 的真实导出是 5 个（多出 hCertStore / dwFlags）。
//    照头文件调会把寄存器里的垃圾当成句柄传进去。
//    所以本模块改用 CertAddEncodedCertificateToStore——它的 MinGW 声明
//    与真实导出完全一致。
//
// 4) Windows **没有** CERT_CHAIN_POLICY_REVOCATION 这个链策略：
//    真实策略只有 BASE(1)…SSL_KEY_PIN(12)，本机实测 13..40 全部不被接受。
//    微软文档也写明「CertVerifyCertificateChainPolicy 不检查证书是否被吊销，
//    要检查吊销请用 CertGetCertificateChain 打开撤销检查」。
//    所以本模块的做法是：建链时带上 CERT_CHAIN_REVOCATION_CHECK_CHAIN，
//    再从每个链元素的 CERT_TRUST_STATUS 里读撤销结论。
//
// ---------------------------------------------- 撤销状态怎么判 ---------------
//
// CERT_REVOCATION_INFO::dwRevocationResult 实测恒为 0，不能用来区分
// 「没被吊销」和「没查到」，所以本模块只用 CERT_TRUST_STATUS 的位：
//   * CERT_TRUST_IS_REVOKED(0x4) —— 已吊销；
//   * CERT_TRUST_REVOCATION_STATUS_UNKNOWN(0x40) /
//     CERT_TRUST_IS_OFFLINE_REVOCATION(0x1000000) —— 状态未知；
//   * infoStatus 里的 CERT_TRUST_AUTO_UPDATE_CA_REVOCATION(0x10) /
//     CERT_TRUST_AUTO_UPDATE_END_REVOCATION(0x20) —— 引擎确实取到了
//     吊销信息，此时「未被吊销」才是有证据的结论。
// 没有正向证据的一律判「状态未知」，绝不假装「未被吊销」。

#include "pdf/security/CertificateChain.h"

#include "i18n/Translate.h"
#include "pdf/security/SignatureInspector.h"

#include <QDateTime>
#include <QStringList>

#include <algorithm>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace aipdf::pdf {

namespace {

// 撤销检查的网络超时（毫秒）。
//
// 建链时会顺带做撤销检查，吊销服务器不可达时不能把界面卡死。
// 这里给 3 秒：够慢的网络也能取到 CRL/OCSP，不够就老实报「状态未知」。
constexpr quint32 kUrlRetrievalTimeoutMs = 3000;

// Crypt32 的信任状态位（与 wincrypt.h 的 CERT_TRUST_* 一致）。
// 这里自己抄一份，是为了让公共头文件不必依赖 windows.h，
// 同时也让这几个常量能被单测直接引用。
constexpr quint32 kTrustIsNotTimeValid = 0x00000001u;
constexpr quint32 kTrustIsRevoked = 0x00000004u;
constexpr quint32 kTrustRevocationStatusUnknown = 0x00000040u;
constexpr quint32 kTrustIsOfflineRevocation = 0x01000000u;
constexpr quint32 kTrustAutoUpdateCaRevocation = 0x00000010u;   // infoStatus
constexpr quint32 kTrustAutoUpdateEndRevocation = 0x00000020u;  // infoStatus

// 撤销信息相关的扩展 OID。
const char* const kOidCrlDistributionPoints = "2.5.29.31";
const char* const kOidAuthorityInfoAccess = "1.3.6.1.5.5.7.1.1";
const char* const kOidOcspNoCheck = "1.3.6.1.5.5.7.48.1.5";

// AIA 里的 accessMethod：1.3.6.1.5.5.7.48.1 = OCSP，1.3.6.1.5.5.7.48.2 = caIssuers。
// 直接比对 DER 内容字节，不必把 OID 解成点分十进制。
const QByteArray kOcspMethodOid = QByteArray::fromHex("2B06010505073001");
const QByteArray kCaIssuersMethodOid = QByteArray::fromHex("2B06010505073002");

#ifdef Q_OS_WIN

// ------------------------------------------------------------ 句柄计数 ----
//
// Crypt32 的句柄全靠人肉释放，早退分支（证书解析不了、链建不起来）
// 最容易漏掉 CertFreeCertificateChain。这里给每个持有句柄的 RAII 包装
// 挂一个进程级计数，自测只要断言「全部调用结束后计数归零」，
// 漏释放就会当场变红，而不是只能靠 code review 发现。

int gLiveHandleCount = 0;

class CertContextHandle {
public:
    explicit CertContextHandle(PCCERT_CONTEXT context = nullptr) : context_(context) {
        if (context_) {
            ++gLiveHandleCount;
        }
    }
    ~CertContextHandle() {
        if (context_) {
            CertFreeCertificateContext(context_);
            --gLiveHandleCount;
        }
    }
    CertContextHandle(const CertContextHandle&) = delete;
    CertContextHandle& operator=(const CertContextHandle&) = delete;

    PCCERT_CONTEXT get() const { return context_; }

private:
    PCCERT_CONTEXT context_ = nullptr;
};

class ChainContextHandle {
public:
    explicit ChainContextHandle(PCCERT_CHAIN_CONTEXT context = nullptr) : context_(context) {
        if (context_) {
            ++gLiveHandleCount;
        }
    }
    ~ChainContextHandle() {
        if (context_) {
            CertFreeCertificateChain(context_);
            --gLiveHandleCount;
        }
    }
    ChainContextHandle(const ChainContextHandle&) = delete;
    ChainContextHandle& operator=(const ChainContextHandle&) = delete;

    PCCERT_CHAIN_CONTEXT get() const { return context_; }

private:
    PCCERT_CHAIN_CONTEXT context_ = nullptr;
};

class ChainEngineHandle {
public:
    ChainEngineHandle() = default;
    ~ChainEngineHandle() { reset(); }
    ChainEngineHandle(const ChainEngineHandle&) = delete;
    ChainEngineHandle& operator=(const ChainEngineHandle&) = delete;

    void reset(HCERTCHAINENGINE engine = nullptr) {
        if (engine_) {
            CertFreeCertificateChainEngine(engine_);
            --gLiveHandleCount;
        }
        engine_ = engine;
        if (engine_) {
            ++gLiveHandleCount;
        }
    }

    HCERTCHAINENGINE get() const { return engine_; }

private:
    HCERTCHAINENGINE engine_ = nullptr;
};

class StoreHandle {
public:
    explicit StoreHandle(HCERTSTORE store = nullptr) : store_(store) {
        if (store_) {
            ++gLiveHandleCount;
        }
    }
    ~StoreHandle() {
        if (store_) {
            CertCloseStore(store_, 0);
            --gLiveHandleCount;
        }
    }
    StoreHandle(const StoreHandle&) = delete;
    StoreHandle& operator=(const StoreHandle&) = delete;

    HCERTSTORE get() const { return store_; }

private:
    HCERTSTORE store_ = nullptr;
};

// 一组证书上下文的持有者：统一在析构里释放，漏释放会被句柄计数抓到。
class CertificateList {
public:
    CertificateList() = default;
    ~CertificateList() {
        for (PCCERT_CONTEXT context : contexts_) {
            CertFreeCertificateContext(context);
            --gLiveHandleCount;
        }
    }
    CertificateList(const CertificateList&) = delete;
    CertificateList& operator=(const CertificateList&) = delete;

    void append(PCCERT_CONTEXT context) {
        if (!context) {
            return;
        }
        contexts_.append(context);
        ++gLiveHandleCount;
    }

    const QVector<PCCERT_CONTEXT>& contexts() const { return contexts_; }

private:
    QVector<PCCERT_CONTEXT> contexts_;
};

// -------------------------------------------------------------- DER ----
//
// 只需要从证书扩展里挖 CRL 分发点 / OCSP 地址，所以只认最浅的几层 TLV，
// 不做通用 ASN.1。

struct DerElement {
    quint8 tag = 0;   // 完整标签字节
    int valueStart = 0;
    int valueLength = 0;
    int end = 0;      // 本元素结束位置
};

bool readDerElement(const QByteArray& data, int offset, int limit, DerElement& element) {
    if (offset < 0 || limit > data.size() || offset + 2 > limit) {
        return false;
    }
    int pos = offset;
    const quint8 tag = static_cast<quint8>(data.at(pos++));
    if ((tag & 0x1f) == 0x1f) {
        // 高位标签号形式：这两个扩展用不到，判失败比猜语义安全。
        return false;
    }
    if (pos >= limit) {
        return false;
    }
    const quint8 lengthByte = static_cast<quint8>(data.at(pos++));
    int length = 0;
    if (lengthByte < 0x80) {
        length = lengthByte;
    } else {
        const int count = lengthByte & 0x7f;
        if (count == 0 || count > 4 || pos + count > limit) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            length = (length << 8) | static_cast<quint8>(data.at(pos++));
        }
    }
    if (length < 0 || pos + length > limit) {
        return false;
    }
    element.tag = tag;
    element.valueStart = pos;
    element.valueLength = length;
    element.end = pos + length;
    return true;
}

// 递归收集所有 [6]（IA5String 的 URI）——CRL 分发点里的 URL 就是这个形状。
void collectUriValues(const QByteArray& data, int offset, int limit, QStringList& out, int depth) {
    if (depth > 8) {
        return;
    }
    int pos = offset;
    while (pos < limit) {
        DerElement element;
        if (!readDerElement(data, pos, limit, element)) {
            return;
        }
        if (element.tag == 0x86) {
            out.append(QString::fromLatin1(data.mid(element.valueStart, element.valueLength)));
        } else if ((element.tag & 0x20) != 0) {
            // 构造型标签（SEQUENCE / SET / 上下文标签）：继续往下走。
            collectUriValues(data, element.valueStart, element.end, out, depth + 1);
        }
        pos = element.end;
    }
}

// AIA：SEQUENCE OF AccessDescription{ SEQUENCE{ OID method, GeneralName location } }。
void collectAuthorityInfoAccess(const QByteArray& data, int offset, int limit,
                                QVector<QPair<QByteArray, QString>>& out) {
    DerElement sequence;
    if (!readDerElement(data, offset, limit, sequence) || sequence.tag != 0x30) {
        return;
    }
    int pos = sequence.valueStart;
    while (pos < sequence.end) {
        DerElement access;
        if (!readDerElement(data, pos, sequence.end, access) || access.tag != 0x30) {
            return;
        }
        DerElement method;
        if (!readDerElement(data, access.valueStart, access.end, method) || method.tag != 0x06) {
            return;
        }
        const QByteArray methodOid = data.mid(method.valueStart, method.valueLength);
        DerElement location;
        if (readDerElement(data, method.end, access.end, location) && location.tag == 0x86) {
            out.append(qMakePair(methodOid,
                                 QString::fromLatin1(data.mid(location.valueStart,
                                                             location.valueLength))));
        }
        pos = access.end;
    }
}

struct RevocationEndpoints {
    QStringList crlDistributionPoints;
    QStringList ocspUrls;
    QStringList caIssuerUrls;
    bool ocspNoCheck = false;
};

RevocationEndpoints readRevocationEndpoints(PCCERT_CONTEXT context) {
    RevocationEndpoints endpoints;
    if (!context || !context->pCertInfo) {
        return endpoints;
    }
    const CERT_INFO* info = context->pCertInfo;
    for (DWORD i = 0; i < info->cExtension; ++i) {
        const CERT_EXTENSION& extension = info->rgExtension[i];
        if (!extension.pszObjId || !extension.Value.pbData || extension.Value.cbData == 0) {
            continue;
        }
        const QByteArray value(reinterpret_cast<const char*>(extension.Value.pbData),
                               static_cast<int>(extension.Value.cbData));
        if (qstrcmp(extension.pszObjId, kOidCrlDistributionPoints) == 0) {
            collectUriValues(value, 0, value.size(), endpoints.crlDistributionPoints, 0);
        } else if (qstrcmp(extension.pszObjId, kOidAuthorityInfoAccess) == 0) {
            QVector<QPair<QByteArray, QString>> entries;
            collectAuthorityInfoAccess(value, 0, value.size(), entries);
            for (const auto& entry : entries) {
                if (entry.first == kOcspMethodOid) {
                    endpoints.ocspUrls.append(entry.second);
                } else if (entry.first == kCaIssuersMethodOid) {
                    endpoints.caIssuerUrls.append(entry.second);
                }
            }
        } else if (qstrcmp(extension.pszObjId, kOidOcspNoCheck) == 0) {
            endpoints.ocspNoCheck = true;
        }
    }
    return endpoints;
}

// ------------------------------------------------------------ 证书描述 ----

QString nameBlobToString(const CERT_NAME_BLOB* blob) {
    if (!blob || blob->cbData == 0 || !blob->pbData) {
        return QString();
    }
    // CERT_X500_NAME_STR 而不是 CERT_X509_NAME_STR：后者只有新 SDK 才有，
    // 且数值不同（4 = SIMPLE_DISPLAY），MinGW 上没有。
    PCERT_NAME_BLOB mutableBlob = const_cast<PCERT_NAME_BLOB>(blob);
    const DWORD needed =
        CertNameToStrW(X509_ASN_ENCODING, mutableBlob, CERT_X500_NAME_STR, nullptr, 0);
    if (needed == 0) {
        return QString();
    }
    QByteArray buffer(static_cast<int>(needed + 1) * static_cast<int>(sizeof(wchar_t)), '\0');
    wchar_t* text = reinterpret_cast<wchar_t*>(buffer.data());
    const DWORD written =
        CertNameToStrW(X509_ASN_ENCODING, mutableBlob, CERT_X500_NAME_STR, text, needed);
    // 返回的字符数**包含结尾的 NUL**，不去掉的话名字后面会挂一个 '\0'。
    const int length = written > 0 ? static_cast<int>(written) - 1 : 0;
    return QString::fromWCharArray(text, length);
}

// Crypt32 的 CERT_INFO.SerialNumber 是**小端**存的（历史遗留），
// 与 DER 里的字节序正好相反。展示前要翻回来。
QString serialNumberToHex(const CERT_INFO* info) {
    if (!info || !info->SerialNumber.pbData || info->SerialNumber.cbData == 0) {
        return QString();
    }
    QByteArray raw(reinterpret_cast<const char*>(info->SerialNumber.pbData),
                   static_cast<int>(info->SerialNumber.cbData));
    std::reverse(raw.begin(), raw.end());
    int index = 0;
    while (index < raw.size() && raw.at(index) == '\0') {
        ++index;
    }
    return raw.mid(index).toHex().toUpper();
}

qint64 fileTimeToMilliseconds(const FILETIME& value) {
    ULARGE_INTEGER ticks;
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    // FILETIME 从 1601-01-01 起算 100ns；换成 Unix 毫秒。
    return static_cast<qint64>(ticks.QuadPart / 10000ULL) - 11644473600000LL;
}

bool isSelfSigned(const CERT_INFO* info) {
    return info && info->Subject.cbData == info->Issuer.cbData && info->Subject.cbData > 0 &&
           std::memcmp(info->Subject.pbData, info->Issuer.pbData, info->Subject.cbData) == 0;
}

// ---------------------------------------------------------- 链元素访问 ----

bool looksLikeUserPointer(const void* pointer) {
    const quintptr value = reinterpret_cast<quintptr>(pointer);
    // 用户态地址在 64 位 Windows 上小于 128TB；用它挡掉「按错误布局读出来的垃圾指针」。
    return value > 0x10000u && value < 0x7FFFFFFFFFFFULL;
}

bool isPlausibleChainElement(PCCERT_CHAIN_ELEMENT element) {
    if (!element || element->cbSize != sizeof(CERT_CHAIN_ELEMENT)) {
        return false;
    }
    const PCCERT_CONTEXT context = element->pCertContext;
    if (!context || !context->pbCertEncoded) {
        return false;
    }
    return context->cbCertEncoded > 0 && context->cbCertEncoded <= 65536;
}

enum class ElementLayout {
    PointerArray, // Windows 实测行为：rgpElement 是「元素指针数组」
    Contiguous,   // SDK 声明的行为：连续的 CERT_CHAIN_ELEMENT 数组
    Unknown,
};

ElementLayout detectElementLayout(const CERT_SIMPLE_CHAIN* simpleChain) {
    if (!simpleChain || simpleChain->cElement == 0) {
        return ElementLayout::Unknown;
    }
    // 布局一（Windows 实测）：rgpElement 指向「元素指针数组」。
    const PCCERT_CHAIN_ELEMENT* pointerArray =
        reinterpret_cast<const PCCERT_CHAIN_ELEMENT*>(simpleChain->rgpElement);
    PCCERT_CHAIN_ELEMENT viaPointer = pointerArray[0];
    if (looksLikeUserPointer(viaPointer) && isPlausibleChainElement(viaPointer)) {
        return ElementLayout::PointerArray;
    }
    // 布局二（SDK 声明）：连续的 CERT_CHAIN_ELEMENT 数组。
    PCCERT_CHAIN_ELEMENT viaContiguous = simpleChain->rgpElement[0];
    if (isPlausibleChainElement(viaContiguous)) {
        return ElementLayout::Contiguous;
    }
    return ElementLayout::Unknown;
}

PCCERT_CHAIN_ELEMENT chainElementAt(const CERT_SIMPLE_CHAIN* simpleChain, ElementLayout layout,
                                     DWORD index) {
    if (!simpleChain || index >= simpleChain->cElement) {
        return nullptr;
    }
    if (layout == ElementLayout::PointerArray) {
        const PCCERT_CHAIN_ELEMENT* pointerArray =
            reinterpret_cast<const PCCERT_CHAIN_ELEMENT*>(simpleChain->rgpElement);
        PCCERT_CHAIN_ELEMENT element = pointerArray[index];
        return isPlausibleChainElement(element) ? element : nullptr;
    }
    if (layout == ElementLayout::Contiguous) {
        PCCERT_CHAIN_ELEMENT element = simpleChain->rgpElement[index];
        return isPlausibleChainElement(element) ? element : nullptr;
    }
    return nullptr;
}

// ------------------------------------------------------------ 证书排序 ----
//
// 按「上一张的签发者 == 下一张的主体」把证书串成一条链（叶子在前）。
// 规则与 SignatureInspector.cpp 的 orderCertificates 一致：
// 没接上的（缺中间证书、交叉签名等）按原顺序补在后面，别丢证书。

bool sameName(const CERT_NAME_BLOB& left, const CERT_NAME_BLOB& right) {
    return left.cbData == right.cbData && left.cbData > 0 &&
           std::memcmp(left.pbData, right.pbData, left.cbData) == 0;
}

QVector<PCCERT_CONTEXT> orderChainCertificates(const QVector<PCCERT_CONTEXT>& certificates,
                                                int leafIndex) {
    QVector<PCCERT_CONTEXT> ordered;
    if (certificates.isEmpty()) {
        return ordered;
    }
    QVector<bool> used(certificates.size(), false);
    if (leafIndex >= 0 && leafIndex < certificates.size()) {
        used[leafIndex] = true;
        ordered.append(certificates.at(leafIndex));
    }
    bool progressed = true;
    while (progressed && !ordered.isEmpty()) {
        progressed = false;
        const CERT_INFO* previous = ordered.last()->pCertInfo;
        if (!previous) {
            break;
        }
        for (int i = 0; i < certificates.size(); ++i) {
            if (used.at(i)) {
                continue;
            }
            const CERT_INFO* candidate = certificates.at(i)->pCertInfo;
            if (!candidate) {
                continue;
            }
            if (sameName(candidate->Subject, previous->Issuer)) {
                used[i] = true;
                ordered.append(certificates.at(i));
                progressed = true;
                break;
            }
        }
    }
    for (int i = 0; i < certificates.size(); ++i) {
        if (!used.at(i)) {
            ordered.append(certificates.at(i));
        }
    }
    return ordered;
}

// 找出叶子（签名者证书）在 certificates 里的下标。
//
// 先用 SignatureInfo::chain 里第一张的序列号认（那是签名者证书）；
// 认不出时退回到「不签发任何一张证书的那张」——
// 叶子不签发别人，CA 签发叶子、根签发自己，所以只有叶子符合。
int findLeafCertificateIndex(const QVector<PCCERT_CONTEXT>& certificates,
                             const SignatureInfo& info) {
    if (certificates.isEmpty()) {
        return -1;
    }
    if (!info.chain.isEmpty()) {
        const QString wanted = info.chain.first().serialNumber;
        if (!wanted.isEmpty()) {
            for (int i = 0; i < certificates.size(); ++i) {
                const CERT_INFO* candidate = certificates.at(i)->pCertInfo;
                if (candidate && serialNumberToHex(candidate) == wanted) {
                    return i;
                }
            }
        }
    }
    int leafCandidateCount = 0;
    int leafCandidateIndex = -1;
    for (int i = 0; i < certificates.size(); ++i) {
        const CERT_INFO* candidate = certificates.at(i)->pCertInfo;
        if (!candidate) {
            continue;
        }
        bool issuesSomeone = false;
        for (int j = 0; j < certificates.size(); ++j) {
            if (i == j) {
                continue;
            }
            const CERT_INFO* other = certificates.at(j)->pCertInfo;
            if (other && sameName(candidate->Subject, other->Issuer)) {
                issuesSomeone = true;
                break;
            }
        }
        if (!issuesSomeone) {
            ++leafCandidateCount;
            leafCandidateIndex = i;
        }
    }
    if (leafCandidateCount == 1) {
        return leafCandidateIndex;
    }
    return 0;
}

#endif  // Q_OS_WIN

}  // namespace

// ------------------------------------------------------------ 纯逻辑映射 ----

QString revocationStateText(RevocationState state) {
    switch (state) {
    case RevocationState::NotRevoked:
        return i18n::tr("aipdf::pdf::CertificateChain", "未被吊销");
    case RevocationState::Revoked:
        return i18n::tr("aipdf::pdf::CertificateChain", "已被吊销");
    case RevocationState::Unknown:
        break;
    }
    return i18n::tr("aipdf::pdf::CertificateChain", "状态未知（没有 CRL/OCSP）");
}

QString chainFailureText(ChainFailure failure) {
    switch (failure) {
    case ChainFailure::CertificateUnparsable:
        return i18n::tr("aipdf::pdf::CertificateChain", "签名证书解析失败：数据不是有效的 X.509 证书。");
    case ChainFailure::ChainNotBuilt:
        return i18n::tr("aipdf::pdf::CertificateChain", "证书链构建失败：缺少中间证书或链结构不完整。");
    case ChainFailure::NotTrusted:
        return i18n::tr("aipdf::pdf::CertificateChain", "证书链的根证书不在本机受信任的证书存储中。");
    case ChainFailure::None:
        break;
    }
    return QString();
}

QString chainVerdictSummary(bool ok, bool trusted, bool revoked, bool expired) {
    if (!ok) {
        return i18n::tr("aipdf::pdf::CertificateChain", "证书链无法建立。");
    }
    if (revoked) {
        return i18n::tr("aipdf::pdf::CertificateChain", "证书链上存在已被吊销的证书。");
    }
    if (expired) {
        return i18n::tr("aipdf::pdf::CertificateChain", "证书链上存在不在有效期内的证书。");
    }
    if (!trusted) {
        return i18n::tr("aipdf::pdf::CertificateChain", "证书链不在本机受信任的证书存储中。");
    }
    return i18n::tr("aipdf::pdf::CertificateChain", "证书链完整且受本机信任。");
}

QString formatCertificateTime(qint64 utcMilliseconds) {
    const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(utcMilliseconds);
    if (!dateTime.isValid()) {
        return QString();
    }
    return dateTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

RevocationState revocationStateFromTrustFlags(quint32 errorStatus, quint32 infoStatus) {
    if ((errorStatus & kTrustIsRevoked) != 0) {
        return RevocationState::Revoked;
    }
    if ((errorStatus & (kTrustRevocationStatusUnknown | kTrustIsOfflineRevocation)) != 0) {
        return RevocationState::Unknown;
    }
    // 只有在引擎确实取到了吊销信息时，「未被吊销」才是有证据的结论。
    if ((infoStatus & (kTrustAutoUpdateCaRevocation | kTrustAutoUpdateEndRevocation)) != 0) {
        return RevocationState::NotRevoked;
    }
    // 其余情况一律「未知」：查不到不等于没被吊销。
    return RevocationState::Unknown;
}

int certificateChainLiveHandleCount() {
#ifdef Q_OS_WIN
    return gLiveHandleCount;
#else
    return 0;
#endif
}

// -------------------------------------------------------------- 主流程 ----

CertificateChainReport inspectCertificateChain(const QByteArray& leafDer,
                                                const QVector<QByteArray>& intermediates,
                                                void* extraStore) {
    CertificateChainReport report;

#ifndef Q_OS_WIN
    // 非 Windows 平台没有 Crypt32，只能给「链建不起来」。
    report.failure = ChainFailure::ChainNotBuilt;
    report.error = chainFailureText(report.failure);
    report.summary = chainVerdictSummary(report.ok, report.trusted, report.revoked, report.expired);
    return report;
#else

    if (leafDer.isEmpty()) {
        report.failure = ChainFailure::CertificateUnparsable;
        report.error = chainFailureText(report.failure);
        report.summary =
            chainVerdictSummary(report.ok, report.trusted, report.revoked, report.expired);
        return report;
    }

    // 一个内存证书库装下：叶子证书 + 中间证书 + 调用方给的额外证书库。
    // 建链时它就是 hAdditionalStore。
    StoreHandle store(CertOpenStore(CERT_STORE_PROV_MEMORY, X509_ASN_ENCODING, 0, 0, nullptr));
    if (!store.get()) {
        report.failure = ChainFailure::ChainNotBuilt;
        report.error = chainFailureText(report.failure);
        report.summary =
            chainVerdictSummary(report.ok, report.trusted, report.revoked, report.expired);
        return report;
    }

    for (const QByteArray& der : intermediates) {
        if (der.isEmpty()) {
            continue;
        }
        PCCERT_CONTEXT stored = nullptr;
        CertAddEncodedCertificateToStore(store.get(), X509_ASN_ENCODING,
                                         reinterpret_cast<const BYTE*>(der.constData()),
                                         static_cast<DWORD>(der.size()), CERT_STORE_ADD_ALWAYS,
                                         &stored);
    }

    if (extraStore) {
        // 把调用方给的证书库也并进来：逐张复制进内存库。
        HCERTSTORE source = static_cast<HCERTSTORE>(extraStore);
        PCCERT_CONTEXT current = nullptr;
        while ((current = CertEnumCertificatesInStore(source, current)) != nullptr) {
            if (!current->pbCertEncoded || current->cbCertEncoded == 0) {
                continue;
            }
            PCCERT_CONTEXT stored = nullptr;
            CertAddEncodedCertificateToStore(store.get(), X509_ASN_ENCODING, current->pbCertEncoded,
                                             current->cbCertEncoded, CERT_STORE_ADD_ALWAYS, &stored);
        }
    }

    // 叶子证书。用 CertAddEncodedCertificateToStore 而不是
    // CertCreateCertificateContext——后者的 MinGW 声明少了两个参数（见文件头注释）。
    PCCERT_CONTEXT storedLeaf = nullptr;
    if (!CertAddEncodedCertificateToStore(store.get(), X509_ASN_ENCODING,
                                         reinterpret_cast<const BYTE*>(leafDer.constData()),
                                         static_cast<DWORD>(leafDer.size()), CERT_STORE_ADD_ALWAYS,
                                         &storedLeaf) ||
        !storedLeaf) {
        report.failure = ChainFailure::CertificateUnparsable;
        report.error = chainFailureText(report.failure);
        report.summary =
            chainVerdictSummary(report.ok, report.trusted, report.revoked, report.expired);
        return report;
    }
    // 复制一份出来，明确由 RAII 负责释放（store 里的那份归 store 管）。
    CertContextHandle leaf(CertDuplicateCertificateContext(storedLeaf));

    // 链引擎：只为把吊销检查的网络超时压到 3 秒（默认 15 秒会把界面卡住）。
    // 建不出来就退回默认引擎，功能不受影响。
    CERT_CHAIN_ENGINE_CONFIG engineConfig;
    std::memset(&engineConfig, 0, sizeof(engineConfig));
    engineConfig.cbSize = sizeof(engineConfig);
    engineConfig.dwUrlRetrievalTimeout = kUrlRetrievalTimeoutMs;
    HCERTCHAINENGINE rawEngine = nullptr;
    ChainEngineHandle engine;
    if (CertCreateCertificateChainEngine(&engineConfig, &rawEngine)) {
        engine.reset(rawEngine);
    }

    CERT_CHAIN_PARA chainPara;
    std::memset(&chainPara, 0, sizeof(chainPara));
    chainPara.cbSize = sizeof(chainPara);
    chainPara.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;

    // 注意：pChainPara 传 nullptr 会直接失败（ERROR_INVALID_PARAMETER），必须给。
    const DWORD flags = CERT_CHAIN_REVOCATION_CHECK_CHAIN | CERT_CHAIN_REVOCATION_ACCUMULATIVE_TIMEOUT;
    PCCERT_CHAIN_CONTEXT rawChain = nullptr;
    if (!CertGetCertificateChain(engine.get(), leaf.get(), nullptr, store.get(), &chainPara, flags,
                                 nullptr, &rawChain) ||
        !rawChain) {
        report.failure = ChainFailure::ChainNotBuilt;
        report.error = chainFailureText(report.failure);
        report.summary =
            chainVerdictSummary(report.ok, report.trusted, report.revoked, report.expired);
        return report;
    }
    ChainContextHandle chain(rawChain);

    if (chain.get()->cChain == 0) {
        // 引擎给了链上下文却没有链元素：当作链建不起来。
        // （这一条早退分支上链句柄还活着，必须由 RAII 归还——
        //   漏释放的话 certificateChainLiveHandleCount() 会一直涨。）
        report.failure = ChainFailure::ChainNotBuilt;
        report.error = chainFailureText(report.failure);
        report.summary =
            chainVerdictSummary(report.ok, report.trusted, report.revoked, report.expired);
        return report;
    }

    // 信任判定：基础链策略（与 SignatureInspector 用的是同一条）。
    CERT_CHAIN_POLICY_STATUS policyStatus;
    std::memset(&policyStatus, 0, sizeof(policyStatus));
    policyStatus.cbSize = sizeof(policyStatus);
    CERT_CHAIN_POLICY_PARA policyPara;
    std::memset(&policyPara, 0, sizeof(policyPara));
    policyPara.cbSize = sizeof(policyPara);
    const bool policyVerified =
        CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain.get(), &policyPara,
                                         &policyStatus) != FALSE;
    const bool chainTrusted = policyVerified && policyStatus.dwError == 0;

    const qint64 nowMilliseconds = QDateTime::currentMSecsSinceEpoch();
    bool anyRevoked = false;
    bool anyExpired = false;

    // 只看第一条简单链（chain[0] 就是引擎给出的最佳链）。
    // 注意：MinGW 的 wincrypt.h 里 CERT_CHAIN_CONTEXT 是完整定义，
    // 所以这里能直接看 cChain/rgpChain；MSVC 的 SDK 把它藏起来了，
    // 本仓库只用 MinGW 工具链，故按 MinGW 走。
    for (DWORD chainIndex = 0; chainIndex < chain.get()->cChain; ++chainIndex) {
        if (chainIndex > 0) {
            // 只看第一条简单链：链上下文里 chain[0] 才是引擎给出的最佳链。
            break;
        }
        const CERT_SIMPLE_CHAIN* simpleChain = chain.get()->rgpChain[chainIndex];
        if (!simpleChain) {
            continue;
        }
        const ElementLayout layout = detectElementLayout(simpleChain);
        for (DWORD index = 0; index < simpleChain->cElement; ++index) {
            PCCERT_CHAIN_ELEMENT element = chainElementAt(simpleChain, layout, index);
            if (!element || !element->pCertContext) {
                continue;
            }
            const PCCERT_CONTEXT context = element->pCertContext;
            const CERT_INFO* info = context->pCertInfo;
            if (!info) {
                continue;
            }

            ChainCertificateInfo certificate;
            certificate.subject = nameBlobToString(&info->Subject);
            certificate.issuer = nameBlobToString(&info->Issuer);
            certificate.serialNumber = serialNumberToHex(info);
            certificate.notBefore = formatCertificateTime(fileTimeToMilliseconds(info->NotBefore));
            certificate.notAfter = formatCertificateTime(fileTimeToMilliseconds(info->NotAfter));
            certificate.selfSigned = isSelfSigned(info);
            certificate.isRoot = certificate.selfSigned && index + 1 == simpleChain->cElement;
            certificate.trustedBySystem =
                chainTrusted && element->TrustStatus.dwErrorStatus == 0;
            certificate.revocationState = revocationStateFromTrustFlags(
                element->TrustStatus.dwErrorStatus, element->TrustStatus.dwInfoStatus);
            certificate.revocationStatus = revocationStateText(certificate.revocationState);

            const RevocationEndpoints endpoints = readRevocationEndpoints(context);
            QStringList details;
            if (!endpoints.crlDistributionPoints.isEmpty()) {
                details.append(i18n::tr("aipdf::pdf::CertificateChain", "CRL 分发点：%1")
                                   .arg(endpoints.crlDistributionPoints.join(QStringLiteral(", "))));
            }
            if (!endpoints.ocspUrls.isEmpty()) {
                details.append(i18n::tr("aipdf::pdf::CertificateChain", "OCSP 响应地址：%1")
                                   .arg(endpoints.ocspUrls.join(QStringLiteral(", "))));
            }
            if (!endpoints.caIssuerUrls.isEmpty()) {
                details.append(i18n::tr("aipdf::pdf::CertificateChain", "CA 吊销地址：%1")
                                   .arg(endpoints.caIssuerUrls.join(QStringLiteral(", "))));
            }
            if (endpoints.ocspNoCheck) {
                details.append(i18n::tr("aipdf::pdf::CertificateChain", "证书声明无需检查 OCSP。"));
            }
            certificate.revocationDetail = details.join(QStringLiteral("；"));

            if (context->pbCertEncoded && context->cbCertEncoded > 0) {
                certificate.der = QByteArray(reinterpret_cast<const char*>(context->pbCertEncoded),
                                             static_cast<int>(context->cbCertEncoded));
            }

            // 有效期：不在有效期内（过期或尚未生效）都算「有效期有问题」。
            const qint64 notBefore = fileTimeToMilliseconds(info->NotBefore);
            const qint64 notAfter = fileTimeToMilliseconds(info->NotAfter);
            if ((element->TrustStatus.dwErrorStatus & kTrustIsNotTimeValid) != 0 ||
                nowMilliseconds > notAfter || nowMilliseconds < notBefore) {
                anyExpired = true;
            }
            if (certificate.revocationState == RevocationState::Revoked) {
                anyRevoked = true;
            }

            report.certificates.append(certificate);
        }
    }

    report.ok = true;
    report.trusted = chainTrusted;
    report.revoked = anyRevoked;
    report.expired = anyExpired;
    if (!chainTrusted) {
        // 链建起来了但没通过基础策略：分清「根不受信任」与「有效期有问题」。
        report.failure = ChainFailure::NotTrusted;
        report.error = chainFailureText(report.failure);
    }
    report.summary = chainVerdictSummary(report.ok, report.trusted, report.revoked, report.expired);
    return report;
#endif  // Q_OS_WIN
}

// ------------------------------------------------------------------ 桥接 ----

QVector<QByteArray> signatureCertificateDer(const SignatureInfo& info) {
    QVector<QByteArray> result;

#ifndef Q_OS_WIN
    return result;
#else
    if (info.pkcs7.isEmpty()) {
        return result;
    }

    // CERT_STORE_PROV_PKCS7 这个 provider 不会自己去读文件：
    // PKCS#7 的字节必须通过 pvPara 传进去（传 nullptr 会在 crypt32 内部空指针崩溃）。
    CRYPT_DATA_BLOB blob;
    blob.cbData = static_cast<DWORD>(info.pkcs7.size());
    blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(info.pkcs7.constData()));
    StoreHandle store(CertOpenStore(CERT_STORE_PROV_PKCS7,
                                     X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, &blob));
    if (!store.get()) {
        return result;
    }

    // 枚举会把上一个上下文释放掉，所以每张都复制一份留着。
    CertificateList collected;
    PCCERT_CONTEXT previous = nullptr;
    PCCERT_CONTEXT found = nullptr;
    while ((found = CertFindCertificateInStore(store.get(), X509_ASN_ENCODING, 0, CERT_FIND_ANY,
                                               nullptr, previous)) != nullptr) {
        collected.append(CertDuplicateCertificateContext(found));
        previous = found;
    }

    const int leafIndex = findLeafCertificateIndex(collected.contexts(), info);
    const QVector<PCCERT_CONTEXT> ordered = orderChainCertificates(collected.contexts(), leafIndex);
    for (PCCERT_CONTEXT context : ordered) {
        if (!context || !context->pbCertEncoded || context->cbCertEncoded == 0) {
            continue;
        }
        result.append(QByteArray(reinterpret_cast<const char*>(context->pbCertEncoded),
                                  static_cast<int>(context->cbCertEncoded)));
    }
    return result;
#endif  // Q_OS_WIN
}

}  // namespace aipdf::pdf
