# 智能朗读（分段 + 停顿）的单元测试。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 依赖 speech/SpeechText.cpp：总字数按「真正会念出来的字符」算，
# 复用朗读分词 splitIntoWords（它保证不丢字符也不改顺序），
# 不另写一套字数的算法。
#
# 不引入 qpdf/PDFium：这一层是纯逻辑，既不碰 PDF 也不碰界面。

qt_add_executable(AiNarrationTests
    ${CMAKE_CURRENT_LIST_DIR}/../AiNarrationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AiNarration.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AiNarration.h
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechText.cpp
    ${CMAKE_SOURCE_DIR}/src/speech/SpeechText.h
)
target_link_libraries(AiNarrationTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(AiNarrationTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AiNarrationTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(AiNarrationTests)
add_test(NAME AiNarrationTests COMMAND AiNarrationTests)
