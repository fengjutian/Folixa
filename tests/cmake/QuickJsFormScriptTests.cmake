# QuickJS 表单脚本引擎：真 JS 语义（变量、循环、标准库、this/getField）。
# 与白名单子集共用 FormJsResult，方便后续替换。

qt_add_executable(QuickJsFormScriptTests
    ${CMAKE_CURRENT_LIST_DIR}/../QuickJsFormScriptTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/QuickJsFormScript.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/QuickJsFormScript.h
)

target_link_libraries(QuickJsFormScriptTests PRIVATE
    Qt6::Core Qt6::Test
    aipdf_quickjs
)
target_include_directories(QuickJsFormScriptTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(QuickJsFormScriptTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(QuickJsFormScriptTests)

add_test(NAME QuickJsFormScriptTests COMMAND QuickJsFormScriptTests)
