# 文档级附件（面板 + 适配层）的测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很长的清单里塞细节。
# 依赖的变量与函数（aipdf_deploy_qt_runtime、aipdf_deploy_pdfium）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 编进来的：
#   - app/panels/AttachmentPanel（面板本身）
#   - app/ui/Language（面板要跟着界面语言切换重译）
#   - pdf/kernel/PdfiumAdapter（附件的领域层：加 / 读 / 删 / 序列化）
#   - pdf/annot/Annotation、pdf/kernel/TextPage（PdfiumAdapter 的依赖）
#
# 夹具用仓库自带的 pdf-fixtures（normal.pdf），不自己造 PDF。

if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

qt_add_executable(AttachmentPanelTests
    ${CMAKE_CURRENT_LIST_DIR}/../AttachmentPanelTests.cpp
    ${AIPDF_REPO_ROOT}/src/app/panels/AttachmentPanel.cpp
    ${AIPDF_REPO_ROOT}/src/app/panels/AttachmentPanel.h
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.cpp
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.h
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.h
)

target_link_libraries(AttachmentPanelTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(AttachmentPanelTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(AttachmentPanelTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

set_target_properties(AttachmentPanelTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(AttachmentPanelTests pdf-fixtures)
aipdf_deploy_qt_runtime(AttachmentPanelTests)
aipdf_deploy_pdfium(AttachmentPanelTests)

add_test(NAME AttachmentPanelTests COMMAND AttachmentPanelTests)
