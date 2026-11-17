#include "app/ConversionReports.h"

#include "i18n/Translate.h"

namespace aipdf::app {

namespace {

}  // namespace

PreflightGroups groupPreflightFindings(const pdf::PreflightReport& report) {
    PreflightGroups groups;
    for (const pdf::PreflightFinding& finding : report.findings) {
        switch (finding.severity) {
        case pdf::PreflightSeverity::Error:
            groups.errors.append(finding);
            break;
        case pdf::PreflightSeverity::Warning:
            groups.warnings.append(finding);
            break;
        case pdf::PreflightSeverity::Info:
        default:
            groups.infos.append(finding);
            break;
        }
    }
    return groups;
}

QString preflightConclusionText(const pdf::PreflightReport& report) {
    // 能不能送印是领域层的判定，界面照着写。
    const QString verdict = report.ok ? i18n::tr("aipdf::app::MainWindow", "可以送印") : i18n::tr("aipdf::app::MainWindow", "不可送印");
    return i18n::tr("aipdf::app::MainWindow", "%1：%2").arg(verdict, report.summary());
}

QString PortfolioEntryRow::sizeText() const {
    if (sizeBytes < 1024) {
        return i18n::tr("aipdf::app::MainWindow", "%1 字节").arg(sizeBytes);
    }
    if (sizeBytes < 1024 * 1024) {
        return i18n::tr("aipdf::app::MainWindow", "%1 KB").arg(sizeBytes / 1024.0, 0, 'f', 1);
    }
    return i18n::tr("aipdf::app::MainWindow", "%1 MB").arg(sizeBytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QVector<PortfolioEntryRow> portfolioEntryRows(const pdf::PortfolioReadResult& result) {
    QVector<PortfolioEntryRow> rows;
    for (const pdf::PortfolioEntry& entry : result.entries) {
        PortfolioEntryRow row;
        row.name = entry.name;
        row.description = entry.description;
        row.sizeBytes = entry.bytes.size();
        rows.append(row);
    }
    return rows;
}

QVector<SuspicionRow> suspicionRows(const QVector<pdf::SuspiciousChar>& items) {
    QVector<SuspicionRow> rows;
    for (const pdf::SuspiciousChar& item : items) {
        SuspicionRow row;
        row.pageNumber = item.pageIndex + 1;
        row.recognized = item.recognized;
        row.suggestion = item.suggestion;
        row.reason = item.reason;
        row.context = item.context;
        rows.append(row);
    }
    return rows;
}

QString flattenFontsNoticeText() {
    // 这一句必须照实说：转曲是**视觉等价**的栅格化版本，
    // 文字变成像素，不能再选中、不能再搜索，文件也会变大。
    // 不许在这里宣称已经变成矢量或轮廓。
    return i18n::tr("aipdf::app::MainWindow", "转曲结果：每一页按设定分辨率栅格化成图片后重新写入 PDF。"
                "外观与原文件一致，但文字不能再选中或搜索，文件体积会变大。");
}

QString imageBasedPdfNoticeText() {
    return i18n::tr("aipdf::app::MainWindow", "图像化 PDF：每一页变成一张位图。页面尺寸不变，"
                "文字默认不能再选中（勾选保留文字层时可以搜索），图片按 JPEG 嵌入。");
}

QString htmlExportNoticeText() {
    return i18n::tr("aipdf::app::MainWindow", "HTML 导出：只包含文字与基本排版，不含图片与图形；"
                "字号由字形高度估算，与原版排版只大致相符。");
}

QString dxfExportNoticeText() {
    return i18n::tr("aipdf::app::MainWindow", "CAD 导出：只导路径与可选的文字，不含颜色、线宽与图层；"
                "曲线会被打散成折线。");
}

QString ofdExportNoticeText() {
    return i18n::tr("aipdf::app::MainWindow", "OFD 导出：只导文本对象，不含图片、图形与签章；坐标单位是毫米。");
}

QString officeExportNoticeText(const QString& formatName) {
    return i18n::tr("aipdf::app::MainWindow", "导出为 %1：只包含文字层，不含图片、表格结构与批注。").arg(formatName);
}

}  // namespace aipdf::app
