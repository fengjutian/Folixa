# 拼写检查的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（aipdf_deploy_qt_runtime 函数）在父作用域里定义好了，include 进来直接可用。
#
# 拼写检查是纯文本能力，不碰 PDF，也就不需要 qpdf 与 PDFium。

qt_add_executable(SpellCheckerTests
    ${CMAKE_CURRENT_LIST_DIR}/../SpellCheckerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/SpellChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/SpellChecker.h
)

target_link_libraries(SpellCheckerTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(SpellCheckerTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(SpellCheckerTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(SpellCheckerTests)

add_test(NAME SpellCheckerTests COMMAND SpellCheckerTests)
