# 版面结构差异（块级归因）：L7 的专项测试。
#
# 仓库规矩用 CMAKE_CURRENT_LIST_DIR/../.. 定位：片段文件本身放在 tests/cmake/ 下，
# 这样无论谁 include、从事哪个目录 configure，路径都能对，不必写死绝对路径。
#
# 依赖的是仓库现有的模块：
#   * pdf::compareDocumentLayout / pdf::diffLayoutBlocks —— 版面块匹配与差异归因；
#   * pdf::compareDocuments —— 文字层 + 版面差异两条通路的入口；
#   * pdf::LayoutBlocks —— 版面块抽取（文字段落 / 图片 / 表格 / 图形）；
#   * PDFium —— 读页、取字符框、取对象框、渲染（算图片像素指纹）。
# 这一组不需要 PDF 夹具：夹具在测试里手搓，块的位置能精确控制。
get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(LayoutDiffTests
    ${CMAKE_CURRENT_LIST_DIR}/../LayoutDiffTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/LayoutDiff.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/LayoutDiff.h
    ${AIPDF_REPO_ROOT}/src/pdf/ops/DocumentComparer.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ops/DocumentComparer.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/LayoutBlocks.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/LayoutBlocks.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TableStructure.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TableStructure.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.h
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.h
)

target_link_libraries(LayoutDiffTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(LayoutDiffTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

# DocumentComparer.cpp 现在会调用版面分析（pdf::compareDocumentLayout），
# 那个目标要是不带上新文件就会链接失败。
#
# 为什么不改 tests/cmake/DocumentComparerTests.cmake：那个文件不在本次改动的
# 归属范围内。新文件由本片段统一加进来，改动只有这一处。
if (TARGET DocumentComparerTests)
    target_sources(DocumentComparerTests PRIVATE
        ${AIPDF_REPO_ROOT}/src/pdf/ops/LayoutDiff.cpp
        ${AIPDF_REPO_ROOT}/src/pdf/ops/LayoutDiff.h
        ${AIPDF_REPO_ROOT}/src/pdf/kernel/LayoutBlocks.cpp
        ${AIPDF_REPO_ROOT}/src/pdf/kernel/LayoutBlocks.h
        ${AIPDF_REPO_ROOT}/src/pdf/kernel/TableStructure.cpp
        ${AIPDF_REPO_ROOT}/src/pdf/kernel/TableStructure.h
    )
endif()

set_target_properties(LayoutDiffTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 部署 Qt 运行时与 PDFium 的动态库到仓库根目录工作区：
# 独立的自检工程不一定有它们，先部署一步再跑。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(LayoutDiffTests)
endif()
if (COMMAND aipdf_deploy_pdfium)
    aipdf_deploy_pdfium(LayoutDiffTests)
endif()

add_test(NAME LayoutDiffTests COMMAND LayoutDiffTests)
