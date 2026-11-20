#include "app/controllers/OfdConverters.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace aipdf::app {

namespace {

// 通用约定：把输入和输出都作为位置参数传给转换器。
//
// 这是本程序一直以来的假设（界面上也写着「转换器应接受"输入文件 输出文件"参数」），
// 认不出具体是哪个转换器时仍然按它走 —— 不能因为换了台机器就转换不了。
const QStringList kGenericArgumentTemplate = {
    QStringLiteral("%1"),
    QStringLiteral("%2"),
};

// 参数模板里的 %1 / %2 换成真实路径。
//
// 单遍替换：先替 %1 再替 %2 的话，输入路径里正好含有 "%2" 这几个字符时会被二次替换。
QStringList expandTemplate(const QStringList& templ, const QString& input, const QString& output)
{
    QStringList arguments;
    arguments.reserve(templ.size());
    for (const QString& item : templ) {
        QString expanded;
        expanded.reserve(item.size());
        for (int index = 0; index < item.size(); ++index) {
            if (item.mid(index, 2) == QLatin1String("%1")) {
                expanded += input;
                ++index;
                continue;
            }
            if (item.mid(index, 2) == QLatin1String("%2")) {
                expanded += output;
                ++index;
                continue;
            }
            expanded += item.at(index);
        }
        arguments.append(expanded);
    }
    return arguments;
}

}  // namespace

QVector<OfdConverterProfile> supportedOfdConverters()
{
    QVector<OfdConverterProfile> profiles;

    {
        // ofd2pdf（Ofd2Pdf，C# / .NET）。
        //
        // 上游 README 的用法是 `ofd2pdf.exe <ofdFilename>`：**只收输入**，
        // PDF 生成在输入旁边、同名不同后缀。所以输出位置由我们自己算，
        // 转换完再搬到用户要的地方。
        //
        // 许可证状况要照实写：上游注明「因版权问题，代码中删除了 Spire.PDF 的
        // License」。这是**不能捆绑**的理由之一 —— 本程序只登记它、不分发它。
        OfdConverterProfile profile;
        profile.id = QStringLiteral("ofd2pdf");
        profile.displayName = QStringLiteral("ofd2pdf");
        profile.programNames = {QStringLiteral("ofd2pdf"), QStringLiteral("ofd2pdf.exe"),
                                QStringLiteral("Ofd2Pdf.exe")};
        profile.argumentTemplate = {QStringLiteral("%1")};
        profile.acceptsOutputPath = false;
        profile.sourceUrl = QStringLiteral("https://github.com/lanbo0829/ofd2pdf");
        profile.licenseNote = QStringLiteral(
            "上游基于 Spire.PDF 且注明已删除其 License —— 不捆绑，只由用户自备");
        profiles.append(profile);
    }

    {
        // ofd-converter（Go，github.com/zc310/ofd）。
        //
        // 上游用法是 `ofd-converter [选项] <输入文件> [输出文件或目录]`，
        // 格式用 `-format pdf` 指定；也可以用 `--input-dir/--output-dir` 批量。
        // 本程序一次喂一个文件，所以用位置参数那一种。
        OfdConverterProfile profile;
        profile.id = QStringLiteral("ofd-converter");
        profile.displayName = QStringLiteral("ofd-converter");
        profile.programNames = {QStringLiteral("ofd-converter"), QStringLiteral("ofd-converter.exe")};
        profile.argumentTemplate = {QStringLiteral("-format"), QStringLiteral("pdf"),
                                    QStringLiteral("%1"), QStringLiteral("%2")};
        profile.acceptsOutputPath = true;
        profile.sourceUrl = QStringLiteral("https://github.com/zc310/ofd");
        profile.licenseNote = QStringLiteral("Apache-2.0 —— 不捆绑，只由用户自备");
        profiles.append(profile);
    }

    return profiles;
}

OfdConverterProfile ofdConverterProfileFor(const QString& programPath)
{
    const QString fileName = QFileInfo(programPath).fileName();
    if (fileName.isEmpty()) {
        return OfdConverterProfile();
    }
    for (const OfdConverterProfile& profile : supportedOfdConverters()) {
        for (const QString& candidate : profile.programNames) {
            // 大小写不敏感地比文件名：Windows 上文件系统不区分大小写，
            // 用户装的可能叫 Ofd2Pdf.exe 而上游写的是 ofd2pdf。
            if (candidate.compare(fileName, Qt::CaseInsensitive) == 0) {
                return profile;
            }
        }
    }
    return OfdConverterProfile();
}

OfdConversionPlan planOfdConversion(const QString& programPath,
                                     const QString& inputPath,
                                     const QString& outputPath)
{
    OfdConversionPlan plan;
    if (programPath.trimmed().isEmpty()) {
        plan.error = QStringLiteral("没有指定 OFD 转换器");
        return plan;
    }
    if (!QFileInfo(inputPath).isFile()) {
        plan.error = QStringLiteral("输入文件不存在");
        return plan;
    }
    if (outputPath.trimmed().isEmpty()) {
        plan.error = QStringLiteral("没有指定输出文件");
        return plan;
    }

    plan.program = programPath;
    const QString expectedOutput = QDir::cleanPath(QFileInfo(outputPath).absoluteFilePath());

    const OfdConverterProfile profile = ofdConverterProfileFor(programPath);
    if (profile.id.isEmpty()) {
        // 认不出是哪个转换器：按通用约定「输入 输出」走。
        plan.arguments = expandTemplate(kGenericArgumentTemplate,
                                         QFileInfo(inputPath).absoluteFilePath(), expectedOutput);
        plan.expectedOutput = expectedOutput;
        return plan;
    }

    if (profile.acceptsOutputPath) {
        plan.arguments = expandTemplate(profile.argumentTemplate,
                                         QFileInfo(inputPath).absoluteFilePath(), expectedOutput);
        plan.expectedOutput = expectedOutput;
        return plan;
    }

    // 只收输入的转换器：输出落在输入旁边、同名不同后缀。
    const QFileInfo inputInfo(inputPath);
    const QString beside = QDir::cleanPath(
        inputInfo.dir().filePath(inputInfo.completeBaseName() + QStringLiteral(".pdf")));
    plan.arguments = expandTemplate(profile.argumentTemplate,
                                     QFileInfo(inputPath).absoluteFilePath(), beside);
    plan.expectedOutput = beside;
    return plan;
}

QString copyOfdResultTo(const QString& sourcePath, const QString& desiredDestination)
{
    if (sourcePath.isEmpty() || desiredDestination.isEmpty()) {
        return QString();
    }
    if (QFileInfo(sourcePath) == QFileInfo(desiredDestination)) {
        return desiredDestination;
    }
    // 目的地已经有人占了：换个名字写，**不覆盖也不删**。
    const QString target = pickFreePdfPath(desiredDestination);
    if (!QFile::copy(sourcePath, target)) {
        return QString();
    }
    return target;
}

OfdSandboxLayout ofdSandboxLayout(const QString& sandboxDir, const QString& inputPath)
{
    OfdSandboxLayout layout;
    const QFileInfo inputInfo(inputPath);
    const QDir sandbox(sandboxDir);

    // 输入副本**保持原文件名**：转换器按自己的规矩找产出（同名不同后缀），
    // 改了名字它就找不到了。
    layout.inputPath = QDir::cleanPath(sandbox.filePath(inputInfo.fileName()));
    // 产出落在输入旁边、同名不同后缀 —— 与 planOfdConversion 算的一致，
    // 三类转换器（通用 / 收输出路径 / 只收输入）在这个沙箱里指向同一个文件。
    layout.outputPath = QDir::cleanPath(
        sandbox.filePath(inputInfo.completeBaseName() + QStringLiteral(".pdf")));
    return layout;
}

QString pickFreePdfPath(const QString& desiredPath)
{
    if (!QFileInfo::exists(desiredPath)) {
        return desiredPath;
    }
    const QFileInfo info(desiredPath);
    const QString base = info.dir().filePath(info.completeBaseName());
    // 同名的 .pdf 已经在了（用户自己的文件）：加后缀接着找。
    // 上限只是防呆 —— 一万个同名文件不会出现在真实目录里。
    for (int index = 1; index <= 1000; ++index) {
        const QString candidate = base + QStringLiteral("-converted") +
                                 (index == 1 ? QString() : QString::number(index)) +
                                 QStringLiteral(".pdf");
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return base + QStringLiteral("-converted-") +
           QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmss")) +
           QStringLiteral(".pdf");
}

QString pickFreeOfdOutputPath(const QString& inputPath)
{
    const QFileInfo info(inputPath);
    return pickFreePdfPath(info.dir().filePath(info.completeBaseName()) + QStringLiteral(".pdf"));
}

OfdConversionSession prepareOfdConversion(const QString& programPath,
                                          const QString& inputPath,
                                          const QString& sandboxDir)
{
    OfdConversionSession session;
    if (sandboxDir.trimmed().isEmpty()) {
        session.error = QStringLiteral("没有可用的临时目录");
        return session;
    }
    if (!QFileInfo(inputPath).isFile()) {
        session.error = QStringLiteral("输入文件不存在");
        return session;
    }

    // 目的地：用户目录里一个**不存在**的名字（不覆盖用户自己的文件）。
    session.destination = pickFreeOfdOutputPath(inputPath);
    session.layout = ofdSandboxLayout(sandboxDir, inputPath);

    // 把输入**复制**进沙箱：转换器看到的是沙箱里那一份，
    // 于是它写在哪都碰不到用户目录。
    QFile::remove(session.layout.inputPath);
    if (!QFile::copy(inputPath, session.layout.inputPath)) {
        session.error = QStringLiteral("无法把输入复制到临时目录：%1").arg(session.layout.inputPath);
        return session;
    }

    session.plan = planOfdConversion(programPath, session.layout.inputPath, session.layout.outputPath);
    if (!session.plan.isValid()) {
        session.error = session.plan.error;
    }
    return session;
}

QString sha256OfFile(const QString& path)
{
    if (path.trimmed().isEmpty()) {
        return QString();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return QString();
    }
    return QString::fromLatin1(hash.result().toHex());
}

OfdConverterIntegrityResult checkOfdConverterIntegrity(const QString& programPath,
                                                       const QString& rememberedPath,
                                                       const QString& rememberedSha256)
{
    OfdConverterIntegrityResult result;
    if (programPath.trimmed().isEmpty()) {
        result.status = OfdConverterIntegrity::Missing;
        result.message = QStringLiteral("没有指定 OFD 转换器。");
        return result;
    }
    const QFileInfo info(programPath);
    if (!info.exists() || !info.isFile()) {
        result.status = OfdConverterIntegrity::Missing;
        result.message = QStringLiteral("找不到 OFD 转换器：%1").arg(programPath);
        return result;
    }
    const QString sha = sha256OfFile(programPath);
    if (sha.isEmpty()) {
        result.status = OfdConverterIntegrity::Unreadable;
        result.message = QStringLiteral("读不出 OFD 转换器的内容：%1").arg(programPath);
        return result;
    }
    result.sha256 = sha;

    // 换了一个转换器（路径变了）：这是新的一套，按首次使用处理。
    //
    // 比较用绝对路径且忽略大小写：Windows 上文件系统不区分大小写，
    // 同一个文件写成 "C:\Tools\ofd2pdf.exe" 与 "c:/tools/ofd2pdf.exe" 是同一个。
    const QString currentPath = QDir::cleanPath(info.absoluteFilePath());
    const QString previousPath =
        rememberedPath.trimmed().isEmpty()
            ? QString()
            : QDir::cleanPath(QFileInfo(rememberedPath).absoluteFilePath());
    if (previousPath.isEmpty() || rememberedSha256.trimmed().isEmpty() ||
        previousPath.compare(currentPath, Qt::CaseInsensitive) != 0) {
        result.status = OfdConverterIntegrity::FirstUse;
        result.message = QStringLiteral("首次使用这个 OFD 转换器，已记下它的 SHA-256（%1）。")
                             .arg(sha.left(16));
        return result;
    }

    if (sha == rememberedSha256.trimmed()) {
        result.status = OfdConverterIntegrity::Unchanged;
        result.message = QStringLiteral("OFD 转换器与上次用的同一个文件（SHA-256 一致）。");
        return result;
    }

    result.status = OfdConverterIntegrity::Changed;
    result.message = QStringLiteral(
                         "这个 OFD 转换器与上次用的不是同一个文件（SHA-256 从 %1 变成 %2）。"
                         "它可能刚被升级，也可能在中间被换过。")
                         .arg(rememberedSha256.left(16), sha.left(16));
    return result;
}

}  // namespace aipdf::app
