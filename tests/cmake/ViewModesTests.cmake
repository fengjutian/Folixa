# 视图域模型（标尺 / 自动滚动 / 页面过渡 / 纯文本查看器 / 逆序镜像）的单元测试目标。
#
# 只依赖 Qt6::Core Qt6::Gui Qt6::Test + PDFium：
# 这一层算的是几何与排版，不碰界面；
# 纯文本那条用例要用 PDFium 打开夹具 PDF 取真实文本层。

qt_add_executable(ViewModesTests
    ${CMAKE_CURRENT_LIST_DIR}/../ViewModesTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/ViewModes.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/ViewModes.h
    ${CMAKE_SOURCE_DIR}/src/app/editor/PageTextLines.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/PageTextLines.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
)

target_link_libraries(ViewModesTests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

target_include_directories(ViewModesTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

# 夹具 PDF 是生成出来的（pdf-fixtures 目标），目录在编译期写死。
target_compile_definitions(ViewModesTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)

# 夹具 PDF 是生成出来的，要先跑 pdf-fixtures 目标。
add_dependencies(ViewModesTests pdf-fixtures)

set_target_properties(ViewModesTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(ViewModesTests)
aipdf_deploy_pdfium(ViewModesTests)

add_test(NAME ViewModesTests COMMAND ViewModesTests)
