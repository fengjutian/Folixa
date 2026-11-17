# 扫描仪取帧链路的测试目标定义。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：片段文件本身在 tests/cmake/ 下，
# 这样无论谁 include、从哪个目录 configure，路径都对，不写死绝对路径。
#
# 为什么这个目标要 Qt6::Gui 和 Qt6::Widgets：
#   * Gui —— 用例要把拼出来的 PDF 渲染成图，核对页序与颜色；
#   * Widgets —— ScannerCapture.cpp 里带着扫描对话框（QDialog），必须能链上 Qt Widgets。
#
# 为什么一定要 PDFium：拼出来的 PDF 必须能被重新打开并渲染，
# 只看字节拼对没有意义——「写得出来」和「读得回去」是两件事。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(ScannerCaptureTests
    ${CMAKE_CURRENT_LIST_DIR}/../ScannerCaptureTests.cpp
    # ---- 被测的领域层 ----
    ${AIPDF_REPO_ROOT}/src/pdf/ImageToPdf.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ImageToPdf.h
    # ---- 被测的应用层（WIA 后端 + 帧源接口）----
    ${AIPDF_REPO_ROOT}/src/app/ScannerCapture.cpp
    ${AIPDF_REPO_ROOT}/src/app/ScannerCapture.h
    # ---- 读回验证用的 PDFium 适配层与它的依赖 ----
    ${AIPDF_REPO_ROOT}/src/pdf/PdfiumAdapter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/PdfiumAdapter.h
    ${AIPDF_REPO_ROOT}/src/pdf/Annotation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/Annotation.h
    ${AIPDF_REPO_ROOT}/src/pdf/TextPage.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/TextPage.h
    ${AIPDF_REPO_ROOT}/src/i18n/Translate.h
)

target_link_libraries(ScannerCaptureTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(ScannerCaptureTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

set_target_properties(ScannerCaptureTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 部署 Qt 运行时与 PDFium 的函数定义在仓库根作用域里；
# 独立的自检工程不一定有它们，所以先问一句再用。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(ScannerCaptureTests)
endif()
if (COMMAND aipdf_deploy_pdfium)
    aipdf_deploy_pdfium(ScannerCaptureTests)
endif()

add_test(NAME ScannerCaptureTests COMMAND ScannerCaptureTests)
