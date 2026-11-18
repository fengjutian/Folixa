# 隐藏数据逐项清理（枚举 + 选择性清理）的单元测试目标。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：片段文件本身在 tests/cmake/ 下，
# 这样无论谁 include、从哪个目录 configure，路径都对，不写死绝对路径。
#
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、AIPDF_PDFIUM_LIBRARY、
# AIPDF_PDFIUM_INCLUDE_DIR）与函数（aipdf_deploy_qt_runtime、aipdf_deploy_pdfium）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。
#
# qpdf 是可选依赖：没有它的时候目标照样建，只是链接不上 qpdf，
# 用例整体跳过。这样「没启用 qpdf 时给出中文错误」那条路径也能被跑到。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(HiddenDataInventoryTests
    ${CMAKE_CURRENT_LIST_DIR}/../HiddenDataInventoryTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/security/PdfSecurity.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/security/PdfSecurity.h
    ${AIPDF_REPO_ROOT}/src/pdf/document/PdfRevisionCounter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/document/PdfRevisionCounter.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.h
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.h
)

target_link_libraries(HiddenDataInventoryTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(HiddenDataInventoryTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(HiddenDataInventoryTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(HiddenDataInventoryTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(HiddenDataInventoryTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET HiddenDataInventoryTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:HiddenDataInventoryTests>"
        COMMENT "Copying qpdf runtime for tests")
endif()

set_target_properties(HiddenDataInventoryTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(HiddenDataInventoryTests pdf-fixtures)

# 部署 Qt 运行时与 PDFium 的函数定义在仓库根作用域里；
# 独立的自检工程不一定有它们，所以先问一句再用。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(HiddenDataInventoryTests)
endif()
if (COMMAND aipdf_deploy_pdfium)
    aipdf_deploy_pdfium(HiddenDataInventoryTests)
endif()

add_test(NAME HiddenDataInventoryTests COMMAND HiddenDataInventoryTests)
