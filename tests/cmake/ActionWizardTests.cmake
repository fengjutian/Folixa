# 动作向导（有序动作编排 + 参数校验 + 执行报告）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：写死绝对路径会让这份片段
# 换一台机器就废掉，而它是跟着仓库走的。
#
# 这一组是纯逻辑：只依赖 QtCore（QSettings、QJsonDocument 都在里面），
# 不碰 PDF、不碰 qpdf、不碰 PDFium。

qt_add_executable(ActionWizardTests
    ${CMAKE_CURRENT_LIST_DIR}/../ActionWizardTests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/doc/batch/ActionWizard.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/doc/batch/ActionWizard.h
)

target_link_libraries(ActionWizardTests PRIVATE Qt6::Core Qt6::Test)

target_include_directories(ActionWizardTests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
set_target_properties(ActionWizardTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(ActionWizardTests)

add_test(NAME ActionWizardTests COMMAND ActionWizardTests)
