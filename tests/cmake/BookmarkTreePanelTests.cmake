# 「编辑书签层级」对话框的控件测试。
#
# 这一层不碰文档也不碰 PDFium：书签树对话框只依赖 pdf 层的树操作
# 与 Qt 控件，所以只需要 Qt6::Widgets 与 Qt6::Test。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。

qt_add_executable(BookmarkTreePanelTests
    ${CMAKE_CURRENT_LIST_DIR}/../BookmarkTreePanelTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/BookmarkTreeDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/BookmarkTreeDialog.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OutlineEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OutlineEditor.h
    ${CMAKE_SOURCE_DIR}/src/pdf/document/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/Navigation.h
)

target_link_libraries(BookmarkTreePanelTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_include_directories(BookmarkTreePanelTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(BookmarkTreePanelTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(BookmarkTreePanelTests)
add_test(NAME BookmarkTreePanelTests COMMAND BookmarkTreePanelTests)
