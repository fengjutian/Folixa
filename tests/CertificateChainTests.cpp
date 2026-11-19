// 证书链与撤销状态检查的专项单测。
//
// 这组测试的立场：**撤销状态是安全结论，判错的代价远高于漏判**。
// 所以「查不到」必须说成「状态未知」，绝不能说成「未被吊销」——
// 那是把「没查出来」冒充成「查过了、没问题」。
// 这一条由纯逻辑用例（pureRevocationStateFromTrustFlags）与真链用例
// （realChainFromFixtureReportsRevocationState）共同盯着。
//
// 另外三条不变量各自都有对应的负例对照：
//   * 根不受信任时必须报 trusted=false；
//   * 撤销状态「未知」不能被当成「未被吊销」；
//   * 任何早退分支都不能漏释放 Crypt32 句柄（用句柄计数盯着）。

#include <QtTest>

#include "pdf/security/CertificateChain.h"
#include "pdf/security/SignatureInspector.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

using namespace aipdf::pdf;

namespace {

QString fixtureDir() {
    return QStringLiteral(AIPDF_SIGNATURE_FIXTURES_DIR);
}

QByteArray readFixture(const QString& name) {
    QFile file(QDir(fixtureDir()).filePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

// 夹具里的签名者证书（自签测试证书）DER。
//
// 走的是本模块自己的桥接函数——顺带就把它测了。
QByteArray fixtureSignerDer(bool* available) {
    *available = false;
    const QByteArray pkcs7 = readFixture(QStringLiteral("signed-sample.p7"));
    if (pkcs7.isEmpty()) {
        return QByteArray();
    }
    SignatureInfo info;
    info.pkcs7 = pkcs7;
    const QVector<QByteArray> ders = signatureCertificateDer(info);
    if (ders.isEmpty()) {
        return QByteArray();
    }
    *available = true;
    return ders.first();
}

#ifdef Q_OS_WIN
// 证书是不是自签（主体 == 颁发者）。
bool isSelfSignedDer(const QByteArray& der) {
    if (der.isEmpty()) {
        return false;
    }
    HCERTSTORE store =
        CertOpenStore(CERT_STORE_PROV_MEMORY, X509_ASN_ENCODING, 0, 0, nullptr);
    if (!store) {
        return false;
    }
    PCCERT_CONTEXT stored = nullptr;
    if (!CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING,
                                          reinterpret_cast<const BYTE*>(der.constData()),
                                          static_cast<DWORD>(der.size()), CERT_STORE_ADD_ALWAYS,
                                          &stored) ||
        !stored) {
        CertCloseStore(store, 0);
        return false;
    }
    const CERT_INFO* info = stored->pCertInfo;
    const bool selfSigned = info && info->Subject.cbData == info->Issuer.cbData &&
                            info->Subject.cbData > 0 &&
                            memcmp(info->Subject.pbData, info->Issuer.pbData,
                                   info->Subject.cbData) == 0;
    CertCloseStore(store, 0);
    return selfSigned;
}
#endif

}  // namespace

class CertificateChainTests : public QObject {
    Q_OBJECT

private slots:
    // ---- 纯逻辑：撤销状态三态 ----
    void pureRevocationStateTextHasThreeDistinctValues();
    void pureRevocationStateFromTrustFlagsKeepsUnknownUnknown();

    // ---- 纯逻辑：结论三态与失败分类 ----
    void pureChainVerdictSummarySeparatesConclusions();
    void pureChainFailureTextSeparatesCauses();

    // ---- 纯逻辑：时间格式 ----
    void pureFormatCertificateTimeUsesLocalTime();

    // ---- 错误分类 ----
    void emptyDerReportsUnparsable();
    void garbageDerReportsUnparsable();

    // ---- 句柄成对释放 ----
    void chainInspectionLeavesNoLiveHandles();

    // ---- 真链（夹具缺失就跳过）----
    void realChainFromFixtureReportsRevocationState();
    void realChainReportsUntrustedRootForSelfSignedFixture();

    // ---- 桥接：从签名里取证书 DER ----
    void signatureCertificateDerPutsLeafFirst();
    void signatureCertificateDerReturnsEmptyOnBadInput();
};

// ------------------------------------------------------------ 撤销状态 ----

void CertificateChainTests::pureRevocationStateTextHasThreeDistinctValues() {
    const QString notRevoked = revocationStateText(RevocationState::NotRevoked);
    const QString revoked = revocationStateText(RevocationState::Revoked);
    const QString unknown = revocationStateText(RevocationState::Unknown);

    QVERIFY2(!notRevoked.isEmpty(), "撤销状态必须有文案");
    QVERIFY2(!revoked.isEmpty(), "撤销状态必须有文案");
    QVERIFY2(!unknown.isEmpty(), "撤销状态必须有文案");

    // 三态必须互相不同——混成两态就等于把「未知」说成了「没被吊销」。
    QVERIFY2(notRevoked != revoked, "「未被吊销」与「已被吊销」不能是同一句");
    QVERIFY2(notRevoked != unknown, "「未被吊销」与「状态未知」不能是同一句");
    QVERIFY2(revoked != unknown, "「已被吊销」与「状态未知」不能是同一句");
}

void CertificateChainTests::pureRevocationStateFromTrustFlagsKeepsUnknownUnknown() {
    // 与 wincrypt.h 的 CERT_TRUST_* 位一致（见 CertificateChain.cpp）。
    constexpr quint32 kTrustIsNotTimeValid = 0x00000001u;
    constexpr quint32 kTrustIsRevoked = 0x00000004u;
    constexpr quint32 kTrustRevocationStatusUnknown = 0x00000040u;
    constexpr quint32 kTrustIsOfflineRevocation = 0x01000000u;
    constexpr quint32 kTrustAutoUpdateCaRevocation = 0x00000010u;
    constexpr quint32 kTrustAutoUpdateEndRevocation = 0x00000020u;

    // 明确被吊销。
    QCOMPARE(static_cast<int>(revocationStateFromTrustFlags(kTrustIsRevoked, 0)),
             static_cast<int>(RevocationState::Revoked));

    // 状态未知（引擎说不知道 / 吊销服务离线）。
    QCOMPARE(static_cast<int>(revocationStateFromTrustFlags(kTrustRevocationStatusUnknown, 0)),
             static_cast<int>(RevocationState::Unknown));
    QCOMPARE(static_cast<int>(revocationStateFromTrustFlags(kTrustIsOfflineRevocation, 0)),
             static_cast<int>(RevocationState::Unknown));

    // 引擎确实取到了吊销信息 → 这时候「未被吊销」才是有证据的结论。
    QCOMPARE(static_cast<int>(revocationStateFromTrustFlags(0, kTrustAutoUpdateCaRevocation)),
             static_cast<int>(RevocationState::NotRevoked));
    QCOMPARE(static_cast<int>(revocationStateFromTrustFlags(0, kTrustAutoUpdateEndRevocation)),
             static_cast<int>(RevocationState::NotRevoked));

    // 关键不变量：没有任何证据时（只有无关的时间错误位）必须判「未知」，
    // 绝不能判成「未被吊销」。
    QCOMPARE(static_cast<int>(revocationStateFromTrustFlags(0, 0)),
             static_cast<int>(RevocationState::Unknown));
    QCOMPARE(static_cast<int>(revocationStateFromTrustFlags(kTrustIsNotTimeValid, 0)),
             static_cast<int>(RevocationState::Unknown));

    // 三态映射到三句不同的文案。
    QVERIFY2(revocationStateText(revocationStateFromTrustFlags(0, 0)) !=
                 revocationStateText(revocationStateFromTrustFlags(0, kTrustAutoUpdateCaRevocation)),
             "「没有证据」与「取到吊销信息」不能给出同一句文案");
}

// -------------------------------------------------------------- 结论 ----

void CertificateChainTests::pureChainVerdictSummarySeparatesConclusions() {
    const QString ok = chainVerdictSummary(true, true, false, false);
    const QString notTrusted = chainVerdictSummary(true, false, false, false);
    const QString revoked = chainVerdictSummary(true, true, true, false);
    const QString expired = chainVerdictSummary(true, true, false, true);
    const QString broken = chainVerdictSummary(false, false, false, false);

    QVERIFY2(!ok.isEmpty(), "必须给一行结论");
    QVERIFY2(ok != notTrusted, "「受信任」与「不受信任」不能是同一句");
    QVERIFY2(ok != revoked, "「正常」与「被吊销」不能是同一句");
    QVERIFY2(ok != expired, "「正常」与「过期」不能是同一句");
    QVERIFY2(ok != broken, "「链建起来了」与「链建不起来」不能是同一句");
    QVERIFY2(notTrusted != broken, "「不受信任」与「链建不起来」不能是同一句");
}

void CertificateChainTests::pureChainFailureTextSeparatesCauses() {
    const QString unparsable = chainFailureText(ChainFailure::CertificateUnparsable);
    const QString notBuilt = chainFailureText(ChainFailure::ChainNotBuilt);
    const QString notTrusted = chainFailureText(ChainFailure::NotTrusted);

    QVERIFY2(!unparsable.isEmpty(), "失败原因必须有中文文案");
    QVERIFY2(!notBuilt.isEmpty(), "失败原因必须有中文文案");
    QVERIFY2(!notTrusted.isEmpty(), "失败原因必须有中文文案");
    QVERIFY2(unparsable != notBuilt, "「证书解析不了」与「链建不起来」要能分辨");
    QVERIFY2(unparsable != notTrusted, "「证书解析不了」与「不受信任」要能分辨");
    QVERIFY2(notBuilt != notTrusted, "「链建不起来」与「不受信任」要能分辨");
    QVERIFY2(chainFailureText(ChainFailure::None).isEmpty(), "没有失败时不该给失败原因");
}

// -------------------------------------------------------------- 时间 ----

void CertificateChainTests::pureFormatCertificateTimeUsesLocalTime() {
    const qint64 epochMilliseconds = 1700000000000LL; // 2023-11-14 前后
    const QString text = formatCertificateTime(epochMilliseconds);

    QVERIFY2(!text.isEmpty(), "时间必须能格式化出来");
    QVERIFY2(QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$"))
                  .match(text)
                  .hasMatch(),
              qPrintable(QStringLiteral("时间格式不对：%1").arg(text)));
    // 必须是**本地**时间，不是 UTC。
    QCOMPARE(text, QDateTime::fromMSecsSinceEpoch(epochMilliseconds)
                      .toLocalTime()
                      .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}

// ---------------------------------------------------------- 错误分类 ----

void CertificateChainTests::emptyDerReportsUnparsable() {
    const int before = certificateChainLiveHandleCount();
    const CertificateChainReport report = inspectCertificateChain(QByteArray());

    QCOMPARE(report.ok, false);
    QCOMPARE(static_cast<int>(report.failure),
             static_cast<int>(ChainFailure::CertificateUnparsable));
    QVERIFY2(!report.error.isEmpty(), "必须给出中文失败原因");
    QCOMPARE(report.certificates.size(), 0);
    QVERIFY2(!report.summary.isEmpty(), "即使失败也要给一行结论");
    QCOMPARE(certificateChainLiveHandleCount(), before);
}

void CertificateChainTests::garbageDerReportsUnparsable() {
    const int before = certificateChainLiveHandleCount();
    // 不是 DER 的字节：必须判成「证书解析不了」，而且不能崩。
    const CertificateChainReport report =
        inspectCertificateChain(QByteArray("this is definitely not a certificate"));

    QCOMPARE(report.ok, false);
    QCOMPARE(static_cast<int>(report.failure),
             static_cast<int>(ChainFailure::CertificateUnparsable));
    QVERIFY2(!report.error.isEmpty(), "必须给出中文失败原因");
    QCOMPARE(certificateChainLiveHandleCount(), before);
}

// ------------------------------------------------------ 句柄成对释放 ----

void CertificateChainTests::chainInspectionLeavesNoLiveHandles() {
    const int before = certificateChainLiveHandleCount();

    // 早退路径。
    inspectCertificateChain(QByteArray());
    inspectCertificateChain(QByteArray("garbage"));
    // 正常路径。
    bool available = false;
    const QByteArray leaf = fixtureSignerDer(&available);
    if (available) {
        inspectCertificateChain(leaf);
        inspectCertificateChain(leaf, QVector<QByteArray>());
    }

    // 每一条路径都必须把句柄还回去：早退分支最容易漏。
    QCOMPARE(certificateChainLiveHandleCount(), before);
}

// ------------------------------------------------------------- 真链 ----

void CertificateChainTests::realChainFromFixtureReportsRevocationState() {
    bool available = false;
    const QByteArray leaf = fixtureSignerDer(&available);
    if (!available) {
        QSKIP("SKIPPED: 缺少签名夹具 signed-sample.p7（或里面没有证书）");
    }

    const CertificateChainReport report = inspectCertificateChain(leaf);
    QVERIFY2(report.ok, qPrintable(report.error));
    QVERIFY2(!report.certificates.isEmpty(), "链上至少要有一张证书");

    for (const ChainCertificateInfo& certificate : report.certificates) {
        // 每张证书都必须有撤销状态文案——三态里的哪一个都行，
        // 但不能是空的，也不能是「未知」之外的糊弄话。
        QVERIFY2(!certificate.revocationStatus.isEmpty(), "每张证书都要有撤销状态");
        QVERIFY2(certificate.revocationStatus == revocationStateText(RevocationState::NotRevoked) ||
                     certificate.revocationStatus == revocationStateText(RevocationState::Revoked) ||
                     certificate.revocationStatus == revocationStateText(RevocationState::Unknown),
                 qPrintable(QStringLiteral("撤销状态文案不在三态之内：%1")
                                .arg(certificate.revocationStatus)));
        QVERIFY2(!certificate.subject.isEmpty(), "每张证书都要有主体");
        QVERIFY2(!certificate.issuer.isEmpty(), "每张证书都要有颁发者");
        QVERIFY2(!certificate.serialNumber.isEmpty(), "每张证书都要有序列号");
        QVERIFY2(!certificate.notBefore.isEmpty(), "每张证书都要有生效时间");
        QVERIFY2(!certificate.notAfter.isEmpty(), "每张证书都要有失效时间");
        QVERIFY2(!certificate.der.isEmpty(), "每张证书都要带 DER");
    }

    // 夹具里的签名者证书既没有 CRL 分发点也没有 AIA（revocationDetail 为空），
    // 所以它的撤销状态**只能是「未知」**——
    // 绝不能因为「没查出来」就报成「未被吊销」。
    const ChainCertificateInfo& leafCertificate = report.certificates.first();
    QVERIFY2(leafCertificate.revocationDetail.isEmpty(),
             qPrintable(QStringLiteral("夹具证书不该有吊销地址：%1")
                            .arg(leafCertificate.revocationDetail)));
    QCOMPARE(leafCertificate.revocationStatus, revocationStateText(RevocationState::Unknown));
    QVERIFY2(leafCertificate.revocationStatus != revocationStateText(RevocationState::NotRevoked),
             "没有 CRL/OCSP 的证书不能报成「未被吊销」");

    QVERIFY2(!report.summary.isEmpty(), "必须给一行结论");
}

void CertificateChainTests::realChainReportsUntrustedRootForSelfSignedFixture() {
    bool available = false;
    const QByteArray leaf = fixtureSignerDer(&available);
    if (!available) {
        QSKIP("SKIPPED: 缺少签名夹具 signed-sample.p7（或里面没有证书）");
    }
#ifdef Q_OS_WIN
    // 夹具里的签名者证书是自签的，而且没有装进本机的受信任证书存储，
    // 所以整条链**不受信任**。这条是「根不受信任不许报 trusted=true」的锚点。
    QVERIFY2(isSelfSignedDer(leaf), "夹具证书应当是自签的");
#endif
    const CertificateChainReport report = inspectCertificateChain(leaf);
    QVERIFY2(report.ok, qPrintable(report.error));
    QVERIFY2(!report.trusted, "自签且未受信任的根证书不能报成 trusted");
    QCOMPARE(static_cast<int>(report.failure), static_cast<int>(ChainFailure::NotTrusted));
    QVERIFY2(!report.error.isEmpty(), "不受信任必须给出中文原因");
    QVERIFY2(!report.revoked, "夹具证书没有被吊销");
}

// -------------------------------------------------------------- 桥接 ----

void CertificateChainTests::signatureCertificateDerPutsLeafFirst() {
    const QByteArray pkcs7 = readFixture(QStringLiteral("signed-sample.p7"));
    if (pkcs7.isEmpty()) {
        QSKIP("SKIPPED: 缺少签名夹具 signed-sample.p7");
    }

    SignatureInfo info;
    info.pkcs7 = pkcs7;

    const int before = certificateChainLiveHandleCount();
    const QVector<QByteArray> ders = signatureCertificateDer(info);
    // 取证书的过程不能漏句柄。
    QCOMPARE(certificateChainLiveHandleCount(), before);

    QVERIFY2(!ders.isEmpty(), "PKCS#7 里应至少有一张证书");
    for (const QByteArray& der : ders) {
        QVERIFY2(!der.isEmpty(), "每张证书的 DER 都不能为空");
    }

    // 叶子在最前：同一份 DER 建链时，链上的第一张就是它。
    const CertificateChainReport report = inspectCertificateChain(ders.first(), ders.mid(1));
    QVERIFY2(report.ok, qPrintable(report.error));
    QVERIFY2(!report.certificates.isEmpty(), "链上至少要有一张证书");
    QCOMPARE(ders.first(), report.certificates.first().der);

#ifdef Q_OS_WIN
    // 夹具的签名者证书是自签的那张（也就是叶子）。
    QVERIFY2(isSelfSignedDer(ders.first()), "叶子应当是自签的签名者证书");
#endif
}

void CertificateChainTests::signatureCertificateDerReturnsEmptyOnBadInput() {
    const int before = certificateChainLiveHandleCount();

    // 完全没有 pkcs7。
    SignatureInfo empty;
    QVERIFY2(signatureCertificateDer(empty).isEmpty(), "没有 pkcs7 时应返回空数组");

    // pkcs7 是垃圾字节：必须返回空数组，不能崩。
    SignatureInfo garbage;
    garbage.pkcs7 = QByteArray("not a pkcs7 at all");
    QVERIFY2(signatureCertificateDer(garbage).isEmpty(), "垃圾字节应返回空数组");

    QCOMPARE(certificateChainLiveHandleCount(), before);
}

QTEST_MAIN(CertificateChainTests)
#include "CertificateChainTests.moc"
