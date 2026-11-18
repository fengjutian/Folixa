# 批注批量筛选（按类型、关键字、页码筛）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 这一组是纯领域层：不碰 PDFium、不碰 qpdf、不需要 PDF 夹具。
get_filename_component(AIPDF_ANNOTATION_FILTER_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(AnnotationFilterTests
    ${CMAKE_CURRENT_LIST_DIR}/../AnnotationFilterTests.cpp
    ${AIPDF_ANNOTATION_FILTER_ROOT}/src/pdf/AnnotationFilter.cpp
    ${AIPDF_ANNOTATION_FILTER_ROOT}/src/pdf/AnnotationFilter.h
)

target_link_libraries(AnnotationFilterTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

target_include_directories(AnnotationFilterTests PRIVATE
    ${AIPDF_ANNOTATION_FILTER_ROOT}/src
)
set_target_properties(AnnotationFilterTests PROPERTIES WIN32_EXECUTABLE FALSE)

if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(AnnotationFilterTests)
endif()

add_test(NAME AnnotationFilterTests COMMAND AnnotationFilterTests)
