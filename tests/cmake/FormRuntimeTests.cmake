# 表单运行时（pdf::FormRuntime）：字段树 + 固定事件顺序 + 计算依赖。
#
# 只依赖 qpdf 与 FormJsEvaluator，不碰渲染与界面。

qt_add_executable(FormRuntimeTests
    ${CMAKE_CURRENT_LIST_DIR}/../FormRuntimeTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormRuntime.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormJsEvaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormJsEvaluator.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormScriptStore.h
)

target_link_libraries(FormRuntimeTests PRIVATE
    Qt6::Core Qt6::Test
    qpdf::libqpdf
)
target_compile_definitions(FormRuntimeTests PRIVATE AIPDF_WITH_QPDF=1)

add_custom_command(TARGET FormRuntimeTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:FormRuntimeTests>"
    COMMENT "Copying qpdf runtime for FormRuntimeTests"
)

target_include_directories(FormRuntimeTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(FormRuntimeTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(FormRuntimeTests)

add_test(NAME FormRuntimeTests COMMAND FormRuntimeTests)
