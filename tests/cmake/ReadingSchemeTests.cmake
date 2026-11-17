# 阅读配色（夜间 / 护眼 / 高对比）：只管配色方案的取值与持久化。
#
# 这一层不碰文档也不碰渲染，所以只需要 Qt6::Widgets（调色板在 Widgets 里）
# 与 Qt6::Test。

qt_add_executable(ReadingSchemeTests
    ${CMAKE_CURRENT_LIST_DIR}/../ReadingSchemeTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ReadingScheme.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ReadingScheme.h
)

target_link_libraries(ReadingSchemeTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_include_directories(ReadingSchemeTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ReadingSchemeTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(ReadingSchemeTests)
add_test(NAME ReadingSchemeTests COMMAND ReadingSchemeTests)
