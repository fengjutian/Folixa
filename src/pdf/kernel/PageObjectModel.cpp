// 页面对象模型的翻译单元。
//
// 这个模块的实现在头文件里（header-only），原因写在 PageObjectModel.h 顶部：
// pdf::reflowTaggedDocument 在 tests/cmake/DocumentReflowTests.cmake 那个目标里编译，
// 那个目标只编 DocumentReflow.cpp 与它的几个依赖，不编本模块的 .cpp；
// 实现放在头文件里，那个目标不用改一行 CMake 就能链接。
//
// 这个 .cpp 的作用是**头文件自包含性检查**：单独编译它，
// 能保证 PageObjectModel.h 不依赖任何别的 .cpp 才能用（少一个头文件就编不过）。
//
// 将来若把实现挪回 .cpp，需要同步做两件事：
//   1. 在 src/CMakeLists.txt 里登记本文件；
//   2. 在 tests/cmake/DocumentReflowTests.cmake 里也登记本文件。

#include "pdf/kernel/PageObjectModel.h"

namespace aipdf::pdf {

// 头文件实现的存在性检查：如果头文件里的东西被删了或改了名字，这里就编不过。
//
// 角色值是**写进文件里**的（标记里的 role 参数），改了它旧文件就认不出来了。
static_assert(static_cast<int>(PageTextRole::Header) == 1 &&
                  static_cast<int>(PageTextRole::Footer) == 2,
              "PageTextRole 的取值不能随便改：它被写进重排结果的标记里。");

}  // namespace aipdf::pdf
