# 注释工具箱（会计计算器 / 符号面板 / 连续批注模式）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（aipdf_deploy_qt_runtime 函数）在父作用域里定义好了，
# include 进来直接可用。
#
# 为什么路径不从 CMAKE_SOURCE_DIR 推：本片段在 <仓库>/tests/cmake/ 下，
# 从片段自己的位置往上两级就是仓库根。绑在片段自己的位置上，
# 同一份片段在仓库内的主工程与仓库外的自检临时工程里都能用。
get_filename_component(AIPDF_ANNOTATION_TOOLKIT_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# 这一组是纯领域层：不碰 PDFium、不碰 qpdf、不需要 PDF 夹具。
qt_add_executable(AnnotationToolkitTests
    ${CMAKE_CURRENT_LIST_DIR}/../AnnotationToolkitTests.cpp
    ${AIPDF_ANNOTATION_TOOLKIT_ROOT}/src/pdf/AnnotationToolkit.cpp
    ${AIPDF_ANNOTATION_TOOLKIT_ROOT}/src/pdf/AnnotationToolkit.h
)

target_link_libraries(AnnotationToolkitTests PRIVATE Qt6::Core Qt6::Test)

target_include_directories(AnnotationToolkitTests PRIVATE
    ${AIPDF_ANNOTATION_TOOLKIT_ROOT}/src
)
set_target_properties(AnnotationToolkitTests PROPERTIES WIN32_EXECUTABLE FALSE)

if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(AnnotationToolkitTests)
endif()

add_test(NAME AnnotationToolkitTests COMMAND AnnotationToolkitTests)
