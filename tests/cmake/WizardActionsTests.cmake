# 动作向导参数解析（动作参数规格 + 强类型取值 + 动作清单）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：写死绝对路径会让这份片段
# 换一台机器就废掉，而它是跟着仓库走的。
#
# 这一组是纯逻辑：只依赖 QtCore（QString、QVector 都在里面），
# 不碰 PDF、不碰 qpdf、不碰 PDFium。
#
# 带上 ActionWizard.cpp：WizardActions 复用 ActionWizard 的动作类型与
# 英文名/中文名（同一份真相不许抄两遍），所以这两个文件要一起编。

qt_add_executable(WizardActionsTests
    ${CMAKE_CURRENT_LIST_DIR}/../WizardActionsTests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/doc/batch/WizardActions.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/doc/batch/WizardActions.h
    ${CMAKE_CURRENT_LIST_DIR}/../../src/doc/batch/ActionWizard.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/doc/batch/ActionWizard.h
)

target_link_libraries(WizardActionsTests PRIVATE Qt6::Core Qt6::Test)

target_include_directories(WizardActionsTests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
set_target_properties(WizardActionsTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(WizardActionsTests)

add_test(NAME WizardActionsTests COMMAND WizardActionsTests)
