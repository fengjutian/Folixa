# PDF → Word / PowerPoint 的图片与表格导出测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include。
#
# 编进来的：
#   - doc/PdfToOffice（门面：PDF → Word / PPT / Excel）
#   - doc/OfficeExport（文字层 → 行 → Word/RTF/纯文本）+ 分带（双栏）
#   - doc/OfficeMedia（OOXML 资源通道：媒体编号、关系号）
#   - doc/PptxWriter（幻灯片 → pptx，含图片与表格）
#   - doc/ZipWriter / doc/XmlText（上面几个的底座）
#   - pdf/PdfiumAdapter（读 PDF：文字层与图片）+ pdf/TableStructure（表格识别）
#
# 为什么又要 PDFium：导出的前提是「真的读到了这份 PDF 的文字层与图片」，
# 读不到就等于没导出。用假的文本页来测只能证明 XML 生成对了，
# 证明不了「PDF → Word」这条链是通的。

if (NOT DEFINED AIPDF_SOURCE_ROOT)
    set(AIPDF_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")
endif()

qt_add_executable(OfficeImagesTests
    ${CMAKE_CURRENT_LIST_DIR}/../OfficeImagesTests.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/export/PdfToOffice.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/export/PdfToOffice.h
    ${AIPDF_SOURCE_ROOT}/src/doc/export/OfficeExport.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/export/OfficeExport.h
    ${AIPDF_SOURCE_ROOT}/src/doc/export/OfficeMedia.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/export/OfficeMedia.h
    ${AIPDF_SOURCE_ROOT}/src/doc/export/SpreadsheetExport.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/export/SpreadsheetExport.h
    ${AIPDF_SOURCE_ROOT}/src/doc/export/PptxWriter.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/export/PptxWriter.h
    ${AIPDF_SOURCE_ROOT}/src/doc/export/ZipWriter.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/export/ZipWriter.h
    ${AIPDF_SOURCE_ROOT}/src/doc/export/XmlText.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/kernel/PdfiumAdapter.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/kernel/PdfiumAdapter.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/annot/Annotation.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/annot/Annotation.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/kernel/TextPage.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/kernel/TextPage.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/kernel/TableStructure.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/kernel/TableStructure.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/kernel/Navigation.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/kernel/Navigation.h
)

target_link_libraries(OfficeImagesTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(OfficeImagesTests PRIVATE
    ${AIPDF_SOURCE_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

set_target_properties(OfficeImagesTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(OfficeImagesTests)
aipdf_deploy_pdfium(OfficeImagesTests)

# ---------------------------------------------------------------------------
# 语料 → 导出 → 独立校验（三道）
# ---------------------------------------------------------------------------

# 语料目录与产物目录都放在构建树里，不污染源码树。
set(AIPDF_OFFICE_CORPUS_DIR "${CMAKE_CURRENT_BINARY_DIR}/office-corpus")
set(AIPDF_OFFICE_EXPORT_DIR "${CMAKE_CURRENT_BINARY_DIR}/office-export")
set(AIPDF_OFFICE_CORPUS_COUNT 50 CACHE STRING "Office 四合一语料的份数（默认 50）")

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# 第一道：生成语料。
#
# 50 份「双栏 + 表格 + 图片 + CJK」四合一 PDF，外加一份写死了期望值的
# corpus.meta.json。语料里要内嵌中文字体（本机没有中文字体时这一道会红，
# 这是如实报告：没有 CJK 的语料测不了 CJK）。
add_test(NAME OfficeCorpusGenerateTests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/gen-office-corpus.py
            --out ${AIPDF_OFFICE_CORPUS_DIR}
            --count ${AIPDF_OFFICE_CORPUS_COUNT}
            --quiet
)

# 第二道：批量导出（同一个二进制换个模式跑）。
#
# 导出这一侧只负责「读 PDF → 写 docx/pptx/xlsx → 落盘 + 自报计数」，
# 读回来对不对由下面的独立校验说了算。
add_test(NAME OfficeCorpusExportTests
    COMMAND OfficeImagesTests --export-corpus ${AIPDF_OFFICE_CORPUS_DIR} ${AIPDF_OFFICE_EXPORT_DIR}
)

# 单测本身：除了自己造的小夹具，还要跟语料的期望值对一遍。
add_test(NAME OfficeImagesTests COMMAND OfficeImagesTests)
set_tests_properties(OfficeImagesTests PROPERTIES
    DEPENDS OfficeCorpusGenerateTests
    ENVIRONMENT "AIPDF_OFFICE_DUMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/office-images;AIPDF_OFFICE_CORPUS_DIR=${AIPDF_OFFICE_CORPUS_DIR}"
)

# 第三道：独立校验。
#
# 用 python-docx / python-pptx / openpyxl（**别的实现**）把产物重新读一遍，
# 断言页数、图片数、表格数与语料写死的期望值一致；还要跑一遍负例对照：
# 故意弄坏产物，校验必须当场变红。
add_test(NAME OfficeImagesVerifyTests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/verify-office-images.py
            --corpus ${AIPDF_OFFICE_CORPUS_DIR}
            --dump ${AIPDF_OFFICE_EXPORT_DIR}
            --samples ${CMAKE_CURRENT_BINARY_DIR}/office-images
)
set_tests_properties(OfficeImagesVerifyTests PROPERTIES
    DEPENDS "OfficeImagesTests;OfficeCorpusExportTests"
)
