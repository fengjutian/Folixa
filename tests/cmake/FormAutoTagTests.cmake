# 表单域自动打标签（结构标签树 + 替代文本）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（qpdf_FOUND、AIPDF_WITH_QPDF）与函数（aipdf_deploy_qt_runtime）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 为什么又要 qpdf：读 AcroForm 域与写标签树都是对象级操作，走 qpdf；
# 读回标签树用 pdf::readStructTree（页面内容流的分词靠 pdf::ContentStream）。
# 这一组不碰 PDFium：PDFium 在本仓库是进程级互斥的，夹具又不需要渲染。

qt_add_executable(FormAutoTagTests
    ${CMAKE_CURRENT_LIST_DIR}/../FormAutoTagTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/FormAutoTag.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/FormAutoTag.h
    ${CMAKE_SOURCE_DIR}/src/pdf/FormFieldWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/FormFieldWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/BarcodeEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/BarcodeEncoder.h
    # FormFieldWriter 里条码域要用到 encodeCode128：不带这两个文件链接不过。
    ${CMAKE_SOURCE_DIR}/src/pdf/BarcodeEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/BarcodeEncoder.h
    ${CMAKE_SOURCE_DIR}/src/pdf/StructTreeReader.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/StructTreeReader.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ContentStream.h
)

target_link_libraries(FormAutoTagTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(FormAutoTagTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(FormAutoTagTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET FormAutoTagTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:FormAutoTagTests>"
        COMMENT "Copying qpdf runtime for FormAutoTagTests"
    )
endif()

target_include_directories(FormAutoTagTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(FormAutoTagTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 部署 Qt 运行库的函数定义在仓库根作用域里；
# 独立的自检工程不一定有它们，所以先问一句再用。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(FormAutoTagTests)
endif()

add_test(NAME FormAutoTagTests COMMAND FormAutoTagTests)
