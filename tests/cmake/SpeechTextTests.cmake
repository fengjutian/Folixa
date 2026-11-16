# 朗读分词的单元测试。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 只依赖 Qt6::Core 与 Qt6::Test：这一层是纯逻辑，不碰界面也不碰 PDFium。

qt_add_executable(SpeechTextTests
    ${CMAKE_CURRENT_LIST_DIR}/../SpeechTextTests.cpp
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechText.cpp
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechText.h
)
target_link_libraries(SpeechTextTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(SpeechTextTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(SpeechTextTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(SpeechTextTests)
add_test(NAME SpeechTextTests COMMAND SpeechTextTests)
