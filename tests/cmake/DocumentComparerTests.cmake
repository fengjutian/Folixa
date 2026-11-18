# 文档比较（领域层 + 对话框）。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：片段文件本身在 tests/cmake/ 下，
# 这样无论谁 include、从哪个目录 configure，路径都对，不写死绝对路径。
#
# 为什么既要 PDFium 又要 Widgets：
#   - 文字块是从 PDFium 的字符盒聚出来的，没有 PDFium 就没法比；
#   - 对话框那一层要真的建控件、真的填表格、真的发跳转信号，
#     只测领域层的话，界面这一层等于没验。
#
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、AIPDF_PDFIUM_LIBRARY、
# AIPDF_PDFIUM_INCLUDE_DIR）与函数（aipdf_deploy_qt_runtime、aipdf_deploy_pdfium）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(DocumentComparerTests
    ${CMAKE_CURRENT_LIST_DIR}/../DocumentComparerTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/DocumentComparer.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/DocumentComparer.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.h
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.h
    ${AIPDF_REPO_ROOT}/src/app/DocumentCompareDialog.cpp
    ${AIPDF_REPO_ROOT}/src/app/DocumentCompareDialog.h
    ${AIPDF_REPO_ROOT}/src/app/Language.cpp
    ${AIPDF_REPO_ROOT}/src/app/Language.h
    ${AIPDF_REPO_ROOT}/src/i18n/Translate.h
)

target_link_libraries(DocumentComparerTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(DocumentComparerTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(DocumentComparerTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

# 翻译目录：对话框要能在切换语言时就地重译，测试里也要能建起来。
file(GLOB AIPDF_COMPARE_I18N_CATALOGS CONFIGURE_DEPENDS
    "${AIPDF_REPO_ROOT}/src/i18n/*.ts")
qt_add_resources(DocumentComparerTests "document_compare_catalogs"
    PREFIX "/i18n"
    BASE "${AIPDF_REPO_ROOT}/src/i18n"
    FILES ${AIPDF_COMPARE_I18N_CATALOGS}
)

set_target_properties(DocumentComparerTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(DocumentComparerTests pdf-fixtures)

# 部署 Qt 运行时与 PDFium 的函数定义在仓库根作用域里；
# 独立的自检工程不一定有它们，所以先问一句再用。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(DocumentComparerTests)
endif()
if (COMMAND aipdf_deploy_pdfium)
    aipdf_deploy_pdfium(DocumentComparerTests)
endif()

add_test(NAME DocumentComparerTests COMMAND DocumentComparerTests)
