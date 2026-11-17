#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::doc {

// 字体转曲（把文字变成不再依赖字体的形式）的参数。
struct FlattenOptions {
    // 栅格化分辨率。300 DPI 是印刷常用的下限；越高越清晰，文件也越大。
    double dpi = 300.0;
    // 转曲之后是否把原来的文本层去掉。
    //
    // true（默认）：输出里只剩位图，文字不能再选中、不能再搜索，
    //   也不再依赖任何字体——这才是「字体转曲」想要的结果。
    // false：位图盖在原文字上面，文字层仍然保留，可以搜索、可以复制，
    //   但字体依赖也仍然在（文件里还嵌着原来的字体）。
    bool removeTextLayer = true;
};

// 把一份 PDF 的字体"转曲"。
//
// ⚠ 重要取舍：这里给的是**视觉等价的转曲**，不是轮廓化转曲。
//
// 真正的轮廓化转曲需要拿到字形轮廓数据（每个字的贝塞尔路径），
// 把文字替换成路径对象。本仓库拿不到 PDF 内嵌字体的字形轮廓：
// PDFium 的公开接口不提供字形路径，仓库里也没有字体轮廓解析器。
// 所以这里的做法是：把每一页渲染成高分辨率位图，再把位图写回一份新 PDF。
// 结果是**看起来一样**，但代价是：
//   - 文字变成像素：不能再选中、不能再搜索、不能再复制；
//   - 文件体积变大（位图比文字大得多）；
//   - 打印质量取决于 dpi，不及矢量输出。
//
// 换句话说：这是一份"图像化 PDF"，不是"轮廓化 PDF"。
// 面向真实印前的轮廓化转曲需要另外接字体轮廓数据源。
//
// 失败时返回空数组，并把原因（中文）写进 error。
QByteArray flattenFontsToOutlines(const QByteArray& pdfBytes, const FlattenOptions& options,
                                  QString* error = nullptr);

// 检查一份 PDF 里是否还有可提取的文字。
//
// 转曲结果的判定依据：转曲后（removeTextLayer = true）这里应当返回 false。
// 输入不是有效 PDF 时返回 false（没有文字可提取）。
bool hasExtractableText(const QByteArray& pdfBytes);

}  // namespace aipdf::doc
