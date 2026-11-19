# 2026-11-19 wave21：3D —— U3D/PRC 嵌入与表现层渲染。

qt_add_executable(Wave213dTests
    ${CMAKE_CURRENT_LIST_DIR}/../Wave213dTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/RichMedia3d.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/RichMedia3d.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/ThreeDModel.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/ThreeDModel.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(Wave213dTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    # PRC 里的 SVG 回退表示要用 QSvgRenderer 画出来。
    Qt6::Svg
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(Wave213dTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(Wave213dTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(Wave213dTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(Wave213dTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET Wave213dTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:Wave213dTests>")
    add_dependencies(Wave213dTests pdf-fixtures)
endif()

set_target_properties(Wave213dTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(Wave213dTests)
aipdf_deploy_pdfium(Wave213dTests)

add_test(NAME Wave213dTests COMMAND Wave213dTests)
