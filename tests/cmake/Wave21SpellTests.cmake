# 2026-11-19 wave21：拼写检查——可插拔词典 + 中文（形近/音近错字）。
#
# 词表是编译进二进制的（SpellChecker.cpp 直接 include src/pdf/ops/spell/ 下的
# 生成头文件），所以这里把它们列进源文件：少一个文件时构建会直接报错，
# 而不是等到编译期报一句难懂的头文件找不到。

qt_add_executable(Wave21SpellTests
    ${CMAKE_CURRENT_LIST_DIR}/../Wave21SpellTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/SpellChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/SpellChecker.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/spell/EnglishWords.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/spell/EnglishCommonWords.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/spell/ChineseWords.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/spell/ChineseCommonWords.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/spell/ChineseConfusables.h
)

target_link_libraries(Wave21SpellTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(Wave21SpellTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(Wave21SpellTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(Wave21SpellTests)

add_test(NAME Wave21SpellTests COMMAND Wave21SpellTests)
