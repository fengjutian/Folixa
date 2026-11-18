# 批量替换的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。
#
# 为什么既要 qpdf 又要 PDFium：改写走 qpdf 的内容流接口（安全内容流重写），
# 验「改完还能不能读出来」走 PDFium 的文本层——两头都要真的跑一遍才算数。

qt_add_executable(BatchReplaceTests
    ${CMAKE_CURRENT_LIST_DIR}/../BatchReplaceTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/BatchReplace.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/BatchReplace.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(BatchReplaceTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(BatchReplaceTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(BatchReplaceTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET BatchReplaceTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:BatchReplaceTests>"
        COMMENT "Copying qpdf runtime for BatchReplaceTests"
    )
endif()

target_include_directories(BatchReplaceTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(BatchReplaceTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(BatchReplaceTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(BatchReplaceTests pdf-fixtures)
aipdf_deploy_qt_runtime(BatchReplaceTests)
aipdf_deploy_pdfium(BatchReplaceTests)

add_test(NAME BatchReplaceTests COMMAND BatchReplaceTests)
