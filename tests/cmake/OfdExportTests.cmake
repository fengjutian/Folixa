# 导出到 OFD（GB/T 33190-2016）的测试片段。
#
# 为什么单独放一个片段：同 OfficeExportTests.cmake，这是一组可以整体拷走的测试。
# 依赖的变量（Qt6::Test、aipdf_deploy_qt_runtime）在父作用域里已经定义好。
#
# 同样不需要 PDFium：OFD 包是纯字节拼出来的（ZIP + XML）。

qt_add_executable(OfdExportTests
    ${CMAKE_CURRENT_LIST_DIR}/../OfdExportTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/OfdExport.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/OfdExport.h
    ${CMAKE_SOURCE_DIR}/src/doc/OfficeExport.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/OfficeExport.h
    ${CMAKE_SOURCE_DIR}/src/doc/ZipWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/ZipWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(OfdExportTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
)

target_include_directories(OfdExportTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
set_target_properties(OfdExportTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(OfdExportTests)

add_test(NAME OfdExportTests COMMAND OfdExportTests)
