# 语义级阅读顺序：读语义树、按语义顺序写回，以及那个独立对话框。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 编进来的分三块：
#   - 语义阅读顺序（src/pdf/compliance/SemanticReadingOrder.*）与它复用的
#     结构树读写件（读结构树、语义模型、标签树写手、角色推断）；
#   - 界面层：语义顺序对话框（src/app/dialogs/SemanticOrderDialog.*）——
#     它不依赖 PdfView，可以单测；
#   - PDFium 适配层：用来验「写回后的文档还打不打得开、页数变没变」。
#
# 为什么要 qpdf：语义树是对象级信息，只有真正的 PDF 解析器给得出答案。

if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

qt_add_executable(SemanticReadingOrderTests
    ${CMAKE_CURRENT_LIST_DIR}/../SemanticReadingOrderTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/SemanticReadingOrder.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/SemanticReadingOrder.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeReader.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeReader.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeModel.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeModel.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/TagTreeWriter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/TagTreeWriter.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/SemanticTagWriter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/SemanticTagWriter.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/RoleInference.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/RoleInference.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.h
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.h
    ${AIPDF_REPO_ROOT}/src/app/dialogs/SemanticOrderDialog.cpp
    ${AIPDF_REPO_ROOT}/src/app/dialogs/SemanticOrderDialog.h
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.cpp
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.h
    ${AIPDF_REPO_ROOT}/src/i18n/Translate.h
)

target_link_libraries(SemanticReadingOrderTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(SemanticReadingOrderTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(SemanticReadingOrderTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET SemanticReadingOrderTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:SemanticReadingOrderTests>"
        COMMENT "Copying qpdf runtime for SemanticReadingOrderTests"
    )
endif()

target_include_directories(SemanticReadingOrderTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(SemanticReadingOrderTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

set_target_properties(SemanticReadingOrderTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(SemanticReadingOrderTests pdf-fixtures)
aipdf_deploy_qt_runtime(SemanticReadingOrderTests)
aipdf_deploy_pdfium(SemanticReadingOrderTests)

add_test(NAME SemanticReadingOrderTests COMMAND SemanticReadingOrderTests)
