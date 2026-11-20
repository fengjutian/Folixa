# 交叉合并（多份文件逐页交叉）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、AIPDF_PDFIUM_LIBRARY、
# AIPDF_PDFIUM_INCLUDE_DIR、aipdf_deploy_* 函数）都在父作用域里定义好了，
# include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。
#
# 为什么要 PDFium：页序计算是纯计算（不碰 PDFium），但「备份一页再还原回去」
# 与「命令在真文档上执行再撤销」这两条必须走真的 PDFium——
# 页搬对没搬对，只有真文档的文本层说了算。

qt_add_executable(CrossMergeTests
    ${CMAKE_CURRENT_LIST_DIR}/../CrossMergeTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/CrossMergePlanner.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/CrossMergePlanner.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/CrossMergeCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/CrossMergeCommand.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PdfPageOperations.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PdfPageOperations.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(CrossMergeTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(CrossMergeTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(CrossMergeTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(CrossMergeTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(CrossMergeTests pdf-fixtures)
aipdf_deploy_qt_runtime(CrossMergeTests)
aipdf_deploy_pdfium(CrossMergeTests)

add_test(NAME CrossMergeTests COMMAND CrossMergeTests)
