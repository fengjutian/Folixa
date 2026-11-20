# 视图设置面板（对话框 + 按文件记忆）的测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很长的清单里塞细节。
# 依赖的变量与函数（aipdf_deploy_qt_runtime）在父作用域里定义好了，
# include 进来直接可用。
#
# 编进来的：
#   - app/dialogs/ViewSettingsDialog（面板本身）
#   - app/editor/ViewMemory（按文件记忆页面布局；缩放键算法也归它）
#   - app/editor/ZoomMemory（缩放记忆，验证老键没变）
#   - app/layout/PageLayout（布局枚举的出处）
#   - app/ui/Language（面板要跟着界面语言切换重译）

if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

qt_add_executable(ViewSettingsTests
    ${CMAKE_CURRENT_LIST_DIR}/../ViewSettingsTests.cpp
    ${AIPDF_REPO_ROOT}/src/app/dialogs/ViewSettingsDialog.cpp
    ${AIPDF_REPO_ROOT}/src/app/dialogs/ViewSettingsDialog.h
    ${AIPDF_REPO_ROOT}/src/app/editor/ViewMemory.cpp
    ${AIPDF_REPO_ROOT}/src/app/editor/ViewMemory.h
    ${AIPDF_REPO_ROOT}/src/app/editor/ZoomMemory.cpp
    ${AIPDF_REPO_ROOT}/src/app/editor/ZoomMemory.h
    ${AIPDF_REPO_ROOT}/src/app/layout/PageLayout.cpp
    ${AIPDF_REPO_ROOT}/src/app/layout/PageLayout.h
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.cpp
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.h
)

target_link_libraries(ViewSettingsTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
)

target_include_directories(ViewSettingsTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)

set_target_properties(ViewSettingsTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(ViewSettingsTests)

add_test(NAME ViewSettingsTests COMMAND ViewSettingsTests)
