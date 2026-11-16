#include "pdf/PdfSecurity.h"
#include "pdf/PdfiumAdapter.h"

#include <QDir>
#include <QFile>
#include <QtTest>

using aipdf::pdf::PdfEncryptionOptions;
using aipdf::pdf::PdfPermissions;
using aipdf::pdf::encryptPdf;
using aipdf::pdf::inspectEncryption;
using aipdf::pdf::sanitizeHiddenData;
using aipdf::pdf::openDocumentFromBytes;
using aipdf::pdf::verifyOwnerPassword;

namespace {

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

QByteArray readFixture(const QString& name) {
    QFile file(fixturePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// PDF 权限位（ISO 32000-1 表 22）。
constexpr int kPrintBit = 1 << 2;         // 4
constexpr int kModifyBit = 1 << 3;        // 8
constexpr int kCopyBit = 1 << 4;          // 16
constexpr int kAnnotateBit = 1 << 5;      // 32
constexpr int kFillFormsBit = 1 << 8;     // 256
constexpr int kAccessibilityBit = 1 << 9; // 512
constexpr int kAssembleBit = 1 << 10;     // 1024
constexpr int kHighQualityPrintBit = 1 << 11;  // 2048

}  // namespace

class PdfSecurityTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void encryptsAndWritesR6Dictionary();
    void wrongPasswordIsRejected();
    void ownerPasswordAlsoOpens();
    void verifiesOwnerPasswordForPermissionUnlock();
    void pdfiumOpensEncryptedDocument();
    void emptyUserPasswordStillEncrypts();
    void permissionFlagsFollowOptions();
    void emptyInputIsRejected();
    void sanitizesAndKeepsDocumentReadable();
    void pdfiumReportsRestrictedPermissions();

private:
    bool hasFixture_ = false;
    bool qpdfAvailable_ = false;
};

void PdfSecurityTests::initTestCase() {
    hasFixture_ = QFile::exists(fixturePath(QStringLiteral("normal.pdf")));
    qpdfAvailable_ = aipdf::pdf::encryptionAvailable();
    if (!hasFixture_) {
        QSKIP("缺少 normal.pdf 夹具");
    }
    if (!qpdfAvailable_) {
        QSKIP("本次构建没有启用 qpdf");
    }
}

void PdfSecurityTests::encryptsAndWritesR6Dictionary() {
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("user-secret");
    options.ownerPassword = QStringLiteral("owner-secret");
    QString error;
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")),
                                            options, &error);
    QVERIFY2(!encrypted.isEmpty(), qPrintable(error));
    QVERIFY(encrypted != readFixture(QStringLiteral("normal.pdf")));

    int revision = 0;
    int permissions = 0;
    QVERIFY2(inspectEncryption(encrypted, QStringLiteral("user-secret"), &revision,
                               &permissions, &error),
             qPrintable(error));
    QCOMPARE(revision, 6);
    QVERIFY(!(permissions & kCopyBit));
    QVERIFY(permissions & kPrintBit);
    QVERIFY(permissions & kHighQualityPrintBit);
    QVERIFY(permissions & kAccessibilityBit);
}

void PdfSecurityTests::wrongPasswordIsRejected() {
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("right");
    options.ownerPassword = QStringLiteral("owner");
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")), options);
    QVERIFY(!encrypted.isEmpty());
    QVERIFY(!inspectEncryption(encrypted, QStringLiteral("wrong"), nullptr, nullptr));
}

void PdfSecurityTests::ownerPasswordAlsoOpens() {
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader");
    options.ownerPassword = QStringLiteral("owner-pass");
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")), options);
    QVERIFY(!encrypted.isEmpty());
    int revision = 0;
    QVERIFY(inspectEncryption(encrypted, QStringLiteral("owner-pass"), &revision, nullptr));
    QCOMPARE(revision, 6);
}

void PdfSecurityTests::verifiesOwnerPasswordForPermissionUnlock() {
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader-pass");
    options.ownerPassword = QStringLiteral("owner-pass");
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")), options);
    QVERIFY(!encrypted.isEmpty());

    QString error;
    QVERIFY2(verifyOwnerPassword(encrypted, QStringLiteral("owner-pass"), &error),
             qPrintable(error));
    QVERIFY(!verifyOwnerPassword(encrypted, QStringLiteral("reader-pass"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!verifyOwnerPassword(encrypted, QStringLiteral("wrong"), &error));
}

void PdfSecurityTests::pdfiumOpensEncryptedDocument() {
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader");
    options.ownerPassword = QStringLiteral("owner-pass");
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")), options);
    QVERIFY(!encrypted.isEmpty());

    // 不带密码：必须报「需要密码」，而不是当成损坏文件。
    const auto locked =
        openDocumentFromBytes(encrypted, QStringLiteral("encrypted.pdf"));
    QVERIFY(!locked.document.isValid());
    QVERIFY(locked.passwordRequired);

    const auto opened = openDocumentFromBytes(
        encrypted, QStringLiteral("encrypted.pdf"), QStringLiteral("reader"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(!page->render(1.0).isNull());

    const auto wrong = openDocumentFromBytes(
        encrypted, QStringLiteral("encrypted.pdf"), QStringLiteral("nope"));
    QVERIFY(!wrong.document.isValid());
}

void PdfSecurityTests::emptyUserPasswordStillEncrypts() {
    PdfEncryptionOptions options;
    options.userPassword.clear();
    options.ownerPassword.clear();
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")), options);
    QVERIFY(!encrypted.isEmpty());

    int revision = 0;
    QVERIFY(inspectEncryption(encrypted, QString(), &revision, nullptr));
    QCOMPARE(revision, 6);
    // 没有用户密码：PDFium 应该能直接打开。
    const auto opened = openDocumentFromBytes(encrypted, QStringLiteral("open.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
}

void PdfSecurityTests::permissionFlagsFollowOptions() {
    PdfPermissions permissions;
    permissions.allowPrinting = false;
    permissions.allowHighQualityPrinting = false;
    permissions.allowCopying = true;
    permissions.allowAccessibility = false;
    permissions.allowAnnotations = false;
    permissions.allowFormFilling = false;
    permissions.allowAssembly = true;
    permissions.allowModifying = true;

    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader");
    options.permissions = permissions;
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")), options);
    QVERIFY(!encrypted.isEmpty());

    int bits = 0;
    QVERIFY(inspectEncryption(encrypted, QStringLiteral("reader"), nullptr, &bits));
    QVERIFY(!(bits & kPrintBit));
    QVERIFY(!(bits & kHighQualityPrintBit));
    QVERIFY(bits & kCopyBit);
    // PDF 2.0 / R6 把「无障碍提取」位定为始终允许：协议不允许再禁止它。
    // 这里断言实际写出来的位，而不是断言我们传进去的选项。
    QVERIFY(bits & kAccessibilityBit);
    QVERIFY(!(bits & kAnnotateBit));
    QVERIFY(!(bits & kFillFormsBit));
    QVERIFY(bits & kAssembleBit);
    QVERIFY(bits & kModifyBit);
}

void PdfSecurityTests::emptyInputIsRejected() {
    QString error;
    QVERIFY(encryptPdf(QByteArray(), PdfEncryptionOptions(), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void PdfSecurityTests::sanitizesAndKeepsDocumentReadable() {
    QString error;
    const QByteArray sanitized = sanitizeHiddenData(readFixture(QStringLiteral("normal.pdf")), &error);
    QVERIFY2(!sanitized.isEmpty(), qPrintable(error));
    const auto opened = openDocumentFromBytes(sanitized, QStringLiteral("sanitized.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
}

void PdfSecurityTests::pdfiumReportsRestrictedPermissions() {
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader");
    options.ownerPassword = QStringLiteral("owner");
    options.permissions.allowPrinting = false;
    options.permissions.allowCopying = false;
    options.permissions.allowAnnotations = false;
    options.permissions.allowFormFilling = false;
    options.permissions.allowAssembly = false;
    options.permissions.allowModifying = false;
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")), options);
    QVERIFY(!encrypted.isEmpty());
    const auto opened = openDocumentFromBytes(encrypted, QStringLiteral("restricted.pdf"),
                                              QStringLiteral("reader"));
    QVERIFY(opened.document.isValid());
    const auto permissions = opened.document.permissions();
    QVERIFY(permissions.encrypted);
    QVERIFY(!permissions.allowPrinting);
    QVERIFY(!permissions.allowCopying);
    QVERIFY(!permissions.allowAnnotations);
    QVERIFY(!permissions.allowFormFilling);
    QVERIFY(!permissions.allowAssembly);
    QVERIFY(!permissions.allowModifying);
}

QTEST_GUILESS_MAIN(PdfSecurityTests)
#include "PdfSecurityTests.moc"
