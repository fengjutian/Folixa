# 网页 → PDF：用系统浏览器引擎渲染并打印的测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很长的清单里塞细节。
# 依赖的变量与函数（aipdf_deploy_qt_runtime / aipdf_deploy_pdfium、
# AIPDF_PDFIUM_LIBRARY、AIPDF_PDFIUM_INCLUDE_DIR）都在父作用域里定义好了，
# include 进来直接可用。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：片段文件本身在 tests/cmake/ 下，
# 这样无论谁 include、从哪个目录 configure，路径都对，不写死绝对路径。
#
# 为什么又要 PDFium：这一组要证明「浏览器打印出来的 PDF 是能用的」——
# 页数对不对、坏文件有没有被拦下来，只能真的用 PDFium 读一遍才知道。
# 坏 PDF 交出去是最糟的失败，所以这一层校验必须真的存在、真的被测到。
#
# 编进来的：
#   - doc/convert/BrowserPdfPrinter（引擎探测 + 参数拼装 + 打印 + 产物校验）
#   - pdf/kernel/PdfiumAdapter（读 PDF：页数、能不能打开）
#   - pdf/annot/Annotation、pdf/kernel/TextPage、Navigation、TableStructure
#     （PdfiumAdapter 的配套件）

if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

qt_add_executable(BrowserPdfPrinterTests
    ${CMAKE_CURRENT_LIST_DIR}/../BrowserPdfPrinterTests.cpp
    ${AIPDF_REPO_ROOT}/src/doc/convert/BrowserPdfPrinter.cpp
    ${AIPDF_REPO_ROOT}/src/doc/convert/BrowserPdfPrinter.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.h
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/Navigation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/Navigation.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TableStructure.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TableStructure.h
)

target_link_libraries(BrowserPdfPrinterTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(BrowserPdfPrinterTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

set_target_properties(BrowserPdfPrinterTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(BrowserPdfPrinterTests)
aipdf_deploy_pdfium(BrowserPdfPrinterTests)

# 这一组**不依赖网络**：真实渲染用的是本地 file:// 页面。
# 没有装浏览器时最后一条用例会 QSKIP（跳过不算失败）。
add_test(NAME BrowserPdfPrinterTests COMMAND BrowserPdfPrinterTests)
