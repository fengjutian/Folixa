#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::pdf {

// qpdf 后端是否编进了这次构建。
//
// 用途只有判断"结构校验能不能做"，不改变写入路径：写盘仍然走手写增量实现，
// qpdf 现在只做**落盘前的结构复核**。等 1.6/1.7 需要加密与签名时，它才会接管写入。
bool qpdfBackendAvailable();

// 用 qpdf 解析一遍字节，作为落盘前的最后一道结构检查。
//
// 与 PDFium 的区别：PDFium 对坏文件很宽容（能渲染就算过），qpdf 会把交叉引用、
// 流长度、字典完整性逐项走一遍，任何"修复过"的痕迹都记成 warning。
// 因此这里把**任何 warning 都当失败**——对刚由我们自己写出的文件，不该有警告。
//
// qpdf 未启用时直接返回 true（error 保持为空）：调用方不需要写 ifdef。
bool verifyWithQpdf(const QByteArray& bytes, QString* error);

}  // namespace aipdf::pdf
