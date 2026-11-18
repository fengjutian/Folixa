#include "app/controllers/LastWaveBridge.h"

#include "i18n/Translate.h"

#include <QHash>
#include <QLocale>

namespace aipdf::app {

namespace {

// 领域层给的标签 → 界面上看得懂的名字。读不到就原样显示。
QString structTagLabel(const QString& tag) {
    if (tag.isEmpty()) {
        return i18n::tr("aipdf::app::LastWaveBridge", "（无标签）");
    }
    return tag;
}

// 千分位分组：自己拼，不用 QLocale（结果必须与机器语言设置无关）。
QString groupThousands(const QString& digits) {
    QString grouped;
    const int count = digits.size();
    for (int i = 0; i < count; ++i) {
        if (i > 0 && (count - i) % 3 == 0) {
            grouped.append(QLatin1Char(','));
        }
        grouped.append(digits.at(i));
    }
    return grouped;
}

// 体积文案：给「嵌入字体子集化省了多少」用。
//
// 为什么自己拼而不复用别处的：那些 helper 挂在具体的类上（PortfolioEntryRow），
// 这里只是一句文案，不值得为一个函数去动别人的结构。
QString fontSizeText(int bytes) {
    if (bytes < 1024) {
        return i18n::tr("aipdf::app::LastWaveBridge", "%1 字节").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return i18n::tr("aipdf::app::LastWaveBridge", "%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    return i18n::tr("aipdf::app::LastWaveBridge", "%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

}  // namespace

// ================================================================ 任务 J ====

bool ocrPageToTextPage(const ocr::OcrPageResult& result,
                        const pdf::MediaBox& contentBox,
                        pdf::TextPage* out,
                        QString* reason) {
    const auto fail = [reason](const QString& text) {
        if (reason != nullptr) {
            *reason = text;
        }
        return false;
    };
    if (out == nullptr) {
        return fail(i18n::tr("aipdf::app::LastWaveBridge", "没有地方可以放转换结果。"));
    }
    if (result.cancelled) {
        return fail(i18n::tr("aipdf::app::LastWaveBridge", "这一页的识别被取消了。"));
    }
    if (!result.error.isEmpty()) {
        return fail(result.error);
    }

    pdf::TextPage page;
    page.pageIndex = result.pageIndex;
    page.contentBox = contentBox;

    int index = 0;
    for (const ocr::OcrLine& line : result.lines) {
        for (const ocr::OcrWord& word : line.words) {
            const QString text = word.text;
            if (text.isEmpty()) {
                continue;
            }
            // 字符个数按「用户感知的字符」算：代理对（emoji 之类）算一个字。
            int charCount = 0;
            for (int i = 0; i < text.size(); ++i) {
                const QChar ch = text.at(i);
                if (ch.isHighSurrogate() && i + 1 < text.size() &&
                    text.at(i + 1).isLowSurrogate()) {
                    ++i;
                }
                ++charCount;
            }
            if (charCount <= 0) {
                continue;
            }
            // 宽度均分，高度取词框高——**框高就是字号**，切矮了字会看不见。
            const double unitWidth = word.rect.width() / static_cast<double>(charCount);
            const double height = word.rect.height();
            for (int i = 0; i < text.size(); ++i) {
                const QChar ch = text.at(i);
                int length = 1;
                if (ch.isHighSurrogate() && i + 1 < text.size() &&
                    text.at(i + 1).isLowSurrogate()) {
                    length = 2;
                }
                pdf::TextChar character;
                character.text = text.mid(i, length);
                character.box = QRectF(word.rect.x() + unitWidth * index, word.rect.y(),
                                       unitWidth * length, height);
                character.index = index;
                page.chars.append(character);
                ++index;
                i += length - 1;
            }
        }
    }
    // 词与词之间不插空格：领域层按字符顺序拼成一段，插空格会让复制出来的文字多出空格。
    *out = page;
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

QString invisibleTextLayerPageMismatchText(int ocrPageCount, int documentPageCount) {
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "识别结果有 %1 页，文档有 %2 页（相差 %3 页）。")
        .arg(ocrPageCount)
        .arg(documentPageCount)
        .arg(qAbs(ocrPageCount - documentPageCount));
}

QString invisibleTextLayerSuccessText(const pdf::InvisibleTextLayerResult& result) {
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "已写入不可见文字层，现在可以搜索/复制这页文字了。共 %1 页、%2 个字符。")
        .arg(result.pageCount)
        .arg(result.charCount);
}

QString invisibleTextLayerFailureText(const QString& error) {
    // 原样给出领域层的中文原因：不写「操作失败」这种谁也看不懂的空话。
    if (error.isEmpty()) {
        return i18n::tr("aipdf::app::LastWaveBridge", "写入不可见文字层失败（领域层没有给出原因）。");
    }
    return error;
}

QString invisibleTextLayerReplaceNoticeText() {
    // 这是**替换**不是叠加：两层不可见文字叠在同一坐标上时阅读器只保留一层。
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "重新识别会替换上一次写入的隐藏文字层（不是再叠一层）："
                     "两层叠在同一坐标上时，阅读器只会保留其中一层。");
}

// ================================================================ 任务 K ====

QVector<pdf::TextAlignment> paragraphAlignments() {
    return QVector<pdf::TextAlignment>{
        pdf::TextAlignment::Left,
        pdf::TextAlignment::Center,
        pdf::TextAlignment::Right,
        pdf::TextAlignment::Justify,
    };
}

QString paragraphAlignmentLabel(pdf::TextAlignment alignment) {
    switch (alignment) {
        case pdf::TextAlignment::Center:
            return i18n::tr("aipdf::app::LastWaveBridge", "居中");
        case pdf::TextAlignment::Right:
            return i18n::tr("aipdf::app::LastWaveBridge", "右对齐");
        case pdf::TextAlignment::Justify:
            return i18n::tr("aipdf::app::LastWaveBridge", "两端对齐");
        case pdf::TextAlignment::Left:
        default:
            return i18n::tr("aipdf::app::LastWaveBridge", "左对齐");
    }
}

QString paragraphFormatSummaryText(const pdf::ParagraphFormat& format) {
    QString spacing;
    if (format.lineSpacingMode == pdf::LineSpacingMode::Fixed) {
        spacing = i18n::tr("aipdf::app::LastWaveBridge", "固定 %1 点").arg(format.lineSpacing);
    } else {
        spacing = i18n::tr("aipdf::app::LastWaveBridge", "%1 倍").arg(format.lineSpacing);
    }
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "字号 %1 点；行距 %2；%3；首行缩进 %4 点；左缩进 %5 点；右缩进 %6 点。")
        .arg(format.fontSize)
        .arg(spacing)
        .arg(paragraphAlignmentLabel(format.alignment))
        .arg(format.firstLineIndent)
        .arg(format.leftIndent)
        .arg(format.rightIndent);
}

// ================================================================ 任务 L ====

QVector<StructTreeRow> structTreeRows(const pdf::StructTreeReport& report) {
    QVector<StructTreeRow> rows;
    rows.reserve(report.elements.size());
    for (const pdf::StructElement& element : report.elements) {
        StructTreeRow row;
        row.tag = structTagLabel(element.tag);
        row.pageIndex = element.pageIndex;
        row.depth = element.depth;
        row.alt = element.alt;
        row.language = element.language;
        row.actualText = element.actualText;
        row.text = element.text;
        rows.append(row);
    }
    return rows;
}

QString structTreeEmptyText() {
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "这份文档没有标签树（文件里没有 /StructTreeRoot）。");
}

QString structTreeSummaryText(const pdf::StructTreeReport& report) {
    if (!report.hasStructTree) {
        return structTreeEmptyText();
    }
    QString text = i18n::tr("aipdf::app::LastWaveBridge",
                             "读到标签树：%1 个结构元素。")
                        .arg(report.elements.size());
    if (report.hasDocumentLanguage) {
        text += i18n::tr("aipdf::app::LastWaveBridge", "文档语言：%1。").arg(report.documentLanguage);
    } else {
        text += i18n::tr("aipdf::app::LastWaveBridge", "文档语言：文件里没有写。");
    }
    if (!report.notes.isEmpty()) {
        text += i18n::tr("aipdf::app::LastWaveBridge", "\n说明：%1")
                    .arg(report.notes.join(QStringLiteral("；")));
    }
    return text;
}

QString structTreeRowText(const StructTreeRow& row) {
    QString text = row.tag;
    if (row.pageIndex >= 0) {
        text += i18n::tr("aipdf::app::LastWaveBridge", "（第 %1 页，层级 %2）")
                    .arg(row.pageIndex + 1)
                    .arg(row.depth);
    } else {
        text += i18n::tr("aipdf::app::LastWaveBridge", "（层级 %1）").arg(row.depth);
    }
    if (!row.alt.isEmpty()) {
        text += i18n::tr("aipdf::app::LastWaveBridge", " 替代文本：%1").arg(row.alt);
    }
    if (!row.language.isEmpty()) {
        text += i18n::tr("aipdf::app::LastWaveBridge", " 语言：%1").arg(row.language);
    }
    if (!row.actualText.isEmpty()) {
        text += i18n::tr("aipdf::app::LastWaveBridge", " 实际文字：%1").arg(row.actualText);
    }
    if (!row.text.isEmpty()) {
        text += i18n::tr("aipdf::app::LastWaveBridge", " 正文：%1").arg(row.text);
    }
    return text;
}

// ================================================================ 任务 M ====

QVector<OcgLayerRow> ocgLayerRows(const pdf::OptionalContentSummary& summary) {
    QVector<OcgLayerRow> rows;
    rows.reserve(summary.groups.size());
    for (const pdf::OptionalContentGroup& group : summary.groups) {
        OcgLayerRow row;
        row.name = group.name;
        row.visible = group.visible;
        row.orderIndex = group.orderIndex;
        row.intent = group.intent;
        rows.append(row);
    }
    return rows;
}

QString ocgSummaryText(const pdf::OptionalContentSummary& summary) {
    if (!summary.hasOptionalContent) {
        return i18n::tr("aipdf::app::LastWaveBridge", "这份文档没有图层（没有 /OCProperties）。");
    }
    return i18n::tr("aipdf::app::LastWaveBridge", "共 %1 个图层。").arg(summary.groups.size());
}

QStringList ocgPageOverrideLines(const pdf::OptionalContentSummary& summary) {
    QStringList lines;
    for (const pdf::OptionalContentPage& page : summary.pages) {
        for (const pdf::OptionalContentCommand& command : page.commands) {
            lines.append(i18n::tr("aipdf::app::LastWaveBridge",
                                   "第 %1 页：%2（涉及图层：%3）")
                             .arg(page.pageIndex + 1)
                             .arg(command.policy)
                             .arg(command.groupNames.join(QStringLiteral("、"))));
        }
    }
    return lines;
}

QString ocgWriteBackNoticeText() {
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "写回是按上表的图层定义整体重写 /OCProperties："
                     "原来标到图层上的内容流标记不会被重建。");
}

QString ocgLayerRowText(const OcgLayerRow& row) {
    QString text = row.name;
    text += row.visible ? i18n::tr("aipdf::app::LastWaveBridge", "（默认可见）")
                        : i18n::tr("aipdf::app::LastWaveBridge", "（默认隐藏）");
    if (row.orderIndex >= 0) {
        text += i18n::tr("aipdf::app::LastWaveBridge", " 顺序：%1").arg(row.orderIndex + 1);
    }
    if (!row.intent.isEmpty()) {
        text += i18n::tr("aipdf::app::LastWaveBridge", " 用途：%1").arg(row.intent);
    }
    return text;
}

// ================================================================ 任务 N ====

QString universalConversionNoticeText(const doc::UniversalConversionResult& result) {
    if (!result.ok) {
        return result.error;
    }
    QString text = i18n::tr("aipdf::app::LastWaveBridge", "已生成 %1 页。").arg(result.pageCount);
    if (result.usedEmbeddedFont) {
        // 说清楚嵌的是哪份字体：可能是用户自己选的，也可能是程序在本机找的。
        // 不说，用户就不知道中文是靠什么显示出来的。
        if (!result.embeddedFontName.isEmpty()) {
            text += i18n::tr("aipdf::app::LastWaveBridge", "已嵌入字体 %1 来显示中文。")
                        .arg(result.embeddedFontName);
        } else {
            text += i18n::tr("aipdf::app::LastWaveBridge", "已嵌入字体，中文按嵌入字形显示。");
        }
        // 子集化省了多少要**说出来**：这是「中文不丢字」之外的另一半收益，
        // 用户看得见体积才不会以为还是 10 MB 的巨文件。
        if (result.fontSubset && result.originalFontBytes > 0 && result.embeddedFontBytes > 0) {
            text += i18n::tr("aipdf::app::LastWaveBridge",
                             "只嵌了用到的字形（%1，原字体 %2），文件不会撑大。")
                        .arg(fontSizeText(result.embeddedFontBytes),
                             fontSizeText(result.originalFontBytes));
        }
    } else if (result.replacedCharacterCount > 0) {
        // **必须**如实说清楚有多少字符显示不出来。
        text += i18n::tr("aipdf::app::LastWaveBridge",
                         "有 %1 个字符无法显示，已替换（默认用标准 14 字体，它没有中文字形；"
                         "要正确显示中文请用「选择字体文件」选一份 TTF）。")
                    .arg(result.replacedCharacterCount);
    } else {
        text += i18n::tr("aipdf::app::LastWaveBridge",
                         "用标准 14 字体排版（没有嵌入字体）。");
    }
    if (!result.warning.isEmpty()) {
        text += i18n::tr("aipdf::app::LastWaveBridge", "\n提醒：%1").arg(result.warning);
    }
    return text;
}

// ================================================================ 任务 O ====
//
// 这四个纯文案函数定义在 LastWaveBridge.h 里（inline）：AiPanel 要用它们，
// 而 AiPanelTests 目标不编译本文件。

// ================================================================ 任务 P ====

QString pdfUaConclusionText(const pdf::PdfUaReport& report) {
    if (report.compliant) {
        return i18n::tr("aipdf::app::LastWaveBridge",
                         "结论：合规（通过 %1 项、警告 %2 项、不通过 %3 项）。")
            .arg(report.passCount)
            .arg(report.warningCount)
            .arg(report.errorCount);
    }
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "结论：不合规（通过 %1 项、警告 %2 项、不通过 %3 项）。")
        .arg(report.passCount)
        .arg(report.warningCount)
        .arg(report.errorCount);
}

QString pdfUaFindingLine(const pdf::PdfUaFinding& finding) {
    QString line = QStringLiteral("[%1] %2：%3").arg(finding.code, finding.title,
                                                      finding.detail);
    if (!finding.suggestion.isEmpty()) {
        line += i18n::tr("aipdf::app::LastWaveBridge", " 建议：%1").arg(finding.suggestion);
    }
    return line;
}

QStringList pdfUaFindingLines(const pdf::PdfUaReport& report) {
    QStringList lines;
    for (const pdf::PdfUaFinding& finding : report.findings) {
        lines.append(pdfUaFindingLine(finding));
    }
    return lines;
}

QString pdfUaScopeNoticeText() {
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "这个检查器只做检查并给出修复建议，不会生成 PDF/UA 合规文件。");
}

// ================================================================ 任务 Q ====

QVector<XfaFieldRow> xfaFieldRows(const pdf::XfaReport& report) {
    QVector<XfaFieldRow> rows;
    rows.reserve(report.fields.size());
    for (const pdf::XfaField& field : report.fields) {
        XfaFieldRow row;
        row.name = field.name;
        row.label = field.label;
        row.type = field.type;
        row.value = field.value;
        row.required = field.required;
        rows.append(row);
    }
    return rows;
}

QString xfaSummaryText(const pdf::XfaReport& report) {
    if (report.status == pdf::XfaStatus::NotXfa) {
        return i18n::tr("aipdf::app::LastWaveBridge", "这份文档不是 XFA 表单（没有 /XFA 数据）。");
    }
    if (report.status == pdf::XfaStatus::Error) {
        return report.error;
    }
    return i18n::tr("aipdf::app::LastWaveBridge", "读到 %1 个 XFA 字段（跳过 %2 个没有名字的）。")
        .arg(report.fields.size())
        .arg(report.skippedWithoutName);
}

QString xfaDynamicNoticeText(const pdf::XfaReport& report) {
    if (!report.hasDynamicLogic) {
        return QString();
    }
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "这份 XFA 含动态逻辑（%1）：本程序只做静态字段提取，不执行脚本，"
                     "表单在阅读器里的实际行为取决于那些脚本。")
        .arg(report.dynamicMarkers.join(QStringLiteral("、")));
}

QString xfaFieldRowText(const XfaFieldRow& row) {
    QString text = row.name;
    if (!row.label.isEmpty() && row.label != row.name) {
        text += i18n::tr("aipdf::app::LastWaveBridge", "（%1）").arg(row.label);
    }
    text += i18n::tr("aipdf::app::LastWaveBridge", " [%1]").arg(row.type);
    if (!row.value.isEmpty()) {
        text += i18n::tr("aipdf::app::LastWaveBridge", " 值：%1").arg(row.value);
    }
    if (row.required) {
        text += i18n::tr("aipdf::app::LastWaveBridge", " 必填");
    }
    return text;
}

// ================================================================ 任务 R ====

QString richMediaScopeNoticeText() {
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "本程序只写对 3D 与多媒体注解的结构，不渲染 3D、不解码音视频；"
                     "要让模型动起来或把视频放出来，得用带相应运行时的阅读器。");
}

QString richMediaResultText(const pdf::RichMediaReadResult& result) {
    if (!result.error.isEmpty()) {
        return result.error;
    }
    return i18n::tr("aipdf::app::LastWaveBridge", "读回：%1 个 3D 注解、%2 个多媒体注解。")
        .arg(result.threeD.size())
        .arg(result.media.size());
}

// ================================================================ 任务 S ====

QString reflowNoticeText(const pdf::ReflowReport& report) {
    QString text = i18n::tr("aipdf::app::LastWaveBridge",
                             "重排完成：原来 %1 页，现在 %2 页；重排了 %3 段正文。")
                        .arg(report.sourcePageCount)
                        .arg(report.outputPageCount)
                        .arg(report.reflowedBlockCount);
    if (report.droppedCount > 0) {
        text += i18n::tr("aipdf::app::LastWaveBridge", "\n有 %1 项内容没有搬过去（见下方清单）。")
                    .arg(report.droppedCount);
    }
    if (report.degradedBlockCount > 0) {
        text += i18n::tr("aipdf::app::LastWaveBridge",
                         "\n另外有 %1 段里的字符标准 14 字体显示不了，已写成占位符。")
                    .arg(report.degradedBlockCount);
    }
    return text;
}

QStringList reflowDroppedLines(const pdf::ReflowReport& report) {
    QStringList lines;
    for (const pdf::ReflowDropped& dropped : report.dropped) {
        QString line = dropped.tag.isEmpty()
                           ? i18n::tr("aipdf::app::LastWaveBridge", "（无标签）")
                           : dropped.tag;
        line += i18n::tr("aipdf::app::LastWaveBridge", "：%1").arg(dropped.reason);
        if (dropped.pageIndex >= 0) {
            line += i18n::tr("aipdf::app::LastWaveBridge", "（原第 %1 页）")
                        .arg(dropped.pageIndex + 1);
        }
        if (!dropped.detail.isEmpty()) {
            line += i18n::tr("aipdf::app::LastWaveBridge", " %1").arg(dropped.detail);
        }
        lines.append(line);
    }
    return lines;
}

QString reflowLayoutChangeNoticeText() {
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "重排会改变版面：页数会变，图表与表格的位置会变，页眉页脚不会搬过来，"
                     "图形、表格与公式不会重排。");
}

// ================================================================ 任务 T ====

QString wizardStepFailureText(const doc::WizardStepResult& result) {
    if (result.succeeded) {
        return QString();
    }
    QString reason = result.reason;
    if (reason.isEmpty()) {
        reason = i18n::tr("aipdf::app::LastWaveBridge", "这一步没有执行。");
    }
    return i18n::tr("aipdf::app::LastWaveBridge", "第 %1 步（%2）失败：%3")
        .arg(result.index + 1)
        .arg(result.typeName)
        .arg(reason);
}

QString wizardRunSummaryText(const doc::WizardRunReport& report) {
    QString text = i18n::tr("aipdf::app::LastWaveBridge",
                             "共 %1 步：执行 %2 步，成功 %3 步，失败 %4 步，因前一步失败没执行 %5 步。")
                        .arg(report.totalSteps)
                        .arg(report.executedSteps)
                        .arg(report.succeededSteps)
                        .arg(report.failedSteps)
                        .arg(report.skippedSteps);
    for (const doc::WizardStepResult& result : report.results) {
        if (result.succeeded || result.executed) {
            continue;
        }
        const QString line = wizardStepFailureText(result);
        if (!line.isEmpty()) {
            text += QLatin1Char('\n') + line;
        }
    }
    return text;
}

QString wizardScopeNoticeText() {
    return i18n::tr("aipdf::app::LastWaveBridge",
                     "动作向导只编排本程序已有的文档级操作（合并、加页码、加水印、加密、转换格式等），"
                     "不是脚本引擎，也不会执行文档里的脚本。");
}

// ================================================================ 任务 U ====

QString invoiceAmountText(qint64 cents, const QString& currencySymbol, int decimals) {
    const bool negative = cents < 0;
    const qint64 absolute = negative ? -cents : cents;
    QString text;
    if (decimals <= 0) {
        // 0 位小数：按元四舍五入到整数。
        text = groupThousands(QString::number((absolute + 50) / 100));
    } else if (decimals == 2) {
        const QString whole = groupThousands(QString::number(absolute / 100));
        const QString fraction = QString::number(absolute % 100).rightJustified(2, QLatin1Char('0'));
        text = whole + QLatin1Char('.') + fraction;
    } else {
        const QString whole = groupThousands(QString::number(absolute / 100));
        QString fraction = QString::number(absolute % 100).rightJustified(2, QLatin1Char('0'));
        fraction += QString(static_cast<qsizetype>(decimals - 2), QLatin1Char('0'));
        text = whole + QLatin1Char('.') + fraction;
    }
    if (negative) {
        text = i18n::tr("aipdf::app::LastWaveBridge", "-") + text;
    }
    return currencySymbol + text;
}

QStringList invoiceCurrencySymbols() {
    return QStringList{
        QStringLiteral("¥"), QStringLiteral("＄"), QStringLiteral("€"),
        QStringLiteral("£"), QStringLiteral("HK$"), QStringLiteral("NT$"),
    };
}

QStringList invoiceCurrencyOptions() {
    // 币种由用户选：符号 + 中文名，不硬编码成一种。
    return QStringList{
        QStringLiteral("¥ 人民币"),
        QStringLiteral("＄ 美元"),
        QStringLiteral("€ 欧元"),
        QStringLiteral("£ 英镑"),
        QStringLiteral("HK$ 港元"),
        QStringLiteral("NT$ 新台币"),
    };
}

QString invoiceTaxRateText(qint64 basisPoints) {
    if (basisPoints < 0) {
        return i18n::tr("aipdf::app::LastWaveBridge", "沿用发票上的税率");
    }
    const QString whole = QString::number(basisPoints / 100);
    const QString fraction = QString::number(basisPoints % 100).rightJustified(2, QLatin1Char('0'));
    return whole + QLatin1Char('.') + fraction + i18n::tr("aipdf::app::LastWaveBridge", "%");
}

}  // namespace aipdf::app
