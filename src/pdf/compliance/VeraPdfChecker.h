#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// ============================================================================
// 这个模块做什么、不做什么
// ============================================================================
//
// 做：**接入外部的 veraPDF**（合规认证的权威工具）——
//   * 探测本机有没有 veraPDF（PATH + 常见安装位置 + 调用方给的路径）；
//   * 拼出一次认证的命令行（**纯函数**，不启动进程，所以单测不依赖本机装没装）；
//   * 解析 veraPDF 的 XML 报告，给出逐条结论与「合不合规」的结论。
//
// 不做：
//   * **不在本仓库里带上 veraPDF**。它是 Java 程序，本仓库的硬约束是不引入新依赖，
//     所以它永远是**可选的外部工具**：装了就能用，没装就如实说没装。
//   * **不做 PDF/A 转换**。veraPDF 是「检查器」，不是「转换器」；
//     转换要 Ghostscript（`doc::DocumentConverter` 那一路），两件事别混。
//   * **不自己判断合规**。合不合规只看 veraPDF 的结论 —— 这里是转发器，不是第二个裁判。
//
// 为什么要有这个模块：`pdf::checkPdfAPreflight` 是**预检**，它看的是文件结构里
// 能读出来的那些要求；**认证**必须由 veraPDF 出结论。以前仓库里没有这条接入点，
// 于是「预检不是认证」这句话说得很硬，但没有任何办法真的去认证。
// 现在接入点有了：装上 veraPDF 就能拿到权威结论，没装就把这件事说清楚。

// veraPDF 工具本身（外部可选依赖）。
struct VeraPdfTool {
    // 本机有没有找到 veraPDF。
    bool found = false;
    // 可执行文件的绝对路径。found 为真时有效。
    QString executable;
    // 版本原文（跑 `--version` 拿到）。拿不到时为空——不影响能不能用。
    QString version;
    // 找不到时的中文原因。found 为真时为空。
    QString error;
};

// 一次认证的命令计划。
//
// 为什么只给计划不直接跑：进程启动、超时、取消是 app 层的事
// （和 `app::DocumentConverter` 同一个分工），领域层只负责「命令长什么样」。
struct VeraPdfPlan {
    // 计划能不能用。valid 为假时 error 里有中文原因。
    bool valid = false;
    // 要跑的程序。
    QString program;
    // 参数（**不含**程序本身，也不含待检文件——那是最后一个参数）。
    QStringList arguments;
    // veraPDF 把 XML 报告写到哪儿。
    QString reportPath;
    // 出错原因（中文）。
    QString error;
};

// veraPDF 的一条结论。
struct VeraPdfFinding {
    // 规范名（例如 "ISO 19005-1"）。
    QString specification;
    // 条款号（例如 "6.1.2"）。
    QString clause;
    // 规则序号（veraPDF 给的 testNumber）。
    QString testNumber;
    // 结论：passed / failed / warning。
    QString status;
    // 结论原文（veraPDF 的 description）。
    QString message;
    // status 是 passed 时为真。
    bool passed = false;
    // status 是 warning 时为真。
    bool warning = false;
};

// veraPDF 的认证报告。
struct VeraPdfReport {
    // 有没有真的拿到并解析出一份报告。false 时 error 里有中文原因。
    bool ok = false;
    // veraPDF 说这份文件**合规**吗。
    //
    // 注意它的意思：这是 **veraPDF 的结论**，不是本程序的判断。
    // 本程序只是把结论转述出来。
    bool compliant = false;
    // 中文失败原因。
    QString error;
    // 这次校验用的规范（例如 "PDF/A-2b"）。verapdf 没写就空。
    QString profile;
    // 逐条结论。
    QVector<VeraPdfFinding> findings;
    int passedCount = 0;
    int failedCount = 0;
    int warningCount = 0;
    // 一行中文摘要。
    QString summary;
};

// 探测本机有没有 veraPDF。
//
// extraPaths 是调用方给的额外候选路径（界面上让用户填「veraPDF 在哪儿」）。
// 探测顺序：extraPaths → PATH → 常见安装位置。
VeraPdfTool findVeraPdf(const QStringList& extraPaths = QStringList());

// 拼一次认证的命令（**纯函数**：不启动进程、不碰磁盘）。
//
// flavour 是要校验的规范（空表示用 veraPDF 的默认规范）。
// inputPath 是待检 PDF 的路径，reportPath 是 XML 报告的输出路径。
VeraPdfPlan planVeraPdfValidation(const QString& program,
                                  const QString& inputPath,
                                  const QString& reportPath,
                                  const QString& flavour = QString());

// 解析 veraPDF 的 XML 报告（**纯函数**）。
//
// 为什么按「找所有 rule 元素」来解析，而不是死抠层级：
// veraPDF 的报告结构在不同版本里套了几层 validationReport，
// 按固定路径取容易随版本走样；`<rule>` 这个元素本身是稳定的，
// 它的属性（specification / clause / testNumber / status / description）也是稳定的。
VeraPdfReport parseVeraPdfXmlReport(const QByteArray& xml, QString* error = nullptr);

// 工具没装时界面要显示的中文说明（原文由领域层给，app 层只负责显示）。
QString veraPdfUnavailableNotice();

// 工具装了、但这份文件没通过时的中文说明（原文由领域层给）。
QString veraPdfFailedNotice(const VeraPdfReport& report);

}  // namespace aipdf::pdf
