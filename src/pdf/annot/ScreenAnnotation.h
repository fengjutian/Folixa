#pragma once

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// ============================================================================
// 这个模块做什么、不做什么
// ============================================================================
//
// 做：把一段**媒体**挂到 PDF 的某一页上，写成一个 **/Screen 注解**
//     （媒体屏幕注解），并把媒体本体作为嵌入文件放进文档。
//     支持的容器有三种：**MP4**（ISO BMFF）、**AVI**（RIFF）、**WAV**（RIFF）。
//     写盘走 qpdf 的对象级读写（不是 PDFium），因此写出来的结构可以被
//     任何解析器重新读回；写完还会自检一遍：重新打开输出，确认注解在页上、
//     媒体字节**逐字节**与输入一致。
//
// 不做——这是**取舍**，不是缺陷：
//   * **只接受上面三种容器**。白名单外的输入**明确失败**，错误信息里会
//     列出支持哪几种、以及「改用附件」的替代做法；而不是写出一份结构好看、
//     实际放不出声的页面。
//   * **不播放**。本程序没有把媒体喂给播放器的通道；「页面内播放」是
//     阅读器（Acrobat 等具备媒体运行时的阅读器）的事。
//     本模块只保证文件结构是对的、字节是全的。
//     边界要说清：**结构写对 ≠ 阅读器会播**——Acrobat 不播 AVI，
//     WAV 在 PDF 里更适合走 /Sound（音频注解）而不是 /Screen。
//     本模块不解决这件事，只是不再把非 MP4 一律挡在门外。
//   * **不写外观流 /AP**。/Screen 是隐藏型注解（屏幕上没有画面），
//     写一个假的 /AP 反而会让阅读器拿它当「页面占位图」画出来。
//     仓库里的 /RichMedia、/Sound 也是这么处理的。
//
// 写出来的结构（字段含义见实现里的注释）：
//   注解    /Type /Annot /Subtype /Screen /Rect [...] /P 页 /F 4
//           /A << /S /Rendition /R <rendition> /OP 0 >>
//   rendition /Type /Rendition /S /MR /OP 0 /N (名字)
//             /MH << /S /MR /D 0 /R /C >>
//   MediaClip /Type /MediaClip /S /MCD /D <filespec> /M (容器对应的 MIME)
//             /MH << /S /MR /D 0 >>
//   filespec /Type /Filespec /F (name.mp4) /UF <UTF-16BE> /EF << /F 流 /UF 流 >>
//   媒体本体 /Type /EmbeddedFile /Subtype (容器对应的 MIME) /Params << /Size N >>
//   MediaClip 登记在 catalog 的 **/Assets 名字树**里，键是媒体名。
//
// MIME 由**容器**决定，调用方不必再传：
//   MP4 → video/mp4      AVI → video/x-msvideo      WAV → audio/wav
// 同一个值落在嵌入流的 /Subtype 与 MediaClip 的 /M 上；
// filespec 上**不写** MIME（PDF 规范里 filespec 也没有这一项）。
//
// 注解与 MediaClip 的关联方式：rendition 的 /N 就是 /Assets 名字树里的键，
// 读的时候按这个名字去名字树里取对应的 MediaClip。

// ============================================================================
// 数据结构
// ============================================================================

// 往页面上放一段媒体（MP4 / AVI / WAV）要带的东西。
struct ScreenMediaSpec {
    // 目标页（0 基）。越界直接失败并写明原因。
    int pageIndex = 0;
    // 注解矩形（PDF user space，左下原点）。写法 QRectF(x, y, 宽, 高)，
    // 其中 y 是矩形的**下边**（PDF 的 y 向上为正），与仓库其它模块一致。
    // 宽高必须大于 0。
    QRectF rect;
    // 媒体文件名。用来做嵌入文件的名字（filespec 的 /F、/UF）与 /Assets
    // 名字树的键。留空时按容器取默认名（clip.mp4 / clip.avi / clip.wav）。
    QString fileName;
    // 媒体原始字节。**原样**写进 PDF：一个字节都不改。
    // 容器按文件头判定，MIME 也由容器推出——所以这里没有 mimeType 可填，
    // 调用方也不需要猜 MIME（猜错了写出去就是「结构对、内容错」）。
    QByteArray mediaBytes;
};

// 读回来的一个 /Screen 注解。
struct ScreenMediaAnnotation {
    // 所在页（0 基）。
    int pageIndex = 0;
    // 注解矩形（PDF user space，y 是下边）。
    QRectF rect;
    // 媒体文件名（来自 filespec 的 /F）。
    QString fileName;
    // 媒体 MIME 类型（"video/mp4" 这种写法，不带前导斜杠）。
    // 写的时候由容器决定（MP4 / AVI / WAV 三选一），读回来是从
    // 嵌入流的 /Subtype 或 MediaClip 的 /M 上取的。
    QString mimeType;
    // 这段媒体在文档 /Assets 名字树里的**序号**（0 基）。
    //
    // 它是读回时按名字树次序编出来的号，不是写进文件里的数字：
    // 文档里已经有同名媒体时，程序会自动给新片段换名（加序号），
    // 所以这个号只在同一份文档内有效，跨文档没有可比性。
    int mediaClipId = 0;
    // 嵌入媒体的字节数（0 表示没找到媒体本体）。
    qint64 mediaSize = 0;
};

// ============================================================================
// 公开接口
// ============================================================================

// 往 PDF 的指定页面上放一个 /Screen 注解，把 spec.mediaBytes 里的媒体
// （MP4 / AVI / WAV）作为嵌入文件挂进文档。
//
// 失败（error 非空、返回空字节）的情况：
//   * 输入不是 PDF 或内容为空；
//   * 媒体字节为空；
//   * 媒体的容器不在支持列表里（按文件头判断，不看扩展名）；
//   * 页码越界；
//   * 注解矩形宽高不大于 0；
//   * 写完的自检没过（注解没落到页上，或媒体字节对不上）。
//
// **同名不覆盖**：文档里已经有同名媒体时，程序自动给新片段换一个带序号的
// 名字（"clip.mp4" → "clip (2).mp4"），两个片段并存，不报错。
QByteArray writeScreenMediaAnnotation(const QByteArray& input,
                                      const ScreenMediaSpec& spec,
                                      QString* error = nullptr);

// 读一份 PDF 里的全部 /Screen 注解（按页序、再按注解在页上的次序）。
//
// 解析不了（不是 PDF、结构坏掉）时 error 非空；文件里没有这类注解不是错误，
// 只是返回空列表。
QVector<ScreenMediaAnnotation> readScreenMediaAnnotations(const QByteArray& input,
                                                          QString* error = nullptr);

}  // namespace aipdf::pdf
