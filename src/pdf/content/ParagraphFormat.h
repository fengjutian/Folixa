#pragma once

#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include "pdf/content/TextReflow.h"

namespace aipdf::pdf {

// 行距口径。
//
// 为什么要自己定一个枚举而不是直接复用 pdf::TextStyle::lineHeight：
// TextStyle 里的 lineHeight 只能是「倍数」，而排版需求里「固定磅值」很常见
//（例如正文 14 磅行距，行距不随字号变）。两者的换算规则不一样，
// 混在一个字段里迟早会有人算错，所以在这里显式区分。
enum class LineSpacingMode {
    // 倍数：行距 = max(字体行高, 字号 × 倍数)。
    //
    // 为什么要有 max：行距比字体自己的行高还小的话，行与行会叠在一起，
    // 那是排版事故而不是「紧凑排版」。所以真正生效的永远是两者里大的那个。
    Multiple,
    // 固定磅值：行距 = max(字体行高, 给定的点数)。
    //
    // 同样不会小于字体行高，理由与上面一样。
    Fixed
};

// 段落排版参数。
struct ParagraphFormat {
    // 字号（点）。必须大于 0。
    double fontSize = 12.0;
    // 字体族名。默认 Helvetica（标准 14 号字体，PDF 里到处都有）。
    QString fontFamily = QStringLiteral("Helvetica");

    LineSpacingMode lineSpacingMode = LineSpacingMode::Multiple;
    // Multiple：倍数（例如 1.2）；Fixed：磅值（例如 14.0）。
    // 实际行距还要和字体自己的行高取大值，详见 LineSpacingMode 的说明。
    double lineSpacing = 1.2;

    // 首行缩进（点）。**只作用于第一行**。
    double firstLineIndent = 0.0;
    // 左缩进（点）。**所有行都生效**。
    double leftIndent = 0.0;
    // 右缩进（点）。**所有行都生效**。
    double rightIndent = 0.0;

    // 对齐方式。复用 pdf::TextAlignment，行为定义见 layoutParagraph 的说明。
    TextAlignment alignment = TextAlignment::Left;
};

// 排版计划里的一行。
//
// 这一行带的是「画/写」所需要的全部信息：文字、基线位置、行首 x、行宽、字号。
// 调用方不需要再算一遍宽度，也不需要知道排版是怎么断行的。
struct ParagraphLine {
    // 这一行的文字（断行后的结果，可能与源文字里的空白略有出入）。
    QString text;
    // 基线位置：PDF user space（左下原点，y 向上）。
    QPointF baseline;
    // 行首的 x（PDF user space）。
    double x = 0.0;
    // 这一行占用的宽度：justify 且被拉伸时等于可用宽度，否则等于自然宽度。
    double width = 0.0;
    // 不拉伸时的自然宽度。
    double naturalWidth = 0.0;
    // 这一行的字号（点）。
    double fontSize = 0.0;
    // justify 时每个空格额外拉开的距离（点）；其余情况为 0。
    //
    // 为什么用「空格加宽」而不是「改字距」：改字距会让字与字之间挤在一起，
    // 中文和西文混排时尤其难看；拉空格是排版里公认的做法，
    // 而且在 PDF 里可以直接用 Tw（字间距）实现，不用改文字本身。
    double spaceExtra = 0.0;
    // 这一行是否被拉伸过（justify 且不是最后一行）。
    bool justified = false;
    // 是否是段落的最后一行。
    bool lastLine = false;
};

// 一次排版的结果。
struct ParagraphPlan {
    QVector<ParagraphLine> lines;
    // 排版实际占用的范围（PDF user space）。
    QRectF bounds;
    // 文字没排完（框里放不下剩下的部分）。
    bool overflow = false;
    // 没排完的那部分文字。
    QString remainingText;
    // 失败原因（中文）。为空表示成功。
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// 把一段文字排进一个框里，产出排版计划。
//
// 坐标系：**PDF user space**（原点在左下、y 向上），与仓库里
// pdf::MediaBox、PDFium 字符框、pdf::TextReflow 是同一套坐标。
// 框的写法是 QRectF(x, y, 宽, 高)，其中 y 是**下边**（文字从这里往上排），
// 与 pdf::TextReflow 的约定一致。
//
// 对齐的具体行为：
//   * Left：每行从内容区左边缘开始；
//   * Center：每行在**自己那一行的可用宽度**里居中；
//   * Right：每行在可用宽度里右对齐；
//   * Justify：**除最后一行外**每一行都撑满可用宽度，
//     拉伸方式是把多出来的宽度分摊给行内的空格（见 spaceExtra），
//     最后一行保持自然宽度。
//
// 缩进：
//   * 首行缩进只作用于第一行，而且**会改变第一行的断行位置**
//     （第一行按「内容宽度 - 首行缩进」断行），不是把整行平移一下；
//   * 左右缩进对所有行生效，且左右缩进会把可用宽度一起扣掉。
//
// 失败（error 非空）的情况：文字为空、框宽/框高不大于 0、字号不大于 0、
// 缩进把可用宽度吃光了、首行缩进不小于可用宽度。
// 文字排不下（框太矮）不算失败：这时 overflow 为真，remainingText 是剩下的文字。
ParagraphPlan layoutParagraph(const QString& text, const QRectF& box, const ParagraphFormat& format);

// 把排版计划写成可以直接塞进页面内容流的内容流片段。
//
// 坐标换算按 PDF 规则：原点左下、y 向上，行基线用绝对定位（Tm），
// 所以计划里的 baseline 可以原样写进去，不用再做上下翻转。
//
// 文字按 Latin-1 字节写成字面量字符串（标准 14 号字体能表示的就是这个范围）；
// 超出 Latin-1 的字符写成 '?'（简单字体无法表示它们，这里不静默丢字，
// 而是留一个可见的占位符）。需要真中文排版时应交给嵌入字体的流程。
//
// fontResourceName 是页面 /Resources /Font 里的字体资源名（例如 "F1"）。
// 计划为空或排版失败时返回空字节。
QByteArray buildParagraphContentStream(const ParagraphPlan& plan,
                                        const QString& fontResourceName = QStringLiteral("F1"));

}  // namespace aipdf::pdf
