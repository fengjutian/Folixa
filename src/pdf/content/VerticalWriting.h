#pragma once

#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 书写方向。
//
// 为什么要单列一个枚举：竖排与横排的差别不只是「字怎么摆」，还牵连到
// 字体字典（/WMode 1、/DW2、/W2）与内容流顺序。把方向显式写出来，
// 调用方就不必各自猜「vertical 这个布尔到底意味着什么」。
enum class WritingMode { Horizontal, Vertical };

// 竖排度量。
//
// 单位一律是 **1/1000 em**（与 PDF 字体字典里的数同口径）：
// 字身框高度、竖排原点、默认宽度都用这个单位，换算成点只需乘字号再除 1000。
struct VerticalMetrics {
    // 竖排原点的高度：字体的竖排 ascender（vhea.ascender）。
    double ascent = 0.0;
    // 竖排原点的下沉量：字体的竖排 descender（vhea.descender，通常是负数）。
    double descent = 0.0;
    // 竖排默认宽度：一个字身框有多高（vmtx 的竖排 advance）。
    double defaultWidth = 0.0;
    // 字体里到底有没有竖排度量。
    //
    // **没有就是没有**：为假时调用方应当退回「竖排默认宽度」（1 em），
    // 并如实把这次排版标成降级，不许拿横排宽度冒充竖排宽度。
    bool hasVerticalMetrics = false;
};

// 读一份 TrueType 字体字节里的竖排度量。
//
// 字体没有 vhea / vmtx 时，返回值的 hasVerticalMetrics 为假，
// 其余字段保持 0 —— 不编数。
VerticalMetrics readVerticalMetrics(const QByteArray& fontBytes);

// 竖排里的一个字。
struct VerticalGlyph {
    // 这个字对应的文本（一个 grapheme）。
    QString text;
    // 内容流里写的字符码（Identity-H 下是两字节 CID 的十六进制串，不带尖括号）。
    //
    // 为什么由调用方给：内容流要写的是**码**，不是 Unicode。竖排的「码」怎么来
    // （Identity-H 的 CID、嵌入字体的 GID……）取决于字体怎么登记，这一层不该猜。
    QByteArray code;
    // 第几列。**0 是最右列**（中文竖排从右往左读）。
    int columnIndex = 0;
    // 列内第几个字。0 是最上面那个。
    int rowIndex = 0;
    // 文字原点（PDF user space，原点左下、y 向上）。
    //
    // 竖排下这个原点落在**字身框的顶边**：字形按竖排原点（w1y / w1x）往下挪，
    // 整个字身框从原点往下占 advance 那么多高。
    QPointF origin;
    // 这个字在列内往下推进的距离（点）。
    double advance = 0.0;
    // 这个字身框的宽度（点）：横排时占多宽，竖排时也一样宽。
    double width = 0.0;
};

// 竖排的一列（竖着的一行）。
struct VerticalColumn {
    // 这一列第一个字的文字原点（PDF user space）。
    QPointF origin;
    // 这一列的字，自上而下。
    QVector<VerticalGlyph> glyphs;
};

// 竖排切列的结果。
struct VerticalLayout {
    // 各列，**columns[0] 是最右列**（阅读顺序的第一列）。
    QVector<VerticalColumn> columns;
    // 按阅读顺序展开的字：列自右向左、列内自上而下。
    //
    // 这就是「内容流顺序 == 阅读顺序」的那份顺序，写内容流时按它写就对了。
    QVector<VerticalGlyph> glyphs;
    // 文字没排完（框里放不下剩下的部分）。
    bool overflow = false;
    // 排进去的源文字长度（UTF-16 码元）。
    qsizetype consumedLength = 0;
    // 没排完的那部分文字。
    QString remainingText;
    // 失败原因（中文）。为空表示成功。
    QString error;
    // 是不是走了降级路径（字体没有竖排度量，按竖排默认宽度推进）。
    bool verticalMetricsDegraded = false;
};

// 竖排排版参数。
//
// 坐标系：**PDF user space**（原点在左下、y 向上），与 PDF 页面、PDFium 字符框同一套。
// 竖排一列的顶边在**上沿**（y 大的那边），列内自上而下就是 y 递减。
struct VerticalLayoutOptions {
    // 字号（点）。
    double fontSize = 12.0;
    // 排版区的四条边（PDF user space）。
    //
    // 为什么不用 QRectF：这个仓库里 PDF 空间的矩形是「y = 下边」的口径
    // （见 PdfiumAdapter 的换算），Qt 的 top() 其实是 PDF 的**下沿**，
    // 竖排里「哪边是上沿」很容易搞反。写成四条边就没有这个坑。
    double left = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    double top = 0.0;
    // 竖排度量（1/1000 em）。hasVerticalMetrics 为假时按降级处理。
    VerticalMetrics metrics;
    // 降级时用的竖排默认宽度（1/1000 em）。默认 1000（1 em）。
    double defaultWidth = 1000.0;
    // 列与列之间的额外间距（点）。列距本身是 1 个字身框。
    double columnGap = 0.0;
    // 列内字与字之间的额外间距（点）。
    double glyphGap = 0.0;
    // 字身框的宽度（点）。0 表示与字身框高度相同（中文竖排是 1 em 见方）。
    double glyphWidth = 0.0;
};

// 把一段文字竖排进一个框里。
//
// 规则（中文竖排的阅读顺序，别写反）：
//   * **列自右向左**：第 0 列在最右边，然后一列一列往左排；
//   * **列内自上而下**：每列的字从上往下走；
//   * 每字按**竖排度量**推进（字身框高度），不是横排宽度。
//
// 没有竖排度量时（metrics.hasVerticalMetrics 为假），按 defaultWidth（1 em）推进，
// 并把返回值里的 verticalMetricsDegraded 标成真 —— 降级要看得见。
VerticalLayout layoutVerticalText(const QString& text, const VerticalLayoutOptions& options);

// 把竖排结果写成内容流片段。
//
// **内容流顺序 == 阅读顺序**：列自右向左、列内自上而下。
// 这是「抽取顺序与渲染一致」的关键 —— 抽取工具（PDFium）按内容流顺序给字符，
// 内容流按阅读顺序写，抽取结果自然就是阅读顺序。
//
// 每个字写成一个 Tj：竖排下字与字之间的推进由字体的竖排量（/W2、/DW2）负责，
// 内容流只负责把每一列的起点摆到位。
//
// fontResourceName 是页面 /Resources /Font 里的字体资源名（例如 "F1"）。
// 排版失败或没有可写的字时返回空字节。
QByteArray buildVerticalContentStream(const VerticalLayout& layout,
                                       const QString& fontResourceName = QStringLiteral("F1"),
                                       double fontSize = 12.0);

// 一个 CID 的竖排度量（1/1000 em）。
struct VerticalCidMetrics {
    // CID（字形号，Identity 映射下就是 GID）。
    int cid = 0;
    // w1y：竖排原点的 y。
    double originY = 0.0;
    // w1x：竖排原点的 x。取半个 em（500）时字身框居中于列心。
    double originX = 0.0;
    // vy：竖排位移。**向下为负**（PDF 规范与 PDFium 都按这个符号约定）。
    double displacement = 0.0;
};

// 生成 /W2 数组（PDF 1.7 表 116：c [w1y w1x vy]）。
//
// 说明两点：
//   * 每个 CID **三个数**（w1y / w1x / vy）。只给一个数是不对的：
//     PDFium 读 /W2 时按每组三个数取（w1y、w1x、vy），缺一个就会读歪。
//   * CID 是**绝对编号**（从 0 起算），不是相对偏移；连续且度量相同的 CID
//     会并成一条 `c [w1y w1x vy w1y w1x vy ...]`。
QByteArray buildVerticalWidthArray(const QVector<VerticalCidMetrics>& cids);

// 生成竖排字体字典的片段：/WMode 1 + /DW2 [w1y vy]（+ /W2）。
//
// /DW2 是竖排的默认度量：w1y 取竖排 ascent，vy 取负的竖排默认宽度（1 em）。
// /W2 只在**有竖排度量且能映射 CID** 时才给：没有竖排度量就只写 /WMode 1 与
// /DW2（用规范默认值 [880 -1000]），不编 /W2 的数。
QByteArray buildVerticalFontEntries(const VerticalMetrics& metrics,
                                    const QVector<VerticalCidMetrics>& cids);

// 按字体字节与用到的 CID 生成竖排字体字典片段。
//
// 这是给「调用方只知道自己用了哪些 CID」的场景用的：竖排度量直接从字体里读，
// 读不到就退回 /DW2 的规范默认值，并且**不给 /W2**（不编数）。
QByteArray buildVerticalFontEntriesForFont(const QByteArray& fontBytes, const QVector<int>& cids);

}  // namespace aipdf::pdf
