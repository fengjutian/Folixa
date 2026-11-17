# 打印模板：纯校验 + QSettings 存取，不碰界面，也不碰 PDFium。
#
# 只依赖 Qt6::Core 与 Qt6::Test：模板存取落在 QSettings 上。
#
# 注意：测试自己把 QSettings 的存储目录指到临时目录，
# 跑测试不会往用户真实的设置里写模板。

qt_add_executable(PrintTemplateTests
    ${CMAKE_CURRENT_LIST_DIR}/../PrintTemplateTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/PrintTemplate.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/PrintTemplate.h
    ${CMAKE_SOURCE_DIR}/src/doc/BlankDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/BlankDocument.h
)
target_link_libraries(PrintTemplateTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(PrintTemplateTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(PrintTemplateTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PrintTemplateTests)
add_test(NAME PrintTemplateTests COMMAND PrintTemplateTests)
