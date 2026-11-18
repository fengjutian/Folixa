# 「表单设计助手」解析层的单元测试。
#
# 只链 Core + Test：解析层不碰界面，也不碰 PDFium，
# 所以这组测试在无头环境下也能跑。

qt_add_executable(FormDesignParserTests
    ${CMAKE_CURRENT_LIST_DIR}/../FormDesignParserTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormDesignParser.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormDesignParser.h
)
target_link_libraries(FormDesignParserTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(FormDesignParserTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(FormDesignParserTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(FormDesignParserTests)
add_test(NAME FormDesignParserTests COMMAND FormDesignParserTests)
