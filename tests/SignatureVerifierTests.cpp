// 签名密码学闭环（RSA 验签 / RFC 3161 时间戳 / OCSP 编解码）的专项单测。
//
// 这一层是纯领域层：验签走 Win32 的 BCrypt，证书解析走 Crypt32，
// 不依赖 PDFium，qpdf 是可选依赖（有就编进来，没有就走纯字节路径）。
//
// 测试的立场：**「验不出来」不等于「验签通过」**。
// 所以每个失败分支都要检查 failureReason 非空，而且失败原因要能看出是哪一环断了。

#include <QtTest>

#include <QDir>
#include <QFile>

#include "pdf/security/SignatureVerifier.h"

using namespace aipdf::pdf;

namespace {

// 夹具目录在编译期写死：夹具是提交进仓库的，CI 上永远有数据可读。
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

// ---------------------------------------------------------- 测试用 DER ----
//
// OCSP 的正例必须有一个**结构正确的响应**才能验。
// 这里手写一个最小但合法的 OCSP 响应（good 状态），
// 而不是拿一段字节硬塞——那样测的就不是解析器而是"字节碰巧能过"。
QByteArray derLength(int length) {
    QByteArray out;
    if (length < 0x80) {
        out.append(static_cast<char>(length));
        return out;
    }
    QByteArray raw;
    int value = length;
    while (value > 0) {
        raw.prepend(static_cast<char>(value & 0xff));
        value >>= 8;
    }
    out.append(static_cast<char>(0x80 | raw.size()));
    out.append(raw);
    return out;
}

QByteArray derTlvTest(int tag, const QByteArray& value) {
    QByteArray out;
    out.append(static_cast<char>(tag));
    out.append(derLength(value.size()));
    out.append(value);
    return out;
}

QByteArray derSeq(const QVector<QByteArray>& items) {
    QByteArray body;
    for (const QByteArray& item : items) {
        body.append(item);
    }
    return derTlvTest(0x30, body);
}

QByteArray derSet(const QVector<QByteArray>& items) {
    QByteArray body;
    for (const QByteArray& item : items) {
        body.append(item);
    }
    return derTlvTest(0x31, body);
}

// 点分十进制 OID → DER 的 OID TLV。
QByteArray derOid(const QString& dotted) {
    const QStringList parts = dotted.split(QLatin1Char('.'));
    QByteArray content;
    if (parts.size() >= 2) {
        const int first = parts.at(0).toInt();
        const int second = parts.at(1).toInt();
        content.append(static_cast<char>(40 * first + second));
        for (int i = 2; i < parts.size(); ++i) {
            int arc = parts.at(i).toInt();
            QByteArray arcBytes;
            arcBytes.append(static_cast<char>(arc & 0x7f));
            arc >>= 7;
            while (arc > 0) {
                arcBytes.prepend(static_cast<char>((arc & 0x7f) | 0x80));
                arc >>= 7;
            }
            content.append(arcBytes);
        }
    }
    return derTlvTest(0x06, content);
}

QByteArray derOctet(const QByteArray& value) {
    return derTlvTest(0x04, value);
}

QByteArray derGenTime(const QByteArray& text) {
    return derTlvTest(0x18, text);
}

QByteArray derEnum(int value) {
    QByteArray body;
    body.append(static_cast<char>(value & 0xff));
    return derTlvTest(0x0a, body);
}

// 造一个 status = good 的 OCSP 响应（DER）。
QByteArray buildGoodOcspResponse() {
    // CertID ::= SEQUENCE { algId, issuerNameHash, issuerKeyHash, serialNumber }
    const QByteArray algId = derSeq({derOid(QStringLiteral("1.3.14.3.2.26")), derTlvTest(0x05, QByteArray())});
    const QByteArray certId = derSeq({algId, derOctet(QByteArray(20, '\x11')),
                                       derOctet(QByteArray(20, '\x22')),
                                       derTlvTest(0x02, QByteArray::fromHex("0102"))});

    // SingleResponse ::= SEQUENCE { certID, certStatus, thisUpdate, nextUpdate [0] }
    const QByteArray good = derTlvTest(0x80, QByteArray());  // good [0] IMPLICIT NULL
    const QByteArray single = derSeq({certId, good,
                                       derGenTime(QByteArray("20240102030405Z")),
                                       derTlvTest(0xa0, derGenTime(QByteArray("20240103030405Z")))});

    // responderID byName [1]：内容是一段 RDNSequence 的**内容**（不含外层 SEQUENCE 标签）。
    const QByteArray rdn = derSet({derSeq({derOid(QStringLiteral("2.5.4.3")),
                                            derTlvTest(0x0c, QByteArray("Test OCSP Responder"))})});
    const QByteArray responderId = derTlvTest(0xa1, rdn);

    // ResponseData ::= SEQUENCE { responderID, producedAt, responses }
    const QByteArray responseData = derSeq({responderId,
                                             derGenTime(QByteArray("20240102030405Z")),
                                             derSeq({single})});

    // BasicOCSPResponse ::= SEQUENCE { tbsResponseData, ... }
    const QByteArray basic = derSeq({responseData});

    // ResponseBytes ::= SEQUENCE { responseType, response OCTET STRING }
    const QByteArray responseBytes = derSeq({derOid(QStringLiteral("1.3.6.1.5.5.7.48.1.1")),
                                              derOctet(basic)});

    // OCSPResponse ::= SEQUENCE { responseStatus, responseBytes [0] EXPLICIT }
    return derSeq({derEnum(0), derTlvTest(0xa0, responseBytes)});
}

}  // namespace

class SignatureVerifierTests : public QObject {
    Q_OBJECT

private slots:
    void digestOidMapsKnownAlgorithms();
    void digestOidReturnsEmptyForUnknown();
    void signedSampleVerifies();
    void tamperedSampleFailsWithReason();
    void signerCertificateCanBeExtracted();
    void verifyFailsOnGarbageInput();
    void verifyPkcs7RejectsMismatchedBytes();
    void timestampTokenRejectsGarbage();
    void ocspRequestRoundTrips();
    void ocspRequestRejectsEmptyInput();
    void ocspResponseParsesGoodStatus();
    void ocspResponseRejectsGarbage();
    void ocspResponseRejectsServerError();
};

void SignatureVerifierTests::digestOidMapsKnownAlgorithms() {
    QCOMPARE(digestAlgorithmNameFromOid(QStringLiteral("2.16.840.1.101.3.4.2.1")),
             QStringLiteral("SHA-256"));
    QCOMPARE(digestAlgorithmNameFromOid(QStringLiteral("1.3.14.3.2.26")), QStringLiteral("SHA-1"));
    QCOMPARE(digestAlgorithmNameFromOid(QStringLiteral("2.16.840.1.101.3.4.2.2")),
             QStringLiteral("SHA-384"));
}

void SignatureVerifierTests::digestOidReturnsEmptyForUnknown() {
    // 空串意味着「不能判」，不是「判成通过」。
    QVERIFY(digestAlgorithmNameFromOid(QStringLiteral("1.2.3.4.5")).isEmpty());
    QVERIFY(digestAlgorithmNameFromOid(QString()).isEmpty());
}

void SignatureVerifierTests::signedSampleVerifies() {
    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QVERIFY2(!bytes.isEmpty(), "缺少签名夹具 signed-sample.pdf");

    QString error;
    const QVector<SignatureCryptoVerdict> verdicts = verifyDocumentSignatures(bytes, &error);
    QVERIFY2(!verdicts.isEmpty(), qPrintable(QStringLiteral("没有解析出签名：%1").arg(error)));
    QCOMPARE(verdicts.size(), 1);

    const SignatureCryptoVerdict& verdict = verdicts.first();
    QVERIFY2(verdict.signatureValid,
             qPrintable(QStringLiteral("验签没通过：%1").arg(verdict.failureReason)));
    QVERIFY(verdict.hashAlgorithmKnown);
    QVERIFY(verdict.digestMatched);
    QCOMPARE(verdict.algorithm, QStringLiteral("SHA-256"));
    QVERIFY(!verdict.signerSubject.isEmpty());
    // 成立时不该留失败原因。
    QVERIFY(verdict.failureReason.isEmpty());
}

void SignatureVerifierTests::tamperedSampleFailsWithReason() {
    const QByteArray bytes = readFixture(QStringLiteral("signed-sample-tampered.pdf"));
    QVERIFY2(!bytes.isEmpty(), "缺少签名夹具 signed-sample-tampered.pdf");

    QString error;
    const QVector<SignatureCryptoVerdict> verdicts = verifyDocumentSignatures(bytes, &error);
    QVERIFY(!verdicts.isEmpty());
    QVERIFY(!verdicts.first().signatureValid);
    // 失败必须说清为什么，否则「验不出来」会被当成「验签通过」。
    QVERIFY(!verdicts.first().failureReason.isEmpty());
}

void SignatureVerifierTests::signerCertificateCanBeExtracted() {
    const QByteArray pkcs7 = readFixture(QStringLiteral("signed-sample.p7"));
    QVERIFY2(!pkcs7.isEmpty(), "缺少签名夹具 signed-sample.p7");

    QString error;
    const QByteArray certificate = signerCertificateDerFromPkcs7(pkcs7, &error);
    QVERIFY2(!certificate.isEmpty(), qPrintable(error));
    // DER 证书一定以 SEQUENCE 开头。
    QCOMPARE(static_cast<quint8>(certificate.at(0)), quint8(0x30));
}

void SignatureVerifierTests::verifyFailsOnGarbageInput() {
    QString error;
    const QVector<SignatureCryptoVerdict> verdicts =
        verifyDocumentSignatures(QByteArray("this is definitely not a pdf"), &error);
    // 空数组 + 中文说明是可以接受的；但**绝不能**凭空造出一个"通过"的判定。
    QVERIFY(verdicts.isEmpty());
    QVERIFY(!error.isEmpty());
}

void SignatureVerifierTests::verifyPkcs7RejectsMismatchedBytes() {
    const QByteArray pkcs7 = readFixture(QStringLiteral("signed-sample.p7"));
    QVERIFY2(!pkcs7.isEmpty(), "缺少签名夹具 signed-sample.p7");

    SignatureCryptoVerdict verdict;
    QString error;
    const bool ok = verifyPkcs7Signature(pkcs7, QByteArray("definitely not the signed bytes"),
                                          QByteArray(), &verdict, &error);
    QVERIFY(!ok);
    QVERIFY(!verdict.signatureValid);
    QVERIFY(!verdict.failureReason.isEmpty());
}

void SignatureVerifierTests::timestampTokenRejectsGarbage() {
    QString timestamp;
    QString authority;
    QString error;
    const bool ok = parseTimestampToken(QByteArray("not a token"), &timestamp, &authority, &error);

    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QVERIFY(timestamp.isEmpty());
}

void SignatureVerifierTests::ocspRequestRoundTrips() {
    const QByteArray pkcs7 = readFixture(QStringLiteral("signed-sample.p7"));
    QVERIFY2(!pkcs7.isEmpty(), "缺少签名夹具 signed-sample.p7");

    QString error;
    const QByteArray certificate = signerCertificateDerFromPkcs7(pkcs7, &error);
    QVERIFY2(!certificate.isEmpty(), qPrintable(error));

    // 夹具里的证书是自签名的，所以它既是签发者也是被查者。
    const QByteArray request = buildOcspRequest(certificate, certificate, &error);
    QVERIFY2(!request.isEmpty(), qPrintable(error));

    OcspRequestInfo info;
    QVERIFY2(parseOcspRequest(request, &info, &error), qPrintable(error));
    QCOMPARE(info.hashAlgorithm, QStringLiteral("SHA-1"));
    QCOMPARE(info.issuerNameHash.size(), 20);
    QCOMPARE(info.issuerKeyHash.size(), 20);
    QVERIFY(!info.serialNumber.isEmpty());
}

void SignatureVerifierTests::ocspRequestRejectsEmptyInput() {
    QString error;
    QVERIFY(buildOcspRequest(QByteArray(), QByteArray(), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(buildOcspRequest(QByteArray("garbage"), QByteArray("garbage"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void SignatureVerifierTests::ocspResponseParsesGoodStatus() {
    OcspResponseInfo info;
    QString error;
    const bool ok = parseOcspResponse(buildGoodOcspResponse(), &info, &error);

    QVERIFY2(ok, qPrintable(error));
    QCOMPARE(info.status, OcspStatus::Good);
    QVERIFY(info.thisUpdateValid);
    QVERIFY(!info.thisUpdate.isEmpty());
    QVERIFY(info.nextUpdateValid);
    QVERIFY(!info.nextUpdate.isEmpty());
    QVERIFY(!info.producedAt.isEmpty());
}

void SignatureVerifierTests::ocspResponseRejectsGarbage() {
    OcspResponseInfo info;
    QString error;
    const bool ok = parseOcspResponse(QByteArray("not an ocsp response"), &info, &error);

    QVERIFY(!ok);
    QCOMPARE(info.status, OcspStatus::Malformed);
    QVERIFY(!error.isEmpty());
}

void SignatureVerifierTests::ocspResponseRejectsServerError() {
    // responseStatus = 2（internalError）：服务器明确说这次没查成。
    // 这既不是"状态良好"也不是"被吊销"，只能算 Unknown。
    const QByteArray response = derSeq({derEnum(2)});

    OcspResponseInfo info;
    QString error;
    const bool ok = parseOcspResponse(response, &info, &error);

    QVERIFY(!ok);
    QCOMPARE(info.status, OcspStatus::Unknown);
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(SignatureVerifierTests)

#include "SignatureVerifierTests.moc"
