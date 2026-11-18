# 真 Office 包（.pptx / .xlsx）的导出测试。
#
# 这一层不碰界面：包就是 zip，zip 由 doc::zip::Writer 写、由 doc::zip 的读接口回读，
# XML 用 QXmlStreamReader 解析（都在 QtCore 里）。
#
# 样例会写到构建目录，由 scripts/verify-office-packages.py 用**独立解析器**
# （Python 标准库 zipfile + 可选的 python-pptx / openpyxl）再验一次。

qt_add_executable(OfficePackageTests
    ${CMAKE_CURRENT_LIST_DIR}/../OfficePackageTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/ZipWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/ZipWriter.h
    ${CMAKE_SOURCE_DIR}/src/doc/SpreadsheetExport.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/SpreadsheetExport.h
    ${CMAKE_SOURCE_DIR}/src/doc/XmlText.h
    ${CMAKE_SOURCE_DIR}/src/ai/PptGeneration.h
    ${CMAKE_SOURCE_DIR}/src/ai/PptxExport.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/PptxExport.h
)

target_link_libraries(OfficePackageTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(OfficePackageTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(OfficePackageTests PRIVATE
    AIPDF_OFFICE_PACKAGE_DIR="${CMAKE_CURRENT_BINARY_DIR}/office-packages"
)
set_target_properties(OfficePackageTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(OfficePackageTests)
add_test(NAME OfficePackageTests COMMAND OfficePackageTests)

# 独立校验：用 Python 的 zipfile 重新解包（不依赖本仓库的 zip 写入器），
# 若装了 python-pptx / openpyxl 再用它们真正打开一次。
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME OfficePackageVerifyTests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/verify-office-packages.py
            --dir ${CMAKE_CURRENT_BINARY_DIR}/office-packages
)
set_tests_properties(OfficePackageVerifyTests PROPERTIES
    DEPENDS OfficePackageTests
)
