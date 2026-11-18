# 页面对象模型：把一页里的对象结构化读出来，并给出稳定身份与分类计数。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很重要的清单里塞细节。
#
# 依赖的都是仓库里现成的模块：
#   * pdf::readPageObjectModel —— 读页面对象、算计数、派生稳定 id；
#   * PDFium —— 页面对象枚举的底座。
#
# 这一组不需要 PDF 夹具：样本自己造（仓库里 StructTreeReaderTests 与
# DocumentReflowTests 有造样本的先例），不依赖「恰好装了的 Python 包」。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(PageObjectModelTests
    ${CMAKE_CURRENT_LIST_DIR}/../PageObjectModelTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PageObjectModel.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PageObjectModel.h
)

target_link_libraries(PageObjectModelTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(PageObjectModelTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(PageObjectModelTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET PageObjectModelTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:PageObjectModelTests>"
        COMMENT "Copying qpdf runtime for PageObjectModelTests"
    )
endif()

target_include_directories(PageObjectModelTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(PageObjectModelTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(PageObjectModelTests)
aipdf_deploy_pdfium(PageObjectModelTests)

add_test(NAME PageObjectModelTests COMMAND PageObjectModelTests)
