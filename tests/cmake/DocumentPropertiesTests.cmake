# 文档属性（PDF 的 /Info）：读写都走 qpdf。
#
# 没有 qpdf 的构建也能编：模块自己会返回"不可用"，
# 这样"没编 qpdf 时会不会静默出错"这件事在 CI 里也测得到。
# PDFium 是为了证明"写出来的文件仍然能打开"。

qt_add_executable(DocumentPropertiesTests
    ${CMAKE_CURRENT_LIST_DIR}/../DocumentPropertiesTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/DocumentProperties.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/DocumentProperties.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(DocumentPropertiesTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(DocumentPropertiesTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(DocumentPropertiesTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(DocumentPropertiesTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET DocumentPropertiesTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:DocumentPropertiesTests>"
        COMMENT "Copying qpdf runtime for tests")
endif()
set_target_properties(DocumentPropertiesTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(DocumentPropertiesTests)
aipdf_deploy_pdfium(DocumentPropertiesTests)

add_test(NAME DocumentPropertiesTests COMMAND DocumentPropertiesTests)
