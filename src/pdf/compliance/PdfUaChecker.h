#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// PDF/UA 合规检查的结论分级。
//
// 为什么把「通过」也做成一档：合规检查的结论是给用户看「查了几项、过了几项」的，
// 只输出问题的话，一份全绿的文件在界面上就什么都没有，
// 用户分不清「检查过且没问题」和「根本没查」。
enum class PdfUaSeverity { Pass, Warning, Error };

// 一条 PDF/UA 检查结论。
struct PdfUaFinding {
    // 机器可读码，例如 "ua-missing-lang"。
    //
    // 为什么要有码：界面按码决定怎么处理（能不能一键修、能不能折叠），
    // 文案会随语言变，码不能变。
    QString code;
    // 中文标题（领域层硬编码中文，不走 tr()：翻译的锅在 app 层）。
    QString title;
    // 具体说明：带对象名、页码、实际值，便于直接定位问题。
    QString detail;
    // 怎么修（中文）。通过项可以为空——没有要修的地方。
    QString suggestion;
    PdfUaSeverity severity = PdfUaSeverity::Error;
};

// PDF/UA 合规检查报告。
struct PdfUaReport {
    // 全部结论，按固定顺序排列（同一份输入两次跑出来的顺序必须一样）。
    QVector<PdfUaFinding> findings;
    int errorCount = 0;
    int warningCount = 0;
    int passCount = 0;
    // 是否合规：没有 Error 才算合规（Warning 不影响判定）。
    bool compliant = false;
    // 一行中文摘要，含各条结论的数量。
    QString summary() const;
};

// 对一份 PDF 字节做 PDF/UA 合规检查。
//
// 覆盖的检查项（每条都能单独触发、单独判定）：
//   * 结构：catalog 里有没有 /StructTreeRoot、结构树根是不是 /Document、
//     有没有 /ParentTree、/MarkInfo /Marked 是不是 true；
//   * 语义结构（PDF/UA 的结构树要求）：/Document 下面有没有 /Part、
//     结构类型是不是标准类型（自定义类型有没有 /RoleMap 映射）、
//     父子嵌套合不合法（例如 /TH 必须在 /TR 里、/LI 必须在 /L 里）；
//   * 页与内容的对应：每页有没有 /StructParents、
//     /ParentTree 与结构树引用的 MCID 对不对得上、
//     每页的内容是不是都进了结构树（没有游离在标签结构之外的内容）；
//   * 语言：catalog 有没有 /Lang，值像不像合法的 BCP 47 语言标记；
//   * 标题：/Info 有没有 /Title、catalog 有没有 /DisplayDocTitle true、
//     XMP 元数据在不在、元数据里有没有声明 pdfuaid；
//   * 替代文本：结构元素里的 /Figure、/Formula、表头 TH 有没有 /Alt；
//   * 字体：页面用到的字体是不是都内嵌了（没内嵌会逐个报出来）；
//   * Tab 顺序：页面的 /Tabs 是不是 /S（结构顺序）而不是 /R（阅读顺序）；
//   * 内容：结构树引用的每个 MCID 能不能在页面内容流里找到对应的标记内容。
//
// 边界（重要，不要越界理解）：
//   * 本模块只做**检查**，不做「一键生成合规文件」。生成合规文件需要完整的
//     标签编辑器（建 /Figure、/Alt、/ParentTree、补标记内容、改 /Tabs…），
//     那不在本模块范围内；本模块给出的是「哪里不合规、怎么改」的结论。
//     （一键生成由 pdf::generateUaPdf 负责，但那**不是**本模块的职责，
//     它只是调用本模块给自己的产物复检。）
//   * 检查依据是 PDF/UA-1（ISO 14289-1）里能从文件本身读出来的那些条款，
//     不做阅读器行为层面的判断（朗读顺序、朗读质量、屏幕阅读器兼容性等）。
//   * 结论里的「合规」只代表**本模块这 20 条**都没报出错误，
//     不等于通过了 PDF/UA 认证（认证要看 veraPDF 这类专业校验器）。
//   * 需要 qpdf：结构树、字体描述符、内容流里的 MCID 都是对象级信息，
//     只有解析器能给答案。本次构建没启用 qpdf（AIPDF_WITH_QPDF 默认关）时，
//     报告里给一条 Error「本次构建没有启用 qpdf」，compliant 为 false——
//     明确说做不到，不假装合规。
PdfUaReport checkPdfUa(const QByteArray& pdfBytes, QString* error = nullptr);

}  // namespace aipdf::pdf
