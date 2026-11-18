# TrueType 子集化（嵌入字体只保留用到的字形）。
#
# 三块：
#   * 子集化本身（字形号不变、复合字形带组件、坏字体老实报错）；
#   * 真机上的中文字体（整份 10 MB → 子集后只剩用到的字形）；
#   * 端到端：转换器嵌的是子集，PDFium 仍能抽出中文、也能把字画出来。

if (NOT DEFINED AIPDF_SOURCE_ROOT)
    set(AIPDF_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")
endif()

qt_add_executable(TrueTypeSubsetTests
    ${CMAKE_CURRENT_LIST_DIR}/../TrueTypeSubsetTests.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/TrueTypeSubset.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/TrueTypeSubset.h
    ${AIPDF_SOURCE_ROOT}/src/pdf/ContentStream.cpp
    ${AIPDF_SOURCE_ROOT}/src/pdf/ContentStream.h
    ${AIPDF_SOURCE_ROOT}/src/doc/UniversalConverter.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/UniversalConverter.h
    ${AIPDF_SOURCE_ROOT}/src/doc/BlankDocument.cpp
    ${AIPDF_SOURCE_ROOT}/src/doc/BlankDocument.h
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

target_link_libraries(TrueTypeSubsetTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(TrueTypeSubsetTests PRIVATE
    ${AIPDF_SOURCE_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

set_target_properties(TrueTypeSubsetTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(TrueTypeSubsetTests)
aipdf_deploy_pdfium(TrueTypeSubsetTests)

add_test(NAME TrueTypeSubsetTests COMMAND TrueTypeSubsetTests)
