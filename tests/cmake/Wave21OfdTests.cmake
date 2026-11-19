# 2026-11-19 wave21：OFD 导出补上图形与图片（矢量路径 → 路径对象，位图 → 图像对象）。
#
# 单独放一个片段：这是一组可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只留一行 include，不再往那个已经很长的清单里填细节。
#
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 的 pdf-fixtures 目标之后，
# 否则 PDF_FIXTURE_DIR 还是空的。

qt_add_executable(Wave21OfdTests
    ${CMAKE_CURRENT_LIST_DIR}/../Wave21OfdTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/export/OfdExport.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/export/OfdExport.h
    ${CMAKE_SOURCE_DIR}/src/doc/export/OfficeExport.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/export/OfficeExport.h
    ${CMAKE_SOURCE_DIR}/src/doc/export/XmlText.h
    ${CMAKE_SOURCE_DIR}/src/doc/export/ZipWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/export/ZipWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageGraphics.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageGraphics.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
)

target_link_libraries(Wave21OfdTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(Wave21OfdTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(Wave21OfdTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(Wave21OfdTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(Wave21OfdTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET Wave21OfdTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:Wave21OfdTests>")
    add_dependencies(Wave21OfdTests pdf-fixtures)
endif()

set_target_properties(Wave21OfdTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(Wave21OfdTests)
aipdf_deploy_pdfium(Wave21OfdTests)

add_test(NAME Wave21OfdTests COMMAND Wave21OfdTests)
