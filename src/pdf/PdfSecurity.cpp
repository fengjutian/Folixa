#include "pdf/PdfSecurity.h"

#include <QRandomGenerator>

#include <memory>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#endif

namespace aipdf::pdf {

namespace {

QString toLocal(const QString& text) {
    return text;
}

#ifdef AIPDF_WITH_QPDF

// qpdf 的 R3 打印枚举：无 / 低分辨率 / 完整。
qpdf_r3_print_e printMode(const PdfPermissions& permissions) {
    if (!permissions.allowPrinting) {
        return qpdf_r3p_none;
    }
    return permissions.allowHighQualityPrinting ? qpdf_r3p_full : qpdf_r3p_low;
}

QString randomOwnerPassword() {
    // 32 个十六进制字符：不需要可记忆，只要不可猜。
    QString result;
    result.reserve(32);
    for (int i = 0; i < 4; ++i) {
        const quint32 value = QRandomGenerator::system()->generate();
        result += QStringLiteral("%1").arg(value, 8, 16, QLatin1Char('0'));
    }
    return result;
}

#else

QString missingQpdfError() {
    return QStringLiteral("本次构建没有启用 qpdf，加密与权限位不可用");
}

#endif

}  // namespace

bool encryptionAvailable() {
#ifdef AIPDF_WITH_QPDF
    return true;
#else
    return false;
#endif
}

QByteArray encryptPdf(const QByteArray& input,
                      const PdfEncryptionOptions& options,
                      QString* error) {
    if (input.isEmpty()) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("没有可加密的内容"));
        }
        return {};
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("input.pdf",
                              input.constData(),
                              static_cast<size_t>(input.size()));

        // 空所有者密码 = 随机生成：避免「留空就等于不设限」这种看起来安全、
        // 实际谁都能改权限的假保护。用户密码留空是另一回事：那表示允许无密码打开。
        const QByteArray userPassword = options.userPassword.toUtf8();
        QByteArray ownerPassword = options.ownerPassword.toUtf8();
        if (ownerPassword.isEmpty()) {
            ownerPassword = randomOwnerPassword().toUtf8();
        }

        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.setR6EncryptionParameters(
            userPassword.constData(),
            ownerPassword.constData(),
            options.permissions.allowAccessibility,
            options.permissions.allowCopying,
            options.permissions.allowAssembly,
            options.permissions.allowAnnotations,
            options.permissions.allowFormFilling,
            options.permissions.allowModifying,
            printMode(options.permissions),
            options.encryptMetadata);
        writer.write();

        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            if (error != nullptr) {
                *error = toLocal(QStringLiteral("qpdf 没有写出加密结果"));
            }
            return {};
        }
        QByteArray output(reinterpret_cast<const char*>(buffer->getBuffer()),
                          static_cast<int>(buffer->getSize()));

        // 自检：必须能用用户密码重新打开，且真的是加密文件。
        // 这一步挡住的是「写出了字节，但密码对不上」的静默失败。
        QPDF reopened;
        reopened.processMemoryFile("encrypted.pdf",
                                   output.constData(),
                                   static_cast<size_t>(output.size()),
                                   userPassword.constData());
        int revision = 0;
        int permissionBits = 0;
        if (!reopened.isEncrypted(revision, permissionBits) || revision < 6) {
            if (error != nullptr) {
                *error = toLocal(QStringLiteral("加密自检失败：写出的文件不是 AES-256 加密文档"));
            }
            return {};
        }
        return output;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("加密失败：%1")
                                 .arg(QString::fromUtf8(exception.what())));
        }
        return {};
    }
#else
    Q_UNUSED(options);
    if (error != nullptr) {
        *error = missingQpdfError();
    }
    return {};
#endif
}

QByteArray sanitizeHiddenData(const QByteArray& input, QString* error) {
    if (input.isEmpty()) {
        if (error != nullptr) *error = toLocal(QStringLiteral("没有可清理的内容"));
        return {};
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("sanitize.pdf", input.constData(),
                              static_cast<size_t>(input.size()));

        QPDFObjectHandle trailer = pdf.getTrailer();
        trailer.removeKey("/Info");

        QPDFObjectHandle root = pdf.getRoot();
        for (const char* key : {"/Metadata", "/PieceInfo", "/OpenAction", "/AA"}) {
            root.removeKey(key);
        }
        if (root.hasKey("/Names")) {
            QPDFObjectHandle names = root.getKey("/Names");
            if (names.isDictionary()) {
                names.removeKey("/JavaScript");
                names.removeKey("/EmbeddedFiles");
            }
        }
        for (QPDFObjectHandle page : pdf.getAllPages()) {
            page.removeKey("/AA");
            page.removeKey("/PieceInfo");
            page.removeKey("/Metadata");
        }

        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.setObjectStreamMode(qpdf_o_generate);
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            if (error != nullptr) *error = toLocal(QStringLiteral("qpdf 没有写出清理结果"));
            return {};
        }
        return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                          static_cast<int>(buffer->getSize()));
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("隐藏信息清理失败：%1")
                                 .arg(QString::fromUtf8(exception.what())));
        }
        return {};
    }
#else
    if (error != nullptr) *error = missingQpdfError();
    return {};
#endif
}

bool verifyOwnerPassword(const QByteArray& input, const QString& password, QString* error) {
    if (input.isEmpty() || password.isEmpty()) {
        if (error != nullptr) *error = toLocal(QStringLiteral("所有者密码不能为空"));
        return false;
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        const QByteArray utf8 = password.toUtf8();
        pdf.processMemoryFile("owner-check.pdf", input.constData(),
                              static_cast<size_t>(input.size()), utf8.constData());
        if (!pdf.isEncrypted()) {
            if (error != nullptr) *error = toLocal(QStringLiteral("文档没有加密"));
            return false;
        }
        if (!pdf.ownerPasswordMatched()) {
            if (error != nullptr) *error = toLocal(QStringLiteral("密码可以打开文档，但不是所有者密码"));
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = toLocal(QString::fromUtf8(exception.what()));
        return false;
    }
#else
    Q_UNUSED(input);
    Q_UNUSED(password);
    if (error != nullptr) *error = missingQpdfError();
    return false;
#endif
}

bool inspectEncryption(const QByteArray& input,
                       const QString& password,
                       int* revision,
                       int* permissionBits,
                       QString* error) {
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        const QByteArray utf8Password = password.toUtf8();
        pdf.processMemoryFile("inspect.pdf",
                              input.constData(),
                              static_cast<size_t>(input.size()),
                              utf8Password.constData());
        int localRevision = 0;
        int localPermissions = 0;
        if (!pdf.isEncrypted(localRevision, localPermissions)) {
            if (revision != nullptr) {
                *revision = 0;
            }
            if (permissionBits != nullptr) {
                *permissionBits = 0;
            }
            return false;
        }
        if (revision != nullptr) {
            *revision = localRevision;
        }
        if (permissionBits != nullptr) {
            *permissionBits = localPermissions;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = toLocal(QString::fromUtf8(exception.what()));
        }
        return false;
    }
#else
    Q_UNUSED(input);
    Q_UNUSED(password);
    Q_UNUSED(revision);
    Q_UNUSED(permissionBits);
    if (error != nullptr) {
        *error = missingQpdfError();
    }
    return false;
#endif
}

}  // namespace aipdf::pdf
