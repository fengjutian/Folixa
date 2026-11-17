# 导出到 Office 友好格式（DOCX / RTF / 纯文本）的测试片段。
#
# 为什么单独放一个片段：这是一组「可以整体拷走」的测试，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（Qt6::Test、aipdf_deploy_qt_runtime）在父作用域里已经定义好。
#
# 不需要 PDFium：这一组测的是纯字节生成（自己拼 ZIP 与 XML），
# 只有 TextPage 那一个数据类参与。

qt_add_executable(OfficeExportTests
    ${CMAKE_CURRENT_LIST_DIR}/../OfficeExportTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/OfficeExport.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/OfficeExport.h
    ${CMAKE_SOURCE_DIR}/src/doc/ZipWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/ZipWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
)

target_link_libraries(OfficeExportTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
)

target_include_directories(OfficeExportTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
set_target_properties(OfficeExportTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(OfficeExportTests)

add_test(NAME OfficeExportTests COMMAND OfficeExportTests)
