# XObject 内部对象的锚点搬迁（递归 + 坐标换算）。

qt_add_executable(AnchorXObjectTests
    ${CMAKE_CURRENT_LIST_DIR}/../AnchorXObjectTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/AnchorMover.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/AnchorMover.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
)

target_link_libraries(AnchorXObjectTests PRIVATE Qt6::Core Qt6::Test qpdf::libqpdf)
target_compile_definitions(AnchorXObjectTests PRIVATE AIPDF_WITH_QPDF=1)
target_include_directories(AnchorXObjectTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_custom_command(TARGET AnchorXObjectTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:AnchorXObjectTests>"
    COMMENT "Copying qpdf runtime for AnchorXObjectTests"
)

set_target_properties(AnchorXObjectTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(AnchorXObjectTests)

add_test(NAME AnchorXObjectTests COMMAND AnchorXObjectTests)
