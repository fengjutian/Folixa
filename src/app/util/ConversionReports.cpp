#include "app/util/ConversionReports.h"

#include "i18n/Translate.h"

#include <QStringList>

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

QString flattenFontsNoticeText(const doc::FlattenReport& report) {
    // 这一句必须照实说：转曲默认走**矢量轮廓化**（文字变成路径，产物里没有字体），
    // 但轮廓化不是对所有文档都成功——Type3 字体、缺 ToUnicode 的 CID 字体都取不到字形。
    // 那些字符的去向（丢掉 / 保留原字）必须当面对用户讲清楚。
    const int unconverted = report.droppedCharCount + report.keptCharCount;
    QString text = i18n::tr("aipdf::app::MainWindow",
                            "转曲结果：%1 个字符已转成矢量路径，%2 个字符取不到轮廓。"
                            "产物里不再有字体依赖，文字不能再选中或搜索。")
                        .arg(report.outlinedCharCount)
                        .arg(unconverted);
    if (!report.skipped.isEmpty()) {
        const int shown = qMin(report.skipped.size(), 5);
        QStringList lines;
        for (int index = 0; index < shown; ++index) {
            lines.append(report.skipped.at(index));
        }
        text += QLatin1Char('\n') +
                i18n::tr("aipdf::app::MainWindow", "取不到轮廓的字符（前 %1 条，共 %2 条）：%3")
                    .arg(shown)
                    .arg(report.skipped.size())
                    .arg(lines.join(QLatin1String("；")));
    }
    for (const QString& note : report.notes) {
        text += QLatin1Char('\n') + note;
    }
    return text;
}

QString imageBasedPdfNoticeText() {
    return i18n::tr("aipdf::app::MainWindow", "图像化 PDF：每一页变成一张位图。页面尺寸不变，"
                "文字默认不能再选中（勾选保留文字层时可以搜索），图片按 JPEG 嵌入。");
}

QString htmlExportNoticeText(const pdf::HtmlExportResult& result) {
    // 这一句必须照实说：现在 HTML 导出带矢量图形（内嵌 SVG）与内嵌图片，
    // 字号优先用内容流里的真实值；只有取不到真实字号的那几行才按字形高度估算。
    QString text = i18n::tr("aipdf::app::MainWindow",
                            "HTML 导出：包含文字、矢量图形（内嵌 SVG，%1 条）与内嵌图片（%2 张）；"
                            "字号优先用内容流里的真实字号，取不到的那几行按字形高度估算。")
                        .arg(result.vectorPathCount)
                        .arg(result.imageCount);
    if (result.fontSizeEstimated && !result.fontSizeNote.isEmpty()) {
        text += QLatin1Char('\n') + result.fontSizeNote;
    }
    return text;
}

QString dxfExportNoticeText(const pdf::CadDxfResult& result) {
    // 这一句必须照实说：现在导的是**深度 DXF**——三次贝塞尔保形（样条实体）、
    // 尺寸标注单独分层、图层表齐全、颜色与线宽都写进文件。
    // 同时把没做的也说清楚：位图不导、渐变与剪裁路径会丢。
    QString text = i18n::tr("aipdf::app::MainWindow",
                             "CAD 导出：直线 %1 条、折线 %2 条、样条（真曲线）%3 条、文字 %4 条、"
                             "尺寸标注 %5 个；图层 %6 个（含 PATH / TEXT / DIM），颜色与线宽一并写入；"
                             "位图不导出，渐变与剪裁路径会丢失。")
                         .arg(result.lineCount)
                         .arg(result.polylineCount)
                         .arg(result.splineCount)
                         .arg(result.textCount)
                         .arg(result.dimensionCount)
                         .arg(result.layers.size());
    if (result.imageCount > 0) {
        text += QLatin1Char('\n') +
                i18n::tr("aipdf::app::MainWindow", "文档里有 %1 张位图，这一版没有导出位图。")
                    .arg(result.imageCount);
    }
    return text;
}

QString ofdExportNoticeText(const doc::OfdExportResult& result) {
    // 这一句必须照实说：OFD 导出现在带文字、矢量图形与内嵌图片，
    // 坐标单位是毫米；签章、注释、书签、附件等 OFD 规范里的东西没导。
    return i18n::tr("aipdf::app::MainWindow",
                    "OFD 导出：文字、矢量图形与内嵌图片都带上（%1 个矢量图形、%2 张图片）；"
                    "坐标单位是毫米，签章与注释不在导出范围内。")
        .arg(result.pathCount)
        .arg(result.imageCount);
}

QString officeExportNoticeText(const QString& formatName, const QString& detail, bool keepsImages) {
    // detail 是这次导出的实际结果（页数 / 工作表数 / 幻灯片数 / 图片数 / 表格数）。
    // 为什么要有它：光说「导出了」用户不知道拿到的是什么，
    // 说清楚「几页、几张图、几个表」才知道对不对。
    QString text = i18n::tr("aipdf::app::MainWindow", "导出为 %1：%2").arg(formatName, detail);
    if (keepsImages) {
        text += i18n::tr("aipdf::app::MainWindow",
                         "保留文字层与图片，表格按识别结果写成表格；"
                         "精确版式、批注与字体样式会简化。");
        return text;
    }
    text += i18n::tr("aipdf::app::MainWindow",
                     "只包含文字层：图片、批注与精确版式不会带过去；"
                     "表格按识别结果写成表格，认不出的按文字行导出。");
    return text;
}

}  // namespace aipdf::app
