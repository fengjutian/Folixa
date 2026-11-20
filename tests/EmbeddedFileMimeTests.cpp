// 文档级嵌入文件的 MIME 类型（/Type /EmbeddedFile + /Subtype）。
//
// 这组用例盯两件事：
//   1. 未加密文档的补类型行为（回归）；
//   2. **加密文档**也能补上类型，而且**不动原加密**（权限位、所有者口令、
//      打开口令一律保持原样）。
//
// 「加密文档不补 MIME」这条旧边界已经作废：实测确认 PDF 的加密只作用在流数据
// 与字符串上，流**字典**是明文，带着打开口令就能改；写回时用
// setPreserveEncryption + copyEncryptionParameters 保留原加密字典即可。

#include "pdf/document/EmbeddedFileMime.h"

#include <QByteArray>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>
#endif

using aipdf::pdf::EmbeddedFileMimeReport;
using aipdf::pdf::applyEmbeddedFileMimeTypes;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 我们关心的那 8 个权限位。
//
// 掩码抄自 src/pdf/security/PdfSecurity.cpp 的 kManagedPermissionBits：
// qpdf 写出来的 /P 会把保留位（第 7、8、13… 位）也置 1，无障碍提取位又恒置 1
// （PDF 2.0 规定始终允许），所以逐位全比必然对不上，只比这 8 位。
constexpr int kBitPrint = 1 << 2;
constexpr int kBitModify = 1 << 3;
constexpr int kBitExtract = 1 << 4;
constexpr int kBitAnnotate = 1 << 5;
constexpr int kBitFormFill = 1 << 8;
constexpr int kBitAccessibility = 1 << 9;
constexpr int kBitAssembly = 1 << 10;
constexpr int kBitPrintHigh = 1 << 11;

constexpr int kManagedPermissionBits =
    kBitPrint | kBitModify | kBitExtract | kBitAnnotate |
    kBitFormFill | kBitAccessibility | kBitAssembly | kBitPrintHigh;

const char* const kUserPassword = "user-pass-123";
const char* const kOwnerPassword = "owner-pass-456";

// 一份带文档级嵌入文件的最小 PDF。
//
// 嵌入文件流**故意不带** /Type 也不带 /Subtype：这就是 PDFium 写出来的东西
// （实测它的字典里只有 /DL /Filter /Length /Params）。
QByteArray pdfWithEmbeddedFile(const QString& fileName, const QByteArray& content) {
    QByteArray out("%PDF-1.7\n");
    const QByteArray name = fileName.toUtf8();

    // 1: Catalog（挂名字树）  2: Pages  3: Page  4: Filespec  5: 嵌入文件流
    const QVector<QByteArray> objects = {
        QByteArray("<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles << /Names [(") +
            name + QByteArray(") 4 0 R] >> >> >>"),
        QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>"),
        QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> >>"),
        QByteArray("<< /Type /Filespec /F (") + name +
            QByteArray(") /UF (") + name + QByteArray(") /EF << /F 5 0 R >> >>"),
        QByteArray("<< /DL ") + QByteArray::number(content.size()) +
            QByteArray(" /Params << /Size ") + QByteArray::number(content.size()) +
            QByteArray(" >> /Length ") + QByteArray::number(content.size()) +
            QByteArray(" >>\nstream\n") + content + QByteArray("\nendstream"),
    };

    QVector<int> offsets;
    for (int index = 0; index < objects.size(); ++index) {
        offsets.append(out.size());
        out += QByteArray::number(index + 1) + " 0 obj\n" + objects.at(index) + "\nendobj\n";
    }

    const int xrefOffset = out.size();
    const int count = objects.size() + 1;
    out += QByteArray("xref\n0 ") + QByteArray::number(count) + "\n0000000000 65535 f \n";
    for (int offset : offsets) {
        out += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    out += QByteArray("trailer\n<< /Size ") + QByteArray::number(count) +
           QByteArray(" /Root 1 0 R >>\nstartxref\n") + QByteArray::number(xrefOffset) +
           "\n%%EOF\n";
    return out;
}

#ifdef AIPDF_WITH_QPDF

// 把一份 PDF 按 AES-256（R6）加密，权限位刻意设成「能打印全文、不能提取正文」，
// 这样「权限位被改掉」在比对时是看得出来的。
QByteArray encryptFixture(const QByteArray& plain) {
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("fixture.pdf", plain.constData(),
                          static_cast<size_t>(plain.size()));

    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    writer.setR6EncryptionParameters(
        kUserPassword, kOwnerPassword,
        /* allow_accessibility */ true,
        /* allow_extract */ false,
        /* allow_assemble */ false,
        /* allow_annotate_and_form */ true,
        /* allow_form_filling */ true,
        /* allow_modify_other */ false,
        qpdf_r3p_full,
        /* encrypt_metadata */ true);
    writer.write();

    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (!buffer || buffer->getSize() == 0) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                       static_cast<int>(buffer->getSize()));
}

// 加密字典里的 /P（权限位整数）。
int permissionValue(QPDF& pdf) {
    const QPDFObjectHandle encrypt = pdf.getTrailer().getKey("/Encrypt");
    if (!encrypt.isDictionary()) {
        return 0;
    }
    const QPDFObjectHandle p = encrypt.getKey("/P");
    return p.isInteger() ? p.getIntValueAsInt() : 0;
}

// 加密字典里的 /Perms（R6 的 16 字节：前 4 字节是 /P，后面是所有者口令的哈希）。
QByteArray permsValue(QPDF& pdf) {
    const QPDFObjectHandle encrypt = pdf.getTrailer().getKey("/Encrypt");
    if (!encrypt.isDictionary()) {
        return {};
    }
    const QPDFObjectHandle perms = encrypt.getKey("/Perms");
    return perms.isString() ? QByteArray(perms.getStringValue().data(),
                                          static_cast<int>(perms.getStringValue().size()))
                             : QByteArray();
}

// /Perms 里前 4 字节编码的 /P。
int permsPermissionValue(QPDF& pdf) {
    const QByteArray perms = permsValue(pdf);
    if (perms.size() < 4) {
        return 0;
    }
    const auto* raw = reinterpret_cast<const unsigned char*>(perms.constData());
    return static_cast<int>(raw[0]) | (static_cast<int>(raw[1]) << 8) |
           (static_cast<int>(raw[2]) << 16) | (static_cast<int>(raw[3]) << 24);
}

// 名字树里的嵌入文件流（本文件的样本只有一层，够用）。
QPDFObjectHandle embeddedStreamOf(QPDF& pdf) {
    const QPDFObjectHandle namesArray =
        pdf.getRoot().getKey("/Names").getKey("/EmbeddedFiles").getKey("/Names");
    if (!namesArray.isArray()) {
        return QPDFObjectHandle();
    }
    const std::vector<QPDFObjectHandle> items = namesArray.getArrayAsVector();
    if (items.size() < 2) {
        return QPDFObjectHandle();
    }
    return items.at(1).getKey("/EF").getKey("/F");
}

// 读回嵌入文件流的 /Type 与 /Subtype。
QString subtypeOf(QPDF& pdf) {
    const QPDFObjectHandle stream = embeddedStreamOf(pdf);
    if (!stream.isStream()) {
        return QString();
    }
    const QPDFObjectHandle dict = stream.getDict();
    if (!dict.hasKey("/Subtype")) {
        return QString();
    }
    return QString::fromStdString(dict.getKey("/Subtype").getName());
}

QString typeOf(QPDF& pdf) {
    const QPDFObjectHandle stream = embeddedStreamOf(pdf);
    if (!stream.isStream()) {
        return QString();
    }
    const QPDFObjectHandle dict = stream.getDict();
    if (!dict.hasKey("/Type")) {
        return QString();
    }
    return QString::fromStdString(dict.getKey("/Type").getName());
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

class EmbeddedFileMimeTests : public QObject {
    Q_OBJECT

private slots:
    void plainDocumentGetsTypeAndSubtype();
    void plainDocumentWithoutPasswordStillWorks();
    void plainDocumentWithPasswordStillWorks();
    void alreadyTypedDocumentIsNotRewritten();
    void unknownSuffixFallsBackToOctetStream();
    void encryptedDocumentGetsSubtypeWithPassword();
    void encryptedDocumentReopensWithSamePassword();
    void encryptedDocumentKeepsPermissionBits();
    void encryptedDocumentKeepsOwnerPassword();
    void wrongPasswordFailsWithoutHalfEditedFile();
    void missingPasswordOnEncryptedDocumentFails();
};

// 1. 未加密文档：附件流补上 /Type /EmbeddedFile 与 /Subtype（回归）。
void EmbeddedFileMimeTests::plainDocumentGetsTypeAndSubtype() {
    REQUIRE_QPDF();
    const QByteArray plain = pdfWithEmbeddedFile(QStringLiteral("hello.txt"),
                                                  QByteArray("Hello, world!"));

    EmbeddedFileMimeReport report;
    QString error;
    const QByteArray patched = applyEmbeddedFileMimeTypes(plain, &report, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!patched.isEmpty());
    QVERIFY(patched != plain);
    QCOMPARE(report.patched, 1);
    QVERIFY(report.rewritten);
    QVERIFY(!report.encrypted);

    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("out.pdf", patched.constData(), static_cast<size_t>(patched.size()));
    QCOMPARE(typeOf(pdf), QStringLiteral("/EmbeddedFile"));
    QCOMPARE(subtypeOf(pdf), QStringLiteral("/text/plain"));
}

// 2. 不传口令的旧入口：未加密文档的行为一点没变（向后兼容）。
void EmbeddedFileMimeTests::plainDocumentWithoutPasswordStillWorks() {
    REQUIRE_QPDF();
    const QByteArray plain = pdfWithEmbeddedFile(QStringLiteral("hello.txt"),
                                                  QByteArray("Hello, world!"));

    EmbeddedFileMimeReport report;
    QString error;
    const QByteArray patched = applyEmbeddedFileMimeTypes(plain, &report, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(report.patched, 1);

    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("out.pdf", patched.constData(), static_cast<size_t>(patched.size()));
    QCOMPARE(subtypeOf(pdf), QStringLiteral("/text/plain"));
}

// 3. 传了口令但文档没加密：正常工作（qpdf 对明文文档忽略口令）。
void EmbeddedFileMimeTests::plainDocumentWithPasswordStillWorks() {
    REQUIRE_QPDF();
    const QByteArray plain = pdfWithEmbeddedFile(QStringLiteral("hello.txt"),
                                                  QByteArray("Hello, world!"));

    EmbeddedFileMimeReport report;
    QString error;
    const QByteArray patched =
        applyEmbeddedFileMimeTypes(plain, QStringLiteral("irrelevant"), &report, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(report.patched, 1);
    QVERIFY(!report.encrypted);

    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("out.pdf", patched.constData(), static_cast<size_t>(patched.size()));
    QCOMPARE(subtypeOf(pdf), QStringLiteral("/text/plain"));
}

// 4. 已经补过类型的文档不再重写（没要补的就不动字节流）。
void EmbeddedFileMimeTests::alreadyTypedDocumentIsNotRewritten() {
    REQUIRE_QPDF();
    const QByteArray plain = pdfWithEmbeddedFile(QStringLiteral("hello.txt"),
                                                  QByteArray("Hello, world!"));

    EmbeddedFileMimeReport firstReport;
    QString error;
    const QByteArray once = applyEmbeddedFileMimeTypes(plain, &firstReport, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    EmbeddedFileMimeReport secondReport;
    QString secondError;
    const QByteArray twice = applyEmbeddedFileMimeTypes(once, &secondReport, &secondError);

    QVERIFY2(secondError.isEmpty(), qPrintable(secondError));
    QCOMPARE(secondReport.patched, 0);
    QVERIFY(!secondReport.rewritten);
    QCOMPARE(twice, once);
}

// 5. 后缀认不出来：/Subtype 走 application/octet-stream。
void EmbeddedFileMimeTests::unknownSuffixFallsBackToOctetStream() {
    REQUIRE_QPDF();
    const QByteArray plain = pdfWithEmbeddedFile(QStringLiteral("data.zzzunknown"),
                                                  QByteArray("binary"));

    EmbeddedFileMimeReport report;
    QString error;
    const QByteArray patched = applyEmbeddedFileMimeTypes(plain, &report, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(report.patched, 1);

    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("out.pdf", patched.constData(), static_cast<size_t>(patched.size()));
    QCOMPARE(subtypeOf(pdf), QStringLiteral("/application/octet-stream"));
}

// 6. 加密文档 + 正确的打开口令：补上 /Subtype。
void EmbeddedFileMimeTests::encryptedDocumentGetsSubtypeWithPassword() {
    REQUIRE_QPDF();
    const QByteArray plain = pdfWithEmbeddedFile(QStringLiteral("hello.txt"),
                                                  QByteArray("Hello, world!"));
    const QByteArray encrypted = encryptFixture(plain);
    QVERIFY(!encrypted.isEmpty());

    EmbeddedFileMimeReport report;
    QString error;
    const QByteArray patched = applyEmbeddedFileMimeTypes(
        encrypted, QString::fromLatin1(kUserPassword), &report, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(patched != encrypted);
    QCOMPARE(report.patched, 1);
    QVERIFY(report.encrypted);

    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("out.pdf", patched.constData(), static_cast<size_t>(patched.size()),
                          kUserPassword);
    QVERIFY(pdf.isEncrypted());
    QCOMPARE(typeOf(pdf), QStringLiteral("/EmbeddedFile"));
    QCOMPARE(subtypeOf(pdf), QStringLiteral("/text/plain"));
}

// 7. 加密文档补完之后，用**同一个打开口令**重开：/Subtype 还在，流也没坏。
void EmbeddedFileMimeTests::encryptedDocumentReopensWithSamePassword() {
    REQUIRE_QPDF();
    const QByteArray plain = pdfWithEmbeddedFile(QStringLiteral("hello.txt"),
                                                  QByteArray("Hello, world!"));
    const QByteArray encrypted = encryptFixture(plain);
    QVERIFY(!encrypted.isEmpty());

    EmbeddedFileMimeReport report;
    QString error;
    const QByteArray patched = applyEmbeddedFileMimeTypes(
        encrypted, QString::fromLatin1(kUserPassword), &report, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("out.pdf", patched.constData(), static_cast<size_t>(patched.size()),
                          kUserPassword);
    QVERIFY(pdf.isEncrypted());
    QCOMPARE(subtypeOf(pdf), QStringLiteral("/text/plain"));

    // 附件内容一字不差：补字典没有把流搞坏。
    QPDFObjectHandle stream = embeddedStreamOf(pdf);
    QVERIFY(stream.isStream());
    const std::shared_ptr<Buffer> data = stream.getStreamData();
    QVERIFY(data);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                         static_cast<int>(data->getSize())),
             QByteArray("Hello, world!"));
}

// 8. 加密文档补完之后：/P 与 /Perms 的**管理位**与改之前逐位一致。
//
// 只比 8 位管理位：qpdf 写出来的 /P 会把保留位置 1、无障碍位恒置 1，
// 逐位全比必然对不上（见 PdfSecurity.cpp 的 kManagedPermissionBits）。
void EmbeddedFileMimeTests::encryptedDocumentKeepsPermissionBits() {
    REQUIRE_QPDF();
    const QByteArray plain = pdfWithEmbeddedFile(QStringLiteral("hello.txt"),
                                                  QByteArray("Hello, world!"));
    const QByteArray encrypted = encryptFixture(plain);
    QVERIFY(!encrypted.isEmpty());

    int beforeP = 0;
    int beforePermsP = 0;
    {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("before.pdf", encrypted.constData(),
                              static_cast<size_t>(encrypted.size()), kUserPassword);
        beforeP = permissionValue(pdf);
        beforePermsP = permsPermissionValue(pdf);
    }
    // 样本的权限位得有内容，否则「一致」这个断言就是空的。
    QVERIFY((beforeP & kManagedPermissionBits) != 0);
    QVERIFY((beforeP & kBitExtract) == 0);  // 样本是「不许提取正文」

    EmbeddedFileMimeReport report;
    QString error;
    const QByteArray patched = applyEmbeddedFileMimeTypes(
        encrypted, QString::fromLatin1(kUserPassword), &report, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("out.pdf", patched.constData(), static_cast<size_t>(patched.size()),
                          kUserPassword);
    QVERIFY(pdf.isEncrypted());

    QCOMPARE(permissionValue(pdf) & kManagedPermissionBits,
             beforeP & kManagedPermissionBits);
    QCOMPARE(permsPermissionValue(pdf) & kManagedPermissionBits,
             beforePermsP & kManagedPermissionBits);
}

// 9. 加密文档补完之后：**所有者口令仍然有效**。
//
// 这是「原加密被原样保留」最硬的证据：所有者口令一旦被重新生成，
// 原所有者口令就再也打不开了。
void EmbeddedFileMimeTests::encryptedDocumentKeepsOwnerPassword() {
    REQUIRE_QPDF();
    const QByteArray plain = pdfWithEmbeddedFile(QStringLiteral("hello.txt"),
                                                  QByteArray("Hello, world!"));
    const QByteArray encrypted = encryptFixture(plain);
    QVERIFY(!encrypted.isEmpty());

    EmbeddedFileMimeReport report;
    QString error;
    const QByteArray patched = applyEmbeddedFileMimeTypes(
        encrypted, QString::fromLatin1(kUserPassword), &report, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // 所有者口令打不开时 qpdf 抛异常：接住它，否则整个测试进程会被带走。
    bool ownerStillWorks = false;
    QString detail;
    try {
        QPDF asOwner;
        asOwner.setSuppressWarnings(true);
        asOwner.processMemoryFile("out.pdf", patched.constData(),
                                  static_cast<size_t>(patched.size()), kOwnerPassword);
        ownerStillWorks = asOwner.isEncrypted() && asOwner.ownerPasswordMatched();
        if (ownerStillWorks) {
            QCOMPARE(subtypeOf(asOwner), QStringLiteral("/text/plain"));
        }
    } catch (const std::exception& exception) {
        detail = QString::fromUtf8(exception.what());
    }
    QVERIFY2(ownerStillWorks,
             qPrintable(detail.isEmpty() ? QStringLiteral("所有者口令不再有效") : detail));
}

// 10. 加密文档 + 错误口令：明确失败、原因可读，**不产出半改的文件**。
void EmbeddedFileMimeTests::wrongPasswordFailsWithoutHalfEditedFile() {
    REQUIRE_QPDF();
    const QByteArray plain = pdfWithEmbeddedFile(QStringLiteral("hello.txt"),
                                                  QByteArray("Hello, world!"));
    const QByteArray encrypted = encryptFixture(plain);
    QVERIFY(!encrypted.isEmpty());

    EmbeddedFileMimeReport report;
    QString error;
    const QByteArray patched = applyEmbeddedFileMimeTypes(
        encrypted, QStringLiteral("definitely-wrong"), &report, &error);

    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("密码")), qPrintable(error));
    // 半改的文件是最糟的结果：字节必须与输入完全一致。
    QCOMPARE(patched, encrypted);
    QCOMPARE(report.patched, 0);
    QVERIFY(!report.rewritten);
}

// 11. 加密文档 + 根本没给口令：明确失败（不再是「静默跳过」）。
void EmbeddedFileMimeTests::missingPasswordOnEncryptedDocumentFails() {
    REQUIRE_QPDF();
    const QByteArray plain = pdfWithEmbeddedFile(QStringLiteral("hello.txt"),
                                                  QByteArray("Hello, world!"));
    const QByteArray encrypted = encryptFixture(plain);
    QVERIFY(!encrypted.isEmpty());

    EmbeddedFileMimeReport report;
    QString error;
    const QByteArray patched = applyEmbeddedFileMimeTypes(encrypted, &report, &error);

    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("密码")), qPrintable(error));
    QCOMPARE(patched, encrypted);
    QCOMPARE(report.patched, 0);
}

QTEST_GUILESS_MAIN(EmbeddedFileMimeTests)
#include "EmbeddedFileMimeTests.moc"
