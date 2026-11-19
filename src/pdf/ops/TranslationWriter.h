#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// ============================================================================
// 整篇翻译并写回文档
// ============================================================================
//
// 做：把整篇文档（或指定的几页）按**段落**切成翻译单元，
//     交给翻译 Provider，译文再写回 PDF 里对应的位置。
//
// 不做——这是取舍，不是缺陷：
//   * **不替 Provider 编译文**。Provider 没配置、调用失败、返回的不是译文，
//     一律逐条给中文原因，那一段保留原文。
//     「把原文当译文写回去」是最坏的一种失败：看上去成功了，内容却没翻。
//   * **不写标准 14 号字体表示不了的字**。译文里出现非 Latin-1 字符时，
//     这一段**保留原文**并写明原因（要真中文译文得先嵌入中文字体，
//     那是另一条流程，不在这里假装支持）。
//   * **不静默截断**。译文比原文长、原来的位置放不下时，
//     这一段保留原文，并在报告里写明「放不下」。
//
// **门禁**：写回之后用 **PDFium**（独立于写回通道的解析器）重新打开、
// 重新抽文字，逐段核对：
//   * 写回去了的段：译文必须真的在页面上；
//   * 没写回去的段：原文必须还在页面上。
// 任何一段对不上，就**不写回**，并在 error 里写明是哪一段。
//
// **Provider 怎么接进来**：
// 这一层不依赖 src/ai 的网络栈——领域层不该知道网络是怎么走的。
// 界面层拿 aipdf::ai::ModelProvider（任务类型 aipdf::ai::Task::Translate）
// 适配成下面的接口即可；单测注入假实现，不打真实网络。

// 翻译 Provider（可注入）。
class TranslationProvider {
public:
    virtual ~TranslationProvider() = default;

    // Provider 标识（写进报告，便于排查是哪一家）。
    virtual QString id() const = 0;

    // 能不能真的发起调用（没有密钥 / 地址就别调用）。
    virtual bool isConfigured() const = 0;

    // 没配置时的中文原因。
    virtual QString notConfiguredReason() const {
        return QStringLiteral("翻译 Provider 没有配置（缺少 API 密钥或服务地址）");
    }

    // 是不是演示模式。演示模式的输出是拼出来的示例，
    // **不能**写回文档——所以这一位会直接拦住整篇翻译。
    virtual bool isDemo() const { return false; }

    // 翻译一段文字。
    //
    // 成功返回 true，译文写进 translation；
    // 失败返回 false，中文原因写进 error。
    virtual bool translate(const QString& sourceText, const QString& targetLanguage,
                           QString* translation, QString* error) = 0;
};

// 一个翻译单元（= 一段）。
struct TranslationUnit {
    // 所在页（0 基）。
    int pageIndex = -1;
    // 这一段的第一个文本块 id。
    int blockId = -1;
    // 原文。
    QString sourceText;
    // 译文（没拿到就是空的）。
    QString translatedText;
    // 是不是真的写回文档了。
    bool written = false;
    // 没写进去的中文原因（为空表示写进去了）。
    QString reason;
};

// 翻译参数。
struct TranslationOptions {
    // 目标语言。
    QString targetLanguage = QStringLiteral("zh-CN");
    // 只翻这几页（0 基）。为空表示整篇都翻。
    QVector<int> pageIndexes;
    // 译文放不进原来的位置时：true = 保留原文并给出提示（默认）；
    // false = 照样写回（文字会溢出原来的位置，报告里会写明）。
    bool keepOriginalOnOverflow = true;
};

// 翻译报告。
struct TranslationReport {
    // 文档页数。
    int pageCount = 0;
    // 参与翻译的段数。
    int unitCount = 0;
    // 拿到译文、并且通过了检查的段数。
    int translatedCount = 0;
    // 真的写回文档的段数。
    int writtenCount = 0;
    // 没写成的段数。
    int failedCount = 0;
    // 原文总字数 / 译文总字数。
    int sourceCharCount = 0;
    int translatedCharCount = 0;
    // 用到的 Provider 标识。
    QString providerId;
    // 逐段的结果。
    QVector<TranslationUnit> units;
    QStringList notes;
    // 失败原因（中文）。为空表示成功。
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// 翻译写回的结果。
struct TranslationWriteResult {
    // 写回后的 PDF 字节。失败（或整篇没有改动）时为空。
    QByteArray bytes;
    TranslationReport report;

    bool ok() const { return report.ok(); }
};

// 整篇（或指定页）翻译并写回。
//
// 失败（report.error 非空、bytes 为空）的情况：
//   * 输入不是 PDF（或内容为空）；
//   * 没有传 Provider；
//   * Provider 没配置（error 里写明没配置的原因）；
//   * Provider 是演示模式（演示文本不能写回文档）；
//   * 内容流解析不了；
//   * 一段都没翻译成功（这时不写回，文档保持原样）；
//   * **门禁没过**：写回后重新抽文字，译文或原文有一段对不上，
//     说明写回出了岔子。这种情况下**不写回**。
//
// 成功的时候 bytes 是写回后的 PDF，report 给出每一段的结果与没写成的原因。
TranslationWriteResult translateDocument(const QByteArray& pdfBytes, TranslationProvider* provider,
                                         const TranslationOptions& options);

}  // namespace aipdf::pdf
