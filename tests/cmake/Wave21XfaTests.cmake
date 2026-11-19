# 2026-11-19 wave21：XFA——字段值改写 + 脚本子集执行。
#
# 为什么要单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 源文件路径一律用 CMAKE_CURRENT_LIST_DIR 反推，绝不写死绝对路径：
# 这个片段既会被 tests/CMakeLists.txt include，也可能被独立的自检工程 include。
get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
#
# 为什么需要 qpdf：/XFA 是对象级的间接引用，只有真正的 PDF 解析器
# 才能按对象定位到它、把改过的流写回去。
# 没有 qpdf 的构建里相关用例会跳过（QSKIP），失败路径的用例照跑。
qt_add_executable(Wave21XfaTests
    ${CMAKE_CURRENT_LIST_DIR}/../Wave21XfaTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/forms/XfaSupport.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/forms/XfaSupport.h
    ${AIPDF_REPO_ROOT}/src/pdf/forms/XfaScriptRunner.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/forms/XfaScriptRunner.h
    ${AIPDF_REPO_ROOT}/src/pdf/forms/FormJsEvaluator.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/forms/FormJsEvaluator.h
)

target_link_libraries(Wave21XfaTests PRIVATE
    Qt6::Core Qt6::Test
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(Wave21XfaTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(Wave21XfaTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET Wave21XfaTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:Wave21XfaTests>"
        COMMENT "Copying qpdf runtime for Wave21XfaTests"
    )
endif()

target_include_directories(Wave21XfaTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)
set_target_properties(Wave21XfaTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 部署 Qt 运行时：函数定义在仓库根的 CMakeLists.txt 里，
# 独立自检工程不一定有它，所以先问一句。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(Wave21XfaTests)
endif()

add_test(NAME Wave21XfaTests COMMAND Wave21XfaTests)
