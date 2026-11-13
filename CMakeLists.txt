cmake_minimum_required(VERSION 3.25)

project(AiPdfEditor VERSION 0.8.0 LANGUAGES CXX)

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

# Windows 版本资源：让可执行文件在资源管理器与安装包清单里带上版本号。
if(WIN32)
    enable_language(RC)
endif()

qt_standard_project_setup()

# 把 Qt 运行库与平台插件部署到构建输出目录，使构建树自包含：
# 测试与验收脚本不再依赖开发机上的 Qt SDK 路径。
# 部署固定用 windeployqt 做动态链接，符合 LGPL 动态链接要求。
if(WIN32)
    if(AIPDF_QT_ROOT AND EXISTS "${AIPDF_QT_ROOT}/bin/windeployqt.exe")
        set(AIPDF_WINDEPLOYQT "${AIPDF_QT_ROOT}/bin/windeployqt.exe")
    else()
        find_program(AIPDF_WINDEPLOYQT windeployqt)
    endif()

    # MinGW 套件只提供 release 运行库；MSVC 套件同时提供 debug 运行库。
    if(AIPDF_QT_ROOT AND EXISTS "${AIPDF_QT_ROOT}/bin/Qt6Cored.dll")
        set(AIPDF_DEPLOY_MODE_DEBUG --debug)
    else()
        set(AIPDF_DEPLOY_MODE_DEBUG --release)
    endif()

    function(aipdf_deploy_qt_runtime target)
        if(AIPDF_WINDEPLOYQT)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${AIPDF_WINDEPLOYQT}"
                        "$<$<CONFIG:Debug>:${AIPDF_DEPLOY_MODE_DEBUG}>$<$<NOT:$<CONFIG:Debug>>:--release>"
                        --no-translations --no-opengl-sw
                        --dir "$<TARGET_FILE_DIR:${target}>"
                        "$<TARGET_FILE:${target}>"
                COMMENT "Deploying Qt runtime for ${target}"
                VERBATIM
            )
        endif()
    endfunction()
endif()

# PDFium：固定版本的 Windows 预编译包，由 scripts/bootstrap-pdfium.ps1 获取。
# 该 DLL 静态链接 CRT，只依赖 Windows 系统库，可直接被 MinGW 构建的程序调用。
set(AIPDF_PDFIUM_ROOT "" CACHE PATH "PDFium 根目录")
if(NOT AIPDF_PDFIUM_ROOT AND DEFINED ENV{AIPDF_PDFIUM_ROOT})
    set(AIPDF_PDFIUM_ROOT "$ENV{AIPDF_PDFIUM_ROOT}")
endif()
if(NOT AIPDF_PDFIUM_ROOT)
    set(AIPDF_PDFIUM_ROOT "${CMAKE_SOURCE_DIR}/tools/pdfium")
endif()

set(AIPDF_PDFIUM_INCLUDE_DIR "${AIPDF_PDFIUM_ROOT}/include")
set(AIPDF_PDFIUM_LIBRARY "${AIPDF_PDFIUM_ROOT}/lib/pdfium.dll.lib")
set(AIPDF_PDFIUM_DLL "${AIPDF_PDFIUM_ROOT}/bin/pdfium.dll")
if(NOT EXISTS "${AIPDF_PDFIUM_LIBRARY}" OR NOT EXISTS "${AIPDF_PDFIUM_DLL}")
    message(FATAL_ERROR
        "未找到 PDFium（${AIPDF_PDFIUM_ROOT}）。\n"
        "请先运行：powershell -ExecutionPolicy Bypass -File scripts/bootstrap-pdfium.ps1")
endif()

function(aipdf_deploy_pdfium target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${AIPDF_PDFIUM_DLL}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying pdfium.dll for ${target}"
        VERBATIM
    )
endfunction()

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
    find_program(AIPDF_POWERSHELL NAMES pwsh powershell)
    if(NOT AIPDF_POWERSHELL)
        message(FATAL_ERROR "未找到 PowerShell，无法执行打包脚本")
    endif()
    add_custom_target(package
        COMMAND ${AIPDF_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/package.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -OutputDir "${CMAKE_SOURCE_DIR}/dist"
                -Version "${PROJECT_VERSION}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Packaging AI PDF Editor ${PROJECT_VERSION}"
        VERBATIM
    )
endif()

