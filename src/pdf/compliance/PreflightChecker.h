#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 印前检查的问题分级。
//
// 为什么分三级：印前检查的结果要交给「能不能送印」这个决定，
// 三种结论的处置完全不同——
//   Error   必须处理（JavaScript 动作会让 RIP 行为不可预期）；
//   Warning 应当处理（字体没内嵌、分辨率不够，印出来可能不是原来那张脸）；
//   Info    只是知会（页面尺寸不一致、透明组存在，本身不违规）。
enum class PreflightSeverity { Info, Warning, Error };

// 一条印前检查结论。
struct PreflightFinding {
    // 机器可读码，例如 "font-not-embedded"。
    //
    // 为什么要有码：界面按码决定怎么处理（哪些能一键修、哪些只能看），
    // 文案会随语言变，码不能变。
    QString code;
    // 人可读标题（中文硬编码，不走 tr()：领域层不碰翻译）。
    QString title;
    // 具体说明：带对象名、页码、实际数值，便于直接定位问题。
    QString detail;
    PreflightSeverity severity = PreflightSeverity::Info;
    // 所属页号（0 基）；-1 表示「整篇文档」级别的问题。
    int pageIndex = -1;
};

// 印前检查报告。
struct PreflightReport {
    // 全部结论，按固定顺序排列（同一份输入两次跑出来的顺序必须一样）。
    QVector<PreflightFinding> findings;
    int errorCount = 0;
    int warningCount = 0;
    // 是否可以送印：没有 Error 才算通过。
    bool ok = false;
    // 一行中文摘要，含各条结论的数量。
    QString summary() const;
};

// 对一份 PDF 字节做印前检查。
//
// 检查项：字体是否内嵌、图片有效分辨率、页面尺寸是否一致、有没有透明度组、
// 有没有加密、有没有 JavaScript、有没有外部引用。
//
// 失败（不是 PDF、解析不了、构建里没带 qpdf）时：
//   error 里写中文原因，报告里带一条 "document-unreadable" 的 Error 结论，
//   ok 为 false。调用方只看一个返回值的话，看 ok 就够。
PreflightReport runPreflightCheck(const QByteArray& pdfBytes, QString* error = nullptr);

}  // namespace aipdf::pdf
