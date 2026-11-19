# 2026-11-19 wave21：导出 RTF 深度（带图片 \pict、带表格 \trowd）。
#
# 单独放一个片段：这是一组可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只留一行 include，不再往那个已经很长的清单里填细节。
#
# 依赖的变量（aipdf_deploy_qt_runtime）在父作用域里已经定义好。
#
# 不需要 PDFium：这一组测的是纯字节生成，只有 TextPage 那一个数据类参与。

qt_add_executable(Wave21RtfTests
    ${CMAKE_CURRENT_LIST_DIR}/../Wave21RtfTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/export/OfficeExport.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/export/OfficeExport.h
    ${CMAKE_SOURCE_DIR}/src/doc/export/OfficeMedia.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/export/OfficeMedia.h
    ${CMAKE_SOURCE_DIR}/src/doc/export/ZipWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/export/ZipWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(Wave21RtfTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
)

target_include_directories(Wave21RtfTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
set_target_properties(Wave21RtfTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(Wave21RtfTests)

add_test(NAME Wave21RtfTests COMMAND Wave21RtfTests)
