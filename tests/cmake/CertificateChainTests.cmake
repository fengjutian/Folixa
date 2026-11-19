# 证书链与撤销状态检查（Windows Crypt32）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很长的清单里塞细节。
#
# 编进来的：
#   * pdf/security/CertificateChain —— 用 Windows 自带的证书存储查证书链、
#     逐张证书的撤销状态（CRL / OCSP）与有效期；
#   * 测试自己直接用 Crypt32 读夹具里的 PKCS#7（不牵动 qpdf），
#     所以这一组在没装 qpdf 的机器上照样能跑。
#
# 依赖 crypt32：证书链与撤销状态全部走 Win32 Crypt32，
# MinGW 下库名就叫 crypt32。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(CertificateChainTests
    ${CMAKE_CURRENT_LIST_DIR}/../CertificateChainTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/security/CertificateChain.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/security/CertificateChain.h
)

target_link_libraries(CertificateChainTests PRIVATE
    Qt6::Core
    Qt6::Test
)

if (WIN32)
    target_link_libraries(CertificateChainTests PRIVATE crypt32)
endif()

target_include_directories(CertificateChainTests PRIVATE ${AIPDF_REPO_ROOT}/src)

# 签名夹具是提交进仓库的（tests/fixtures/signed/），不依赖生成脚本。
# 真链用例拿不到夹具时会 QSKIP，不会红。
target_compile_definitions(CertificateChainTests PRIVATE
    AIPDF_SIGNATURE_FIXTURES_DIR="${AIPDF_REPO_ROOT}/tests/fixtures/signed"
)

set_target_properties(CertificateChainTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(CertificateChainTests)

add_test(NAME CertificateChainTests COMMAND CertificateChainTests)
