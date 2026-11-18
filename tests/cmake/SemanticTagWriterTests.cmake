# 语义标签树（Document → Part → Sect → H1/P/L/Table/Figure/Form）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很重要的清单里塞细节。
#
# 仓库根目录怎么找：这个片段既会被 tests/CMakeLists.txt include，
# 也可能被独立的自检工程 include（自检工程的 CMAKE_SOURCE_DIR 不是仓库根），
# 所以源文件路径一律用 CMAKE_CURRENT_LIST_DIR 反推，绝不写死绝对路径。
get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
#
# 为什么需要 qpdf：这一组要读结构树、读内容流里的 MCID、读 ParentTree——
# 全是对象级信息，只有解析器能给出答案。没有 qpdf 的构建里相关用例会跳过（QSKIP）。
qt_add_executable(SemanticTagWriterTests
    ${CMAKE_CURRENT_LIST_DIR}/../SemanticTagWriterTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeModel.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/StructTreeModel.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/RoleInference.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/RoleInference.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/AltTextGenerator.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/AltTextGenerator.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/SemanticTagWriter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/SemanticTagWriter.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/TagTreeWriter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/TagTreeWriter.h
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/PdfUaChecker.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/compliance/PdfUaChecker.h
    ${AIPDF_REPO_ROOT}/src/pdf/forms/FormAutoTag.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/forms/FormAutoTag.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/ContentStream.h
)

target_link_libraries(SemanticTagWriterTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(SemanticTagWriterTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(SemanticTagWriterTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET SemanticTagWriterTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:SemanticTagWriterTests>"
        COMMENT "Copying qpdf runtime for SemanticTagWriterTests"
    )
endif()

target_include_directories(SemanticTagWriterTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)
set_target_properties(SemanticTagWriterTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 部署 Qt 运行时：函数定义在仓库根的 CMakeLists.txt 里，
# 独立自检工程不一定有它，所以先问一句。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(SemanticTagWriterTests)
endif()

add_test(NAME SemanticTagWriterTests COMMAND SemanticTagWriterTests)
