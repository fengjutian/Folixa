# /Screen 注解（页面内播放）的读写测试。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：这样这个片段既能被
# tests/CMakeLists.txt include，也能被仓库外的自检工程 include，
# 不必写死绝对路径。
#
# 为什么既要 qpdf 又要 PDFium：
#   * 写与读 /Screen 注解、读回嵌入媒体字节，都是对象级操作，走 qpdf；
#   * 验「写完还能不能被打开、页数变没变、原来的文字还在不在」走 PDFium——
#     两头都要真的跑一遍才算数。

if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

# 夹具目录：跟着 tests/CMakeLists.txt 的 PDF_FIXTURE_DIR 走；
# 单独 include 这个片段时退回当前二进制目录下的 fixtures。
if (NOT DEFINED AIPDF_SCREEN_FIXTURES_DIR)
    if (DEFINED PDF_FIXTURE_DIR)
        set(AIPDF_SCREEN_FIXTURES_DIR "${PDF_FIXTURE_DIR}")
    else()
        set(AIPDF_SCREEN_FIXTURES_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    endif()
endif()

qt_add_executable(ScreenAnnotationTests
    ${CMAKE_CURRENT_LIST_DIR}/../ScreenAnnotationTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/annot/ScreenAnnotation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/annot/ScreenAnnotation.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.h
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.h
)

target_link_libraries(ScreenAnnotationTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
    qpdf::libqpdf
)

target_include_directories(ScreenAnnotationTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

target_compile_definitions(ScreenAnnotationTests PRIVATE
    AIPDF_WITH_QPDF=1
    AIPDF_PDF_FIXTURES_DIR="${AIPDF_SCREEN_FIXTURES_DIR}"
)

set_target_properties(ScreenAnnotationTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 夹具由 tests/CMakeLists.txt 里的 pdf-fixtures 目标生成；
# 单独 include 时没有这个目标，就不挂依赖。
if (TARGET pdf-fixtures)
    add_dependencies(ScreenAnnotationTests pdf-fixtures)
endif()

aipdf_deploy_qt_runtime(ScreenAnnotationTests)
aipdf_deploy_pdfium(ScreenAnnotationTests)

# qpdf 是动态库：运行时必须与可执行文件同目录。
add_custom_command(TARGET ScreenAnnotationTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:ScreenAnnotationTests>"
    COMMENT "Copying qpdf runtime for ScreenAnnotationTests"
)

add_test(NAME ScreenAnnotationTests COMMAND ScreenAnnotationTests)
