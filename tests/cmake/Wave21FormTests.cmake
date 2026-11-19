# 2026-11-19 wave21：表单自动识别（无域表单页）+ 设计助手画版面与跨页。

qt_add_executable(Wave21FormTests
    ${CMAKE_CURRENT_LIST_DIR}/../Wave21FormTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormAutoDetect.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormAutoDetect.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormDesignParser.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormDesignParser.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormFieldWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormFieldWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/BarcodeEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/BarcodeEncoder.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageGraphics.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageGraphics.h
)

target_link_libraries(Wave21FormTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(Wave21FormTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(Wave21FormTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(Wave21FormTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(Wave21FormTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET Wave21FormTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:Wave21FormTests>")
    add_dependencies(Wave21FormTests pdf-fixtures)
endif()

set_target_properties(Wave21FormTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(Wave21FormTests)
aipdf_deploy_pdfium(Wave21FormTests)

add_test(NAME Wave21FormTests COMMAND Wave21FormTests)
