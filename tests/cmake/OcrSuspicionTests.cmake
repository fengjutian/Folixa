# OCR 疑似错误：在文字层里找出形近字（0/O、1/l、2/Z……）并给出候选。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 为什么不需要 qpdf：这一层只看文字层的内容，不碰 PDF 对象。

qt_add_executable(OcrSuspicionTests
    ${CMAKE_CURRENT_LIST_DIR}/../OcrSuspicionTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/OcrSuspicion.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/OcrSuspicion.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
)

target_link_libraries(OcrSuspicionTests PRIVATE
    Qt6::Core Qt6::Test
)

target_include_directories(OcrSuspicionTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
set_target_properties(OcrSuspicionTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(OcrSuspicionTests)

add_test(NAME OcrSuspicionTests COMMAND OcrSuspicionTests)
