# 可选内容图层（OCG）的单元测试目标：读 /OCProperties、写图层与标记内容。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：写死绝对路径会让这份片段
# 换一台机器就废掉，而它是跟着仓库走的。
#
# 为什么既要 qpdf 又要 PDFium：图层的读写是对象级操作，走 qpdf；
# 验「写完还能不能打开」「文字有没有丢」走 PDFium——两头都要真的跑一遍才算数。
# 没有 qpdf 的构建跳过这些用例（测试里 QSKIP）。

qt_add_executable(OptionalContentTests
    ${CMAKE_CURRENT_LIST_DIR}/../OptionalContentTests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/pdf/OptionalContent.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/pdf/OptionalContent.h
    ${CMAKE_CURRENT_LIST_DIR}/../../src/pdf/PdfiumAdapter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/pdf/PdfiumAdapter.h
    ${CMAKE_CURRENT_LIST_DIR}/../../src/pdf/Annotation.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/pdf/Annotation.h
    ${CMAKE_CURRENT_LIST_DIR}/../../src/pdf/TextPage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/pdf/TextPage.h
)

target_link_libraries(OptionalContentTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(OptionalContentTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(OptionalContentTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET OptionalContentTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:OptionalContentTests>"
        COMMENT "Copying qpdf runtime for OptionalContentTests"
    )
endif()

target_include_directories(OptionalContentTests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(OptionalContentTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(OptionalContentTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(OptionalContentTests pdf-fixtures)
aipdf_deploy_qt_runtime(OptionalContentTests)
aipdf_deploy_pdfium(OptionalContentTests)

add_test(NAME OptionalContentTests COMMAND OptionalContentTests)
