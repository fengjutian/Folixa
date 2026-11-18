# 界面主题（浅色 / 暗色 / 跟随系统）。
#
# 只碰样式表与调色板：不加载文档、不渲染页面，跑得快。
# 主题设置存在 QSettings 里，用例自己隔离了设置路径。

qt_add_executable(ThemeTests
    ${CMAKE_CURRENT_LIST_DIR}/../ThemeTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Theme.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Theme.h
)

target_link_libraries(ThemeTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_include_directories(ThemeTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ThemeTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(ThemeTests)

add_test(NAME ThemeTests COMMAND ThemeTests)
