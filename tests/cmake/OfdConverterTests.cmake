# OFD 转换器注册（支持列表 + 参数生成 + 哈希校验）的测试目标。
#
# 编进来的：
#   - app/controllers/OfdConverters（被测本体，纯 Core，不碰 Widgets）
if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

qt_add_executable(OfdConverterTests
    ${CMAKE_CURRENT_LIST_DIR}/../OfdConverterTests.cpp
    ${AIPDF_REPO_ROOT}/src/app/controllers/OfdConverters.cpp
    ${AIPDF_REPO_ROOT}/src/app/controllers/OfdConverters.h
)

target_link_libraries(OfdConverterTests PRIVATE
    Qt6::Core Qt6::Test
)

target_include_directories(OfdConverterTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)

set_target_properties(OfdConverterTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(OfdConverterTests)

add_test(NAME OfdConverterTests COMMAND OfdConverterTests)
