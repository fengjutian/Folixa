# 印前检查（领域层）：字体是否内嵌、图片分辨率、页面尺寸、透明度组、加密、JS、外部引用。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的。
#
# 为什么需要 qpdf：印前检查是对象级检查（字体字典里有没有 /FontFile、
# 图片的显示尺寸在内容流的哪条 cm 上），只有解析器能给出答案。
# 没有 qpdf 的构建里，印前检查整体不可用，相关用例会跳过。

qt_add_executable(PreflightCheckerTests
    ${CMAKE_CURRENT_LIST_DIR}/../PreflightCheckerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/PreflightChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/PreflightChecker.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
)

target_link_libraries(PreflightCheckerTests PRIVATE
    Qt6::Core Qt6::Test
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(PreflightCheckerTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(PreflightCheckerTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET PreflightCheckerTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:PreflightCheckerTests>"
        COMMENT "Copying qpdf runtime for PreflightCheckerTests"
    )
endif()

target_include_directories(PreflightCheckerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# qpdf 命令行：用来跑一次 qpdf --check（结构校验）。
# 找不到就传空串，用例里会跳过那一条。
if(NOT DEFINED AIPDF_QPDF_CLI)
    set(AIPDF_QPDF_CLI "${CMAKE_SOURCE_DIR}/tools/qpdf/bin/qpdf.exe")
    if(NOT EXISTS "${AIPDF_QPDF_CLI}")
        set(AIPDF_QPDF_CLI "")
    endif()
endif()

target_compile_definitions(PreflightCheckerTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
    AIPDF_QPDF_CLI="${AIPDF_QPDF_CLI}"
)
set_target_properties(PreflightCheckerTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(PreflightCheckerTests pdf-fixtures)
aipdf_deploy_qt_runtime(PreflightCheckerTests)

add_test(NAME PreflightCheckerTests COMMAND PreflightCheckerTests)
