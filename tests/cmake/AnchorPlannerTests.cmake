# 锚点搬迁的几何规划与冲突检测（不写回）。

qt_add_executable(AnchorPlannerTests
    ${CMAKE_CURRENT_LIST_DIR}/../AnchorPlannerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/AnchorPlanner.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/AnchorPlanner.h
)

target_link_libraries(AnchorPlannerTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(AnchorPlannerTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AnchorPlannerTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(AnchorPlannerTests)

add_test(NAME AnchorPlannerTests COMMAND AnchorPlannerTests)
