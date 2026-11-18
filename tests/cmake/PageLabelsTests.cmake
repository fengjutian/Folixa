# 页面标签与编号（D6）：往 PDF 里写 /PageLabels，并把页号换算成显示文本。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。
#
# 为什么既要 qpdf 又要 PDFium：写 /PageLabels 是对象级操作，走 qpdf；
# 验「写完还能不能打开」「正文文字有没有变」走 PDFium——两头都要真跑一遍才算数。
#
# 目标名用 PageLabelsTests（跟源文件同名）；测试类叫 PageLabelTests。

qt_add_executable(PageLabelsTests
    ${CMAKE_CURRENT_LIST_DIR}/../PageLabelsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/PageLabels.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/PageLabels.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(PageLabelsTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(PageLabelsTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(PageLabelsTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET PageLabelsTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:PageLabelsTests>"
        COMMENT "Copying qpdf runtime for PageLabelsTests"
    )
endif()

target_include_directories(PageLabelsTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(PageLabelsTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(PageLabelsTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(PageLabelsTests pdf-fixtures)
aipdf_deploy_qt_runtime(PageLabelsTests)
aipdf_deploy_pdfium(PageLabelsTests)

add_test(NAME PageLabelsTests COMMAND PageLabelsTests)
