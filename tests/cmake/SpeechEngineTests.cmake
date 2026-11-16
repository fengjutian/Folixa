# 朗读引擎的行为测试。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 这组测试会真的起 PowerShell 子进程，机器上没有语音时会走 failed() 分支，
# 所以额外加了 TIMEOUT 兜底：CI 上也不许把 ctest 挂死。

qt_add_executable(SpeechEngineTests
    ${CMAKE_CURRENT_LIST_DIR}/../SpeechEngineTests.cpp
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechEngine.h
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechText.cpp
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechText.h
)
target_link_libraries(SpeechEngineTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(SpeechEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(SpeechEngineTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(SpeechEngineTests)
add_test(NAME SpeechEngineTests COMMAND SpeechEngineTests)
set_tests_properties(SpeechEngineTests PROPERTIES TIMEOUT 120)
