# 字体溯源（pdf::FontResource）：读页面 /Resources /Font，解析 ToUnicode、
# /Differences、/Widths 与 /W，判断「这个字在这个字体里写不写得出」。
#
# 只依赖 qpdf：这一层不碰渲染，不引 PDFium，测试跑得快。

qt_add_executable(FontResourceTests
    ${CMAKE_CURRENT_LIST_DIR}/../FontResourceTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/FontResource.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/FontResource.h
)

target_link_libraries(FontResourceTests PRIVATE
    Qt6::Core Qt6::Test
    qpdf::libqpdf
)
target_compile_definitions(FontResourceTests PRIVATE AIPDF_WITH_QPDF=1)

add_custom_command(TARGET FontResourceTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:FontResourceTests>"
    COMMENT "Copying qpdf runtime for FontResourceTests"
)

target_include_directories(FontResourceTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(FontResourceTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(FontResourceTests)

add_test(NAME FontResourceTests COMMAND FontResourceTests)
