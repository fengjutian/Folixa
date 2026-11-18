# 新建空白文档：领域层自己拼 PDF 字节，测试用 PDFium 验证"能打开、页数对"。
#
# 为什么测试要 PDFium：光比对字节没有意义——文件能不能被真的打开、
# 页数对不对、页面尺寸读出来是多少，只有解析器说了算。
# 这里直接用 PDFium 的 C 接口验，不牵进 PdfiumAdapter 那一串依赖。
#
# 只依赖 Qt6::Core 与 Qt6::Test + PDFium：这一层没有界面。

qt_add_executable(BlankDocumentTests
    ${CMAKE_CURRENT_LIST_DIR}/../BlankDocumentTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/BlankDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/BlankDocument.h
)
target_link_libraries(BlankDocumentTests PRIVATE
    Qt6::Core Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(BlankDocumentTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(BlankDocumentTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(BlankDocumentTests)
aipdf_deploy_pdfium(BlankDocumentTests)
add_test(NAME BlankDocumentTests COMMAND BlankDocumentTests)
