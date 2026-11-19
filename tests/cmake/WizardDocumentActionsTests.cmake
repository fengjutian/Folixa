# 动作向导「加书签」「拆分文档」两步的 app 层接线测试。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：写死绝对路径会让这份片段
# 换一台机器就废掉，而它是跟着仓库走的。
#
# 这一组是纯逻辑：只依赖 QtCore（参数解析、区间换算、文件名拼接都在里面），
# 不开窗口、不碰 PDFium、不碰 qpdf。
#
# 编进来的两半：
#   - app 层：参数解释那几个自由函数（在 app/editor/PdfView.h 里，
#     头文件形式是为了让这一步不用开窗口就能测）；
#   - 领域层：pdf::PageSplitPlanner（页区间的切法归它算，app 层只翻译参数）。

qt_add_executable(WizardDocumentActionsTests
    ${CMAKE_CURRENT_LIST_DIR}/../WizardDocumentActionsTests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/pdf/ops/PageSplitPlanner.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/pdf/ops/PageSplitPlanner.h
)

target_link_libraries(WizardDocumentActionsTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)

target_include_directories(WizardDocumentActionsTests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
set_target_properties(WizardDocumentActionsTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(WizardDocumentActionsTests)

add_test(NAME WizardDocumentActionsTests COMMAND WizardDocumentActionsTests)
