# Windows 构建环境固定文件：Qt 6.8.3（官方二进制套件，mingw_64）+ MinGW-w64 GCC 13.1.0。
#
# 安装方式见 docs/versions/v0.1.0.md 的「构建环境」章节：
#   aqt install-qt windows desktop 6.8.3 win64_mingw --outputdir C:\Qt --archives qtbase
#   aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 --outputdir C:\Qt
#
# 两个根目录都可以用环境变量覆盖：
#   $env:AIPDF_QT_ROOT    = 'C:\Qt\6.8.3\mingw_64'
#   $env:AIPDF_MINGW_ROOT = 'C:\Qt\Tools\mingw1310_64'

set(AIPDF_QT_ROOT "$ENV{AIPDF_QT_ROOT}" CACHE PATH "Qt 套件根目录（包含 lib/cmake/Qt6）")
if(NOT AIPDF_QT_ROOT)
    set(AIPDF_QT_ROOT "C:/Qt/6.8.3/mingw_64")
endif()

set(AIPDF_MINGW_ROOT "$ENV{AIPDF_MINGW_ROOT}" CACHE PATH "MinGW-w64 工具链根目录")
if(NOT AIPDF_MINGW_ROOT)
    set(AIPDF_MINGW_ROOT "C:/Qt/Tools/mingw1310_64")
endif()

set(CMAKE_C_COMPILER "${AIPDF_MINGW_ROOT}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${AIPDF_MINGW_ROOT}/bin/g++.exe")
set(CMAKE_RC_COMPILER "${AIPDF_MINGW_ROOT}/bin/windres.exe")

# Qt 的 CMake 包位于 <prefix>/lib/cmake/Qt6，find_package 会按 CMAKE_PREFIX_PATH 查找。
list(APPEND CMAKE_PREFIX_PATH "${AIPDF_QT_ROOT}")

set(CMAKE_FIND_ROOT_PATH "${AIPDF_QT_ROOT}" "${AIPDF_MINGW_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
