# OneNote 导出的测试片段。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：片段文件本身在 tests/cmake/ 下，
# 这样无论谁 include、从哪个目录 configure，路径都对，不写死绝对路径。
#
# 为什么这个目标要链 ole32 / oleaut32：
#   * ole32    —— CLSIDFromProgID / CoCreateInstance / CoInitializeEx；
#   * oleaut32 —— VARIANT、BSTR（晚绑定传参与出参都要用）。
# 这两个库是 MinGW 工具链自带的静态库，链上不引入任何新依赖。
#
# 为什么不链 Office 的类型库：OneNote 走的是 COM 晚绑定（IDispatch），
# 全程不 #import "OneNote.tlb"，所以**构建机上没装 Office 也能编译**。
#
# 为什么这个测试不需要 PDFium：被测的是「导出计划 + 图片编码 + OneNote 页面 XML
# + 三条失败路径」，页面图像由测试自己造，不经过 PDF 渲染。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(OneNoteExporterTests
    ${CMAKE_CURRENT_LIST_DIR}/../OneNoteExporterTests.cpp
    # ---- 被测的应用层 ----
    ${AIPDF_REPO_ROOT}/src/app/OneNoteExporter.cpp
    ${AIPDF_REPO_ROOT}/src/app/OneNoteExporter.h
    ${AIPDF_REPO_ROOT}/src/i18n/Translate.h
)

target_link_libraries(OneNoteExporterTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ole32 oleaut32
)

target_include_directories(OneNoteExporterTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)

set_target_properties(OneNoteExporterTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 部署 Qt 运行时的函数定义在仓库根作用域里；
# 独立的自检工程不一定有它们，所以先问一句再用。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(OneNoteExporterTests)
endif()

add_test(NAME OneNoteExporterTests COMMAND OneNoteExporterTests)
