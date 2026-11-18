#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

#include "pdf/kernel/PdfiumAdapter.h"

namespace aipdf::pdf {

// 一处密文标记。
//
// rect 用的是 **PDF user space**（左下原点、y 向上），和字符框、contentBox()
// 同一坐标系；页面自带的 /Rotate **不**被应用进去。
struct RedactionMark {
    QRectF rect;
    // 目标页序号（0 基）。-1 表示「文档里的每一页都放这一处标记」。
    int pageIndex = -1;
};

// 密文处理方式。
//
// 这四个不是同一件事的四种叫法，它们对文字层与渲染层的影响各不相同：
//
//   Mark   标记：只在页面上盖一块醒目的黑块，**内容原封不动**留在文件里，
//                  文字层照样能搜到、复制到原文。
//   Mosaic 马赛克：把区域像素化（块状化），并删掉与区域相交的对象：
//                  肉眼看不清原文，文字层里也读不到了。
//   Erase  擦除：删掉与区域相交的对象，文字层里读不到；
//                  **页面其余部分仍然是矢量文字**（可以再编辑、再搜索）。
//   Apply  应用密文：整页位图化，标记落实；原内容既不可见也不可搜索，
//                  但代价是整页都变成了一张图。
//
// 诚实性边界（很重要，别在界面上承诺过头）：
//   Erase / Mosaic / Apply 都是**渲染层**的移除。它们改的是 PDFium 文档里的
//   对象与内容流；除非整页位图化后再整体重新序列化，否则原始字形数据仍可能
//   以别的形式留在文件字节里。任何情况下都**不能**把它们说成
//   「密码学安全的不可恢复删除」——真要那个保证，得把整页重新栅格化并
//   重新编码写盘（Apply 接近这个效果，但仍然不是密码学擦除）。
enum class RedactionMode {
    Mark,
    Mosaic,
    Erase,
    Apply,
};

// 标记为密文：只在页面上画一块黑块，不动任何内容。
//
// 注意：标记**只是视觉提示**，原文还在文件里，文字层照样能搜到。
// 要真正落实标记，必须用 applyRedactions()（整页位图化）。
//
// 调用约定：这几个函数会改写页面的内容流。PdfDocument::page() 每次都是
// FPDF_LoadPage，拿到的句柄对内容流改动是「旧视图」——想看到结果要**重新取一次
// 页面句柄**（或重新渲染）。上层做完密文处理后要重新拿页面，别拿旧句柄去断言。
bool markRedactions(PdfDocument& document,
                    const QVector<RedactionMark>& marks,
                    QString* error = nullptr);

// 马赛克：把区域像素化，并删掉与区域相交的对象。
// 只把马赛克图贴回该区域，页面其余部分保持矢量。
bool mosaicRedactions(PdfDocument& document,
                      const QVector<RedactionMark>& marks,
                      QString* error = nullptr);

// 擦除：删掉与区域相交的对象（文字层里也不再出现），并补一块白底，
// 避免留下半截字形。页面其余部分仍是矢量文字。
bool eraseRedactions(PdfDocument& document,
                     const QVector<RedactionMark>& marks,
                     QString* error = nullptr);

// 应用密文：把标记真正落实——整页位图化，标记区域变黑。
// 之后整页都没有文字层了。
bool applyRedactions(PdfDocument& document,
                     const QVector<RedactionMark>& marks,
                     QString* error = nullptr);

}  // namespace aipdf::pdf
