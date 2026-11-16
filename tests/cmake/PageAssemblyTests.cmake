# 文档拼装（合并 / 提取 / 页面装饰 / 拆分口径）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。

qt_add_executable(PageAssemblyTests
    ${CMAKE_CURRENT_LIST_DIR}/../PageAssemblyTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PageSplitPlanner.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PageSplitPlanner.h
    ${CMAKE_SOURCE_DIR}/src/pdf/PageDecorations.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PageDecorations.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
)

target_link_libraries(PageAssemblyTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(PageAssemblyTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(PageAssemblyTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(PageAssemblyTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(PageAssemblyTests pdf-fixtures)
aipdf_deploy_qt_runtime(PageAssemblyTests)
aipdf_deploy_pdfium(PageAssemblyTests)

add_test(NAME PageAssemblyTests COMMAND PageAssemblyTests)
