# 字体转曲（视觉等价版：渲染成位图再写回 PDF）的测试片段。
#
# 为什么单独放一个片段：同 OfficeExportTests.cmake，这是一组可以整体拷走的测试。
#
# 这一组要真的走 PDFium：夹具 PDF 是自己造的（空白文档 + 文字层），
# 转曲与「还能不能取到文字」的判定都要真的渲染、真的再打开。
# 需要的变量：AIPDF_PDFIUM_LIBRARY、AIPDF_PDFIUM_INCLUDE_DIR、
# aipdf_deploy_pdfium（父作用域里都有）。

qt_add_executable(FontFlatteningTests
    ${CMAKE_CURRENT_LIST_DIR}/../FontFlatteningTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/FontFlattening.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/FontFlattening.h
    ${CMAKE_SOURCE_DIR}/src/doc/BlankDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/BlankDocument.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(FontFlatteningTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(FontFlatteningTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(FontFlatteningTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(FontFlatteningTests)
aipdf_deploy_pdfium(FontFlatteningTests)

add_test(NAME FontFlatteningTests COMMAND FontFlatteningTests)
