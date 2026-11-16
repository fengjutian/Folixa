# 「表单工具设置」的单元测试。
#
# 这里要链 Qt6::Gui：QColor 在 QtGui 里，FormToolSettings 用它存文字颜色。
# 测试本身不建窗口（用 QTEST_GUILESS_MAIN），不需要平台插件。

qt_add_executable(FormToolSettingsTests
    ${CMAKE_CURRENT_LIST_DIR}/../FormToolSettingsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/FormToolSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/FormToolSettings.h
)
target_link_libraries(FormToolSettingsTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(FormToolSettingsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(FormToolSettingsTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(FormToolSettingsTests)
add_test(NAME FormToolSettingsTests COMMAND FormToolSettingsTests)
