#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "pdf/AppearanceStream.h"

namespace aipdf::pdf {

// 增量写入的结果。
struct IncrementalWrite {
    QByteArray bytes;
    bool ok = false;
    QString error;

    // 写入的外观流数量与被重写的对象数量（调试与测试用）。
    int appearanceCount = 0;
    int rewrittenObjects = 0;
};

// 一条待写入的外观流：用 marker 找到批注，把 appearance 写成它的 /AP。
struct AppearanceAttachment {
    QByteArray marker;  // 批注的 /NM，本程序写的是 "Folixa:<id>"
    Appearance appearance;
};

// 给一批批注补上外观流（/AP），**一次写入**完成。
//
// 为什么必须批量：同一页上的多条批注常常内联在**同一个页面对象**里。
// 逐条调用会把这个页面对象重写多次，只有最后一次生效——前面几条的外观流凭空消失。
// 批量版本按「承载对象」分组，同一个对象只重写一次，把所有 /AP 一起插进去。
//
// 为什么需要它：PDFium 只会渲染自带 /AP 的注释，而它的公开 API 不能创建 XObject
// （FPDFAnnot_SetAP 只写字典、不建对象）。不补这一步，我们写进文件的批注在福昕、
// Acrobat 里就是不可见的。
//
// 输入必须是 **PDFium 保存出来的字节**（FPDF_SaveAsCopy 的输出）：这里做的是在文件
// 尾部追加「重写的对象 + 外观流对象 + 新 xref」，原有字节一个都不动。解析器故意做得
// 极窄——只认经典 xref 表与直接对象，遇到对象流、交叉引用流、加密文件直接失败。
//
// 失败时 bytes 为空，调用方应当**放弃这次写入**，不要落盘半成品。
IncrementalWrite appendAnnotationAppearances(const QByteArray& pdf,
                                             const QVector<AppearanceAttachment>& attachments);

// 单条版本：内部转调批量版本。
IncrementalWrite appendAnnotationAppearance(const QByteArray& pdf,
                                            const QByteArray& marker,
                                            const Appearance& appearance);

}  // namespace aipdf::pdf
