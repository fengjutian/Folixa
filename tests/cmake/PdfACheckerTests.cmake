# PDF/A-2b 预检（领域层）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根目录怎么找：这个片段既会被 tests/CMakeLists.txt include，
# 也可能被独立的自检工程 include（自检工程的 CMAKE_SOURCE_DIR 不是仓库根），
# 所以源文件路径一律用 CMAKE_CURRENT_LIST_DIR 反推，绝不写死绝对路径。
get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
#
# 为什么需要 qpdf：PDF/A 预检是对象级检查（XMP 包、输出意图与 ICC、字体描述符、
# 流的过滤名、注解字典都是对象里的东西），只有解析器能给出答案。
# 没有 qpdf 的构建里相关用例会跳过（QSKIP），失败路径的用例照跑。
#
# 这一组自己用标准库拼 PDF 夹具，不依赖构建期生成的 PDF 夹具目录，
# 所以不必依赖 pdf-fixtures 目标。
#
# 为什么还要编 ContentStream.cpp：预检要扫页面内容流（设备色彩空间操作符、
# 内联图像的 LZW 缩写），复用的是仓库里现成的内容流分词器，不另写一套。

qt_add_executable(PdfACheckerTests
    ${CMAKE_CURRENT_LIST_DIR}/../PdfACheckerTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/PdfAChecker.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/PdfAChecker.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.h
)

target_link_libraries(PdfACheckerTests PRIVATE
    Qt6::Core Qt6::Test
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(PdfACheckerTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(PdfACheckerTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET PdfACheckerTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:PdfACheckerTests>"
        COMMENT "Copying qpdf runtime for PdfACheckerTests"
    )
endif()

target_include_directories(PdfACheckerTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)
set_target_properties(PdfACheckerTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 部署 Qt 运行时：函数定义在仓库根的 CMakeLists.txt 里，
# 独立自检工程不一定有它，所以先问一句。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(PdfACheckerTests)
endif()

add_test(NAME PdfACheckerTests COMMAND PdfACheckerTests)
