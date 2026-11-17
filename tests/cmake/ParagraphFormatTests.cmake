# 段落排版（对齐 / 行距 / 缩进）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（aipdf_deploy_qt_runtime 函数）在父作用域里定义好了，
# include 进来直接可用。
#
# 为什么链 Qt6::Gui：排版要量文字宽度（pdf::TextReflow 用 QFontMetricsF），
# 而 Qt 的字体库必须在 QGuiApplication 之后才能访问——
# 所以这一组测试用 QTEST_MAIN（建 QGuiApplication），不能用 QTEST_GUILESS_MAIN。
#
# 这一组不需要 PDF 夹具，也不碰 PDFium：它测的是纯排版几何。

qt_add_executable(ParagraphFormatTests
    ${CMAKE_CURRENT_LIST_DIR}/../ParagraphFormatTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ParagraphFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ParagraphFormat.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextReflow.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextReflow.h
)

target_link_libraries(ParagraphFormatTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

target_include_directories(ParagraphFormatTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ParagraphFormatTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(ParagraphFormatTests)

add_test(NAME ParagraphFormatTests COMMAND ParagraphFormatTests)
