#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// PDF/A-2b **预检**的结论分级。
//
// 为什么分三档、但语义不能照抄 PdfUaSeverity：预检回答的是
// 「这份文件离 PDF/A-2b 还差什么」，不是「合不合规」。
// Pass 只表示这一条要求在文件里成立，它**不**表示文件通过了 PDF/A 认证。
enum class PdfAPreflightSeverity { Pass, Warning, Error };

// 一条 PDF/A-2b 预检结论。
//
// 为什么领域层给的是 ASCII 而不是中文：仓库的硬约束是领域层不碰翻译、
// 只返回结构化结果或 ASCII 技术细节，中文句子由 app 层拼。
// 所以这里 title / detail / suggestion 一律是 ASCII 技术说法
// （对象名、键名、实际值），app 层拿 code 去查自己的中文表，
// 再把 detail 当作技术细节嵌进中文句子里。
//
// 与 PdfUaFinding 的差别只有这一处：那边把中文标题硬编码在领域层，
// 这边按本仓库的领域层约束改成「码 + ASCII 技术细节」。
struct PdfAPreflightFinding {
    // 机器可读码，例如 "a-missing-output-intent"。
    //
    // 为什么要有码：界面按码决定怎么处理（能不能一键修、折叠在哪一组），
    // 文案会随语言变，码不能变。
    QString code;
    // 对应的 ISO 19005-2（PDF/A-2）条款号，例如 "6.2.3"。
    //
    // 为什么给条款号：预检的每一条结论都要能追溯到标准条款，
    // 否则用户只看到「这里有问题」，不知道权威依据在哪一条。
    QString clause;
    // 短标题（ASCII 技术说法）。
    QString title;
    // 具体说明（ASCII 技术细节）：带对象号、键名、实际值，便于直接定位问题。
    QString detail;
    // 怎么修（ASCII 技术建议）。Pass 项可以为空——没有要修的地方。
    QString suggestion;
    PdfAPreflightSeverity severity = PdfAPreflightSeverity::Error;
};

// 这个模块能给出的结论类型。
//
// 为什么只有一个取值：真正的 PDF/A 合规**认证**要 veraPDF（Java 依赖），
// 本仓库不引入新依赖（硬约束），所以领域层永远只给 Preflight。
// 做成枚举而不是布尔量，是为了让「这里只做预检」在类型上就看得出来，
// 接线的人不会顺手写出「已通过 PDF/A 校验」这种结论。
enum class PdfACheckKind {
    // 预检：从文件结构里能读出来的那些 PDF/A-2b 要求，逐条给结论与修复建议。
    Preflight
};

// PDF/A-2b 预检报告。
struct PdfAPreflightReport {
    // 全部结论，按固定顺序排列（同一份输入两次跑出来的顺序必须一样）。
    QVector<PdfAPreflightFinding> findings;
    int errorCount = 0;
    int warningCount = 0;
    int passCount = 0;
    // 结论类型。恒为 Preflight：这里没有「认证」这个结果。
    PdfACheckKind kind = PdfACheckKind::Preflight;
    // 恒为 false：预检不产生认证结论。
    //
    // 为什么留这个字段：app 层拼文案时需要一个「能不能说已通过 PDF/A 校验」的开关，
    // 写成恒假字段比让 app 层自己记着「这个模块只是预检」更不容易出错。
    bool certification = false;
    // XMP 里声明的 PDF/A 部分（"2"）与一致级别（"B"）；空串表示文件没有声明。
    //
    // 为什么单独拿出来：声明只是「文件自己声称自己是 PDF/A-2b」，
    // 它不等于文件真的符合——认证要看 veraPDF 的结论，不是看这两个字段。
    QString declaredPart;
    QString declaredConformance;
    // 「有问题」的条数（Error + Warning）。app 层说「预检发现 N 个问题」时用这个。
    int issueCount() const { return errorCount + warningCount; }
    // 一行 ASCII 摘要，含各条结论的数量与「这是预检不是认证」的说明。
    QString summary() const;
};

// 预检的边界说明（ASCII）。
//
// 为什么单独给一个函数：界面要显示「这不是合规认证」这句提示，
// 但这句话属于 app 层的话术；领域层只提供这句技术说明的原文。
QString pdfAPreflightDisclaimer();

// 结论码一览（app 层按码查自己的中文表；码稳定，文案可变）：
//
//   预检跑不起来（4 条，都是 Error）：
//     a-empty-document / a-not-a-pdf / a-qpdf-unavailable / a-unreadable-document
//
//   文件格式：
//     a-pdf-version-ok / a-pdf-version-unsupported          6.1.2
//     a-file-id-ok / a-missing-file-id                      6.1.3
//     a-document-not-encrypted / a-document-encrypted        6.1.3
//
//   XMP 元数据：
//     a-missing-xmp-metadata / a-xmp-metadata-invalid       6.6.2.1
//     a-xmp-missing-pdfaid / a-xmp-wrong-part /
//     a-xmp-wrong-conformance / a-xmp-pdfaid-ok             6.6.2.3
//
//   输出意图与 ICC：
//     a-missing-output-intent / a-output-intent-wrong-subtype  6.2.3
//     a-output-intent-icc-missing / a-output-intent-icc-invalid  6.2.3 / 6.2.4.2
//     a-output-intent-ok
//
//   字体：
//     a-fonts-embedded / a-font-not-embedded                  6.2.11
//
//   过滤与外部引用：
//     a-no-lzw-compression / a-lzw-compression                6.1.7
//     a-no-external-file-reference / a-external-file-reference 6.1.7
//     a-no-javascript / a-javascript-action                   6.4.4
//
//   色彩空间：
//     a-colorspaces-ok / a-colorspace-not-allowed            6.2.4
//
//   透明：
//     a-no-transparency / a-transparency-allowed             6.2.10
//     a-transparency-without-output-intent                  6.2.4.3
//     a-transparency-without-page-group                     6.2.10
//     a-softmask-tr-not-identity                             6.2.10.2
//
//   注解：
//     a-annotations-ok / a-forbidden-annotation              6.3
//     a-annotation-missing-appearance                        6.3.3
//     a-annotation-flags                                     6.3.2
QString pdfAPreflightDisclaimer();

// 对一份 PDF 字节做 PDF/A-2b **预检**。
//
// 覆盖的检查项（每条都能单独触发、单独判定）：
//   * 文件格式：文件头版本是不是 %PDF-1.7、trailer 里有没有 /ID；
//   * 加密：PDF/A 不允许任何加密（包括只有所有者密码的权限限制）；
//   * XMP：文档目录有没有 /Metadata 流、是不是 /Subtype /XML、
//     里面有没有声明 pdfaid:part="2" 与 pdfaid:conformance="B"；
//   * OutputIntent：有没有 /OutputIntents、里面有没有 /S /GTS_PDFA2、
//     /DestOutputProfile 里是不是**嵌入**的 ICC 配置文件、
//     ICC 头（设备类别 / 数据色彩空间 / 版本）合不合 PDF/A-2 的要求；
//   * 字体：页面用到的字体是不是都带了字体程序（没内嵌会逐个报出来）；
//   * 过滤：有没有用 LZW（PDF/A 禁止）、流里有没有外部文件引用（/F）、
//     有没有 JavaScript 动作；
//   * 色彩空间：有没有 CIE 色彩空间（Lab / CalRGB / CalGray）这类
//     不能用 ICC 表达的空间、ICCBased 的配置文件在不在文件里、
//     索引与专色的基空间合不合法、用的设备色彩空间与 OutputIntent 对不对得上；
//   * 透明：PDF/A-2 **允许**透明（PDF/A-1 不允许），但透明页必须有
//     OutputIntent 与页面透明组，软掩码的 /TR 必须是 /Identity；
//   * 注解：有没有 PDF/A 禁止的注解类型（Movie / Screen / RichMedia / 3D / Sound）、
//     有没有缺外观流、有没有没设 Print 标志或设了隐藏类标志。
//
// 边界（重要，不要越界理解）：
//   * 这是**预检**，不是合规认证。真正的 PDF/A 合规认证要 veraPDF
//     （Java 依赖），本仓库不引入新依赖，所以本模块永远不给
//     「已通过 PDF/A 校验」这种结论——报告里只有逐条的结构性结论。
//   * 预检只覆盖「能从文件结构里读出来」的那部分要求。至少这些没查：
//     字体程序里是否真的有页面用到的全部字形、字宽是否与程序一致、
//     XMP 是不是格式良好的 XML 与合法 RDF、ICC 配置文件内部是不是自洽、
//     透明混合模式与渲染意图的细项、页面内容与实际显示效果是否一致。
//   * 内容流里的色彩空间只在**资源字典**层面查（/ColorSpace、图像、图案、渐变）；
//     内容流里用 `cs`/`CS` 直接内联写出的色彩空间数组不查。
//   * 需要 qpdf：这些结论全是对象级信息，只有解析器能给答案。
//     本次构建没启用 qpdf（AIPDF_WITH_QPDF 默认关）时，报告里给一条
//     Error「本次构建没有启用 qpdf」，明确说做不到，不假装检查过。
PdfAPreflightReport checkPdfAPreflight(const QByteArray& pdfBytes, QString* error = nullptr);

}  // namespace aipdf::pdf
