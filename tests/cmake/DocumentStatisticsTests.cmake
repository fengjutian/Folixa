# 字数统计：纯聚合层，不碰文件也不碰界面。
#
# 只依赖 Qt6::Core 与 Qt6::Test：这一层没有界面，也不需要 PDFium。

qt_add_executable(DocumentStatisticsTests
    ${CMAKE_CURRENT_LIST_DIR}/../DocumentStatisticsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/DocumentStatistics.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/DocumentStatistics.h
    ${CMAKE_SOURCE_DIR}/src/doc/ChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/ChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/doc/DocumentChunk.h
)
target_link_libraries(DocumentStatisticsTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(DocumentStatisticsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(DocumentStatisticsTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(DocumentStatisticsTests)
add_test(NAME DocumentStatisticsTests COMMAND DocumentStatisticsTests)
