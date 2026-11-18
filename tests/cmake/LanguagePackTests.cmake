# 语言包的用户侧安装与切换（评分卡 I3）。
#
# 只碰 QtCore：语言包就是一份 .ts 文件，校验 / 复制 / 加载都不需要窗口。
# 英文目录要打进资源：有用例要证明「用户目录优先于程序自带」。

qt_add_executable(LanguagePackTests
    ${CMAKE_CURRENT_LIST_DIR}/../LanguagePackTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Language.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Language.h
    ${CMAKE_SOURCE_DIR}/src/i18n/Translate.h
)

target_link_libraries(LanguagePackTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(LanguagePackTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(LanguagePackTests PROPERTIES WIN32_EXECUTABLE FALSE)

file(GLOB AIPDF_LANGUAGE_PACK_TEST_CATALOGS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/i18n/*.ts")
qt_add_resources(LanguagePackTests "language_pack_test_catalogs"
    PREFIX "/i18n"
    BASE "${CMAKE_SOURCE_DIR}/src/i18n"
    FILES ${AIPDF_LANGUAGE_PACK_TEST_CATALOGS}
)

aipdf_deploy_qt_runtime(LanguagePackTests)

add_test(NAME LanguagePackTests COMMAND LanguagePackTests)
