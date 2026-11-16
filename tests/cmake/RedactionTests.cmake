# 密文（保护 > 密文）四种处理方式的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。

qt_add_executable(RedactionTests
    ${CMAKE_CURRENT_LIST_DIR}/../RedactionTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Redaction.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Redaction.h
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
)
target_link_libraries(RedactionTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test ${AIPDF_PDFIUM_LIBRARY})
target_include_directories(RedactionTests PRIVATE ${CMAKE_SOURCE_DIR}/src ${AIPDF_PDFIUM_INCLUDE_DIR})
target_compile_definitions(RedactionTests PRIVATE AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}")
set_target_properties(RedactionTests PROPERTIES WIN32_EXECUTABLE FALSE)
add_dependencies(RedactionTests pdf-fixtures)
aipdf_deploy_qt_runtime(RedactionTests)
aipdf_deploy_pdfium(RedactionTests)
add_test(NAME RedactionTests COMMAND RedactionTests)
