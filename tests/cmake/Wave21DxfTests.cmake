# 2026-11-19 wave21：导出 DXF 深度（真曲线 / 尺寸标注 / 图层 / 颜色与线宽）。
#
# 单独放一个片段：这是一组可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只留一行 include，不再往那个已经很长的清单里填细节。
#
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 的 pdf-fixtures 目标之后，
# 否则 PDF_FIXTURE_DIR 还是空的。
#
# 为什么整组都挂在 qpdf 条件下：深度的 DXF 导出走的是共享底座
# pdf::extractPageGraphics（只有 qpdf 能读全内容流里的曲线控制点、
# 颜色与线宽）。没有 qpdf 的构建里这条能力根本不存在，硬编一套
# 只会得到一份永远失败的测试。

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    qt_add_executable(Wave21DxfTests
        ${CMAKE_CURRENT_LIST_DIR}/../Wave21DxfTests.cpp
        ${CMAKE_SOURCE_DIR}/src/pdf/convert/DxfExport.cpp
        ${CMAKE_SOURCE_DIR}/src/pdf/convert/DxfExport.h
        ${CMAKE_SOURCE_DIR}/src/pdf/convert/ExportFormats.cpp
        ${CMAKE_SOURCE_DIR}/src/pdf/convert/ExportFormats.h
        ${CMAKE_SOURCE_DIR}/src/pdf/content/PageGraphics.cpp
        ${CMAKE_SOURCE_DIR}/src/pdf/content/PageGraphics.h
        ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
        ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
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

    target_link_libraries(Wave21DxfTests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Test
        ${AIPDF_PDFIUM_LIBRARY}
        qpdf::libqpdf
    )

    target_include_directories(Wave21DxfTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${AIPDF_PDFIUM_INCLUDE_DIR}
    )

    target_compile_definitions(Wave21DxfTests PRIVATE
        AIPDF_WITH_QPDF=1
        AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
    )

    add_custom_command(TARGET Wave21DxfTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:Wave21DxfTests>")
    add_dependencies(Wave21DxfTests pdf-fixtures)

    set_target_properties(Wave21DxfTests PROPERTIES WIN32_EXECUTABLE FALSE)

    aipdf_deploy_qt_runtime(Wave21DxfTests)
    aipdf_deploy_pdfium(Wave21DxfTests)

    add_test(NAME Wave21DxfTests COMMAND Wave21DxfTests)
else()
    message(STATUS "Wave21DxfTests：当前构建没有 qpdf，跳过这组测试")
endif()
