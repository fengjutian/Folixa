// OOXML 资源通道的实现。
//
// 为什么实现放在头文件里（inline）而不是这个 .cpp：
// 主程序目标的源文件清单在 src/CMakeLists.txt 里，那份清单由统一收口的人维护，
// 新增 .cpp 如果没被登记进去，链接期就会报「undefined reference」。
// 把实现内联进头文件，任何目标只要 include 就能用，不必改任何清单——
// 这是「新增文件不该拖别人下水」的代价，值。
//
// 这个 .cpp 保留下来，是为了让「这是一个独立模块」这件事在目录里看得见，
// 也让测试目标可以把它列出来（它会编译成一个空的翻译单元，无副作用）。

#include "doc/export/OfficeMedia.h"

namespace aipdf::doc::office {

// 目前这个模块的全部实现都在头文件里（见上）。这里留一个翻译单元，
// 将来要把媒体集合的落盘逻辑收拢到这里时，不用再动头文件。

}  // namespace aipdf::doc::office
