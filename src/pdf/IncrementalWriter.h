#pragma once

#include <QByteArray>
#include <QString>

#include "pdf/AppearanceStream.h"

namespace aipdf::pdf {

// 增量写入的结果。
struct IncrementalWrite {
    QByteArray bytes;
    bool ok = false;
    QString error;

    // 外观流对象的编号（调试与后续复用外观流时用）。
    int appearanceObject = 0;
    // 被重写的对象编号（页面或独立注释对象）。
    int rewrittenObject = 0;
};

// 给「/NM 等于 marker」的批注补上外观流（/AP）。
//
// 为什么需要它：PDFium 只会渲染自带 /AP 的注释，而它的公开 API 不能创建
// XObject（FPDFAnnot_SetAP 只写字典、不建对象）。不补这一步，我们写进文件的
// 批注在福昕、Acrobat 里就是不可见的。
//
// 输入必须是 **PDFium 保存出来的字节**（FPDF_SaveAsCopy 的输出）：这里做的是
// 在文件尾部追加「重写的对象 + 外观流对象 + 新 xref」，原有字节一个都不动。
// 解析器故意做得极窄——只认线性扫描得到的对象与经典 xref 表，遇到对象流、
// 加密文件直接返回失败，交给后续的完整对象层处理。
//
// 失败时 bytes 为空，调用方应当**放弃这次写入**，不要落盘半成品。
IncrementalWrite appendAnnotationAppearance(const QByteArray& pdf,
                                            const QByteArray& marker,
                                            const Appearance& appearance);

}  // namespace aipdf::pdf
