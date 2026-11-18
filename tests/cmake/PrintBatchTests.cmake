# 批量打印：纯队列规划，不碰界面，也不碰 PDFium。
#
# 只依赖 Qt6::Core 与 Qt6::Test：这一层只排任务，不读 PDF 内容，
# 真正的打印在界面层。

qt_add_executable(PrintBatchTests
    ${CMAKE_CURRENT_LIST_DIR}/../PrintBatchTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/print/PrintBatch.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/print/PrintBatch.h
)
target_link_libraries(PrintBatchTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(PrintBatchTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(PrintBatchTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PrintBatchTests)
add_test(NAME PrintBatchTests COMMAND PrintBatchTests)
