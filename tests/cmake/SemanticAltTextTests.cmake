# 改写「已存在」的结构元素替代文本（/Alt）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很重要的清单里塞细节。
#
# 仓库根目录怎么找：这个片段既会被 tests/CMakeLists.txt include，
# 也可能被独立的自检工程 include（自检工程的 CMAKE_SOURCE_DIR 不是仓库根），
# 所以源文件路径一律用 CMAKE_CURRENT_LIST_DIR 反推，绝不写死绝对路径。
get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
#
# 为什么需要 qpdf：这一组改的是 PDF 里的对象（结构元素字典上的 /Alt），
# 读结构树、找 /OBJR、写回文件全都要对象级信息，只有解析器能给出答案。
# 没有 qpdf 的构建里相关用例会跳过（QSKIP）。
qt_add_executable(SemanticAltTextTests
    ${CMAKE_CURRENT_LIST_DIR}/../SemanticAltTextTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/SemanticAltText.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/SemanticAltText.h
)

target_link_libraries(SemanticAltTextTests PRIVATE
    Qt6::Core Qt6::Test
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(SemanticAltTextTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(SemanticAltTextTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET SemanticAltTextTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:SemanticAltTextTests>"
        COMMENT "Copying qpdf runtime for SemanticAltTextTests"
    )
endif()

target_include_directories(SemanticAltTextTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)
set_target_properties(SemanticAltTextTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 部署 Qt 运行时：函数定义在仓库根的 CMakeLists.txt 里，
# 独立自检工程不一定有它，所以先问一句。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(SemanticAltTextTests)
endif()

add_test(NAME SemanticAltTextTests COMMAND SemanticAltTextTests)
