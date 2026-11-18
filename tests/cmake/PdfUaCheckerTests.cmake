# PDF/UA 合规检查（领域层）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根目录怎么找：这个片段既会被 tests/CMakeLists.txt include，
# 也可能被独立的自检工程 include（自检工程的 CMAKE_SOURCE_DIR 不是仓库根），
# 所以源文件路径一律用 CMAKE_CURRENT_LIST_DIR 反推，绝不写死绝对路径。
get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
#
# 为什么需要 qpdf：PDF/UA 检查是对象级检查（标签树、语言、字体描述符、
# 内容流里的 MCID 都是对象里的东西），只有解析器能给出答案。
# 没有 qpdf 的构建里相关用例会跳过（QSKIP），失败路径的用例照跑。
#
# 这一组自己用标准库拼 PDF 夹具，不依赖构建期生成的 PDF 夹具目录，
# 所以不必依赖 pdf-fixtures 目标。

qt_add_executable(PdfUaCheckerTests
    ${CMAKE_CURRENT_LIST_DIR}/../PdfUaCheckerTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/PdfUaChecker.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/PdfUaChecker.h
)

target_link_libraries(PdfUaCheckerTests PRIVATE
    Qt6::Core Qt6::Test
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(PdfUaCheckerTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(PdfUaCheckerTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET PdfUaCheckerTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:PdfUaCheckerTests>"
        COMMENT "Copying qpdf runtime for PdfUaCheckerTests"
    )
endif()

target_include_directories(PdfUaCheckerTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)
set_target_properties(PdfUaCheckerTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 部署 Qt 运行时：函数定义在仓库根的 CMakeLists.txt 里，
# 独立自检工程不一定有它，所以先问一句。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(PdfUaCheckerTests)
endif()

add_test(NAME PdfUaCheckerTests COMMAND PdfUaCheckerTests)
