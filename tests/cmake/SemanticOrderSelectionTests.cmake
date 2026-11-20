# 「应用区域阅读顺序（选区）」的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（qpdf_FOUND、AIPDF_WITH_QPDF、aipdf_deploy_* 函数）都在父作用域里
# 定义好了，include 进来直接可用。
#
# 编进来的分四块：
#   - 选区重排的领域层（src/pdf/compliance/SemanticOrderSelection.*）与它复用的
#     语义阅读顺序件（读语义树、写回顺序）、结构树读写件、标签树写手、角色推断；
#   - 界面层：选区对话框（src/app/dialogs/SemanticOrderSelectionDialog.*）——
#     它不依赖 PdfView，可以单测；
#   - PDFium 适配层：用来验「写回后的文档还打不打得开、页数变没变」；
#   - 内容流分词（src/pdf/content/ContentStream.*）：领域层要靠它把
#     标记内容与文本运行对上号。
#
# 为什么要 qpdf：语义树是对象级信息，只有真正的 PDF 解析器给得出答案。

if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

qt_add_executable(SemanticOrderSelectionTests
    ${CMAKE_CURRENT_LIST_DIR}/../SemanticOrderSelectionTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/SemanticOrderSelection.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/SemanticOrderSelection.h
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
    ${AIPDF_REPO_ROOT}/src/app/dialogs/SemanticOrderSelectionDialog.cpp
    ${AIPDF_REPO_ROOT}/src/app/dialogs/SemanticOrderSelectionDialog.h
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.cpp
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.h
    ${AIPDF_REPO_ROOT}/src/i18n/Translate.h
)

target_link_libraries(SemanticOrderSelectionTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(SemanticOrderSelectionTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(SemanticOrderSelectionTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET SemanticOrderSelectionTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:SemanticOrderSelectionTests>"
        COMMENT "Copying qpdf runtime for SemanticOrderSelectionTests"
    )
endif()

target_include_directories(SemanticOrderSelectionTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

set_target_properties(SemanticOrderSelectionTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(SemanticOrderSelectionTests)
aipdf_deploy_pdfium(SemanticOrderSelectionTests)

add_test(NAME SemanticOrderSelectionTests COMMAND SemanticOrderSelectionTests)
