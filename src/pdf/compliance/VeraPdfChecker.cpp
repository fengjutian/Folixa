#include "pdf/compliance/VeraPdfChecker.h"

#include "i18n/Translate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QXmlStreamReader>

namespace aipdf::pdf {

namespace {

// 候选可执行文件名（Windows 上习惯带 .exe，PATH 里通常不带）。
const QStringList kExecutableNames = {
    QStringLiteral("verapdf"),
    QStringLiteral("verapdf.exe"),
    QStringLiteral("verapdf.bat"),
    QStringLiteral("verapdf.cmd"),
};

// 常见的安装位置（相对几个盘根与 Program Files）。
QStringList commonInstallPaths() {
    QStringList roots;
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    if (!programFiles.isEmpty()) roots << programFiles;
    if (!programFilesX86.isEmpty()) roots << programFilesX86;
    // 注意：不要一边 range-for 遍历 roots 一边往 roots 里 append——
    // 追加会让迭代器失效（实测约 27 次里崩 1 次，退出码 0xC0000005）。
    QStringList paths;
    for (const QString& root : roots) {
        paths << QDir(root).filePath(QStringLiteral("verapdf/bin/verapdf"));
        paths << QDir(root).filePath(QStringLiteral("verapdf/verapdf"));
    }
    paths << QStringLiteral("C:/verapdf/bin/verapdf");
    paths << QStringLiteral("C:/Program Files/verapdf/bin/verapdf");
    return paths;
}

// 从一个候选路径里把可执行文件的绝对路径取出来；不行就返回空串。
//
// 为什么看后缀而不是只看「文件存在」：Windows 上 `verapdf` 常常是 .bat / .cmd 包装，
// 只看存在会把它当成二进制去跑。
QString resolveExecutable(const QString& candidate) {
    if (candidate.isEmpty()) return QString();
    const QFileInfo info(candidate);
    if (!info.exists() || !info.isFile()) return QString();
    const QString suffix = info.suffix().toLower();
    if (suffix != QLatin1String("exe") && suffix != QLatin1String("bat") &&
        suffix != QLatin1String("cmd")) {
        return QString();
    }
    return QDir::cleanPath(info.absoluteFilePath());
}

}  // namespace

VeraPdfTool findVeraPdf(const QStringList& extraPaths) {
    VeraPdfTool tool;

    // 1) 调用方给的路径优先：用户在界面上明确填了「veraPDF 在哪儿」，
    //    就应该用他给的那个，不该被 PATH 里的另一个版本盖掉。
    for (const QString& candidate : extraPaths) {
        const QString resolved = resolveExecutable(candidate);
        if (!resolved.isEmpty()) {
            tool.found = true;
            tool.executable = resolved;
            return tool;
        }
    }

    // 2) PATH。
    for (const QString& name : kExecutableNames) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) {
            tool.found = true;
            tool.executable = QDir::cleanPath(found);
            return tool;
        }
    }

    // 3) 常见安装位置。
    const QStringList paths = commonInstallPaths();
    for (const QString& candidate : paths) {
        const QString resolved = resolveExecutable(candidate);
        if (!resolved.isEmpty()) {
            tool.found = true;
            tool.executable = resolved;
            return tool;
        }
    }

    tool.error = i18n::tr("aipdf::pdf::VeraPdfChecker",
                          "本机没有找到 veraPDF。合规认证要用 veraPDF（Java 程序），"
                          "本仓库不自带它：装好并加入 PATH，或在界面上填它的完整路径。");
    return tool;
}

VeraPdfPlan planVeraPdfValidation(const QString& program,
                                  const QString& inputPath,
                                  const QString& reportPath,
                                  const QString& flavour) {
    VeraPdfPlan plan;
    if (program.isEmpty()) {
        plan.error = i18n::tr("aipdf::pdf::VeraPdfChecker", "没有找到 veraPDF，没法做合规认证。");
        return plan;
    }
    if (inputPath.isEmpty()) {
        plan.error = i18n::tr("aipdf::pdf::VeraPdfChecker", "没有待认证的文件。");
        return plan;
    }
    plan.program = program;
    plan.reportPath = reportPath;
    // --format xml：报告是机器可读的，界面才能逐条列出来。
    // --reportfile：报告落到文件里（XML 里可能带内嵌的 PDF 元数据，管道不方便）。
    plan.arguments << QStringLiteral("--format") << QStringLiteral("xml");
    if (!reportPath.isEmpty()) {
        plan.arguments << QStringLiteral("--reportfile") << reportPath;
    }
    if (!flavour.isEmpty()) {
        plan.arguments << QStringLiteral("--flavour") << flavour;
    }
    plan.arguments << inputPath;
    plan.valid = true;
    return plan;
}

VeraPdfReport parseVeraPdfXmlReport(const QByteArray& xml, QString* error) {
    VeraPdfReport report;
    if (xml.trimmed().isEmpty()) {
        report.error = i18n::tr("aipdf::pdf::VeraPdfChecker", "veraPDF 没有给出报告。");
        if (error) *error = report.error;
        return report;
    }

    QXmlStreamReader reader(xml);
    bool sawAnyRule = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) continue;
        const QString name = reader.name().toString();
        if (name == QLatin1String("rule")) {
            sawAnyRule = true;
            VeraPdfFinding finding;
            const QXmlStreamAttributes attributes = reader.attributes();
            finding.specification = attributes.value(QLatin1String("specification")).toString();
            finding.clause = attributes.value(QLatin1String("clause")).toString();
            finding.testNumber = attributes.value(QLatin1String("testNumber")).toString();
            finding.status = attributes.value(QLatin1String("status")).toString();
            finding.message = attributes.value(QLatin1String("description")).toString();
            finding.passed = finding.status.compare(QLatin1String("passed"), Qt::CaseInsensitive) == 0;
            finding.warning = finding.status.compare(QLatin1String("warning"), Qt::CaseInsensitive) == 0;
            if (!finding.passed && !finding.warning) {
                ++report.failedCount;
            } else if (finding.warning) {
                ++report.warningCount;
            } else {
                ++report.passedCount;
            }
            report.findings.append(finding);
            continue;
        }
        // 规范名：veraPDF 把用到的 profile 写在 profileInformation/name 里。
        if (name == QLatin1String("name") && report.profile.isEmpty()) {
            const QString value = reader.readElementText().trimmed();
            if (!value.isEmpty() && value.contains(QLatin1String("PDF/A"), Qt::CaseInsensitive)) {
                report.profile = value;
            }
        }
    }

    if (reader.hasError()) {
        report.error = i18n::tr("aipdf::pdf::VeraPdfChecker",
                                "veraPDF 的报告读不出来：%1").arg(reader.errorString());
        if (error) *error = report.error;
        return report;
    }
    if (!sawAnyRule) {
        report.error = i18n::tr("aipdf::pdf::VeraPdfChecker",
                                "veraPDF 的报告里没有逐条结论，读不出认证结果。");
        if (error) *error = report.error;
        return report;
    }

    report.compliant = report.failedCount == 0;
    report.ok = true;
    report.summary = report.compliant
                         ? i18n::tr("aipdf::pdf::VeraPdfChecker",
                                    "veraPDF 判定这份文件合规（%1 条规则全部通过）。")
                               .arg(report.passedCount)
                         : i18n::tr("aipdf::pdf::VeraPdfChecker",
                                    "veraPDF 判定这份文件不合规：%1 条规则没通过。")
                               .arg(report.failedCount);
    if (error) error->clear();
    return report;
}

QString veraPdfUnavailableNotice() {
    return i18n::tr("aipdf::pdf::VeraPdfChecker",
                    "本机没有装 veraPDF，本程序做不了 PDF/A 合规认证。"
                    "本程序自带的 PDF/A 预检只看文件结构里能读出来的要求，"
                    "**预检通过不等于合规**——认证结论只能由 veraPDF 给。");
}

QString veraPdfFailedNotice(const VeraPdfReport& report) {
    QStringList parts;
    parts.append(report.summary);
    for (const VeraPdfFinding& finding : report.findings) {
        if (finding.passed) continue;
        QString one = finding.clause.isEmpty() ? finding.message
                                               : finding.clause + QStringLiteral(": ") + finding.message;
        if (one.trimmed().isEmpty()) continue;
        parts.append(QStringLiteral("- ") + one.trimmed());
    }
    return parts.join(QLatin1Char('\n'));
}

}  // namespace aipdf::pdf
