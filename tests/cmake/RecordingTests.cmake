# 录制中心（AVI 封装 / 录制清单 / 录制会话）的单元测试。
#
# 这一层不碰界面：录制中心对话框只是把三个件摆到界面上。
# 依赖里刻意不带 PDFium 与 qpdf——录制这条链跟文档解析无关，
# 因此这组测试在任何机器上都能跑（包括没有显示器的 CI）。
#
# Qt6::Gui 是为了 QImage 与 Qt 内建的 JPEG 编码；用 QTEST_GUILESS_MAIN
# 建 QCoreApplication 就够，不必开一个真的显示服务。
qt_add_executable(RecordingTests
    ${CMAKE_CURRENT_LIST_DIR}/../RecordingTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/recording/MotionJpegAviWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/app/recording/MotionJpegAviWriter.h
    ${CMAKE_SOURCE_DIR}/src/app/recording/RecordingLibrary.cpp
    ${CMAKE_SOURCE_DIR}/src/app/recording/RecordingLibrary.h
    ${CMAKE_SOURCE_DIR}/src/app/recording/ScreenRecorder.cpp
    ${CMAKE_SOURCE_DIR}/src/app/recording/ScreenRecorder.h
    ${CMAKE_SOURCE_DIR}/src/app/controllers/ScreenCapture.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/ScreenCapture.h
)

target_link_libraries(RecordingTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(RecordingTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(RecordingTests PRIVATE
    AIPDF_RECORDING_DIR="${CMAKE_CURRENT_BINARY_DIR}/recordings"
)
set_target_properties(RecordingTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(RecordingTests)
add_test(NAME RecordingTests COMMAND RecordingTests)

# 独立校验：Python 另写一份 AVI 解析器（外加 PIL 的 JPEG 解码器）再验一次样例。
# 为什么不复用 C++ 侧的解析器：两边共享同一套「对格式的理解」，
# 写错了会一起错，坏文件就带着错出厂。
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME RecordingVerifyTests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/verify-recordings.py
            --dir ${CMAKE_CURRENT_BINARY_DIR}/recordings
            --self-test
)
set_tests_properties(RecordingVerifyTests PROPERTIES
    DEPENDS RecordingTests
)
