// DocumentOpen 的实现放在头文件里（inline），这个编译单元只留一份说明。
//
// 为什么放在头文件：pdf/kernel/DocumentOpen.h 被 pdf/kernel/PdfiumAdapter.cpp 包含，
// 而仓库里有四十来个测试目标各自编译了一份 pdf/kernel/PdfiumAdapter.cpp。
// 新增一个 .cpp 就要在那些目标里逐个登记，少登记一个就链接不过；
// 放在头文件里，谁编译 PdfiumAdapter.cpp 谁就自动带上实现，
// 谁也不用改。
//
// 这个 .cpp 保留下来，是为了「源文件清单里写着 pdf/kernel/DocumentOpen.cpp」
// 时也有对应的文件可编（空编译单元是合法的 C++）。
//
// 头文件实现的内容见 pdf/kernel/DocumentOpen.h：
//   * DocumentSourceKind —— 文档字节的来源（磁盘上的文件 / 内存里的字节）。
//   * DocumentReader      —— 给 PDFium 的 FPDF_FILEACCESS 回调用，
//                            按路径打开时只记路径、按需读块，
//                            不会把整份文件读进内存。
//   * documentContainsMarker —— 分块扫文件里的标记（用来判断有没有 /XFA）。
#include "pdf/kernel/DocumentOpen.h"
