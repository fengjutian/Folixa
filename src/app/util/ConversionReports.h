#pragma once

#include <QString>
#include <QVector>

#include "doc/convert/FontFlattening.h"
#include "doc/export/OfdExport.h"
#include "pdf/convert/ExportFormats.h"
#include "pdf/convert/DxfExport.h"
#include "pdf/ops/OcrSuspicion.h"
#include "pdf/convert/PortfolioWriter.h"
#include "pdf/compliance/PreflightChecker.h"

namespace aipdf::app {

// 印前检查的结论按严重程度分组。
//
// 为什么分组放在界面层：分组只是给人看的排版，
// 但**数量不能自己重算**——领域层已经给了 errorCount / warningCount，
// 界面再数一遍就会有两个说法。
struct PreflightGroups {
    QVector<pdf::PreflightFinding> errors;
    QVector<pdf::PreflightFinding> warnings;
    QVector<pdf::PreflightFinding> infos;

    int total() const { return errors.size() + warnings.size() + infos.size(); }
};

// 按 severity 分组。
PreflightGroups groupPreflightFindings(const pdf::PreflightReport& report);

// 顶部结论：可送印 / 不可送印 + 领域层给的摘要。
//
// 判定用 report.ok（领域层说的），摘要用 report.summary()，
// 界面不自己数一遍错误条数。
QString preflightConclusionText(const pdf::PreflightReport& report);

// 活页夹里的一行。
struct PortfolioEntryRow {
    QString name;
    QString description;
    int sizeBytes = 0;

    // 给用户看的大小。
    QString sizeText() const;
};

// 把读回来的活页夹条目变成可显示的行（顺序与包里一致）。
QVector<PortfolioEntryRow> portfolioEntryRows(const pdf::PortfolioReadResult& result);

// OCR 疑似错误的一行。
//
// 这只是**提示**，不是自动改：原始文字一个字都不能动，
// 所以这里只把领域层的结果排成行，不提供任何"替换"动作。
struct SuspicionRow {
    int pageNumber = 1;  // 1 起
    QString recognized;
    QString suggestion;
    QString reason;
    QString context;
};

// 把疑似字符排成行。
QVector<SuspicionRow> suspicionRows(const QVector<pdf::SuspiciousChar>& items);

// 各种导出的说明文案。
//
// 这些文案必须**照实说**：转曲默认是矢量轮廓化（文字变成路径、产物里没有字体），
// 轮廓化取不到字形的那些字符必须逐条告诉用户，不许只报一句「已完成」。
QString flattenFontsNoticeText(const doc::FlattenReport& report);
QString imageBasedPdfNoticeText();
// HTML 导出的说明：带图形、带图片，字号优先用内容流里的真实值。
// 走过估算时必须说出来——真实字号与估算字号对用户不是一回事。
QString htmlExportNoticeText(const pdf::HtmlExportResult& result);
// OFD 导出的说明：带图形与图片，带数量。
QString ofdExportNoticeText(const doc::OfdExportResult& result);
// CAD（DXF）导出的说明：带实体计数与图层数，并说清没做的（位图、渐变、剪裁路径）。
QString dxfExportNoticeText(const pdf::CadDxfResult& result);
// 导出到 Office 的说明文案：带这次导出的实际结果（页数 / 工作表数 / 幻灯片数）。
//
// keepsImages：这个格式带不带图片与表格（Word / PowerPoint 带，Excel 不带）。
// 为什么要有它：Word 与 PPT 现在真的带图片与表格对象，界面文案不能再说
// 「只包含文字层」——那会让用户以为图片丢了。
QString officeExportNoticeText(const QString& formatName, const QString& detail,
                               bool keepsImages = false);

}  // namespace aipdf::app
