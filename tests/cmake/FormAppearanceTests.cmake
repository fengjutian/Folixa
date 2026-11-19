# 表单值写回与 /AP 外观流：值写 /V，widget 写可被别的阅读器看见的外观流。

qt_add_executable(FormAppearanceTests
    ${CMAKE_CURRENT_LIST_DIR}/../FormAppearanceTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormAppearance.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormAppearance.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
)

target_link_libraries(FormAppearanceTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
    qpdf::libqpdf
)
target_compile_definitions(FormAppearanceTests PRIVATE AIPDF_WITH_QPDF=1)

add_custom_command(TARGET FormAppearanceTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:FormAppearanceTests>"
    COMMENT "Copying qpdf runtime for FormAppearanceTests"
)

target_include_directories(FormAppearanceTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(FormAppearanceTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(FormAppearanceTests)
aipdf_deploy_pdfium(FormAppearanceTests)

add_test(NAME FormAppearanceTests COMMAND FormAppearanceTests)
