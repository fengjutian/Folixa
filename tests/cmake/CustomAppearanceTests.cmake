# 批注自定义外观（callout 箭头框 + 区域高亮）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（aipdf_deploy_qt_runtime 函数）在父作用域里定义好了，
# include 进来直接可用。
#
# 这一组不需要 PDF 夹具，也不碰 PDFium：它测的是纯几何与内容流。
# 只链接 Qt6::Core Qt6::Gui Qt6::Test —— 外观流里有颜色（QColor），
# 所以要 Gui。

qt_add_executable(CustomAppearanceTests
    ${CMAKE_CURRENT_LIST_DIR}/../CustomAppearanceTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationShapes.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationShapes.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AppearanceStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AppearanceStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
)

target_link_libraries(CustomAppearanceTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

target_include_directories(CustomAppearanceTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(CustomAppearanceTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(CustomAppearanceTests)

add_test(NAME CustomAppearanceTests COMMAND CustomAppearanceTests)
