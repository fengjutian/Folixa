# 任意文件 → PDF（领域层模块 UniversalConverter）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量与函数（aipdf_deploy_qt_runtime / aipdf_deploy_pdfium、
# AIPDF_PDFIUM_LIBRARY、AIPDF_PDFIUM_INCLUDE_DIR）都在父作用域里定义好了，
# include 进来直接可用。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：片段文件本身在 tests/cmake/ 下，
# 这样无论谁 include、从哪个目录 configure，路径都对，不写死绝对路径。
#
# 为什么又要 PDFium：这一组要证明「生成的 PDF 能被别的阅读器打开」——
# 页数对不对、文字抽不抽得回来，只能真的用 PDFium 读一遍才知道。
# 用 qpdf 只能量结构，证明不了阅读器看得见。
#
# 为什么又要 qpdf：内容流自检（ContentStream）写坏了 PDFium 未必报错；
# 让 qpdf 把整个文件重新解析一遍，才算证明结构没问题。
# 没有 qpdf 的构建跳过这条结构校验，其余用例照跑。

set(AIPDF_UNIVERSAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

qt_add_executable(UniversalConverterTests
    ${CMAKE_CURRENT_LIST_DIR}/../UniversalConverterTests.cpp
    ${AIPDF_UNIVERSAL_ROOT}/src/doc/UniversalConverter.cpp
    ${AIPDF_UNIVERSAL_ROOT}/src/doc/UniversalConverter.h
    ${AIPDF_UNIVERSAL_ROOT}/src/pdf/content/TrueTypeSubset.cpp
    ${AIPDF_UNIVERSAL_ROOT}/src/pdf/content/TrueTypeSubset.h
    ${AIPDF_UNIVERSAL_ROOT}/src/doc/BlankDocument.cpp
    ${AIPDF_UNIVERSAL_ROOT}/src/doc/BlankDocument.h
    ${AIPDF_UNIVERSAL_ROOT}/src/pdf/content/ContentStream.cpp
    ${AIPDF_UNIVERSAL_ROOT}/src/pdf/content/ContentStream.h
    ${AIPDF_UNIVERSAL_ROOT}/src/pdf/kernel/PdfiumAdapter.cpp
    ${AIPDF_UNIVERSAL_ROOT}/src/pdf/kernel/PdfiumAdapter.h
    ${AIPDF_UNIVERSAL_ROOT}/src/pdf/annot/Annotation.cpp
    ${AIPDF_UNIVERSAL_ROOT}/src/pdf/annot/Annotation.h
    ${AIPDF_UNIVERSAL_ROOT}/src/pdf/kernel/TextPage.cpp
    ${AIPDF_UNIVERSAL_ROOT}/src/pdf/kernel/TextPage.h
)

target_link_libraries(UniversalConverterTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(UniversalConverterTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(UniversalConverterTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET UniversalConverterTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:UniversalConverterTests>"
        COMMENT "Copying qpdf runtime for UniversalConverterTests"
    )
endif()

target_include_directories(UniversalConverterTests PRIVATE
    ${AIPDF_UNIVERSAL_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(UniversalConverterTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(UniversalConverterTests)
aipdf_deploy_pdfium(UniversalConverterTests)

add_test(NAME UniversalConverterTests COMMAND UniversalConverterTests)
