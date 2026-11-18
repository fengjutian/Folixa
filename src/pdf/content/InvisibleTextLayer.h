#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "pdf/kernel/TextPage.h"

namespace aipdf::pdf {

// 不可见文字层的写法选项。
struct InvisibleTextLayerOptions {
    // 用页面 /Resources /Font 里**已有的**哪个字体资源来写文字。
    //
    // 为空（默认）→ 本实现自己按内容选字体，分两种情况：
    //
    //   1) 全部字符都能用 WinAnsi 表示（ASCII 与常见西文）：
    //      用**标准 14 号字体 Helvetica**（页面里没有就新建一个资源）。
    //      不依赖文档里已有的字体，所以对任何 PDF 都能用。
    //
    //   2) 含 WinAnsi 表示不了的字符（中文、日文、俄文等）：
    //      仍然以 Helvetica 为基字体，但额外写一张 /ToUnicode，
    //      把每个字节映射回它本来的 Unicode。
    //
    //      为什么这么做：直接用 Helvetica 写中文会变成乱码或问号，
    //      等于把识别出来的字悄悄丢掉；写 /ToUnicode 之后，
    //      搜索、复制、读屏软件拿到的都是正确的 Unicode。
    //      这条路线是实测可行的（不可见文字 + ToUnicode 在 PDFium 里能正常取字），
    //      代价是「字形宽度」按 Helvetica 的缺省字宽估算（见下面的说明）。
    //
    // 非空时：本实现**不新建字体**，直接用这个名字写文字，
    // 字符按「Unicode 码点拆成高、低两个字节」写进内容流（Identity-H 的写法）。
    // 调用方要保证这个字体是 Identity-H 编码、并且它的 /ToUnicode
    // 把这两个字节映射回原来的字符；本实现无法通用校验这一点，
    // 写错的后果是「搜不到字」，而不是「文件坏掉」。
    QString fontResourceName;

    // 字号兜底值：0 表示按每个字符的框高自动推算（推荐）。
    // 字符框太矮时用 minimumFontSize 兜底，避免出现 0 号字。
    double defaultFontSize = 0.0;
    double minimumFontSize = 1.0;
};

// 加了不可见文字层之后的结果。
struct InvisibleTextLayerResult {
    // 写出来的 PDF；失败时为空。
    QByteArray bytes;
    // 处理了几页。
    int pageCount = 0;
    // 写进去多少个字符。
    int charCount = 0;
    // 写成了多少段文字（连续的一段算一段）。
    int runCount = 0;
    // 是否用到了带 /ToUnicode 的字体（也就是文档里出现了非 WinAnsi 字符）。
    bool unicodeFontUsed = false;
    // 失败原因（中文）。为空表示成功。
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// 给一份 PDF 加上不可见文字层（OCR 回写）。
//
// 做什么：把识别出来的文字按原来的位置画回页面，但用**渲染模式 3**
// （3 Tr，既不填充也不描边），所以用户看不见，文档却变得可搜索、可复制、
// 能被读屏软件读出来。页面原有的可见内容一个字节都不改。
//
// 输入：
//   * pdfBytes：原始 PDF 的字节；
//   * pages：**每页一段**识别出来的文字，用 pdf::TextPage 表示
//     （每个 TextChar 带一个 PDF user space 的字符框）。
//     列表的顺序就是页序：pages[0] 对应第 0 页。
//     如果某一页的 TextPage::pageIndex 大于等于 0，它必须与它在列表里的位置一致，
//     不一致直接报错——静默按位置放会让文字落到别的页上，比报错难查得多。
//
// 页数必须与 PDF 的页数一致：少了多了都报错，并在错误里说清差几页。
// 一页文字都没有（所有页都识别不出字）时**原样返回输入字节**：不是错误，
// 也没有东西可写，调用方不必特判。
//
// 坐标：字符框与 PDFium 的字符框同一套坐标系（PDF user space，原点左下、y 向上），
// 框的下边就是基线，字号取框高。
//
// 已知边界：
//   * 中日韩等字符走「Helvetica + /ToUnicode」这条路线：文字内容是对的，
//     但字形宽度按 Helvetica 的缺省字宽估算，字形本身是空的（反正不可见）；
//     要让宽度也准，调用方可以用 fontResourceName 注入自己的字体资源。
//   * 拉丁字符的宽度按 PDF 标准 14 号字体的宽度表估算（只覆盖 ASCII 0x20-0x7E，
//     其余按 556/1000 em 估）。
//   * 一个简单字体的 /Differences 最多只能放 256 个码，所以文档里超过 255 个
//     **不同**字符时要换字体资源。换资源的位置，读者按位置补空格的启发式
//     可能会在提取出的文字里插一个空格——那是阅读器加的，不是这里写进去的；
//     字符本身一个都不会少。
//   * 每一段连续文字按 OCR 给出的字符顺序拼接；调用方给的顺序就是阅读顺序。
InvisibleTextLayerResult writeInvisibleTextLayer(const QByteArray& pdfBytes,
                                                 const QVector<TextPage>& pages,
                                                 const InvisibleTextLayerOptions& options = InvisibleTextLayerOptions());

}  // namespace aipdf::pdf
