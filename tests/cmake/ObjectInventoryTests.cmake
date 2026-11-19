# 页面对像清单：重排写回前后对照，丢一个对象就拒绝。

qt_add_executable(ObjectInventoryTests
    ${CMAKE_CURRENT_LIST_DIR}/../ObjectInventoryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ObjectInventory.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ObjectInventory.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageGraphics.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageGraphics.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
)

target_link_libraries(ObjectInventoryTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
    qpdf::libqpdf
)
target_compile_definitions(ObjectInventoryTests PRIVATE AIPDF_WITH_QPDF=1)

add_custom_command(TARGET ObjectInventoryTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:ObjectInventoryTests>"
    COMMENT "Copying qpdf runtime for ObjectInventoryTests"
)

target_include_directories(ObjectInventoryTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(ObjectInventoryTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(ObjectInventoryTests)
aipdf_deploy_pdfium(ObjectInventoryTests)

add_test(NAME ObjectInventoryTests COMMAND ObjectInventoryTests)
