# PDF 文件包（活页夹）：把多份 PDF/附件组织成一个包，catalog 挂 /Collection。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、aipdf_deploy_* 函数）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 为什么需要 qpdf：写包是对象级操作（Filespec、嵌入文件流、名字树、/Collection），
# 读包也是；没有 qpdf 的构建里整条链路不可用，相关用例会跳过。

qt_add_executable(PortfolioWriterTests
    ${CMAKE_CURRENT_LIST_DIR}/../PortfolioWriterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/convert/PortfolioWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/convert/PortfolioWriter.h
)

target_link_libraries(PortfolioWriterTests PRIVATE
    Qt6::Core Qt6::Test
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(PortfolioWriterTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(PortfolioWriterTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET PortfolioWriterTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:PortfolioWriterTests>"
        COMMENT "Copying qpdf runtime for PortfolioWriterTests"
    )
endif()

target_include_directories(PortfolioWriterTests PRIVATE
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

target_compile_definitions(PortfolioWriterTests PRIVATE
    AIPDF_QPDF_CLI="${AIPDF_QPDF_CLI}"
)
set_target_properties(PortfolioWriterTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(PortfolioWriterTests pdf-fixtures)
aipdf_deploy_qt_runtime(PortfolioWriterTests)

add_test(NAME PortfolioWriterTests COMMAND PortfolioWriterTests)
