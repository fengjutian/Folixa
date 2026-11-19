# wave21：全文流式编辑 + 跨文本块合并 + 整篇翻译写回。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很重要的清单里塞细节。
#
# 依赖的都是仓库里现成的模块：
#   * pdf::ContentStream —— 内容流词法与写回通道（写回就走它）；
#   * pdf::TextReflow —— 断行；
#   * pdf::readStructTree —— 读标签树（段落边界的依据）；
#   * pdf::readPageObjectModel —— 页面对象模型（认得出表格与图片）；
#   * qpdf —— 页内容流的读写；
#   * PDFium —— 写回之后用**独立解析器**回读校验。
#
# 这一组不需要 PDF 夹具：带标签的样本自己造。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(Wave21StreamTests
    ${CMAKE_CURRENT_LIST_DIR}/../Wave21StreamTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/StreamingEdit.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/StreamingEdit.h
    ${AIPDF_REPO_ROOT}/src/pdf/ops/TranslationWriter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/TranslationWriter.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/TextReflow.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/TextReflow.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeReader.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeReader.h
)

target_link_libraries(Wave21StreamTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(Wave21StreamTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(Wave21StreamTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET Wave21StreamTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:Wave21StreamTests>"
        COMMENT "Copying qpdf runtime for Wave21StreamTests"
    )
endif()

target_include_directories(Wave21StreamTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

set_target_properties(Wave21StreamTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(Wave21StreamTests)
aipdf_deploy_pdfium(Wave21StreamTests)

add_test(NAME Wave21StreamTests COMMAND Wave21StreamTests)
