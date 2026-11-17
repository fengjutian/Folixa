# 导出格式模块（HTML / 图像化 PDF / DXF）的专项单测。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。
#
# AIPDF_SOURCE_ROOT：默认就是仓库根（CMAKE_SOURCE_DIR）。留这个口子是给
# 仓库外的自检工程用的——它 include 同一个片段，但要把路径指回仓库。

if (NOT DEFINED AIPDF_SOURCE_ROOT)
    set(AIPDF_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")
endif()

qt_add_executable(ExportFormatsTests
    ${CMAKE_CURRENT_LIST_DIR}/../ExportFormatsTests.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/ExportFormats.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/ExportFormats.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/PdfiumAdapter.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/PdfiumAdapter.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/Annotation.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/Annotation.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/TextPage.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/TextPage.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/Navigation.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/Navigation.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/TableStructure.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/TableStructure.h
)

target_link_libraries(ExportFormatsTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(ExportFormatsTests PRIVATE
    ${AIPDF_SOURCE_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(ExportFormatsTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

set_target_properties(ExportFormatsTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(ExportFormatsTests pdf-fixtures)
aipdf_deploy_qt_runtime(ExportFormatsTests)
aipdf_deploy_pdfium(ExportFormatsTests)

add_test(NAME ExportFormatsTests COMMAND ExportFormatsTests)
