# 转换输入三件套（剪贴板 / 网页 / 表单文件）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量与函数（aipdf_deploy_qt_runtime / aipdf_deploy_pdfium、
# AIPDF_PDFIUM_LIBRARY、AIPDF_PDFIUM_INCLUDE_DIR）都在父作用域里定义好了，
# include 进来直接可用。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：片段文件本身在 tests/cmake/ 下，
# 这样无论谁 include、从哪个目录 configure，路径都对，不写死绝对路径。
#
# 编进来的分三块：
#   - 领域层：src/doc/ConversionInputs.*（本批新加的三个输入能力）；
#   - 应用层：src/app/ConversionInputDialog.*（网络抓取与对话框）；
#   - 被复用的既有能力：doc/UniversalConverter（排版）、pdf/FormData（FDF 解析）、
#     pdf/Xfdf（XFDF 解析）、pdf/PdfiumAdapter（打开与读回）。
# 被复用的那几个源文件必须一起编进来：这一组不重写它们，只是不重复实现。
#
# 为什么又要 PDFium：这一组要证明「生成的 PDF 能被别的阅读器打开」——
# 页数对不对、文字抽不抽得回来、表单值填没填进去，只能真的用 PDFium 读一遍
# 才知道。用 qpdf 只能量结构，证明不了阅读器看得见。
#
# 夹具：tests/CMakeLists.txt 里的 pdf-fixtures 目标会生成 form.pdf
# （带 AcroForm 的表单：文本域、复选框、下拉框、只读域）与 normal.pdf。
# 表单套模板的用例用 form.pdf 当模板。

set(AIPDF_CONVERSION_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

# 夹具目录：正常情况下片段在 tests/ 作用域里被 include，夹具在
# <tests 的二进制目录>/fixtures；从根作用域 include 时（自检用）改用
# <构建目录>/tests/fixtures —— 两种情况都要能找到同一批文件。
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL "${AIPDF_CONVERSION_ROOT}/tests")
    set(AIPDF_CONVERSION_FIXTURES "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
else()
    set(AIPDF_CONVERSION_FIXTURES "${CMAKE_BINARY_DIR}/tests/fixtures")
endif()

qt_add_executable(ConversionInputsTests
    ${CMAKE_CURRENT_LIST_DIR}/../ConversionInputsTests.cpp
    ${AIPDF_CONVERSION_ROOT}/src/doc/ConversionInputs.cpp
    ${AIPDF_CONVERSION_ROOT}/src/doc/ConversionInputs.h
    ${AIPDF_CONVERSION_ROOT}/src/doc/UniversalConverter.cpp
    ${AIPDF_CONVERSION_ROOT}/src/doc/UniversalConverter.h
    ${AIPDF_CONVERSION_ROOT}/src/pdf/content/TrueTypeSubset.cpp
    ${AIPDF_CONVERSION_ROOT}/src/pdf/content/TrueTypeSubset.h
    ${AIPDF_CONVERSION_ROOT}/src/doc/BlankDocument.cpp
    ${AIPDF_CONVERSION_ROOT}/src/doc/BlankDocument.h
    ${AIPDF_CONVERSION_ROOT}/src/pdf/content/ContentStream.cpp
    ${AIPDF_CONVERSION_ROOT}/src/pdf/content/ContentStream.h
    ${AIPDF_CONVERSION_ROOT}/src/pdf/forms/FormData.cpp
    ${AIPDF_CONVERSION_ROOT}/src/pdf/forms/FormData.h
    ${AIPDF_CONVERSION_ROOT}/src/pdf/convert/ImageToPdf.cpp
    ${AIPDF_CONVERSION_ROOT}/src/pdf/convert/ImageToPdf.h
    ${AIPDF_CONVERSION_ROOT}/src/pdf/annot/Xfdf.cpp
    ${AIPDF_CONVERSION_ROOT}/src/pdf/annot/Xfdf.h
    ${AIPDF_CONVERSION_ROOT}/src/pdf/annot/Annotation.cpp
    ${AIPDF_CONVERSION_ROOT}/src/pdf/annot/Annotation.h
    ${AIPDF_CONVERSION_ROOT}/src/pdf/kernel/PdfiumAdapter.cpp
    ${AIPDF_CONVERSION_ROOT}/src/pdf/kernel/PdfiumAdapter.h
    ${AIPDF_CONVERSION_ROOT}/src/pdf/kernel/TextPage.cpp
    ${AIPDF_CONVERSION_ROOT}/src/pdf/kernel/TextPage.h
    ${AIPDF_CONVERSION_ROOT}/src/app/ConversionInputDialog.cpp
    ${AIPDF_CONVERSION_ROOT}/src/app/ConversionInputDialog.h
    ${AIPDF_CONVERSION_ROOT}/src/i18n/Translate.h
)

target_link_libraries(ConversionInputsTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(ConversionInputsTests PRIVATE
    ${AIPDF_CONVERSION_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(ConversionInputsTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${AIPDF_CONVERSION_FIXTURES}"
)

set_target_properties(ConversionInputsTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(ConversionInputsTests pdf-fixtures)
aipdf_deploy_qt_runtime(ConversionInputsTests)
aipdf_deploy_pdfium(ConversionInputsTests)

add_test(NAME ConversionInputsTests COMMAND ConversionInputsTests)
