# 「清理隐藏信息」对话框的勾选行为测试。
#
# 为什么单独放一个片段：这个对话框在 app 层（要 Widgets），而 HiddenDataInventoryTests
# 是 guiless 的领域层测试 —— 一个可执行文件不能既是 guiless 又拉进 QWidget。
#
# 编译进来的：
#   - app/dialogs/HiddenDataDialog（被测本体）
#   - i18n/Translate.h（头文件内联，不用编译）
# 领域层只用到 pdf::HiddenDataCategory 这个纯数据结构，不调用它的任何函数，
# 因此不需要把 pdf/security/PdfSecurity.cpp 拖进来。
if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

qt_add_executable(HiddenDataDialogTests
    ${CMAKE_CURRENT_LIST_DIR}/../HiddenDataDialogTests.cpp
    ${AIPDF_REPO_ROOT}/src/app/dialogs/HiddenDataDialog.cpp
    ${AIPDF_REPO_ROOT}/src/app/dialogs/HiddenDataDialog.h
)

target_link_libraries(HiddenDataDialogTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
)

target_include_directories(HiddenDataDialogTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)

set_target_properties(HiddenDataDialogTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(HiddenDataDialogTests)

add_test(NAME HiddenDataDialogTests COMMAND HiddenDataDialogTests)
