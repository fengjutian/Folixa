# 带标签文档的整篇重排：按新页宽把正文重排一遍，产出新 PDF。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很重要的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：这样这个片段既能被
# tests/CMakeLists.txt include，也能被仓库外的自检工程 include，
# 不必写死绝对路径。
#
# 依赖的都是仓库里现成的模块：
#   * pdf::readStructTree —— 读既有标签树（正文的来源）；
#   * pdf::layoutParagraph / pdf::buildParagraphContentStream —— 排版与内容流；
#   * pdf::TagTreeWriter —— 给重排后的新文档重新打标签；
#   * PDFium —— 验「写完还能不能打开、页数对不对、文字在不在页面上」。
#
# 这一组不需要 PDF 夹具：带标签的样本自己造（仓库里 StructTreeReaderTests
# 有造样本的先例），不依赖「恰好装了的 Python 包」。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(DocumentReflowTests
    ${CMAKE_CURRENT_LIST_DIR}/../DocumentReflowTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/DocumentReflow.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/DocumentReflow.h
    ${AIPDF_REPO_ROOT}/src/pdf/ops/TablePagination.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/TablePagination.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeReader.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeReader.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/ParagraphFormat.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/ParagraphFormat.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/TextReflow.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/TextReflow.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/TagTreeWriter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/TagTreeWriter.h
)

target_link_libraries(DocumentReflowTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(DocumentReflowTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(DocumentReflowTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET DocumentReflowTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:DocumentReflowTests>"
        COMMENT "Copying qpdf runtime for DocumentReflowTests"
    )
endif()

target_include_directories(DocumentReflowTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(DocumentReflowTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(DocumentReflowTests)
aipdf_deploy_pdfium(DocumentReflowTests)

add_test(NAME DocumentReflowTests COMMAND DocumentReflowTests)
