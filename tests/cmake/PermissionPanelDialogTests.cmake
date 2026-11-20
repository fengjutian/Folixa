# 权限管理面板（事后单独改权限位）的测试目标。
#
# 编进来的：
#   - app/dialogs/PermissionPanelDialog（被测本体，只碰 Widgets）
#   - app/ui/Language（对话框的翻译机制要用）
if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

qt_add_executable(PermissionPanelDialogTests
    ${CMAKE_CURRENT_LIST_DIR}/../PermissionPanelDialogTests.cpp
    ${AIPDF_REPO_ROOT}/src/app/dialogs/PermissionPanelDialog.cpp
    ${AIPDF_REPO_ROOT}/src/app/dialogs/PermissionPanelDialog.h
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.cpp
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.h
    ${AIPDF_REPO_ROOT}/src/i18n/Translate.h
)

target_link_libraries(PermissionPanelDialogTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_include_directories(PermissionPanelDialogTests PRIVATE ${AIPDF_REPO_ROOT}/src)
set_target_properties(PermissionPanelDialogTests PROPERTIES WIN32_EXECUTABLE FALSE)

if (DEFINED AIPDF_TEST_I18N_CATALOGS)
    qt_add_resources(PermissionPanelDialogTests "permission_panel_catalogs"
        PREFIX "/i18n"
        BASE "${AIPDF_REPO_ROOT}/src/i18n"
        FILES ${AIPDF_TEST_I18N_CATALOGS}
    )
endif()

aipdf_deploy_qt_runtime(PermissionPanelDialogTests)

add_test(NAME PermissionPanelDialogTests COMMAND PermissionPanelDialogTests)
