#include "app/DigitalSignatureBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

namespace aipdf::app {

QString DigitalSignatureBackend::findPyHanko() {
    const QString bundled = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("pyhanko/pyhanko.exe"));
    if (QFileInfo::exists(bundled)) return QDir::toNativeSeparators(bundled);
    QString executable = QStandardPaths::findExecutable(QStringLiteral("pyhanko"));
    if (executable.isEmpty())
        executable = QStandardPaths::findExecutable(QStringLiteral("pyhanko.exe"));
    return executable;
}

SignatureCommand DigitalSignatureBackend::signPkcs12(
    const QString& inputPdf, const QString& outputPdf, const QString& pkcs12File,
    const QString& passFile, const QString& timestampUrl, bool embedValidationInfo,
    const QString& executable) {
    SignatureCommand result;
    if (!QFileInfo(inputPdf).isFile() || !QFileInfo(pkcs12File).isFile() ||
        !QFileInfo(passFile).isFile() || outputPdf.trimmed().isEmpty()) {
        result.error = QStringLiteral("签名输入、证书或密码文件无效");
        return result;
    }
    if (!timestampUrl.isEmpty()) {
        const QUrl url(timestampUrl);
        if (!url.isValid() || (url.scheme() != QStringLiteral("http") &&
                               url.scheme() != QStringLiteral("https"))) {
            result.error = QStringLiteral("时间戳地址必须是有效的 HTTP(S) URL");
            return result;
        }
    }
    result.program = executable.isEmpty() ? findPyHanko() : executable;
    if (result.program.isEmpty()) {
        result.error = QStringLiteral("未找到 pyHanko 命令行工具");
        return result;
    }
    result.arguments = {QStringLiteral("sign"), QStringLiteral("addsig"),
                        QStringLiteral("--field"), QStringLiteral("FolixaSignature")};
    if (!timestampUrl.isEmpty())
        result.arguments << QStringLiteral("--timestamp-url") << timestampUrl;
    if (embedValidationInfo)
        result.arguments << QStringLiteral("--with-validation-info") << QStringLiteral("--use-pades");
    result.arguments << QStringLiteral("pkcs12") << QStringLiteral("--passfile") << passFile
                     << inputPdf << outputPdf << pkcs12File;
    return result;
}

SignatureCommand DigitalSignatureBackend::verify(const QString& inputPdf,
                                                  const QString& executable) {
    SignatureCommand result;
    if (!QFileInfo(inputPdf).isFile()) {
        result.error = QStringLiteral("待验证的 PDF 不存在");
        return result;
    }
    result.program = executable.isEmpty() ? findPyHanko() : executable;
    if (result.program.isEmpty()) {
        result.error = QStringLiteral("未找到 pyHanko 命令行工具");
        return result;
    }
    result.arguments = {QStringLiteral("sign"), QStringLiteral("validate"),
                        QStringLiteral("--pretty-print"), inputPdf};
    return result;
}

}  // namespace aipdf::app
