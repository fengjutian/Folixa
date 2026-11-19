# 动作向导「打印」这一步的页范围解析测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：写死绝对路径会让这份片段
# 换一台机器就废掉，而它是跟着仓库走的。
#
# 这一组是纯逻辑：只依赖 QtCore（QStringList）+ QtTest，
# 不碰 PDF、不碰 qpdf、不碰 PDFium、不碰界面。

qt_add_executable(WizardPrintRangeTests
    ${CMAKE_CURRENT_LIST_DIR}/../WizardPrintRangeTests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/app/util/WizardPageRange.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/app/util/WizardPageRange.h
)

target_link_libraries(WizardPrintRangeTests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

target_include_directories(WizardPrintRangeTests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
set_target_properties(WizardPrintRangeTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(WizardPrintRangeTests)

add_test(NAME WizardPrintRangeTests COMMAND WizardPrintRangeTests)
