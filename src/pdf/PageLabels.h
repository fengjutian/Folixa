#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一个页码显示规则区间（对应 PDF 32000-1 §12.4.2 里的一个「页标签字典」）。
//
// startIndex 是 0 基页号：这一段从哪一页开始。
//
// count 是这一段覆盖多少页；0 表示「一直到下一个区间」（最后一段就是一直到文档末尾）。
//   为什么这个字段写进去之后读出来可能不一样：PDF 的 /PageLabels **只存起点**，
//   不存长度——一段盖到哪一页是由**下一个起点**决定的（PDF 32000-1 §12.4.2）。
//   所以 count 只是调用方表达意图用的；回读时它由「下一个起点 - 本起点」推出来。
//
// style 是编号样式："/D" 阿拉伯、"/r" 小写罗马、"/R" 大写罗马、
//   "/A" 大写字母、"/a" 小写字母；空串表示「沿用上一段的样式」。
//   写的时候给 "D" 或 "/D" 都认，统一按 "/D" 存。
//   沿用规则见下面 writePageLabels 的说明。
//
// prefix 是标签前缀（例如 "A-"）；空串表示没有前缀。
struct PageLabelRange {
    int startIndex = 0;
    int count = 0;
    QString style;
    QString prefix;
};

// 一整套标签规则。
//
// ranges 按 startIndex 升序（写的时候会自动排序，不需要调用方自己排）。
//
// firstPageNumber 是**第一段第一页**显示的号码（≥ 1）。
//   后面某段如果**沿用同一套编号样式**（style 留空表示沿用），就接着上一段往下数；
//   换了样式就从 1 重新开始——这正是「前言罗马数字、正文阿拉伯数字从 1 开始」的做法。
struct PageLabelScheme {
    QVector<PageLabelRange> ranges;
    int firstPageNumber = 1;
};

// 写入 /PageLabels，返回新的 PDF 字节；失败返回空 QByteArray 并在 error 里写中文原因。
//
// 做法：catalog → /PageLabels → <</Nums [起点 标签字典 起点 标签字典 …]>>，
//   标签字典里是 /S（样式）、/P（前缀）、/St（这一段的起始号码）。
//
// 号码怎么算（写与显示用同一套算法）：
//   * 第一段从 firstPageNumber 开始；
//   * 后面某段沿用同一套样式 → 接着上一段往下数（上一段盖了多少页由起点差算出）；
//   * 后面某段换了样式 → 从 1 重新开始。
//
// 样式的沿用规则（写与显示用同一套口径）：
//   * 某段 style 非空 → 用它；
//   * 某段 style 为空 → 沿用**上一段**（按 startIndex 排序后的前一段）解析出来的样式；
//   * 第一段 style 为空 → 这一段不带编号（PDF 里没有 /S，标签就只有前缀）。
//   沿用出来的样式会被**显式写进文件**，这样任何阅读器看到的标签都和界面预览一致。
//
// 写完会把结果重新打开读一遍：读回来的区间必须和写进去的一致，
// 对不上就算失败（宁可报错，也不要交出一份「看着写成功了其实没写进去」的 PDF）。
//
// 已知边界：/PageLabels 只影响阅读器的显示（状态栏、跳转框），
//   它不改页面内容里的页码文字——那是另一件事（页眉页脚装饰）。
QByteArray writePageLabels(const QByteArray& input, const PageLabelScheme& scheme, QString* error = nullptr);

// 读回 /PageLabels：没有 /PageLabels 时返回空列表（不是错误）。
//
// 读回来的区间按起点升序；count 由「下一个起点 - 本起点」推出来，最后一段是 0。
// 样式与前缀是文件里存着的原样（文件里没写 /S 就是空串）。
QVector<PageLabelRange> readPageLabels(const QByteArray& input, QString* error = nullptr);

// 把「第 pageIndex 页（0 基）」换算成显示文本，例如 "iv" / "A-3"。
//
// 落在没有规则的页上（任何一段的起点都比它大）时返回空串。
// 样式沿用规则与 writePageLabels 一致。
QString pageLabelText(const QVector<PageLabelRange>& ranges, int pageIndex, int firstPageNumber = 1);

// 把一个号码按样式渲染成文本（纯函数，不碰 PDF 文件）。
//
// style 用 "/D" "/R" "/r" "/A" "/a"；空串或未知样式按阿拉伯处理。
// number < 1 返回空串：PDF 的页码必须 ≥ 1。
// 罗马数字超过 3999 没有标准写法，这时退回阿拉伯数字，而不是给出一个空标签。
QString formatPageNumber(const QString& style, int number);

// 把 firstNumber 起的 count 个号码按样式渲染（纯函数）。
//
// firstNumber < 1 时从 1 开始；count < 0 返回空列表。
QVector<QString> formatPageNumbers(const QString& style, int firstNumber, int count);

}  // namespace aipdf::pdf
