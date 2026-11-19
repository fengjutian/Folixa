# veraPDF 接入点的专项测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很重要的清单里塞细节。
#
# 这一组不需要 PDF 夹具：探测、命令拼装、报告解析都是纯逻辑，
# 夹具是测试自己造的（临时 .cmd + 内嵌的 XML 报告）。
# 真的跑一次 veraPDF 需要 Java 与 veraPDF 本身，那是环境能力，
# 不该成为单元测试的前提。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(VeraPdfCheckerTests
    ${CMAKE_CURRENT_LIST_DIR}/../VeraPdfCheckerTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/VeraPdfChecker.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/VeraPdfChecker.h
)

target_link_libraries(VeraPdfCheckerTests PRIVATE
    Qt6::Core Qt6::Test
)

target_include_directories(VeraPdfCheckerTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)

set_target_properties(VeraPdfCheckerTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(VeraPdfCheckerTests)

add_test(NAME VeraPdfCheckerTests COMMAND VeraPdfCheckerTests)
