# 2026-11-19 wave21：字体转曲改成真正的矢量轮廓化（字形轮廓 → 路径），不再是栅格化。
#
# 这一组要真的渲染，所以链接 PDFium（渲染前后逐像素比对）。
# 轮廓取自 Windows GDI（AddFontMemResourceEx + GetGlyphOutline），
# 因此链接 gdi32（见 src/CMakeLists.txt 里的链接口径）。

qt_add_executable(Wave21FlattenTests
    ${CMAKE_CURRENT_LIST_DIR}/../Wave21FlattenTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/convert/FontFlattening.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/convert/FontFlattening.h
    ${CMAKE_SOURCE_DIR}/src/doc/convert/GlyphOutline.h
    ${CMAKE_SOURCE_DIR}/src/doc/convert/OutlinePdfWriter.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/BlankDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/BlankDocument.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/document/QpdfBackend.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/QpdfBackend.h
)

target_link_libraries(Wave21FlattenTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
    gdi32
)

target_include_directories(Wave21FlattenTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(Wave21FlattenTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(Wave21FlattenTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET Wave21FlattenTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:Wave21FlattenTests>")
    add_dependencies(Wave21FlattenTests pdf-fixtures)
endif()

set_target_properties(Wave21FlattenTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(Wave21FlattenTests)
aipdf_deploy_pdfium(Wave21FlattenTests)

add_test(NAME Wave21FlattenTests COMMAND Wave21FlattenTests)
