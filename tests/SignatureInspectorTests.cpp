#include <QtTest>

#include "pdf/security/SignatureInspector.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

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

bool isSha1Hex(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^[0-9A-F]{40}$"));
    return pattern.match(text).hasMatch();
}

bool isReadableDate(const QString& text) {
    static const QRegularExpression pattern(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$"));
    return pattern.match(text).hasMatch();
}

// 本次构建有没有 qpdf。qpdf 是可选依赖。
bool qpdfEnabled() {
#ifdef AIPDF_WITH_QPDF
    return true;
#else
    return false;
#endif
}

// 依赖 qpdf 的用例在没 qpdf 时整体跳过，但要在输出里写明原因——不做静默通过。
#define REQUIRE_QPDF()                                                    \
    do {                                                                   \
        if (!qpdfEnabled()) {                                             \
            qInfo("SKIPPED: 无 qpdf —— 本用例依赖 qpdf，跳过。");         \
            QSKIP("SKIPPED: 无 qpdf");                                   \
        }                                                                  \
    } while (false)

}  // namespace

class SignatureInspectorTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // 负输入：空数据、非 PDF、未签名文档
    void emptyInputReportsError();
    void nonPdfInputReportsError();
    void unsignedPdfReportsNoSignature();
    void negativeGuardUnsignedIsNeverReportedAsSigned();

    // 签名本体
    void signedPdfHasExactlyOneSignature();
    void subFilterIsDetached();
    void byteRangeIsPairedAndSane();
    void coverageIsClean();
    void pkcs7MatchesFixtureFile();
    void fieldNameIsNotEmpty();
    void signingTimeIsReadable();

    // 证书链
    void chainHasAtLeastOneCertificate();
    void leafSubjectContainsCommonName();
    void thumbprintIsSha1Hex();
    void validityPeriodIsReadable();
    void selfSignedFlagMatchesSubjectAndIssuer();

    // 负例守卫：文件被改过必须被抓到
    void tamperedSampleFailsCoverage();
    void negativeGuardTamperedVariantsAlwaysCaught();

    // 缺 qpdf 时的行为
    void missingQpdfBehaviour();

    // 把真实解析结果打出来：格式变化时一眼能看出来
    void dumpParsedDetails();
};

void SignatureInspectorTests::initTestCase() {
    // 夹具是提交进仓库的：缺了就说明有人把仓库改坏了，必须硬失败。
    const QStringList required = {QStringLiteral("signed-sample.pdf"),
                                  QStringLiteral("signed-sample-tampered.pdf"),
                                  QStringLiteral("unsigned-sample.pdf"),
                                  QStringLiteral("signed-sample.p7")};
    for (const QString& name : required) {
        const QString path = QDir(fixtureDir()).filePath(name);
        QVERIFY2(QFileInfo::exists(path), qPrintable(QStringLiteral("缺少夹具：%1").arg(path)));
    }
#ifndef AIPDF_WITH_QPDF
    // 不能静默通过：明确告诉人这批用例是被跳过的。
    qInfo("SKIPPED: 无 qpdf —— 本次构建未启用 AIPDF_WITH_QPDF，依赖 qpdf 的用例全部跳过。");
    qInfo("SKIPPED: 无 qpdf —— 原因：qpdf 是可选依赖，见 scripts/bootstrap-qpdf.ps1。");
#endif
}

void SignatureInspectorTests::emptyInputReportsError() {
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(QByteArray(), &error);
    QVERIFY(signatures.isEmpty());
    QVERIFY2(!error.isEmpty(), "空输入必须给出错误说明");
    QVERIFY2(error.contains(QStringLiteral("空")), qPrintable(error));
}

void SignatureInspectorTests::nonPdfInputReportsError() {
    QString error;
    const QVector<SignatureInfo> signatures =
        inspectSignatures(QByteArray("this is definitely not a pdf file"), &error);
    QVERIFY(signatures.isEmpty());
    QVERIFY2(!error.isEmpty(), "非 PDF 输入必须给出错误说明，不能崩也不能静默");
    QVERIFY2(error.contains(QStringLiteral("不是 PDF")), qPrintable(error));
}

void SignatureInspectorTests::unsignedPdfReportsNoSignature() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("unsigned-sample.pdf"));
    QVERIFY(!bytes.isEmpty());
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QVERIFY(signatures.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("无签名")), qPrintable(error));
}

void SignatureInspectorTests::negativeGuardUnsignedIsNeverReportedAsSigned() {
    REQUIRE_QPDF();

    // 守卫：未签名文档绝不能被误报成有签名（实现退化成「永远返回 true」时这里会炸）。
    const QByteArray bytes = readFixture(QStringLiteral("unsigned-sample.pdf"));
    QVERIFY(!bytes.isEmpty());
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 0);
    QVERIFY2(error.contains(QStringLiteral("无签名")), qPrintable(error));

    // 仓库里另一个现成的未签名文档也过一遍。
    const QByteArray blank =
        readFixture(QStringLiteral("../blank-pages.pdf"));
    if (!blank.isEmpty()) {
        QString blankError;
        const QVector<SignatureInfo> blankSignatures = inspectSignatures(blank, &blankError);
        QCOMPARE(blankSignatures.size(), 0);
        QVERIFY2(blankError.contains(QStringLiteral("无签名")), qPrintable(blankError));
    }
}

void SignatureInspectorTests::signedPdfHasExactlyOneSignature() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QVERIFY(!bytes.isEmpty());
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(signatures.size(), 1);
}

void SignatureInspectorTests::subFilterIsDetached() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    QCOMPARE(signatures.first().subFilter, QStringLiteral("adbe.pkcs7.detached"));
}

void SignatureInspectorTests::byteRangeIsPairedAndSane() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    const QVector<qint64> range = signatures.first().byteRange;
    QVERIFY(!range.isEmpty());
    QCOMPARE(range.size() % 2, 0);
    // 第一段必须从文件开头开始，最后一段必须到文件末尾。
    QCOMPARE(range.first(), 0);
    qint64 end = 0;
    for (int i = 0; i + 1 < range.size(); i += 2) {
        QVERIFY(range.at(i) >= 0);
        QVERIFY(range.at(i + 1) >= 0);
        end = range.at(i) + range.at(i + 1);
    }
    QCOMPARE(end, static_cast<qint64>(bytes.size()));
}

void SignatureInspectorTests::coverageIsClean() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    const SignatureInfo& signature = signatures.first();
    QVERIFY2(signature.coversDocument, qPrintable(signature.coverError));
    QVERIFY2(signature.coverError.isEmpty(), qPrintable(signature.coverError));
}

void SignatureInspectorTests::pkcs7MatchesFixtureFile() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    const QByteArray pkcs7 = signatures.first().pkcs7;
    QVERIFY(!pkcs7.isEmpty());
    // 夹具里的 .p7 就是签名域 /Contents 的原始 DER，两者必须一致。
    QCOMPARE(pkcs7, readFixture(QStringLiteral("signed-sample.p7")));
    // 至少要能解析出一张证书。
    QVERIFY(!signatures.first().chain.isEmpty());
}

void SignatureInspectorTests::fieldNameIsNotEmpty() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    QVERIFY2(!signatures.first().fieldName.isEmpty(), "签名域名字不能为空");
}

void SignatureInspectorTests::signingTimeIsReadable() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    const QString time = signatures.first().signingTime;
    QVERIFY2(!time.isEmpty(), "签名时间不能为空");
    QVERIFY2(isReadableDate(time), qPrintable(time));
}

void SignatureInspectorTests::chainHasAtLeastOneCertificate() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    const QVector<SignerCertificate>& chain = signatures.first().chain;
    QVERIFY(!chain.isEmpty());
    // 叶子在前：叶子自己的序列号应当能对上自己的主体。
    QVERIFY2(!chain.first().subject.isEmpty(), "叶子证书必须有主体");
    QVERIFY2(!chain.first().commonName.isEmpty(), "叶子证书必须有 CN");
}

void SignatureInspectorTests::leafSubjectContainsCommonName() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    const QVector<SignerCertificate>& chain = signatures.first().chain;
    QVERIFY(!chain.isEmpty());
    QVERIFY2(chain.first().subject.contains(QStringLiteral("CN=")),
             qPrintable(chain.first().subject));
    QVERIFY2(!chain.first().issuer.isEmpty(), "签发者不能为空");
    QVERIFY2(!chain.first().issuerCommonName.isEmpty(), "签发者 CN 不能为空");
    QVERIFY2(!chain.first().serialNumber.isEmpty(), "序列号不能为空");
    // 夹具用的序列号是固定的（生成脚本里写死 0x4A3B2C1D0E9F）。
    // 这条断言专门盯字节序：Crypt32 的 CERT_INFO 里序列号是小端存的，
    // 忘了翻回来就会变成 9F0E1D2C3B4A。
    QCOMPARE(chain.first().serialNumber, QStringLiteral("4A3B2C1D0E9F"));
}

void SignatureInspectorTests::thumbprintIsSha1Hex() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    for (const SignerCertificate& certificate : signatures.first().chain) {
        QVERIFY2(isSha1Hex(certificate.thumbprintSha1),
                 qPrintable(QStringLiteral("指纹不是 40 位大写十六进制：%1")
                                .arg(certificate.thumbprintSha1)));
    }
}

void SignatureInspectorTests::validityPeriodIsReadable() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    for (const SignerCertificate& certificate : signatures.first().chain) {
        QVERIFY2(!certificate.notBefore.isEmpty(), "生效时间不能为空");
        QVERIFY2(!certificate.notAfter.isEmpty(), "失效时间不能为空");
        QVERIFY2(isReadableDate(certificate.notBefore), qPrintable(certificate.notBefore));
        QVERIFY2(isReadableDate(certificate.notAfter), qPrintable(certificate.notAfter));
    }
}

void SignatureInspectorTests::selfSignedFlagMatchesSubjectAndIssuer() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    for (const SignerCertificate& certificate : signatures.first().chain) {
        const bool expected = certificate.subject == certificate.issuer;
        QCOMPARE(certificate.selfSigned, expected);
        // 一次性自签证书不在系统根证书库里，所以「不受信任」是正常结果。
        QVERIFY2(!certificate.trusted,
                 "临时自签证书不应被判为受信任（除非有人把它装进了系统根证书库）");
    }
}

void SignatureInspectorTests::tamperedSampleFailsCoverage() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample-tampered.pdf"));
    QVERIFY(!bytes.isEmpty());
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    const SignatureInfo& signature = signatures.first();
    QVERIFY2(!signature.coversDocument, "被改过的文件不能被判为覆盖正常");
    QVERIFY2(!signature.coverError.isEmpty(), "被改过的文件必须给出覆盖错误");
    // 摘要对不上，报错要说人话。
    QVERIFY2(signature.coverError.contains(QStringLiteral("改动")) ||
                 signature.coverError.contains(QStringLiteral("不一致")),
             qPrintable(signature.coverError));
}

void SignatureInspectorTests::negativeGuardTamperedVariantsAlwaysCaught() {
    REQUIRE_QPDF();

    // 守卫：同长度改字节不会挪动任何偏移，光看空洞位置抓不到，
    // 必须靠摘要比对。所以这里在正文里换好几种位置，每一种都必须被抓到。
    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QVERIFY(!bytes.isEmpty());
    const int marker = bytes.indexOf("AI PDF Editor signature fixture");
    QVERIFY2(marker > 0, "正文标记没找到，夹具结构变了");

    const QVector<int> offsets = {marker, marker + 3, marker + 9, marker + 17, marker + 26};
    for (int offset : offsets) {
        QByteArray tampered = bytes;
        tampered[offset] = tampered.at(offset) == 'A' ? 'B' : 'A';
        QString error;
        const QVector<SignatureInfo> signatures = inspectSignatures(tampered, &error);
        if (signatures.isEmpty()) {
            // 解析失败也算没通过，但必须明确报错。
            QVERIFY2(!error.isEmpty(), "篡改样本必须报错，不能静默通过");
            continue;
        }
        for (const SignatureInfo& signature : signatures) {
            QVERIFY2(!signature.coversDocument,
                     qPrintable(QStringLiteral("偏移 %1 的篡改没有被抓到").arg(offset)));
            QVERIFY2(!signature.coverError.isEmpty(),
                     qPrintable(QStringLiteral("偏移 %1 的篡改没有覆盖错误").arg(offset)));
        }
    }
}

void SignatureInspectorTests::missingQpdfBehaviour() {
#ifdef AIPDF_WITH_QPDF
    QSKIP("本次构建启用了 qpdf，缺依赖这条路径不可达");
#else
    // 没有 qpdf 时必须明确说「缺依赖」，不能报成「无签名」——那是误导。
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(QByteArray("%PDF-1.7\n"), &error);
    QVERIFY(signatures.isEmpty());
    QVERIFY2(!error.isEmpty(), "缺 qpdf 时必须给出错误说明");
    QVERIFY2(error.contains(QStringLiteral("qpdf")), qPrintable(error));
    QVERIFY2(!error.contains(QStringLiteral("无签名")),
             "缺 qpdf 不能报成「无签名」");
    qInfo("SKIPPED: 无 qpdf —— 依赖 qpdf 的用例全部跳过，缺依赖行为已验证。");
#endif
}

void SignatureInspectorTests::dumpParsedDetails() {
    REQUIRE_QPDF();

    const QByteArray bytes = readFixture(QStringLiteral("signed-sample.pdf"));
    QString error;
    const QVector<SignatureInfo> signatures = inspectSignatures(bytes, &error);
    QCOMPARE(signatures.size(), 1);
    const SignatureInfo& signature = signatures.first();
    qInfo().noquote() << "fieldName    :" << signature.fieldName;
    qInfo().noquote() << "signerName   :" << signature.signerName;
    qInfo().noquote() << "subFilter    :" << signature.subFilter;
    qInfo().noquote() << "reason       :" << signature.reason;
    qInfo().noquote() << "signingTime  :" << signature.signingTime;
    qInfo().noquote() << "coversDocument:" << signature.coversDocument;
    qInfo().noquote() << "coverError   :" << signature.coverError;
    qInfo().noquote() << "byteRange    :" << signature.byteRange;
    qInfo().noquote() << "pkcs7 bytes  :" << signature.pkcs7.size();
    for (const SignerCertificate& certificate : signature.chain) {
        qInfo().noquote() << "--- certificate ---";
        qInfo().noquote() << "  subject    :" << certificate.subject;
        qInfo().noquote() << "  issuer     :" << certificate.issuer;
        qInfo().noquote() << "  commonName :" << certificate.commonName;
        qInfo().noquote() << "  issuerCN   :" << certificate.issuerCommonName;
        qInfo().noquote() << "  serial     :" << certificate.serialNumber;
        qInfo().noquote() << "  sha1       :" << certificate.thumbprintSha1;
        qInfo().noquote() << "  notBefore  :" << certificate.notBefore;
        qInfo().noquote() << "  notAfter   :" << certificate.notAfter;
        qInfo().noquote() << "  selfSigned :" << certificate.selfSigned;
        qInfo().noquote() << "  trusted    :" << certificate.trusted;
    }

    // 篡改样本：覆盖检查必须报出不一致
    const QByteArray tampered = readFixture(QStringLiteral("signed-sample-tampered.pdf"));
    QString tamperedError;
    const QVector<SignatureInfo> tamperedSignatures = inspectSignatures(tampered, &tamperedError);
    QCOMPARE(tamperedSignatures.size(), 1);
    qInfo().noquote() << "=== tampered ===";
    qInfo().noquote() << "  coversDocument:" << tamperedSignatures.first().coversDocument;
    qInfo().noquote() << "  coverError   :" << tamperedSignatures.first().coverError;
}

QTEST_MAIN(SignatureInspectorTests)
#include "SignatureInspectorTests.moc"
