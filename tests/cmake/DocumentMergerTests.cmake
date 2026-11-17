# 多文件合并 + 跨文件书签搬运（领域层）。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。
#
# 为什么既要 qpdf 又要 PDFium：合并、书签搬运与链接重定向是对象级操作，走 qpdf；
# 验「写完还能不能打开」「页面内容有没有变」「链接跳得对不对」走 PDFium——
# 两头都要真的跑一遍才算数。

qt_add_executable(DocumentMergerTests
    ${CMAKE_CURRENT_LIST_DIR}/../DocumentMergerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/DocumentMerger.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/DocumentMerger.h
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TableStructure.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TableStructure.h
)

target_link_libraries(DocumentMergerTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(DocumentMergerTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(DocumentMergerTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET DocumentMergerTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:DocumentMergerTests>"
        COMMENT "Copying qpdf runtime for DocumentMergerTests"
    )
endif()

target_include_directories(DocumentMergerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

# qpdf 命令行：用来跑一次 qpdf --check（结构校验）。
# 找不到就传空串，用例里会跳过那一条。
set(AIPDF_QPDF_CLI "${CMAKE_SOURCE_DIR}/tools/qpdf/bin/qpdf.exe")
if(NOT EXISTS "${AIPDF_QPDF_CLI}")
    set(AIPDF_QPDF_CLI "")
endif()
target_compile_definitions(DocumentMergerTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
    AIPDF_QPDF_CLI="${AIPDF_QPDF_CLI}"
)
set_target_properties(DocumentMergerTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(DocumentMergerTests pdf-fixtures)
aipdf_deploy_qt_runtime(DocumentMergerTests)
aipdf_deploy_pdfium(DocumentMergerTests)

add_test(NAME DocumentMergerTests COMMAND DocumentMergerTests)
