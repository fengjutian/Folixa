#pragma once

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "doc/export/OfficeMedia.h"
#include "pdf/kernel/TextPage.h"

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
// 这一条是**纯文字**的快捷路径：输入只有文本层，所以产出里也只有文字。
// 要带图片与表格，走 exportPagesTo 的整篇那条（带内容与媒体集合的重载）。
//
// pageSizePt 是页面尺寸（点），只影响 Word 的页面设置（sectPr），
// 其余格式忽略它。
ExportResult exportPageTo(ExportFormat format, const pdf::TextPage& text,
                           const QSizeF& pageSizePt);

// 把**整篇**文档导成指定格式（页与页之间插分页符）。
//
// 为什么要有这个：原来的导出只吃一页，用户导 30 页的合同只能拿到第一页——
// 命令叫「导出到 Word」，产出却只有一页，这是**命令名与产出不符**。
// 页面尺寸统一取第一页（各页尺寸不一致时以第一页为准，并在说明里讲清楚）。
ExportResult exportPagesTo(ExportFormat format, const QVector<pdf::TextPage>& pages,
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
    // text 是**已经写好的格式控制串**，序列化时原样输出，不再转义。
    //
    // 只给「页与页之间的分页符」这类控制片段用：RTF 的 `\page` 如果走
    // escapeRtf 会被转成 `\\page`，Word 里显示成字面反斜杠、分页失效。
    bool raw = false;
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
    // 这一行的基线（PDF user space，**较小的 y**）。
    //
    // 为什么单独给一个字段：归行不能按字符盒的**中心**算——
    // 句点、逗号这类字符的盒子只有基线以上那一丁点高，中心明显偏低，
    // 按中心归组的话 "3.50" 会被拆成 "350" 与 "." 两行。
    double baseline = 0.0;
};

// 把一页的字符重建成若干行。
//
// 判定规则（都是启发式，PDF 本身不记录"行"）：
//   - 字符按上边缘从高到低、同高时按 x 从小到大排序；
//   - 字符的垂直中心与当前行中心的差不超过 0.6 倍字高时算同一行；
//   - 行内相邻字符的水平间隙超过 0.2 倍字高时，中间补一个空格
//     （PDF 的文字流里词与词之间常常没有任何标记）。
//
// 双栏页面会被**按栏拆开**（见 buildTextBands）：左右两栏同基线的文字
// 不会被并成同一行。
QVector<TextExportLine> buildTextLines(const pdf::TextPage& text);

// ---------------------------------------------------------------------------
// 分带：双栏页面为什么要拆
// ---------------------------------------------------------------------------
//
// 一页 PDF 的正文可能是**两段并排的独立文字流**（双栏）。它们有两个特点：
//   1. 同一基线上，左栏的字与右栏的字**不是同一句话**；
//   2. 每栏自己是从上到下连续排下去的。
//
// 按「整页一行行」来读，左右两栏会被并成一行（串行）；按「整页一张表」
// 来认，两栏正文会被误判成一张两列的表。
//
// 所以先把页面按**竖直的空白带**切成若干「带」：
//   - 通栏带（横跨整页的标题、跨页宽的表格）；
//   - 每个正文栏一条带。
// 每条带单独归行、单独认表：两栏正文各归各位，跨栏的表格也不会被拆散。
struct TextBand {
    // 带里的行（从上到下）。
    QVector<TextExportLine> lines;
    // 带的横向范围（点）。
    double left = 0.0;
    double right = 0.0;
    // 带的纵向范围（点）。
    //
    // 上下沿用的是 **PDF 的上下**：bottom 是较小的 y（页面上偏下），
    // top 是较大的 y（页面上偏上）——与 QRectF 的约定正好相反。
    double bottom = 0.0;
    double top = 0.0;
    // 是不是通栏（横跨整页宽度）。
    bool fullWidth = true;
};

// 把一页的字符重建成若干条带。
//
// 单栏页面只有一条通栏带（与 buildTextLines 的结果一致）。
// 双栏页面是「通栏带（标题）+ 各栏 + 通栏带（跨栏内容）」。
QVector<TextBand> buildTextBands(const pdf::TextPage& text);

// 一个落位（图片、表格）属于哪条带。
//
// 带的纵向范围是首尾相接的（通栏带占区域上下，栏带占区域本身），
// 横向范围互不重叠，所以「先按纵向找到一层，再按横向找到那一栏」。
// 找不到就退到纵向最近的那条带。
int bandIndexForPoint(const QVector<TextBand>& bands, double x, double y);

// ---------------------------------------------------------------------------
// 导出用的「一页内容」
// ---------------------------------------------------------------------------

// 一页里可导出的内容：文字带、图片、表格。
//
// 三样东西放在一起，是因为它们要按**页面上的位置**交错排进正文；
// 分开传就没法排序了。
struct OfficePageContent {
    // 文字带（按阅读顺序：通栏在前，各栏随后）。
    QVector<TextBand> bands;
    // 图片落位（坐标是 PDF user space 的点）。
    QVector<office::ImagePlacement> images;
    // 表格（坐标是 PDF user space 的点）。
    QVector<office::TableGrid> tables;

    // 这一页的全部文字行（各带依次拼接）。
    QVector<TextExportLine> allLines() const;
};

// 把**整篇**文档导成指定格式，带图片与表格。
//
// 与上面那两条的区别：那两条只有文字（`exportPageTo` 那一族本来就是
// 「把一页的文本层变成另一种格式」的工具）；这一条会把 media 里的图片
// 写成包里的独立部件，并在正文里引用，表格也写成真正的表格对象。
//
// media 是**整个包共用**的一份媒体集合：编号在包内全局连续，
// 不能按页从 1 重来（两页各一张图会撞名，一页的图会覆盖另一页的）。
ExportResult exportPagesTo(ExportFormat format, const QVector<OfficePageContent>& contents,
                            const QSizeF& pageSizePt, const office::MediaSet& media);

}  // namespace aipdf::doc
