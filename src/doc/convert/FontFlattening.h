#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace aipdf::doc {

// 转曲的模式。
enum class FlattenMode {
    // 矢量轮廓化：文字变成路径对象（m / l / c + f）。
    //
    // 产物里**没有字体、没有可提取文字**：文字不能再选中、不能再搜索，
    // 但打印与缩放仍是矢量，文件也不会因为位图而变大。
    Outline,
    // 栅格化（旧行为）：整页渲染成高分辨率位图再写回一份新 PDF。
    //
    // 视觉等价，但文字变成像素：不能再选中、不能再搜索，体积随 dpi 平方增长，
    // 打印质量取决于 dpi。
    Raster,
};

// 字体转曲（把文字变成不再依赖字体的形式）的参数。
struct FlattenOptions {
    // 转曲方式。默认是矢量轮廓化。
    FlattenMode mode = FlattenMode::Outline;

    // 栅格化分辨率。300 DPI 是印刷常用的下限；越高越清晰，文件也越大。
    // **只在 Raster 模式下有意义。**
    double dpi = 300.0;

    // 栅格化之后是否把原来的文本层去掉。
    //
    // true（默认）：输出里只剩位图，文字不能再选中、不能再搜索，
    //   也不再依赖任何字体——这才是「字体转曲」想要的结果。
    // false：位图盖在原文字上面，文字层仍然保留，可以搜索、可以复制，
    //   但字体依赖也仍然在（文件里还嵌着原来的字体）。
    // **只在 Raster 模式下有意义。**
    bool removeTextLayer = true;

    // 轮廓化时，转不了字的那些页要不要保留原文字。
    //
    // false（默认）：转不了的字被丢掉，产物里一定没有字体。
    //   代价是那些字**真的没了**——所以每一处丢掉都会写进报告，
    //   调用方必须把报告给用户看，不许当成功就完事。
    // true：转不了的页保留原文字与字体（那一页的字体仍然嵌在产物里），
    //   报告里会写明哪一页走了这条回退。
    //
    // **只在 Outline 模式下有意义。**
    bool keepTextWhenUnconvertible = false;
};

// 一次转曲的汇报。
//
// 为什么要有它：轮廓化不可能对所有文档都成功（Type3 字体、缺字形、
// 没有 ToUnicode 的 CID 字体……）。这些情况必须**逐条说出来**，
// 不许悄悄跳过后宣称「全部转曲」。
struct FlattenReport {
    // 整体是否成功（产物已生成）。
    bool ok = false;
    // 处理的页数。
    int pageCount = 0;
    // 转成了路径的字符数。
    int outlinedCharCount = 0;
    // 转不了、被丢掉的字符数（逐条原因见 skipped）。
    int droppedCharCount = 0;
    // 走「保留原文字」回退的字符数。
    int keptCharCount = 0;
    // 总体说明（中文）：替代字体、渲染模式、回退策略等。
    QStringList notes;
    // 逐条说明：哪个字符、为什么没转成轮廓。
    QStringList skipped;
    // 失败原因（中文）。成功时为空。
    QString error;
};

// 把一份 PDF 的字体"转曲"。
//
// 两种模式（FlattenOptions::mode）：
//
//   Outline（默认，真正的矢量轮廓化）：
//     逐字取字形轮廓（PDFium 的字形路径 API），把文字换成路径对象，
//     并把页面内容流里的显示文字算子删掉、把字体资源去掉。
//     产物：没有 FontFile、没有可提取文字、页数与页面尺寸不变、
//     文字仍然是矢量（可选中不了，但打印与缩放不失真）。
//     取不到轮廓的字会按 FlattenOptions::keepTextWhenUnconvertible 处理，
//     并逐条写进 FlattenReport。
//
//   Raster（旧行为，视觉等价的整页栅格化）：
//     把每一页渲染成高分辨率位图再写回一份新 PDF。
//     结果是**看起来一样**，但文字变成像素：不能再选中、不能再搜索，
//     文件体积变大（位图比文字大得多），打印质量取决于 dpi。
//
// 失败时返回空数组，并把原因（中文）写进 error。
//
// 旧调用方注意：默认模式从「栅格化」变成了「轮廓化」。
// 想要旧行为就显式写 FlattenOptions::mode = FlattenMode::Raster。
QByteArray flattenFontsToOutlines(const QByteArray& pdfBytes, const FlattenOptions& options,
                                  QString* error = nullptr);

// 同上，但额外给一份汇报（转了多少字、哪些没转、为什么）。
QByteArray flattenFontsToOutlines(const QByteArray& pdfBytes, const FlattenOptions& options,
                                  FlattenReport* report, QString* error = nullptr);

// 检查一份 PDF 里是否还有可提取的文字。
//
// 转曲结果的判定依据：轮廓化产物（以及 removeTextLayer = true 的栅格化产物）
// 这里应当返回 false。
// 输入不是有效 PDF 时返回 false（没有文字可提取）。
bool hasExtractableText(const QByteArray& pdfBytes);

}  // namespace aipdf::doc
