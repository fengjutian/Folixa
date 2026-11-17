#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ai/OfflineQa.h"
#include "doc/ActionWizard.h"
#include "doc/InvoiceLayout.h"
#include "doc/UniversalConverter.h"
#include "i18n/Translate.h"
#include "ocr/OcrTypes.h"
#include "pdf/DocumentReflow.h"
#include "pdf/InvisibleTextLayer.h"
#include "pdf/OptionalContent.h"
#include "pdf/PdfUaChecker.h"
#include "pdf/ParagraphFormat.h"
#include "pdf/RichMedia3d.h"
#include "pdf/StructTreeReader.h"
#include "pdf/TextPage.h"
#include "pdf/XfaSupport.h"

namespace aipdf::app {

// ================================================================ 任务 J ====
//
// OCR 隐藏文本层回写。
//
// 领域层要的是 pdf::TextPage（**每个字符**一个 PDF user space 的框），
// OCR 侧给的是 ocr::OcrPageResult（**词级**框）。仓库里没有现成的转换函数，
// 这里补上：把一个词的框横向按字符数均分成若干个字符框。
//
// 为什么宽度均分、**高度取词框高**：领域层把框高当字号用，
// 切矮了字会看不见（InvisibleTextLayer.cpp 里就是这么用的）。

// 把一页 OCR 识别结果转成 pdf::TextPage。
//
// 失败时返回 false，reason 里是中文原因（识别被取消 / 引擎报错）。
bool ocrPageToTextPage(const ocr::OcrPageResult& result,
                        const pdf::MediaBox& contentBox,
                        pdf::TextPage* out,
                        QString* reason = nullptr);

// 识别页数与文档页数对不上时的中文原因（差几页要说清楚）。
QString invisibleTextLayerPageMismatchText(int ocrPageCount, int documentPageCount);

// 写完之后的提示语。
QString invisibleTextLayerSuccessText(const pdf::InvisibleTextLayerResult& result);

// 写失败时的提示：**原样给出领域层的中文原因**，不写「操作失败」这种空话。
QString invisibleTextLayerFailureText(const QString& error);

// 写之前的说明：这一遍是**替换**上一次写入的隐藏文字层，不是再叠一层。
QString invisibleTextLayerReplaceNoticeText();

// ================================================================ 任务 K ====
//
// 段落排版。

// 可选的对齐方式（顺序就是界面上排列框里的顺序）。
QVector<pdf::TextAlignment> paragraphAlignments();
// 对齐方式的中文名。
QString paragraphAlignmentLabel(pdf::TextAlignment alignment);
// 把段落参数摊成一行说明（界面上要显示「原样交给领域层」的那些值）。
QString paragraphFormatSummaryText(const pdf::ParagraphFormat& format);

// ================================================================ 任务 L ====

// 标签树里的一行（界面表格用）。
struct StructTreeRow {
    QString tag;
    int pageIndex = -1;
    int depth = 0;
    QString alt;
    QString language;
    QString actualText;
    QString text;
};

// 把领域层的结构树摊成界面的一行行。
QVector<StructTreeRow> structTreeRows(const pdf::StructTreeReport& report);

// 顶部摘要。**没有标签树时给出明确说明**，不让用户以为读过了。
QString structTreeSummaryText(const pdf::StructTreeReport& report);

// 没有标签树时显示的那句话（单独一个函数，方便测试直接断言）。
QString structTreeEmptyText();

// 一行结构元素的说明（表格里显示什么）。
QString structTreeRowText(const StructTreeRow& row);

// ================================================================ 任务 M ====

// 图层面板的一行。
struct OcgLayerRow {
    QString name;
    bool visible = true;
    int orderIndex = -1;
    QString intent;
};

// 图层清单（顺序与 readOptionalContent 给的一致）。
QVector<OcgLayerRow> ocgLayerRows(const pdf::OptionalContentSummary& summary);

// 顶部摘要。
QString ocgSummaryText(const pdf::OptionalContentSummary& summary);

// 页面级覆盖（哪些页面单独改过某个图层的可见性）。
QStringList ocgPageOverrideLines(const pdf::OptionalContentSummary& summary);

// 写回时的边界说明：写回是按方案整体重写 /OCProperties 的。
QString ocgWriteBackNoticeText();

// 一行图层的说明。
QString ocgLayerRowText(const OcgLayerRow& row);

// ================================================================ 任务 N ====

// 任意文件 → PDF 之后的提示。
//
// **必须**如实说出「有 N 个字符无法显示，已替换」——除非真的用上了嵌入字体。
QString universalConversionNoticeText(const doc::UniversalConversionResult& result);

// ================================================================ 任务 O ====
//
// 下面这四个是**纯文案**函数，内联在头里：
//
// AiPanel 要用它们渲染离线问答的界面，而既有测试目标 AiPanelTests 只编译
// AiPanel.cpp（tests/CMakeLists.txt 不是本批能改的清单）。文案函数定义在
// LastWaveBridge.cpp 里的话，AiPanelTests 会链接失败（未定义引用）。
// 内联在头里就没有这个跨目标依赖了。

// 找到答案时的正文（找不到时返回空串：找不到就说找不到，不编答案）。
inline QString offlineAnswerText(const ai::OfflineAnswer& answer) {
    // 找不到就是没有正文：不编一句「看起来像答案」的话。
    if (answer.isNoMatch()) {
        return QString();
    }
    return answer.text;
}

// 没找到时的说明。
inline QString offlineNoMatchText(const ai::OfflineAnswer& answer) {
    if (!answer.isNoMatch()) {
        return QString();
    }
    QString text = i18n::tr("aipdf::app::LastWaveBridge", "本地索引里没找到相关内容。");
    if (!answer.notice.isEmpty()) {
        text += i18n::tr("aipdf::app::LastWaveBridge", "\n%1").arg(answer.notice);
    }
    return text;
}

// 引用清单（编号 / 页码 / 引文）。
inline QStringList offlineCitationLines(const ai::OfflineAnswer& answer) {
    QStringList lines;
    for (const ai::OfflineCitation& citation : answer.citations) {
        lines.append(i18n::tr("aipdf::app::LastWaveBridge", "[%1] 第 %2 页：%3")
                         .arg(citation.number)
                         .arg(citation.pageIndex + 1)
                         .arg(citation.quote));
    }
    return lines;
}

// 最接近的几个片段（找不到时列出来供人工判断）。
inline QStringList offlineClosestLines(const ai::OfflineAnswer& answer) {
    QStringList lines;
    for (const ai::OfflineCitation& citation : answer.closest) {
        lines.append(i18n::tr("aipdf::app::LastWaveBridge", "第 %1 页：%2")
                         .arg(citation.pageIndex + 1)
                         .arg(citation.quote));
    }
    return lines;
}

// ================================================================ 任务 P ====

// 顶部结论（是否合规 + 各项计数）。
QString pdfUaConclusionText(const pdf::PdfUaReport& report);

// 一条结论：code + title + detail + suggestion。
QString pdfUaFindingLine(const pdf::PdfUaFinding& finding);

// 全部结论（按领域层给的顺序）。
QStringList pdfUaFindingLines(const pdf::PdfUaReport& report);

// 检查器的边界说明：**只做检查**，不生成合规文件。
QString pdfUaScopeNoticeText();

// ================================================================ 任务 Q ====

// XFA 字段的一行。
struct XfaFieldRow {
    QString name;
    QString label;
    QString type;
    QString value;
    bool required = false;
};

// 字段清单。
QVector<XfaFieldRow> xfaFieldRows(const pdf::XfaReport& report);

// 顶部摘要。
QString xfaSummaryText(const pdf::XfaReport& report);

// 动态 XFA 的提示（**必须**出现「只做静态字段提取，不执行脚本」）。
QString xfaDynamicNoticeText(const pdf::XfaReport& report);

// 一行字段的说明。
QString xfaFieldRowText(const XfaFieldRow& row);

// ================================================================ 任务 R ====

// 3D / 多媒体注解的边界说明：**只写结构，不渲染、不解码**。
QString richMediaScopeNoticeText();

// 写完之后的结果说明。
QString richMediaResultText(const pdf::RichMediaReadResult& result);

// ================================================================ 任务 S ====

// 重排的整体说明（版面会变、丢了什么）。
QString reflowNoticeText(const pdf::ReflowReport& report);

// 丢了哪些内容：**逐条**一行。
QStringList reflowDroppedLines(const pdf::ReflowReport& report);

// 重排前的警告：版面一定会变。
QString reflowLayoutChangeNoticeText();

// ================================================================ 任务 T ====

// 一步失败时显示的原因（用领域层给的中文原因，不写「操作失败」）。
QString wizardStepFailureText(const doc::WizardStepResult& result);

// 整份向导跑完的摘要。
QString wizardRunSummaryText(const doc::WizardRunReport& report);

// 向导的能力边界：只编排已有的文档级操作，不是脚本引擎。
QString wizardScopeNoticeText();

// ================================================================ 任务 U ====

// 金额的显示文本：币种符号 + 千分位 + 小数位。
//
// 币种与小数位由用户选，不硬编码成一种。
QString invoiceAmountText(qint64 cents, const QString& currencySymbol, int decimals);

// 可选币种（符号 + 中文名）。
QStringList invoiceCurrencySymbols();

// 可选币种的完整清单（符号 + 中文名），给界面上的下拉框用。
QStringList invoiceCurrencyOptions();

// 税率（万分比）显示成百分比。
QString invoiceTaxRateText(qint64 basisPoints);

}  // namespace aipdf::app
