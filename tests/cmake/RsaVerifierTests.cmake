# RSA 公钥验签（PKCS#1 v1.5）的单元测试目标。
#
# 只依赖 Qt6::Core 与 Qt6::Test：
# 这一层是纯数学（模幂 + PKCS#1 v1.5 填充校验），
# 不碰 Windows API，也不碰 PDFium/qpdf。
#
# 测试向量是仓库夹具里的真实签名，硬编码在测试文件里，
# 所以这个目标不依赖任何外部文件，CI 上永远能跑。

qt_add_executable(RsaVerifierTests
    ${CMAKE_CURRENT_LIST_DIR}/../RsaVerifierTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/security/RsaVerifier.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/security/RsaVerifier.h
)

target_link_libraries(RsaVerifierTests PRIVATE
    Qt6::Core
    Qt6::Test
)

target_include_directories(RsaVerifierTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(RsaVerifierTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(RsaVerifierTests)

add_test(NAME RsaVerifierTests COMMAND RsaVerifierTests)
