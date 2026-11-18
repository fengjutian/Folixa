# 行聚类：把一页的字符聚成文字行（区域阅读顺序的底座）。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 为什么要真 PDFium：夹具那一组用例会真的打开 text.pdf 抽出字符，
# 验证的是「真实排版」下的行数，不是人造坐标下的算术。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。

qt_add_executable(PageTextLinesTests
    ${CMAKE_CURRENT_LIST_DIR}/../PageTextLinesTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTextLines.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTextLines.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(PageTextLinesTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(PageTextLinesTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(PageTextLinesTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

set_target_properties(PageTextLinesTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(PageTextLinesTests pdf-fixtures)
aipdf_deploy_qt_runtime(PageTextLinesTests)
aipdf_deploy_pdfium(PageTextLinesTests)

add_test(NAME PageTextLinesTests COMMAND PageTextLinesTests)
