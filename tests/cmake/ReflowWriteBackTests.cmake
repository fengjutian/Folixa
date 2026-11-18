# 整篇重排的写回：重排 → 落盘 → 重新打开 → 重新统计，以及门禁。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很重要的清单里塞细节。
#
# 依赖的都是仓库里现成的模块：
#   * pdf::reflowTaggedDocument —— 整篇重排（含页面对象模型与门禁）；
#   * pdf::readPageObjectModel —— 重新统计重排结果里的对象；
#   * PDFium —— 验「写完还能不能打开、页数对不对、文字在不在页面上」。
#
# 这一组不需要 PDF 夹具：样本自己造（仓库里 StructTreeReaderTests 与
# DocumentReflowTests 有造样本的先例），不依赖「恰好装了的 Python 包」。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(ReflowWriteBackTests
    ${CMAKE_CURRENT_LIST_DIR}/../ReflowWriteBackTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/DocumentReflow.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/DocumentReflow.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PageObjectModel.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PageObjectModel.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/ReflowPlan.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/ReflowPlan.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeReader.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeReader.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/TagTreeWriter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/TagTreeWriter.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/ParagraphFormat.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/ParagraphFormat.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/TextReflow.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/TextReflow.h
)

target_link_libraries(ReflowWriteBackTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(ReflowWriteBackTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(ReflowWriteBackTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET ReflowWriteBackTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:ReflowWriteBackTests>"
        COMMENT "Copying qpdf runtime for ReflowWriteBackTests"
    )
endif()

target_include_directories(ReflowWriteBackTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(ReflowWriteBackTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(ReflowWriteBackTests)
aipdf_deploy_pdfium(ReflowWriteBackTests)

add_test(NAME ReflowWriteBackTests COMMAND ReflowWriteBackTests)
