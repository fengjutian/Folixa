# 界面接线（第二批）：Ribbon 无障碍、注释三件套、转换报告的机械可测部分。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（aipdf_deploy_qt_runtime 函数）在父作用域里定义好了，
# include 进来直接可用。
#
# 编进来的都是「不碰 PDFium、不碰 qpdf」的那一半：
#   - Ribbon 的无障碍名与助记符（纯 Widgets）；
#   - 计算器与符号面板（只用 pdf/AnnotationToolkit）；
#   - 印前检查 / 活页夹 / 疑似错误的分组与展示（只用结构体）。
# 需要 PDFium 与 qpdf 的导出动作在 src/app/ConversionExports.cpp 里，
# 由主程序目标编译，不进这个测试目标。

qt_add_executable(UiWiringTests
    ${CMAKE_CURRENT_LIST_DIR}/../UiWiringTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/RibbonBar.cpp
    ${CMAKE_SOURCE_DIR}/src/app/RibbonBar.h
    ${CMAKE_SOURCE_DIR}/src/app/AnnotationToolboxDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/AnnotationToolboxDialog.h
    ${CMAKE_SOURCE_DIR}/src/app/ConversionReports.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ConversionReports.h
    ${CMAKE_SOURCE_DIR}/src/app/ConversionToolsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ConversionToolsDialog.h
    ${CMAKE_SOURCE_DIR}/src/app/PrintToolsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PrintToolsDialog.h
    ${CMAKE_SOURCE_DIR}/src/pdf/AnnotationToolkit.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/AnnotationToolkit.h
    ${CMAKE_SOURCE_DIR}/src/pdf/OcrSuspicion.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/OcrSuspicion.h
    ${CMAKE_SOURCE_DIR}/src/pdf/PortfolioWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PortfolioWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/PreflightChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PreflightChecker.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/doc/BlankDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/BlankDocument.h
    ${CMAKE_SOURCE_DIR}/src/doc/PrintBatch.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/PrintBatch.h
    ${CMAKE_SOURCE_DIR}/src/doc/PrintTemplate.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/PrintTemplate.h
    ${CMAKE_SOURCE_DIR}/src/i18n/Translate.h
)

target_link_libraries(UiWiringTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
)

# 印前检查的领域层实现需要 qpdf（只有解析器能给出字体/图片结论）。
# 没有 qpdf 的构建里，印前检查退化成空报告，但界面层仍然能跑。
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(UiWiringTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(UiWiringTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET UiWiringTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:UiWiringTests>"
        COMMENT "Copying qpdf runtime for UiWiringTests"
    )
endif()

target_include_directories(UiWiringTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# 翻译目录的路径：测试要核对「新增的用户可见文案都登记进去了」。
target_compile_definitions(UiWiringTests PRIVATE
    AIPDF_I18N_TS_FILE="${CMAKE_SOURCE_DIR}/src/i18n/ai-pdf-editor_en.ts"
)

set_target_properties(UiWiringTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(UiWiringTests)

add_test(NAME UiWiringTests COMMAND UiWiringTests)
