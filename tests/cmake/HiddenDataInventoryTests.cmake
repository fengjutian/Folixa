# 隐藏数据逐项清理（枚举 + 选择性清理）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。
#
# qpdf 是可选依赖：没有它的时候目标照样建，只是链接不上 qpdf，
# 用例整体跳过。这样「没启用 qpdf 时给出中文错误」那条路径也能被跑到。

qt_add_executable(HiddenDataInventoryTests
    ${CMAKE_CURRENT_LIST_DIR}/../HiddenDataInventoryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfSecurity.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfSecurity.h
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
)
target_link_libraries(HiddenDataInventoryTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test ${AIPDF_PDFIUM_LIBRARY})
target_include_directories(HiddenDataInventoryTests PRIVATE ${CMAKE_SOURCE_DIR}/src ${AIPDF_PDFIUM_INCLUDE_DIR})
target_compile_definitions(HiddenDataInventoryTests PRIVATE AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}")

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(HiddenDataInventoryTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(HiddenDataInventoryTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET HiddenDataInventoryTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:HiddenDataInventoryTests>"
        COMMENT "Copying qpdf runtime for tests")
endif()

set_target_properties(HiddenDataInventoryTests PROPERTIES WIN32_EXECUTABLE FALSE)
add_dependencies(HiddenDataInventoryTests pdf-fixtures)
aipdf_deploy_qt_runtime(HiddenDataInventoryTests)
aipdf_deploy_pdfium(HiddenDataInventoryTests)
add_test(NAME HiddenDataInventoryTests COMMAND HiddenDataInventoryTests)
