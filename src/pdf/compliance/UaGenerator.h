#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "pdf/compliance/PdfUaChecker.h"
#include "pdf/compliance/StructTreeModel.h"

namespace aipdf::pdf {

// 一键生成 PDF/UA 文件的选项。
struct UaGeneratorOptions {
    // 文档语言（写进 catalog 的 /Lang）。空表示不写。
    //
    // 默认 zh-CN：中文文档最常见的语言。界面应当让用户改。
    QString language = QStringLiteral("zh-CN");
    // 文档标题（写进 /Info /Title）。空表示沿用原文件里的标题。
    QString title;
    // 要不要写 /DisplayDocTitle true（阅读器窗口显示标题而不是文件名）。
    bool displayDocTitle = true;
};

// 一键生成的结果。
struct UaGeneratorResult {
    // 产物有没有通过**本项目检查器**的全部检查（没有 Error）。
    //
    // 注意它的意思：不是「通过了 PDF/UA 认证」。真正的认证要 veraPDF。
    bool ok = false;
    // 失败原因（中文）。生成过程本身失败时这里有内容。
    QString error;
    // 产物字节。生成过程失败时为空。
    QByteArray bytes;
    // 对产物**重跑** checkPdfUa 得到的报告。
    PdfUaReport report;
    // 写进去的语义树（回读的结果，便于界面回显与测试核对）。
    SemanticTree tree;
    // 需要人工确认的地方：归类不可靠的块、缺替代文本的块、检查器报出的问题。
    QStringList issues;
    // 过了本项目检查器的项数。
    int passedChecks = 0;
    // 还需要人工确认的项数。
    int manualReviewItems = 0;
    // 一行中文摘要（**不是**合规声明）。
    QString summaryText;
};

// 一键生成：角色推断 → 写语义树 → 补元数据 → 对产物重跑检查器。
//
// 做的四件事：
//   1. 扫内容流，把页面上的文字、图、表单域整理成「块」；
//   2. 推断每个块的角色（标题 / 段落 / 列表 / 表格 / 图 / 域），
//      搭出 Document → Part → Sect → … 的语义树；
//   3. 写语义树，并补上 /Lang、/Info /Title、/DisplayDocTitle、
//      含 pdfuaid:part="1" 的 XMP 元数据、每页的 /Tabs /S；
//   4. **对产物重跑** pdf::checkPdfUa，把报告一并返回。
//
// 纪律：
//   * 产物仍有 Error 时 ok=false，并把问题列进 issues；
//   * 本函数**绝不**宣布「已通过 PDF/UA 校验」——它只能报告
//     「过了本项目检查器的 N 项检查，另有 M 项需人工确认」；
//   * 没有依据的替代文本**不写**，并记一条 issue（不替用户编一句）。
UaGeneratorResult generateUaPdf(const QByteArray& pdfBytes,
                                const UaGeneratorOptions& options = UaGeneratorOptions(),
                                QString* error = nullptr);

// 生成器的边界说明（中文原文，供界面显示）。
//
// 为什么单独给一个函数：界面要显示「这不是合规认证」这句提示，
// 但这句话属于 app 层的话术；领域层只提供这句技术说明的原文。
QString uaGenerationDisclaimer();

}  // namespace aipdf::pdf
