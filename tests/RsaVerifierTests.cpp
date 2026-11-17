// RSA 公钥验签（PKCS#1 v1.5）的专项单测。
//
// 测试向量是仓库夹具 tests/fixtures/signed/signed-sample.p7 里的**真实数据**：
// 那份签名由 cryptography 独立验证过是有效的，所以这里既能当正向用例，
// 也能保证「我们的实现与主流实现对同一份数据给出同一结论」。
//
// 这组测试的立场：验签是安全边界，**判错的代价远高于漏判**。
// 所以负例（数据被改过一个字节、密钥不匹配、长度不符）必须全部挡住，
// 而且失败原因要能看出是哪一环断了。

#include <QtTest>

#include "pdf/RsaVerifier.h"

using namespace aipdf::pdf;

namespace {

// SHA-256 的 OID。
const char* const kSha256Oid = "2.16.840.1.101.3.4.2.1";

QByteArray fromHex(const char* text) {
    return QByteArray::fromHex(QByteArray(text));
}

// 夹具里的真实公钥与签名。
QByteArray fixtureModulus() {
    return fromHex("d139f12c178df8be3de4698e75d4404472a5cee1cab1ecb331af522d61f59970"
                   "b8091b1f401318c29565252f8e3e3f5d23e4999b330b43fa3756a36843400c411"
                   "051f93db63903aef8fcadc1d40f09c33ca29629dbac948839d0633d0b3971e066"
                   "49a4939282fc1b82d7760bf784136294a9397ed11e2758c1181d305d1c76676300"
                   "2230d2d6639ffd3cf9e49158d4694f5f94414045b8da18dff41f27adfecefea3f"
                   "a78c02c6c8c9a73adda8c43e1a7495bb946c6423c94e3307ff55c729bcec9992b"
                   "d2d8eedb3d803f3a127e15721b6b97e6451b6077080914d738715eca8a5662c54"
                   "529099841d6c09da9f45c47db6df892e3078f971e93143c0cd433d845");
}

QByteArray fixtureExponent() {
    return fromHex("010001");
}

QByteArray fixtureDigest() {
    return fromHex("829b6591329546324ec49e885b681170ea8c4b410d303d9ccaa38f40bc2b5f95");
}

QByteArray fixtureSignature() {
    return fromHex("54a650c0a8e856d190d69e40fe7a26f58b6677653152b444e1e335ae8b75074"
                   "02c3c214e63488056d0998cbc0297eb232d4c300f36488c6b17b3d805fe642cd2"
                   "63cb0f5a23663576ca36930c9d931e8759d4fa0c0fe1fd73f5076f61f6277661e"
                   "86c3f34f4e80e69a4dc58d8afd93f05d8b6d577e5cdea6125e664d30e74414ea"
                   "3e6d9b3af77ff1955a2f84932e3f2820baecffb1cc0aaa7ae9240adca7e70f55f"
                   "7618e5108145bea1d508e39bb6e4dcc89e9f25b40321724378840c49abd0329a9"
                   "35fea7c1f6feb768ad39e32e411bed896842ef848da174d4b94559f954a22967c"
                   "325e72f456f9b2e2c2a351284737823ee091e603e76315cd676f4665fe5f");
}

// 把一个字节的第 index 位翻转，用来制造"只改了一个字节"的数据。
QByteArray flipBit(QByteArray bytes, int index) {
    if (index < 0 || index >= bytes.size()) {
        return bytes;
    }
    bytes[index] = static_cast<char>(bytes.at(index) ^ 0x01);
    return bytes;
}

}  // namespace

class RsaVerifierTests : public QObject {
    Q_OBJECT

private slots:
    void fixtureSignatureVerifies();
    void reasonIsEmptyOnSuccess();
    void tamperedDigestFails();
    void tamperedSignatureFails();
    void wrongKeyFails();
    void unknownAlgorithmFails();
    void emptyModulusFails();
    void emptyExponentFails();
    void emptyDigestFails();
    void emptySignatureFails();
    void lengthMismatchFails();
    void signatureNotSmallerThanModulusFails();
    void degenerateModulusDoesNotCrash();
    void sha1VectorIsRejectedBySha256Expectation();
};

void RsaVerifierTests::fixtureSignatureVerifies() {
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(fixtureModulus(), fixtureExponent(), fixtureDigest(),
                                            QString::fromLatin1(kSha256Oid), fixtureSignature(),
                                            &reason);

    QVERIFY2(ok, qPrintable(reason));
}

void RsaVerifierTests::reasonIsEmptyOnSuccess() {
    QString reason = QStringLiteral("残留");
    const bool ok = verifyRsaPkcs1Signature(fixtureModulus(), fixtureExponent(), fixtureDigest(),
                                            QString::fromLatin1(kSha256Oid), fixtureSignature(),
                                            &reason);

    QVERIFY(ok);
    // 成立时不该留失败原因，否则界面会把"通过"和"有话要说"混在一起显示。
    QVERIFY(reason.isEmpty());
}

void RsaVerifierTests::tamperedDigestFails() {
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(
        fixtureModulus(), fixtureExponent(), flipBit(fixtureDigest(), 5),
        QString::fromLatin1(kSha256Oid), fixtureSignature(), &reason);

    QVERIFY(!ok);
    QVERIFY(!reason.isEmpty());
}

void RsaVerifierTests::tamperedSignatureFails() {
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(
        fixtureModulus(), fixtureExponent(), fixtureDigest(), QString::fromLatin1(kSha256Oid),
        flipBit(fixtureSignature(), 100), &reason);

    QVERIFY(!ok);
    QVERIFY(!reason.isEmpty());
}

void RsaVerifierTests::wrongKeyFails() {
    // 用另一把"公钥"（把模数改一位）去验：签名长度对得上，但解出来的块结构不对。
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(
        flipBit(fixtureModulus(), 10), fixtureExponent(), fixtureDigest(),
        QString::fromLatin1(kSha256Oid), fixtureSignature(), &reason);

    QVERIFY(!ok);
    QVERIFY(!reason.isEmpty());
}

void RsaVerifierTests::unknownAlgorithmFails() {
    // 摘要算法对不上：DigestInfo 里带的 OID 不一样，验签必须不成立。
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(fixtureModulus(), fixtureExponent(), fixtureDigest(),
                                            QStringLiteral("1.2.3.4.5"), fixtureSignature(), &reason);

    QVERIFY(!ok);
    QVERIFY(!reason.isEmpty());
}

void RsaVerifierTests::emptyModulusFails() {
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(QByteArray(), fixtureExponent(), fixtureDigest(),
                                            QString::fromLatin1(kSha256Oid), fixtureSignature(),
                                            &reason);

    QVERIFY(!ok);
    QVERIFY(!reason.isEmpty());
}

void RsaVerifierTests::emptyExponentFails() {
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(fixtureModulus(), QByteArray(), fixtureDigest(),
                                            QString::fromLatin1(kSha256Oid), fixtureSignature(),
                                            &reason);

    QVERIFY(!ok);
    QVERIFY(!reason.isEmpty());
}

void RsaVerifierTests::emptyDigestFails() {
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(fixtureModulus(), fixtureExponent(), QByteArray(),
                                            QString::fromLatin1(kSha256Oid), fixtureSignature(),
                                            &reason);

    QVERIFY(!ok);
    QVERIFY(!reason.isEmpty());
}

void RsaVerifierTests::emptySignatureFails() {
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(fixtureModulus(), fixtureExponent(), fixtureDigest(),
                                            QString::fromLatin1(kSha256Oid), QByteArray(), &reason);

    QVERIFY(!ok);
    QVERIFY(!reason.isEmpty());
}

void RsaVerifierTests::lengthMismatchFails() {
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(
        fixtureModulus(), fixtureExponent(), fixtureDigest(), QString::fromLatin1(kSha256Oid),
        fixtureSignature().left(200), &reason);

    QVERIFY(!ok);
    QVERIFY(!reason.isEmpty());
}

void RsaVerifierTests::signatureNotSmallerThanModulusFails() {
    // 签名值 >= 模数：这不是合法的 RSA 签名（真签名的数值必然小于模数）。
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(
        fixtureModulus(), fixtureExponent(), fixtureDigest(), QString::fromLatin1(kSha256Oid),
        fixtureModulus(), &reason);

    QVERIFY(!ok);
    QVERIFY(!reason.isEmpty());
}

void RsaVerifierTests::degenerateModulusDoesNotCrash() {
    // 退化输入（模数是 0 或 1）不能崩，也不能判成通过。
    QString reason;
    QVERIFY(!verifyRsaPkcs1Signature(fromHex("00"), fixtureExponent(), fixtureDigest(),
                                     QString::fromLatin1(kSha256Oid), fixtureSignature(), &reason));
    reason.clear();
    QVERIFY(!verifyRsaPkcs1Signature(fromHex("01"), fixtureExponent(), fixtureDigest(),
                                     QString::fromLatin1(kSha256Oid), fixtureSignature(), &reason));
    reason.clear();
    QVERIFY(!verifyRsaPkcs1Signature(fixtureModulus(), fromHex("00"), fixtureDigest(),
                                     QString::fromLatin1(kSha256Oid), fixtureSignature(), &reason));
}

void RsaVerifierTests::sha1VectorIsRejectedBySha256Expectation() {
    // 同一份签名，用 SHA-1 的 OID 去期望，DigestInfo 对不上 → 必须判不成立。
    // 这一条专门防"摘要算法只看名字不看内容"的实现。
    QString reason;
    const bool ok = verifyRsaPkcs1Signature(fixtureModulus(), fixtureExponent(), fixtureDigest(),
                                            QStringLiteral("1.3.14.3.2.26"), fixtureSignature(),
                                            &reason);

    QVERIFY(!ok);
    QVERIFY(!reason.isEmpty());
}

QTEST_GUILESS_MAIN(RsaVerifierTests)

#include "RsaVerifierTests.moc"
