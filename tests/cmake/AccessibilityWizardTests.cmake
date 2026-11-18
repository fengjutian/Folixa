# 无障碍配置向导的单元测试目标。
#
# 这组测试要建真的 QDialog，所以链 Qt6::Widgets（QTEST_MAIN 会建 QApplication）。
# 对话框只构造不 show：不弹窗、不抢前台，控件取值照样能断言。
#
# 音色列表是注入的，所以不需要真的去查系统语音 —— 但链接时仍要带上
# SpeechEngine（向导里那个"问系统要音色"的构造重载会用到它）。
# 测的是界面层能不能在没有系统依赖的情况下被测。

qt_add_executable(AccessibilityWizardTests
    ${CMAKE_CURRENT_LIST_DIR}/../AccessibilityWizardTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/AccessibilityWizard.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/AccessibilityWizard.h
    ${CMAKE_SOURCE_DIR}/src/app/ui/AccessibilityPlan.h
    ${CMAKE_SOURCE_DIR}/src/app/ui/ReadingScheme.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/ReadingScheme.h
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechEngine.h
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechSettings.h
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechText.cpp
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechText.h
)

target_link_libraries(AccessibilityWizardTests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)

target_include_directories(AccessibilityWizardTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AccessibilityWizardTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(AccessibilityWizardTests)

add_test(NAME AccessibilityWizardTests COMMAND AccessibilityWizardTests)
