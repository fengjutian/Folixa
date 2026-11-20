cmake_minimum_required(VERSION 3.25)

project(AiPdfEditor VERSION 0.15.0 LANGUAGES C CXX)

# 应用图标：Windows 资源里嵌一份（资源管理器、任务栏与文件关联用），
# 运行时窗口图标用同一份设计的 SVG（src/app/icons/app.svg），
# 两份都由 src/app/icons/app.svg 派生，改设计时一起换。
set(AIPDF_APP_ICON "${CMAKE_SOURCE_DIR}/src/app/app.ico")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AIPDF_BUILD_TESTS "Build tests" ON)

# 必需依赖：qpdf（保存前结构校验 + 对象级读写）。
#
# 这里以前是「可选」，关掉时结构校验是空操作：于是「单测全绿」和
# 「真实写盘路径一次都没跑过」可以同时成立。降级路径已经删掉——
# 找不到 qpdf 就直接配置失败，不会生成一个悄悄少一半能力的构建。
# 获取：scripts/bootstrap-qpdf.ps1
option(AIPDF_WITH_QPDF "Require qpdf for structural validation and object-level I/O" ON)
if(NOT AIPDF_WITH_QPDF)
    message(FATAL_ERROR
        "AIPDF_WITH_QPDF 已不再支持关闭：qpdf 是必需依赖。\n"
        "如果只是想跳过校验，请在测试里造损坏样本，不要砍依赖。")
endif()

# qpdf 的包配置在 tools/qpdf/lib/cmake/qpdf。直接钉 qpdf_DIR 比依赖
# CMAKE_PREFIX_PATH 的搜索规则可靠——工具链文件也会改这个变量，容易互相覆盖。
if(NOT qpdf_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/tools/qpdf/lib/cmake/qpdf/qpdfConfig.cmake")
    set(qpdf_DIR "${CMAKE_SOURCE_DIR}/tools/qpdf/lib/cmake/qpdf")
endif()
find_package(qpdf CONFIG QUIET)
if(NOT qpdf_FOUND)
    message(FATAL_ERROR
        "未找到 qpdf（${CMAKE_SOURCE_DIR}/tools/qpdf）。qpdf 已是必需依赖。\n"
        "请先运行：powershell -ExecutionPolicy Bypass -File scripts/bootstrap-qpdf.ps1")
endif()

# 必需依赖：QuickJS-ng（表单脚本引擎）。
#
# 表单脚本不能永远靠手写白名单子集：real-world 的 /AA 脚本里有变量、循环、
# Math、getField、字符串方法，子集永远追不上。这里把源码一起编译，
# 不引动态库、不跨 ABI。
# 获取：scripts/bootstrap-quickjs.ps1
set(AIPDF_QUICKJS_ROOT "${CMAKE_SOURCE_DIR}/third_party/quickjs")
if(NOT EXISTS "${AIPDF_QUICKJS_ROOT}/quickjs.h")
    message(FATAL_ERROR
        "未找到 QuickJS 源码（${AIPDF_QUICKJS_ROOT}）。\n"
        "请先运行：powershell -ExecutionPolicy Bypass -File scripts/bootstrap-quickjs.ps1")
endif()
add_library(aipdf_quickjs STATIC
    "${AIPDF_QUICKJS_ROOT}/quickjs.c"
    "${AIPDF_QUICKJS_ROOT}/libregexp.c"
    "${AIPDF_QUICKJS_ROOT}/libunicode.c"
    "${AIPDF_QUICKJS_ROOT}/libbf.c"
    "${AIPDF_QUICKJS_ROOT}/cutils.c"
)
target_include_directories(aipdf_quickjs PUBLIC "${AIPDF_QUICKJS_ROOT}")
target_compile_definitions(aipdf_quickjs PRIVATE
    _GNU_SOURCE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
set_target_properties(aipdf_quickjs PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 主程序与 Worker 必须落在同一目录：主程序按自身路径查找 PdfAiWorker。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin")
endforeach()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Svg PrintSupport)

# Qlementine：Qt Widgets 的现代 QStyle。固定版本保证 CI、开发机和发布包外观一致。
# 它负责标准控件的基础绘制；应用品牌色与专属面板仍由 Theme.cpp 叠加。
include(FetchContent)
set(QLEMENTINE_SANDBOX OFF CACHE BOOL "" FORCE)
set(QLEMENTINE_SHOWCASE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    qlementine
    GIT_REPOSITORY https://github.com/oclero/qlementine.git
    # Release v1.4.2. The immutable commit keeps builds reproducible even if
    # an upstream tag is moved.
    GIT_TAG 13f72eb8b53bafd9ac24e5562d8ddc28d5440469
)
FetchContent_MakeAvailable(qlementine)

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

