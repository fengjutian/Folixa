# PDF → Excel / PowerPoint / Word（整篇）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include。
#
# 编进来的：
#   - doc/PdfToOffice（这一批新做的导出）
#   - doc/OfficeExport（文字层 → 行 → Word/RTF/纯文本）
#   - doc/SpreadsheetExport（工作表 → xlsx）
#   - doc/PptxWriter（幻灯片 → pptx）
#   - doc/ZipWriter / doc/XmlText（上面几个的底座）
#   - pdf/PdfiumAdapter（读 PDF）+ pdf/TableStructure（表格识别）
#
# 为什么又要 PDFium：导出的前提是「真的读到了这份 PDF 的文字层」，
# 读不到就等于没导出。用假的文本页来测只能证明 XML 生成对了，
# 证明不了「PDF → Excel」这条链是通的。

if (NOT DEFINED AIPDF_SOURCE_ROOT)
    set(AIPDF_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")
endif()

qt_add_executable(PdfToOfficeTests
    ${CMAKE_CURRENT_LIST_DIR}/../PdfToOfficeTests.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/PdfToOffice.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/PdfToOffice.h
    ${AIPDF_SOURCE_ROOT}/src/doc/OfficeExport.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/OfficeExport.h
    ${AIPDF_SOURCE_ROOT}/src/doc/SpreadsheetExport.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/SpreadsheetExport.h
    ${AIPDF_SOURCE_ROOT}/src/doc/PptxWriter.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/PptxWriter.h
    ${AIPDF_SOURCE_ROOT}/src/doc/ZipWriter.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/ZipWriter.h
    ${AIPDF_SOURCE_ROOT}/src/doc/XmlText.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/PdfiumAdapter.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/PdfiumAdapter.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/Annotation.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/Annotation.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/TextPage.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/TextPage.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/TableStructure.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/TableStructure.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/Navigation.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/Navigation.h
)

target_link_libraries(PdfToOfficeTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(PdfToOfficeTests PRIVATE
    ${AIPDF_SOURCE_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(PdfToOfficeTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${CMAKE_CURRENT_BINARY_DIR}/fixtures")

set_target_properties(PdfToOfficeTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(PdfToOfficeTests pdf-fixtures)
aipdf_deploy_qt_runtime(PdfToOfficeTests)
aipdf_deploy_pdfium(PdfToOfficeTests)

# 样例目录：导出产物落在那里，交给独立校验脚本（见下）。
set(PDF_OFFICE_SAMPLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/office-packages")

add_test(NAME PdfToOfficeTests COMMAND PdfToOfficeTests)
# 让测试把产物落盘：不落盘的话，独立脚本就没有东西可验。
set_tests_properties(PdfToOfficeTests PROPERTIES
    ENVIRONMENT "AIPDF_OFFICE_DUMP_DIR=${PDF_OFFICE_SAMPLE_DIR}"
)

# 独立校验：用 Python 的 zipfile + openpyxl / python-pptx / LibreOffice
# 重新读一遍产物（不依赖本仓库的 zip 写入器与读回器）。
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME PdfToOfficeVerifyTests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/verify-pdf-office-packages.py
            --dir ${PDF_OFFICE_SAMPLE_DIR}
)
set_tests_properties(PdfToOfficeVerifyTests PROPERTIES
    DEPENDS PdfToOfficeTests
)
