# 批处理执行器 + JSON 报告：纯领域层，不碰界面，也不碰 PDFium。
#
# 只依赖 Qt6::Core 与 Qt6::Test：这一层只处理字节与报告，不读 PDF 内容。

qt_add_executable(BatchProcessorTests
    ${CMAKE_CURRENT_LIST_DIR}/../BatchProcessorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/batch/BatchProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/batch/BatchProcessor.h
)
target_link_libraries(BatchProcessorTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(BatchProcessorTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(BatchProcessorTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(BatchProcessorTests)
add_test(NAME BatchProcessorTests COMMAND BatchProcessorTests)
