# 字形级内容流替换（pdf::TextSplice）：
# 只改被选中的字符，用原字体重新编码，宽度变化用 TJ 位移补偿。

qt_add_executable(TextSpliceTests
    ${CMAKE_CURRENT_LIST_DIR}/../TextSpliceTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TextSplice.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TextSplice.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/FontResource.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/FontResource.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TrueTypeFace.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TrueTypeFace.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TrueTypeSubset.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TrueTypeSubset.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
)

target_link_libraries(TextSpliceTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
    qpdf::libqpdf
)
target_compile_definitions(TextSpliceTests PRIVATE AIPDF_WITH_QPDF=1)

add_custom_command(TARGET TextSpliceTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:TextSpliceTests>"
    COMMENT "Copying qpdf runtime for TextSpliceTests"
)

target_include_directories(TextSpliceTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(TextSpliceTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(TextSpliceTests)
aipdf_deploy_pdfium(TextSpliceTests)

add_test(NAME TextSpliceTests COMMAND TextSpliceTests)
