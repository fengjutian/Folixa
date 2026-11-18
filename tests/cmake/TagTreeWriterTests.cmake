# 区域阅读顺序：往 PDF 里写一棵真正的标签树（/StructTreeRoot + /ParentTree + marked content）。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。
#
# 为什么既要 qpdf 又要 PDFium：写标签树是对象级操作，走 qpdf；
# 验「写完还能不能打开」「文字有没有变」「页面长相有没有变」走 PDFium——
# 两头都要真的跑一遍才算数。

qt_add_executable(TagTreeWriterTests
    ${CMAKE_CURRENT_LIST_DIR}/../TagTreeWriterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/TagTreeWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/TagTreeWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(TagTreeWriterTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(TagTreeWriterTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(TagTreeWriterTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET TagTreeWriterTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:TagTreeWriterTests>"
        COMMENT "Copying qpdf runtime for TagTreeWriterTests"
    )
endif()

target_include_directories(TagTreeWriterTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(TagTreeWriterTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(TagTreeWriterTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(TagTreeWriterTests pdf-fixtures)
aipdf_deploy_qt_runtime(TagTreeWriterTests)
aipdf_deploy_pdfium(TagTreeWriterTests)

add_test(NAME TagTreeWriterTests COMMAND TagTreeWriterTests)
