# 自动创建书签：分块 → 大纲树 → 写进 PDF → PDFium 读回。
#
# 最后一环要 PDFium（读回）与夹具（normal.pdf），所以这一组跟着 pdf-fixtures 走。

qt_add_executable(OutlineGeneratorTests
    ${CMAKE_CURRENT_LIST_DIR}/../OutlineGeneratorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OutlineGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OutlineGenerator.h
    ${CMAKE_SOURCE_DIR}/src/pdf/document/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/Navigation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OutlineWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OutlineWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/doc/ChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/ChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/doc/DocumentChunk.h
)

target_link_libraries(OutlineGeneratorTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(OutlineGeneratorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(OutlineGeneratorTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${CMAKE_CURRENT_BINARY_DIR}/fixtures"
)
set_target_properties(OutlineGeneratorTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(OutlineGeneratorTests pdf-fixtures)
aipdf_deploy_qt_runtime(OutlineGeneratorTests)
aipdf_deploy_pdfium(OutlineGeneratorTests)

add_test(NAME OutlineGeneratorTests COMMAND OutlineGeneratorTests)
