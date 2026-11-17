# AI 内容检测（判定 + 理由）的单元测试。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 只依赖 Qt6::Core 与 Qt6::Test：这一层是纯逻辑，不发网络请求，也不碰界面。
#
# 注意：AiContentDetection 复用了 ai/ChatTypes.cpp 里的材料边界写法
# （wrapMaterial / untrustedMaterialNotice），所以那份也要一起编进来。

qt_add_executable(AiContentDetectionTests
    ${CMAKE_CURRENT_LIST_DIR}/../AiContentDetectionTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AiContentDetection.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AiContentDetection.h
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.h
)
target_link_libraries(AiContentDetectionTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(AiContentDetectionTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AiContentDetectionTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(AiContentDetectionTests)
add_test(NAME AiContentDetectionTests COMMAND AiContentDetectionTests)
