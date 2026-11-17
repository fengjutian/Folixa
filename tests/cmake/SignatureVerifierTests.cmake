# 签名密码学闭环（RSA 验签 / RFC 3161 时间戳 / OCSP 编解码）的单元测试目标。
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
# 验签走 Win32 的 BCrypt，证书链解析走 Crypt32，所以这两个库要显式链接。
# qpdf 是可选依赖：SignatureVerifier 要读 PDF 里的签名域，
# 没编 qpdf 时它走纯字节路径，用例照样能跑。

qt_add_executable(SignatureVerifierTests
    ${CMAKE_CURRENT_LIST_DIR}/../SignatureVerifierTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/SignatureVerifier.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/SignatureVerifier.h
    ${CMAKE_SOURCE_DIR}/src/pdf/RsaVerifier.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/RsaVerifier.h
    ${CMAKE_SOURCE_DIR}/src/pdf/SignatureInspector.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/SignatureInspector.h
)

target_link_libraries(SignatureVerifierTests PRIVATE
    Qt6::Core
    Qt6::Test
)

if (WIN32)
    target_link_libraries(SignatureVerifierTests PRIVATE crypt32 bcrypt)
endif()

target_include_directories(SignatureVerifierTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

# 签名夹具是提交进仓库的（tests/fixtures/signed/），不依赖生成脚本。
target_compile_definitions(SignatureVerifierTests PRIVATE
    AIPDF_SIGNATURE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/signed"
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(SignatureVerifierTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(SignatureVerifierTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET SignatureVerifierTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:SignatureVerifierTests>"
        COMMENT "Copying qpdf runtime for tests")
endif()

set_target_properties(SignatureVerifierTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(SignatureVerifierTests)

add_test(NAME SignatureVerifierTests COMMAND SignatureVerifierTests)
