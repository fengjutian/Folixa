cmake_minimum_required(VERSION 3.25)

project(AiPdfEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AIPDF_BUILD_TESTS "Build tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network)

if(AIPDF_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
endif()

qt_standard_project_setup()

add_subdirectory(src)
add_subdirectory(worker)

if(AIPDF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

