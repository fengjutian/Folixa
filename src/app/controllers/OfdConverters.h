#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::app {

// 一个已知的 OFD → PDF 转换器。
//
// 为什么要有「支持列表」而不是直接调一个 exe：不同转换器的**命令行约定不一样**。
// ofd2pdf 只收一个输入文件（输出 PDF 落在输入旁边），ofd-converter 收
// `-format pdf <输入> <输出>`。用错约定，转换器要么报错、要么把文件写到
// 意料之外的地方。
//
// 列表里的每一项都必须**核实过上游 README**，不能凭印象填：
//   - ofd2pdf      https://github.com/lanbo0829/ofd2pdf
//   - ofd-converter https://github.com/zc310/ofd
struct OfdConverterProfile {
    QString id;                 // 稳定标识，例如 "ofd2pdf"
    QString displayName;        // 界面显示名
    QStringList programNames;   // 在 PATH 上探测的程序名（大小写不敏感地匹配文件名）
    // 参数模板：%1 = 输入文件，%2 = 输出文件。
    //
    // 只收输入的转换器（acceptsOutputPath == false）模板里不会有 %2，
    // 输出落在输入旁边，由调用方搬走。
    QStringList argumentTemplate;
    bool acceptsOutputPath = true;
    QString sourceUrl;          // 上游地址：界面把它显示出来，用户能自己去核对
    QString licenseNote;        // 许可证状况（决定能不能捆绑）
};

// 已知的 OFD 转换器列表。
//
// 只有**核实过命令行约定**的才进这张表；用户自带的其它可执行文件仍可用，
// 走通用约定「输入 输出」（见 planOfdConversion）。
QVector<OfdConverterProfile> supportedOfdConverters();

// 按程序文件名认出是哪一个转换器。认不出返回空的 profile（id 为空）。
OfdConverterProfile ofdConverterProfileFor(const QString& programPath);

// 一次转换的命令行。
struct OfdConversionPlan {
    QString program;
    QStringList arguments;
    QString expectedOutput;  // 转换器把 PDF 写到哪儿
    QString error;

    bool isValid() const { return !program.isEmpty() && error.isEmpty(); }
};

// 生成一次转换的命令行。
//
// 认出是列表里的转换器 → 按它的约定生成；
// 认不出 → 按通用约定「输入 输出」生成（与旧行为一致）。
OfdConversionPlan planOfdConversion(const QString& programPath,
                                     const QString& inputPath,
                                     const QString& outputPath);

// ---------------------------------------------------------------- 沙箱

// 一次转换在沙箱里的布局。
struct OfdSandboxLayout {
    QString inputPath;   // 沙箱里的输入副本
    QString outputPath;  // 沙箱里的产出（转换器写到这里）
};

// 在沙箱目录里安排一次转换的输入与产出路径。
//
// 为什么必须沙箱：转换器是**用户自备的可执行文件**，命令行约定各异。
// 有的只收一个输入、把 PDF 写到输入旁边（ofd2pdf 就是这样）。不沙箱就等于
// 让它往用户自己的目录里写文件 —— 同名的 .pdf 会被覆盖，用户的数据就没了。
// 沙箱里跑，转换器碰不到用户目录，产物再由我们搬到用户指定的位置。
OfdSandboxLayout ofdSandboxLayout(const QString& sandboxDir, const QString& inputPath);

// 挑一个**不存在**的输出路径（放在输入旁边）。
//
// 为什么不能直接用同名 .pdf：用户目录里很可能已经有那一份（同名不同后缀），
// 覆盖它等于丢用户的数据。逐个加后缀，直到撞上一个不存在的名字。
QString pickFreeOfdOutputPath(const QString& inputPath);

// 给一个「想要用」的文件路径挑一个**不冲突**的名字。
//
// 什么时候用得上：挑名字和真正落盘之间隔着一段时间（转换器要跑），
// 这期间那个名字可能被别人占了。那就换个名字，**不要删掉占着的那个** ——
// 这一轮修的就是「别弄丢用户自己的文件」。
QString pickFreePdfPath(const QString& desiredPath);

// 把沙箱里的产物搬到用户目录里。
//
// 落盘时**不删**也不**覆盖**已存在的同名文件：挑名字到落盘之间转换器还在跑，
// 那个名字可能被别人占了 —— 换个名字写。
// 返回实际写到的路径；没写成返回空串。
QString copyOfdResultTo(const QString& sourcePath, const QString& desiredDestination);

// 一次转换的完整安排。
struct OfdConversionSession {
    QString destination;      // 用户目录里的目的地（保证是**不存在**的那个名字）
    OfdSandboxLayout layout;  // 沙箱里的输入副本与产出
    OfdConversionPlan plan;   // 命令行
    QString error;            // 非空表示没准备好

    [[nodiscard]] bool isValid() const { return error.isEmpty() && plan.isValid(); }
};

// 安排一次转换：挑目的地、进沙箱、复制输入、生成命令行。
//
// 为什么转换要在沙箱里跑：转换器是**用户自备的可执行文件**，命令行约定各异。
// 有的只收一个输入、把 PDF 写到输入旁边（ofd2pdf 就是这样）。不沙箱就等于
// 让它往用户自己的目录里写文件 —— 同名的 .pdf 会被覆盖，用户的数据就没了。
// 沙箱里跑，转换器碰不到用户目录；产物再由调用方搬到 destination。
//
// sandboxDir 由调用方持有（通常是 QTemporaryDir）：它得活到转换结束。
OfdConversionSession prepareOfdConversion(const QString& programPath,
                                          const QString& inputPath,
                                          const QString& sandboxDir);

// ---------------------------------------------------------------- 哈希校验

// 算出文件的 SHA-256（小写十六进制）。读不了返回空串。
QString sha256OfFile(const QString& path);

enum class OfdConverterIntegrity {
    FirstUse,    // 第一次用这个转换器：记下它的哈希，以后每次复核
    Unchanged,   // 与上次记录的哈希一致
    Changed,     // 与上次**不一致**：文件被替换或改过了
    Missing,     // 文件不在了
    Unreadable,  // 在，但读不出内容（权限 / 是目录）
};

struct OfdConverterIntegrityResult {
    OfdConverterIntegrity status = OfdConverterIntegrity::FirstUse;
    QString sha256;
    QString message;  // 界面直接能显示的中文说明
};

// 复核转换器可执行文件的哈希。
//
// 为什么要有这道校验：这个程序要**执行**一个用户指定的外部可执行文件。
// 如果那个文件在两次运行之间被换掉了（被顶替、路径被劫持、用户自己升级了
// 却被篡改），本程序会毫无察觉地执行新内容。记下首次使用的哈希、之后每次
// 复核，就把这种变化摆到用户面前让他决定。
//
// rememberedPath / rememberedSha256 是上次记下的值（空 = 从来没用过）。
//
// 注意：本程序**不捆绑**任何转换器，也没有一份「可信哈希白名单」——
// 转换器由用户自备，版本各不相同。白名单在这里没有意义；
// 有意义的是「还是上次那个文件吗」。
OfdConverterIntegrityResult checkOfdConverterIntegrity(const QString& programPath,
                                                       const QString& rememberedPath,
                                                       const QString& rememberedSha256);

}  // namespace aipdf::app
