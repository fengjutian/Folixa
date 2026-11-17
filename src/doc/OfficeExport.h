#pragma once

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "pdf/TextPage.h"

namespace aipdf::doc {

// 导出目标格式。
//
// 为什么只有这三种：这一层是"从 PDF 的文本层生成别的格式"的纯字节生成器，
// 不依赖任何外部程序（不像 app/DocumentConverter 那样调 LibreOffice）。
// 三种格式覆盖了三档需求：能进 Word 的、能进大多数富文本编辑器的、纯文本。
enum class ExportFormat {
    // Office Open XML 文字文档（.docx，zip 打包）。
    WordOpenXml,
    // RTF（Rich Text Format）。
    Rtf,
    // 纯文本。
    PlainText,
};

// 某个格式能保住什么。
//
// 为什么要有这张表：导出必然有损，调用方（界面）要能提前告诉用户
// "导出会丢什么"，而不是等用户打开文件才发现。limitation 用中文写死，
// 直接显示给用户看。
struct ExportCapability {
    // 文字内容能保住。
    bool keepsText = true;
    // 版式（位置、字号、精确排版）能保住。
    bool keepsLayout = false;
    // 图片能保住。
    bool keepsImages = false;
    // 表格结构能保住。
    bool keepsTables = false;
    // 这个格式保不住什么（中文，一句话）。
    QString limitation;
};

// 某个格式能保住什么。
ExportCapability exportCapability(ExportFormat format);

// 导出结果。
struct ExportResult {
    bool ok = false;
    // 导出的字节。失败时为空。
    QByteArray bytes;
    // 建议的文件扩展名（不含点）："docx" / "rtf" / "txt"。
    QString suggestedSuffix;
    // 失败原因（中文）。成功时为空。
    QString error;
};

// 把一页文本导成指定格式。
//
// 只导出**文本层**：图片、表格结构、批注都不会进导出结果，
// 各格式能保住什么见 exportCapability()。
//
// pageSizePt 是页面尺寸（点），只影响 Word 的页面设置（sectPr），
// 其余格式忽略它。
ExportResult exportPageTo(ExportFormat format, const pdf::TextPage& text,
                           const QSizeF& pageSizePt);

// ---------------------------------------------------------------------------
// 行重建：PDF 的字符流不是按行组织的，导出必须先把字符重建成行。
//
// 这一段放在 OfficeExport 里而不是另开一个头文件，是因为 Office 导出与
// OFD 导出都需要它，而这两个模块同属一层、同属一个人维护。
// ---------------------------------------------------------------------------

// 一行文字。
struct TextExportLine {
    // 行内文字（词之间的空格已补好）。
    QString text;
    // 行内字符，按 x 从小到大排。
    QVector<pdf::TextChar> chars;
    // 行的包围盒（PDF user space，左下原点）。
    //
    // 注意：这里的上下是**PDF 的上下**——bottom 是较小的 y（页面上偏下），
    // top 是较大的 y（页面上偏上）。这与 QRectF::top()/bottom() 的
    // 约定正好相反（QRectF 的 top() 是较小的 y），换算时别弄反。
    double left = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    double top = 0.0;
    // 行内字高（用来判断两个字符之间是不是断词）。
    double fontSize = 0.0;
};

// 把一页的字符重建成若干行。
//
// 判定规则（都是启发式，PDF 本身不记录"行"）：
//   - 字符按上边缘从高到低、同高时按 x 从小到大排序；
//   - 字符的垂直中心与当前行中心的差不超过 0.6 倍字高时算同一行；
//   - 行内相邻字符的水平间隙超过 0.2 倍字高时，中间补一个空格
//     （PDF 的文字流里词与词之间常常没有任何标记）。
QVector<TextExportLine> buildTextLines(const pdf::TextPage& text);

}  // namespace aipdf::doc
