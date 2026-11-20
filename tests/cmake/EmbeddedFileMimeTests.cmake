# 文档级嵌入文件 MIME 类型（/Type /EmbeddedFile + /Subtype）的测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 编进来的：
#   - src/pdf/document/EmbeddedFileMime（被测本体，纯领域层，不碰 Widgets）
#
# 依赖 qpdf：补 MIME 走的是 qpdf 的对象级读写（读名字树、改流字典、写回并
# 保留原加密），没有 qpdf 的构建里整条链路不可用，相关用例会跳过。

if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

qt_add_executable(EmbeddedFileMimeTests
    ${CMAKE_CURRENT_LIST_DIR}/../EmbeddedFileMimeTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/document/EmbeddedFileMime.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/document/EmbeddedFileMime.h
    ${AIPDF_REPO_ROOT}/src/pdf/kernel/MimeTypes.h
)

target_link_libraries(EmbeddedFileMimeTests PRIVATE
    Qt6::Core Qt6::Test
)

target_include_directories(EmbeddedFileMimeTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(EmbeddedFileMimeTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(EmbeddedFileMimeTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET EmbeddedFileMimeTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:EmbeddedFileMimeTests>"
        COMMENT "Copying qpdf runtime for EmbeddedFileMimeTests"
    )
endif()

set_target_properties(EmbeddedFileMimeTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(EmbeddedFileMimeTests)

add_test(NAME EmbeddedFileMimeTests COMMAND EmbeddedFileMimeTests)
