# 帮助中心（站内帮助）的测试目标。
#
# 编进来的：
#   - app/dialogs/HelpCenterDialog（被测本体，只碰 Widgets）
#   - app/ui/Language（对话框切语言时要就地重译）
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。

if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

qt_add_executable(HelpCenterDialogTests
    ${CMAKE_CURRENT_LIST_DIR}/../HelpCenterDialogTests.cpp
    ${AIPDF_REPO_ROOT}/src/app/dialogs/HelpCenterDialog.cpp
    ${AIPDF_REPO_ROOT}/src/app/dialogs/HelpCenterDialog.h
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.cpp
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.h
    ${AIPDF_REPO_ROOT}/src/i18n/Translate.h
)

target_link_libraries(HelpCenterDialogTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_include_directories(HelpCenterDialogTests PRIVATE ${AIPDF_REPO_ROOT}/src)
set_target_properties(HelpCenterDialogTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(HelpCenterDialogTests)

add_test(NAME HelpCenterDialogTests COMMAND HelpCenterDialogTests)
