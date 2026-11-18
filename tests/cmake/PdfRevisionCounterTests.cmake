# 数文档自己的修订数（xref 链长度）的单元测试。
#
# 这一层不碰 qpdf、也不碰 PDFium：只数字节里的 xref 链，
# 所以只需要 Qt6::Core 与 Qt6::Test。

qt_add_executable(PdfRevisionCounterTests
    ${CMAKE_CURRENT_LIST_DIR}/../PdfRevisionCounterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/PdfRevisionCounter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/PdfRevisionCounter.h
)

target_link_libraries(PdfRevisionCounterTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(PdfRevisionCounterTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(PdfRevisionCounterTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PdfRevisionCounterTests)
add_test(NAME PdfRevisionCounterTests COMMAND PdfRevisionCounterTests)
