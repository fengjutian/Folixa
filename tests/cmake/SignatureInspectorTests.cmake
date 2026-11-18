# 验签 UI 底座（SignatureInspector）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（AIPDF_WITH_QPDF、qpdf_FOUND、aipdf_deploy_qt_runtime 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：源文件路径必须写成 ${CMAKE_CURRENT_LIST_DIR}/../XxxTests.cpp。
# 顶层作用域里裸文件名会解析失败（include 进来时 CMAKE_CURRENT_SOURCE_DIR
# 指向仓库根，不是 tests/）。
#
# qpdf 是可选依赖：没有它的时候目标照样建（模块走「缺依赖」那条路），
# 依赖 qpdf 的用例整体跳过，并在输出里明确打印「SKIPPED: 无 qpdf」。
# 这样「没启用 qpdf 时给出明确错误」这条路径也能被跑到，而不是整组用例消失。

qt_add_executable(SignatureInspectorTests
    ${CMAKE_CURRENT_LIST_DIR}/../SignatureInspectorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/security/SignatureInspector.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/security/SignatureInspector.h
)

target_link_libraries(SignatureInspectorTests PRIVATE
    Qt6::Core
    Qt6::Test
)

# 证书链解析走 Win32 Crypt32，SHA-1 指纹走 BCrypt。
if (WIN32)
    target_link_libraries(SignatureInspectorTests PRIVATE crypt32 bcrypt)
endif()

target_include_directories(SignatureInspectorTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

# 签名夹具是提交进仓库的（tests/fixtures/signed/），不依赖生成脚本。
target_compile_definitions(SignatureInspectorTests PRIVATE
    AIPDF_SIGNATURE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/signed"
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(SignatureInspectorTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(SignatureInspectorTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET SignatureInspectorTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:SignatureInspectorTests>"
        COMMENT "Copying qpdf runtime for tests")
endif()

set_target_properties(SignatureInspectorTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(SignatureInspectorTests)

add_test(NAME SignatureInspectorTests COMMAND SignatureInspectorTests)
