# 读既有标签树（/StructTreeRoot）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（aipdf_deploy_qt_runtime 函数）在父作用域里定义好了，
# include 进来直接可用。
#
# 为什么又要 qpdf：读标签树是对象级操作，走 qpdf；
# 页面内容流里的标记内容用 pdf::ContentStream 的分词器解析，
# 所以要一起编进来。
#
# 这一组不需要 PDF 夹具，也不碰 PDFium：夹具要「结构上不同于本仓库
# TagTreeWriter 输出」，自己造才说得清（数组 /K、两层嵌套、带 /Alt 与 /Lang）。

qt_add_executable(StructTreeReaderTests
    ${CMAKE_CURRENT_LIST_DIR}/../StructTreeReaderTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/StructTreeReader.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/StructTreeReader.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
)

target_link_libraries(StructTreeReaderTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(StructTreeReaderTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(StructTreeReaderTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET StructTreeReaderTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:StructTreeReaderTests>"
        COMMENT "Copying qpdf runtime for StructTreeReaderTests"
    )
endif()

target_include_directories(StructTreeReaderTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(StructTreeReaderTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(StructTreeReaderTests)

add_test(NAME StructTreeReaderTests COMMAND StructTreeReaderTests)
