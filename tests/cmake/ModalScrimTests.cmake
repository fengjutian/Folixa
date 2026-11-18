# 弹窗蒙层。
#
# 只建控件、开一个空对话框：几何与生命周期，不碰文档与 PDFium。
# 用例里窗口都带 WA_DontShowOnScreen，本地跑不会抢桌面。

qt_add_executable(ModalScrimTests
    ${CMAKE_CURRENT_LIST_DIR}/../ModalScrimTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ModalScrim.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ModalScrim.h
)

target_link_libraries(ModalScrimTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_include_directories(ModalScrimTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ModalScrimTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(ModalScrimTests)

add_test(NAME ModalScrimTests COMMAND ModalScrimTests)
