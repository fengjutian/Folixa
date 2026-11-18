#pragma once

#include <QByteArray>

namespace aipdf::pdf {

// 数出这份 PDF 自己的修订数（也就是它的 xref 链有多长）。
//
// 口径：单修订的文档返回 1；被增量更新过 n 次的文档返回 n + 1。
// 解析不出来（不是 PDF、xref 链断了、字节被截断）时返回 0 ——
// 调用方要按「没有增量历史」处理，不要编一个数出来。
//
// 为什么不能直接数字节里的 %%EOF：
// 把另一份 PDF 当附件嵌进来时，内层 PDF 的 %%EOF 也在字节里，
// 会被一起数进去（多报一截增量历史）。而 %%EOF 只是「修订结束标记」，
// 文件里有多少个 %%EOF 与「这份文档有几版」并不是一回事。
//
// 办法：从文件末尾的 startxref 出发，一节一节沿 xref 的 /Prev 往回走。
// 偏移是文件里的绝对位置，内层 PDF 的偏移落在这个坐标系里是乱的，
// 因此走不到它们 —— 这条链只属于这份文档自己。
//
// 支持两种 xref 写法：
//   - classic xref 表（"xref" 开头，条目之后跟 "trailer" 字典）；
//   - xref 流（一个间接对象，字典里有 /Type /XRef 与 /Prev）。
int countPdfRevisions(const QByteArray& bytes);

}  // namespace aipdf::pdf
