cmake_minimum_required(VERSION 3.25)

project(AiPdfEditor VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AIPDF_BUILD_TESTS "Build tests" ON)

# 主程序与 Worker 必须落在同一目录：主程序按自身路径查找 PdfAiWorker。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin")
endforeach()

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

# 安装规则：主程序与 Worker 平铺在同一目录，运行库由 scripts/package.ps1 部署。
install(TARGETS AiPdfEditor PdfAiWorker
    RUNTIME DESTINATION .
)
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION .)
install(FILES ${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES.md DESTINATION licenses)

# 打包目标：生成自带 Qt 运行库、许可证材料与 SBOM 的 Windows x64 ZIP。
if(WIN32)
    add_custom_target(package
        COMMAND pwsh -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/package.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -OutputDir "${CMAKE_SOURCE_DIR}/dist"
                -Version "${PROJECT_VERSION}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Packaging AI PDF Editor ${PROJECT_VERSION}"
        VERBATIM
    )
endif()

