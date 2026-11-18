# 图像生成领域层（文生图 + 总结配图）的单元测试。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（aipdf_deploy_qt_runtime 函数）在父作用域里定义好了，
# include 进来直接可用。
#
# 注意：这个测试不碰 PDF：它验的是「提示词怎么构造」「响应怎么解析」
# 「总结配图怎么出要点」，所以只链 QtCore/QtNetwork/QtTest，
# 不引入 qpdf 与 PDFium——那两样会让一个纯逻辑测试平白多出几秒钟的启动开销。

qt_add_executable(AiImageGenerationTests
    ${CMAKE_CURRENT_LIST_DIR}/../AiImageGenerationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/content/AiImageGeneration.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/content/AiImageGeneration.h
)

target_link_libraries(AiImageGenerationTests PRIVATE
    Qt6::Core Qt6::Network Qt6::Test
)

target_include_directories(AiImageGenerationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

set_target_properties(AiImageGenerationTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(AiImageGenerationTests)

add_test(NAME AiImageGenerationTests COMMAND AiImageGenerationTests)
