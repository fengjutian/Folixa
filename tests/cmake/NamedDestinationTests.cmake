# 命名目标（named destination）：把「某一页的某个位置」起个名字写进 PDF，
# 并能按名字读回页码与落点。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。
#
# 为什么既要 qpdf 又要 PDFium：写命名目标是对象级操作，走 qpdf；
# 验「写完还能不能打开」「正文有没有变」「页数有没有变」走 PDFium——
# 两头都要真的跑一遍才算数。

qt_add_executable(NamedDestinationTests
    ${CMAKE_CURRENT_LIST_DIR}/../NamedDestinationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/NamedDestination.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/NamedDestination.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(NamedDestinationTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(NamedDestinationTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(NamedDestinationTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET NamedDestinationTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:NamedDestinationTests>"
        COMMENT "Copying qpdf runtime for NamedDestinationTests"
    )
endif()

target_include_directories(NamedDestinationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(NamedDestinationTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(NamedDestinationTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(NamedDestinationTests pdf-fixtures)
aipdf_deploy_qt_runtime(NamedDestinationTests)
aipdf_deploy_pdfium(NamedDestinationTests)

add_test(NAME NamedDestinationTests COMMAND NamedDestinationTests)
