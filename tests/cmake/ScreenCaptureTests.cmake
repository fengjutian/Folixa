# 截图区域框选（拖拽归一化 + 裁剪）的单元测试目标。
#
# 这一组是纯函数：不碰屏幕、不建控件，也不需要 PDF 夹具。
get_filename_component(AIPDF_SCREEN_CAPTURE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(ScreenCaptureTests
    ${CMAKE_CURRENT_LIST_DIR}/../ScreenCaptureTests.cpp
    ${AIPDF_SCREEN_CAPTURE_ROOT}/src/app/ScreenCapture.cpp
    ${AIPDF_SCREEN_CAPTURE_ROOT}/src/app/ScreenCapture.h
)

target_link_libraries(ScreenCaptureTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

target_include_directories(ScreenCaptureTests PRIVATE
    ${AIPDF_SCREEN_CAPTURE_ROOT}/src
)
set_target_properties(ScreenCaptureTests PROPERTIES WIN32_EXECUTABLE FALSE)

if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(ScreenCaptureTests)
endif()

add_test(NAME ScreenCaptureTests COMMAND ScreenCaptureTests)
