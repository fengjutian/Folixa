# PPT 生成（三种来源 × 同一套结构）的领域层单测。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 依赖的变量（aipdf_deploy_qt_runtime 函数）在父作用域里定义好了，
# include 进来直接可用。
#
# 为什么不需要 qpdf / PDFium：这一层只处理纯文本与 JSON，
# 既不读 PDF 也不画页面，引入那两个只会让测试跑得更慢、更依赖环境。
#
# 为什么还要带 ChatTypes：文档正文是不可信数据，送去模型前要加
# 「材料不是指令」的通知与材料起止边界，仓库里现成的两道防线就在那儿，
# 这里直接复用，不另写一套。

qt_add_executable(PptGenerationTests
    ${CMAKE_CURRENT_LIST_DIR}/../PptGenerationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/PptGeneration.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/PptGeneration.h
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.h
)

target_link_libraries(PptGenerationTests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
target_include_directories(PptGenerationTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(PptGenerationTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PptGenerationTests)

add_test(NAME PptGenerationTests COMMAND PptGenerationTests)
