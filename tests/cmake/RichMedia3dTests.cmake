# 3D 注解（/Subtype /3D）与多媒体注解（/Subtype /RichMedia）的读写测试。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：这样这个片段既能被
# tests/CMakeLists.txt include，也能被仓库外的自检工程 include，
# 不必写死绝对路径。
#
# 为什么既要 qpdf 又要 PDFium：
#   * 写与读 3D / 多媒体注解都是对象级操作，走 qpdf；
#   * 验「写完还能不能被打开、页数变没变」走 PDFium——
#     两头都要真的跑一遍才算数。
#
# 为什么还要 PortfolioWriter：这个测试要证明「作品集走的是仓库既有的
# pdf::writePortfolio / pdf::readPortfolio」，所以那一份也得编进来。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(RichMedia3dTests
    ${CMAKE_CURRENT_LIST_DIR}/../RichMedia3dTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/annot/RichMedia3d.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/annot/RichMedia3d.h
    ${AIPDF_REPO_ROOT}/src/pdf/convert/PortfolioWriter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/convert/PortfolioWriter.h
)

target_link_libraries(RichMedia3dTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(RichMedia3dTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(RichMedia3dTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET RichMedia3dTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:RichMedia3dTests>"
        COMMENT "Copying qpdf runtime for RichMedia3dTests"
    )
endif()

target_include_directories(RichMedia3dTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
set_target_properties(RichMedia3dTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(RichMedia3dTests)
aipdf_deploy_pdfium(RichMedia3dTests)

add_test(NAME RichMedia3dTests COMMAND RichMedia3dTests)
