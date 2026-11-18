# 注释形状（callout 箭头框 + 区域高亮）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（aipdf_deploy_qt_runtime 函数）在父作用域里定义好了，
# include 进来直接可用。
#
# 为什么又要 qpdf：外观流是内容流，写坏了 PDFium 未必报错；
# 让 qpdf 把整个文件重新解析一遍，才算证明结构没问题。
# 没有 qpdf 的构建跳过这两条结构校验（测试里 QSKIP），其余用例照跑。
#
# 这一组不需要 PDF 夹具，也不碰 PDFium：它测的是纯几何与内容流。

qt_add_executable(AnnotationShapesTests
    ${CMAKE_CURRENT_LIST_DIR}/../AnnotationShapesTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationShapes.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationShapes.h
)

target_link_libraries(AnnotationShapesTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(AnnotationShapesTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(AnnotationShapesTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET AnnotationShapesTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:AnnotationShapesTests>"
        COMMENT "Copying qpdf runtime for AnnotationShapesTests"
    )
endif()

target_include_directories(AnnotationShapesTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AnnotationShapesTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(AnnotationShapesTests)

add_test(NAME AnnotationShapesTests COMMAND AnnotationShapesTests)
