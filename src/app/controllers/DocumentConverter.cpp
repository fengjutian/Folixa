#include "app/controllers/DocumentConverter.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace aipdf::app {

namespace {

QString firstExecutable(const QStringList& names, const QStringList& fixedPaths) {
    for (const QString& name : names) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) return QDir::cleanPath(found);
    }
    for (const QString& path : fixedPaths) {
        if (QFileInfo(path).isExecutable()) return QDir::cleanPath(path);
    }
    return {};
}

QString normalizedOutput(const QString& output) {
    return QDir::cleanPath(QFileInfo(output).absoluteFilePath());
}

}  // namespace

QString DocumentConverter::findLibreOffice() {
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    return firstExecutable(
        {QStringLiteral("soffice"), QStringLiteral("libreoffice")},
        {QDir(programFiles).filePath(QStringLiteral("LibreOffice/program/soffice.exe")),
         QDir(programFilesX86).filePath(QStringLiteral("LibreOffice/program/soffice.exe"))});
}

QString DocumentConverter::findGhostscript() {
    QStringList fixed;
    const QDir root(qEnvironmentVariable("ProgramFiles") + QStringLiteral("/gs"));
    const QFileInfoList versions = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                       QDir::Name | QDir::Reversed);
    for (const QFileInfo& version : versions) {
        fixed.append(QDir(version.absoluteFilePath()).filePath(QStringLiteral("bin/gswin64c.exe")));
        fixed.append(QDir(version.absoluteFilePath()).filePath(QStringLiteral("bin/gswin32c.exe")));
    }
    return firstExecutable({QStringLiteral("gswin64c"), QStringLiteral("gswin32c"),
                            QStringLiteral("gs")}, fixed);
}

ConversionPlan DocumentConverter::plan(ConversionKind kind,
                                        const QString& input,
                                        const QString& output,
                                        const QString& libreOffice,
                                        const QString& ghostscript) {
    ConversionPlan result;
    if (!QFileInfo(input).isFile()) {
        result.error = QStringLiteral("输入文件不存在");
        return result;
    }
    if (output.trimmed().isEmpty()) {
        result.error = QStringLiteral("没有指定输出文件");
        return result;
    }
    result.expectedOutput = normalizedOutput(output);

    if (kind == ConversionKind::OfficeToPdf || kind == ConversionKind::HtmlToPdf) {
        result.program = libreOffice.isEmpty() ? findLibreOffice() : libreOffice;
        result.engineName = QStringLiteral("LibreOffice");
        if (result.program.isEmpty()) {
            result.error = QStringLiteral("未找到 LibreOffice（soffice）");
            return result;
        }
        const QString outputDirectory = QFileInfo(result.expectedOutput).absolutePath();
        result.arguments = {QStringLiteral("--headless"), QStringLiteral("--convert-to"),
                            QStringLiteral("pdf"), QStringLiteral("--outdir"),
                            outputDirectory, QDir::cleanPath(QFileInfo(input).absoluteFilePath())};
        // LibreOffice 固定按输入文件名写出；调用方在成功后再原子改名到用户指定名称。
        result.expectedOutput = QDir(outputDirectory).filePath(
            QFileInfo(input).completeBaseName() + QStringLiteral(".pdf"));
        return result;
    }

    result.program = ghostscript.isEmpty() ? findGhostscript() : ghostscript;
    result.engineName = QStringLiteral("Ghostscript");
    if (result.program.isEmpty()) {
        result.error = QStringLiteral("未找到 Ghostscript");
        return result;
    }
    result.arguments = {QStringLiteral("-dBATCH"), QStringLiteral("-dNOPAUSE"),
                        QStringLiteral("-dSAFER"), QStringLiteral("-sDEVICE=pdfwrite")};
    if (kind == ConversionKind::PdfA2b) {
        result.arguments << QStringLiteral("-dPDFA=2")
                         << QStringLiteral("-dPDFACompatibilityPolicy=1")
                         << QStringLiteral("-sColorConversionStrategy=RGB")
                         << QStringLiteral("-dEmbedAllFonts=true")
                         << QStringLiteral("-dSubsetFonts=true");
    } else {
        result.arguments << QStringLiteral("-dPDFSETTINGS=/ebook")
                         << QStringLiteral("-dDetectDuplicateImages=true")
                         << QStringLiteral("-dCompressFonts=true");
    }
    result.arguments << QStringLiteral("-sOutputFile=%1").arg(result.expectedOutput)
                     << QDir::cleanPath(QFileInfo(input).absoluteFilePath());
    return result;
}

}  // namespace aipdf::app
