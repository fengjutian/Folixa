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
# 需要 PDFium 与 qpdf 的导出动作在 src/app/util/ConversionExports.cpp 里，
# 由主程序目标编译，不进这个测试目标。

qt_add_executable(UiWiringTests
    ${CMAKE_CURRENT_LIST_DIR}/../UiWiringTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/RibbonBar.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/RibbonBar.h
    ${CMAKE_SOURCE_DIR}/src/app/ui/Theme.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Theme.h
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/AnnotationToolboxDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/AnnotationToolboxDialog.h
    ${CMAKE_SOURCE_DIR}/src/app/util/ConversionReports.cpp
    ${CMAKE_SOURCE_DIR}/src/app/util/ConversionReports.h
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/ConversionToolsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/ConversionToolsDialog.h
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/PrintToolsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/PrintToolsDialog.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationToolkit.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationToolkit.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OcrSuspicion.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OcrSuspicion.h
    ${CMAKE_SOURCE_DIR}/src/pdf/convert/PortfolioWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/convert/PortfolioWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/PreflightChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/PreflightChecker.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/BlankDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/BlankDocument.h
    ${CMAKE_SOURCE_DIR}/src/doc/print/PrintBatch.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/print/PrintBatch.h
    ${CMAKE_SOURCE_DIR}/src/doc/print/PrintTemplate.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/print/PrintTemplate.h
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
    AIPDF_MAINWINDOW_SOURCE="${CMAKE_SOURCE_DIR}/src/app/shell/MainWindow.cpp"
)

set_target_properties(UiWiringTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(UiWiringTests)

add_test(NAME UiWiringTests COMMAND UiWiringTests)
