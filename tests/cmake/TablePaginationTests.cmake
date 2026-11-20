# 表格按行跨页切分的纯几何函数。

qt_add_executable(TablePaginationTests
    ${CMAKE_CURRENT_LIST_DIR}/../TablePaginationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/TablePagination.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/TablePagination.h
)

target_link_libraries(TablePaginationTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(TablePaginationTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(TablePaginationTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(TablePaginationTests)

add_test(NAME TablePaginationTests COMMAND TablePaginationTests)
