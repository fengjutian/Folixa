#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一个字符及其在 PDF 用户空间里的位置。
//
// box 用的是 **PDF user space**（原点在左下、y 向上），与
// FPDFText_GetCharBox 的定义一致；页面自带的 /Rotate 并没有被应用，
// 需要显示坐标时由上层（app/PageTransform）做旋转变换。
//
// text 是完整的 Unicode 字符：代理对会被合并成一个 QString
// （QString 内部就是 UTF-16，代理对占两个码元）。
struct TextChar {
    QString text;
    QRectF box;
    // 在 PDFium 文本页里的字符序号，用于回查原始页。
    int index = 0;
};

// 一页的文本层。
class TextPage {
public:
    // 页序号（从 0 开始）。
    int pageIndex = -1;
    // 字符按 PDFium 给出的顺序排列（内容流顺序，不一定是视觉阅读顺序；
    // 视觉顺序由 app/TextLayout 按行重建）。
    QVector<TextChar> chars;
    // 页面里存在无法映射到 Unicode 的字符（缺 ToUnicode 等情况）。
    bool unicodeMapError = false;

    bool isEmpty() const { return chars.isEmpty(); }
    int charCount() const { return chars.size(); }

    // 按字符顺序拼接的纯文本。
    QString plainText() const;
};

}  // namespace aipdf::pdf
