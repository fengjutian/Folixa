# 竖排（真正的 WMode 1）与超大字符集（A12 欠账）。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很重要的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：这样这个片段既能被
# tests/CMakeLists.txt include，也能被仓库外的自检工程 include，
# 不必写死绝对路径。
#
# 三块：
#   * 竖排度量：从字体的 vhea / vmtx 读出来；没有就如实说没有；
#   * 竖排切列与内容流：列右→左、列内上→下，内容流顺序 == 阅读顺序；
#   * 字体字典片段：/WMode 1 + /DW2（+ /W2）。
#
# 另外把 TextReflow 竖排分支的两个老毛病钉住（空白丢源串偏移、竖排拿横排宽度步进）。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(VerticalWritingTests
    ${CMAKE_CURRENT_LIST_DIR}/../VerticalWritingTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/VerticalWriting.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/VerticalWriting.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/TrueTypeSubset.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/TrueTypeSubset.h
    ${AIPDF_REPO_ROOT}/src/pdf/content/TextReflow.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/content/TextReflow.h
)

target_link_libraries(VerticalWritingTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(VerticalWritingTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(VerticalWritingTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET VerticalWritingTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:VerticalWritingTests>"
        COMMENT "Copying qpdf runtime for VerticalWritingTests"
    )
endif()

target_include_directories(VerticalWritingTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(VerticalWritingTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(VerticalWritingTests)
aipdf_deploy_pdfium(VerticalWritingTests)

add_test(NAME VerticalWritingTests COMMAND VerticalWritingTests)
