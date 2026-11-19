# 2026-11-19 wave21：自动创建书签（读标签树）+ 交叉引用自动识别。
#
# 依赖说明：
#   * StructTreeReader 读标签树要用 ContentStream 分词器；
#   * OutlineWriter 负责把生成的大纲写回 PDF；
#   * PageAnnotsInjector 负责把识别出来的引用变成可点击的跳转；
#   * PdfiumAdapter 用来读回文本层与链接注释。

qt_add_executable(Wave21NavTests
    ${CMAKE_CURRENT_LIST_DIR}/../Wave21NavTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/OutlineGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/OutlineGenerator.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/CrossReference.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/CrossReference.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OutlineWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OutlineWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/PageAnnotsInjector.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/PageAnnotsInjector.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/StructTreeReader.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/StructTreeReader.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(Wave21NavTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(Wave21NavTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(Wave21NavTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(Wave21NavTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(Wave21NavTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET Wave21NavTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:Wave21NavTests>")
    add_dependencies(Wave21NavTests pdf-fixtures)
endif()

set_target_properties(Wave21NavTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(Wave21NavTests)
aipdf_deploy_pdfium(Wave21NavTests)

add_test(NAME Wave21NavTests COMMAND Wave21NavTests)
