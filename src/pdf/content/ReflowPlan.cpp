// 重排计划的翻译单元。
//
// 与 PageObjectModel.cpp 同理：实现在头文件里（header-only），
// 原因是 pdf::reflowTaggedDocument 所在的测试目标不编本模块的 .cpp
// （详见 PageObjectModel.h 顶部的说明与 src/pdf/content/ReflowPlan.h）。
//
// 这个 .cpp 的作用是**头文件自包含性检查**。

#include "pdf/content/ReflowPlan.h"

namespace aipdf::pdf {

// 头文件实现的存在性检查。
static_assert(sizeof(ReflowAnchor) > 0, "ReflowAnchor 不能是空壳。");

}  // namespace aipdf::pdf
