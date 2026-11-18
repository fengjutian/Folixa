# OCR 隐藏文字层回写的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（aipdf_deploy_qt_runtime / aipdf_deploy_pdfium 函数）在父作用域里
# 定义好了，include 进来直接可用。
#
# 为什么既要 qpdf 又要 PDFium：写隐藏文字层是对象级操作，走 qpdf；
# 验「写完还能不能打开」「页数有没有变」「能不能搜到字」走 PDFium——
# 两头都要真的跑一遍才算数。
#
# 这一组不需要 PDF 夹具：夹具里的页面内容不可控，
# 而这里要断言「写出来的内容流长什么样」，所以夹具是自己造的。

qt_add_executable(InvisibleTextLayerTests
    ${CMAKE_CURRENT_LIST_DIR}/../InvisibleTextLayerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/InvisibleTextLayer.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/InvisibleTextLayer.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
)

target_link_libraries(InvisibleTextLayerTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(InvisibleTextLayerTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(InvisibleTextLayerTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET InvisibleTextLayerTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:InvisibleTextLayerTests>"
        COMMENT "Copying qpdf runtime for InvisibleTextLayerTests"
    )
endif()

target_include_directories(InvisibleTextLayerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(InvisibleTextLayerTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(InvisibleTextLayerTests)
aipdf_deploy_pdfium(InvisibleTextLayerTests)

add_test(NAME InvisibleTextLayerTests COMMAND InvisibleTextLayerTests)
