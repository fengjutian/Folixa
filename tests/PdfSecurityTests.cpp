#include "pdf/security/PdfSecurity.h"
#include "pdf/kernel/PdfiumAdapter.h"

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

// 把一页的文字拼起来（用来比「改权限没有改内容」）。
QString pageText(const aipdf::pdf::PdfDocument& document, int pageIndex) {
    const auto page = document.page(pageIndex);
    if (!page.has_value()) {
        return {};
    }
    const auto text = page->extractText();
    if (!text.has_value()) {
        return {};
    }
    QString result;
    for (const auto& character : text->chars) {
        result += character.text;
    }
    return result;
}

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
    void reencryptChangesPermissionsOnEncryptedDocument();
    void reencryptRejectsNonOwnerPassword();
    void reencryptRejectsSameUserAndOwnerPassword();
    void reencryptRejectsUnencryptedDocument();
    void reencryptKeepsContentAndOwnerPassword();

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

void PdfSecurityTests::reencryptChangesPermissionsOnEncryptedDocument() {
    // 起点：一份「什么都不许」的加密文档。
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader");
    options.ownerPassword = QStringLiteral("owner");
    options.permissions.allowPrinting = false;
    options.permissions.allowHighQualityPrinting = false;
    options.permissions.allowCopying = false;
    options.permissions.allowAnnotations = false;
    options.permissions.allowFormFilling = false;
    options.permissions.allowAssembly = false;
    options.permissions.allowModifying = false;
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")), options);
    QVERIFY(!encrypted.isEmpty());

    // 新权限：允许打印与复制，其余仍然禁止。
    PdfPermissions updated;
    updated.allowPrinting = true;
    updated.allowHighQualityPrinting = true;
    updated.allowCopying = true;
    updated.allowAccessibility = true;
    updated.allowAnnotations = false;
    updated.allowFormFilling = false;
    updated.allowAssembly = false;
    updated.allowModifying = false;

    QString error;
    const QByteArray changed = aipdf::pdf::reencryptWithPermissions(
        encrypted, updated, QStringLiteral("owner"), QStringLiteral("reader2"), &error);
    QVERIFY2(!changed.isEmpty(), qPrintable(error));

    // 新的打开口令能打开，权限位是新的那一套。
    int bits = 0;
    QVERIFY2(inspectEncryption(changed, QStringLiteral("reader2"), nullptr, &bits), qPrintable(error));
    QVERIFY(bits & kPrintBit);
    QVERIFY(bits & kHighQualityPrintBit);
    QVERIFY(bits & kCopyBit);
    QVERIFY(!(bits & kAnnotateBit));
    QVERIFY(!(bits & kFillFormsBit));
    QVERIFY(!(bits & kAssembleBit));
    QVERIFY(!(bits & kModifyBit));

    // 旧的打开口令作废。
    QVERIFY(!inspectEncryption(changed, QStringLiteral("reader"), nullptr, nullptr));

    // 读者实际执行的权限（PDFium 读出来的那一套）也是新的。
    const auto opened =
        openDocumentFromBytes(changed, QStringLiteral("changed.pdf"), QStringLiteral("reader2"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    const auto permissions = opened.document.permissions();
    QVERIFY(permissions.encrypted);
    QVERIFY(permissions.allowPrinting);
    QVERIFY(permissions.allowCopying);
    QVERIFY(!permissions.allowAnnotations);
    QVERIFY(!permissions.allowFormFilling);
    QVERIFY(!permissions.allowAssembly);
    QVERIFY(!permissions.allowModifying);
}

void PdfSecurityTests::reencryptRejectsNonOwnerPassword() {
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader");
    options.ownerPassword = QStringLiteral("owner");
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")), options);
    QVERIFY(!encrypted.isEmpty());

    QString error;
    // 拿**打开口令**当所有者口令用：它能打开文档，但不该有权改权限。
    const QByteArray changed = aipdf::pdf::reencryptWithPermissions(
        encrypted, PdfPermissions(), QStringLiteral("reader"), QStringLiteral("reader2"), &error);
    QVERIFY(changed.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("所有者")), qPrintable(error));
}

void PdfSecurityTests::reencryptRejectsSameUserAndOwnerPassword() {
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader");
    options.ownerPassword = QStringLiteral("owner");
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")), options);
    QVERIFY(!encrypted.isEmpty());

    QString error;
    // 打开口令与所有者口令相同 = 任何拿到打开口令的人都是所有者，权限形同虚设。
    const QByteArray changed = aipdf::pdf::reencryptWithPermissions(
        encrypted, PdfPermissions(), QStringLiteral("owner"), QStringLiteral("owner"), &error);
    QVERIFY(changed.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("打开口令")), qPrintable(error));
}

void PdfSecurityTests::reencryptRejectsUnencryptedDocument() {
    QString error;
    const QByteArray changed = aipdf::pdf::reencryptWithPermissions(
        readFixture(QStringLiteral("normal.pdf")), PdfPermissions(), QStringLiteral("owner"),
        QStringLiteral("reader"), &error);
    QVERIFY(changed.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("加密")), qPrintable(error));
}

void PdfSecurityTests::reencryptKeepsContentAndOwnerPassword() {
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader");
    options.ownerPassword = QStringLiteral("owner");
    const QByteArray encrypted = encryptPdf(readFixture(QStringLiteral("normal.pdf")), options);
    QVERIFY(!encrypted.isEmpty());

    PdfPermissions updated;
    updated.allowPrinting = true;
    updated.allowHighQualityPrinting = true;
    updated.allowCopying = true;
    updated.allowAccessibility = true;
    updated.allowAnnotations = true;
    updated.allowFormFilling = true;
    updated.allowAssembly = true;
    updated.allowModifying = true;

    QString error;
    const QByteArray changed = aipdf::pdf::reencryptWithPermissions(
        encrypted, updated, QStringLiteral("owner"), QStringLiteral("reader2"), &error);
    QVERIFY2(!changed.isEmpty(), qPrintable(error));

    // 所有者口令仍然有效（否则用户会失去解除限制的能力）。
    QVERIFY2(verifyOwnerPassword(changed, QStringLiteral("owner"), &error), qPrintable(error));

    // 内容一点没变：页数一样，文字一样。
    const auto before = openDocumentFromBytes(encrypted, QStringLiteral("before.pdf"),
                                               QStringLiteral("reader"));
    QVERIFY(before.document.isValid());
    const auto after = openDocumentFromBytes(changed, QStringLiteral("after.pdf"),
                                              QStringLiteral("reader2"));
    QVERIFY(after.document.isValid());
    QCOMPARE(after.document.pageCount(), before.document.pageCount());
    QCOMPARE(pageText(after.document, 0), pageText(before.document, 0));
}

QTEST_GUILESS_MAIN(PdfSecurityTests)
#include "PdfSecurityTests.moc"
