#include "pdf/annot/AnnotationSummary.h"

#include "i18n/Translate.h"

#include <QDateTime>
#include <QHash>

namespace aipdf::pdf {

namespace {

QString escape(const QString& text) {
    // 批注正文是用户输入：不转义就会把表格结构打断，甚至在打印视图里执行脚本。
    QString escaped = text;
    escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return escaped;
}

}  // namespace

QString annotationsToHtmlSummary(const QVector<Annotation>& annotations, int pageCount,
                                 const QString& title) {
    QString html;
    html += QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
    html += QStringLiteral("<title>") + escape(title) + QStringLiteral("</title></head>");
    html += QStringLiteral("<body style=\"font-family:Segoe UI,Microsoft YaHei,sans-serif;"
                           "color:#202a3a;margin:24px;\">");
    html += QStringLiteral("<h1 style=\"font-size:20px;margin:0 0 4px;\">") + escape(title) +
            QStringLiteral("</h1>");
    html += QStringLiteral("<p style=\"color:#5b6675;margin:0 0 16px;\">") +
            i18n::tr("aipdf::pdf::AnnotationSummary", "共 %1 条批注 · %2 页 · 导出于 %3")
                .arg(annotations.size())
                .arg(pageCount)
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))) +
            QStringLiteral("</p>");

    if (annotations.isEmpty()) {
        html += QStringLiteral("<p style=\"color:#5b6675;\">") +
                i18n::tr("aipdf::pdf::AnnotationSummary", "这份文档还没有批注。") +
                QStringLiteral("</p>");
    } else {
        html += QStringLiteral("<table style=\"border-collapse:collapse;width:100%;font-size:13px;\">");
        html += QStringLiteral(
                    "<thead><tr style=\"background:#f2f5f9;text-align:left;\">"
                    "<th style=\"padding:6px 8px;border:1px solid #d8dee8;\">%1</th>"
                    "<th style=\"padding:6px 8px;border:1px solid #d8dee8;\">%2</th>"
                    "<th style=\"padding:6px 8px;border:1px solid #d8dee8;\">%3</th>"
                    "<th style=\"padding:6px 8px;border:1px solid #d8dee8;\">%4</th>"
                    "<th style=\"padding:6px 8px;border:1px solid #d8dee8;\">%5</th>"
                    "</tr></thead><tbody>")
                    .arg(i18n::tr("aipdf::pdf::AnnotationSummary", "页"),
                         i18n::tr("aipdf::pdf::AnnotationSummary", "类型"),
                         i18n::tr("aipdf::pdf::AnnotationSummary", "内容"),
                         i18n::tr("aipdf::pdf::AnnotationSummary", "作者"),
                         i18n::tr("aipdf::pdf::AnnotationSummary", "时间"));

        QVector<Annotation> sorted = annotations;
        std::sort(sorted.begin(), sorted.end(), [](const Annotation& a, const Annotation& b) {
            if (a.pageIndex != b.pageIndex) {
                return a.pageIndex < b.pageIndex;
            }
            return a.id < b.id;
        });

        for (const Annotation& annotation : sorted) {
            const QString color = annotation.color.isValid() ? annotation.color.name()
                                                             : QStringLiteral("#ffd600");
            html += QStringLiteral("<tr>");
            html += QStringLiteral("<td style=\"padding:6px 8px;border:1px solid #d8dee8;\">%1</td>")
                        .arg(annotation.pageIndex + 1);
            html += QStringLiteral(
                        "<td style=\"padding:6px 8px;border:1px solid #d8dee8;\">"
                        "<span style=\"display:inline-block;width:10px;height:10px;margin-right:6px;"
                        "background:%1;\"></span>%2</td>")
                        .arg(color, escape(annotationTypeName(annotation.type)));
            const QString contents = annotation.contents.isEmpty() ? QStringLiteral("—")
                                                                   : escape(annotation.contents);
            html += QStringLiteral("<td style=\"padding:6px 8px;border:1px solid #d8dee8;\">%1</td>")
                        .arg(contents);
            const QString author = annotation.author.isEmpty() ? QStringLiteral("—")
                                                               : escape(annotation.author);
            html += QStringLiteral("<td style=\"padding:6px 8px;border:1px solid #d8dee8;\">%1</td>")
                        .arg(author);
            const QString when = annotation.createdAt.isValid()
                                     ? annotation.createdAt.toLocalTime().toString(
                                           QStringLiteral("yyyy-MM-dd HH:mm"))
                                     : QStringLiteral("—");
            html += QStringLiteral("<td style=\"padding:6px 8px;border:1px solid #d8dee8;\">%1</td>")
                        .arg(when);
            html += QStringLiteral("<td style=\"display:none;\">%1</td>").arg(annotation.reviewState);
            html += QStringLiteral("</tr>");
        }
        html += QStringLiteral("</tbody></table>");
    }

    html += QStringLiteral("</body></html>");
    return html;
}

}  // namespace aipdf::pdf
