# 真正改写页内容流，把命中的图片按锚点规划平移。

qt_add_executable(AnchorMoverTests
    ${CMAKE_CURRENT_LIST_DIR}/../AnchorMoverTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/AnchorMover.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/AnchorMover.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
)

target_link_libraries(AnchorMoverTests PRIVATE Qt6::Core Qt6::Test qpdf::libqpdf)
target_compile_definitions(AnchorMoverTests PRIVATE AIPDF_WITH_QPDF=1)
target_include_directories(AnchorMoverTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_custom_command(TARGET AnchorMoverTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:AnchorMoverTests>"
    COMMENT "Copying qpdf runtime for AnchorMoverTests"
)

set_target_properties(AnchorMoverTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(AnchorMoverTests)

add_test(NAME AnchorMoverTests COMMAND AnchorMoverTests)
