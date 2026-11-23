qt_add_executable(FluentComponentsTests
    ${CMAKE_CURRENT_LIST_DIR}/../FluentComponentsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/FluentTokens.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/FluentTokens.h
    ${CMAKE_SOURCE_DIR}/src/app/ui/components/FluentControls.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/components/FluentControls.h
)

target_link_libraries(FluentComponentsTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_include_directories(FluentComponentsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(FluentComponentsTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(FluentComponentsTests)
add_test(NAME FluentComponentsTests COMMAND FluentComponentsTests)
