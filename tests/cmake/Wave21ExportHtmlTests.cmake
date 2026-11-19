# 2026-11-19 wave21：导出 HTML 深度（带图 + 带图形 + 真字号）。
#
# 单独放一个片段：这是一组可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只留一行 include，不再往那个已经很长的清单里填细节。
#
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 的 pdf-fixtures 目标之后，
# 否则 PDF_FIXTURE_DIR 还是空的。

qt_add_executable(Wave21ExportHtmlTests
    ${CMAKE_CURRENT_LIST_DIR}/../Wave21ExportHtmlTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageGraphics.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageGraphics.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/convert/ExportFormats.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/convert/ExportFormats.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TableStructure.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TableStructure.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
)

target_link_libraries(Wave21ExportHtmlTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(Wave21ExportHtmlTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(Wave21ExportHtmlTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(Wave21ExportHtmlTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(Wave21ExportHtmlTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET Wave21ExportHtmlTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:Wave21ExportHtmlTests>")
    add_dependencies(Wave21ExportHtmlTests pdf-fixtures)
endif()

set_target_properties(Wave21ExportHtmlTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(Wave21ExportHtmlTests)
aipdf_deploy_pdfium(Wave21ExportHtmlTests)

add_test(NAME Wave21ExportHtmlTests COMMAND Wave21ExportHtmlTests)
