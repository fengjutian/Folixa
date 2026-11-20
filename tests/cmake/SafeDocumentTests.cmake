# 安全文档（隔离进程解析）的测试目标。
#
# 编进来的：
#   - app/controllers/SafeDocumentController（主进程侧的控制端）
#   - app/dialogs/SafeDocumentViewerDialog（只读查看窗口）
#   - ipc/WorkerClient + ipc/IpcProtocol（既有 IPC 底座，不另造一套）
if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

# 重复 include 时不重复定义目标（例如本文件既被 tests/CMakeLists.txt 引用，
# 又在独立构建目录里被临时注入一次）。
if (TARGET SafeDocumentTests)
    return()
endif()

# 夹具目录：tests/CMakeLists.txt 里已经定义过 PDF_FIXTURE_DIR；
# 独立构建目录里没有这个变量时按同样的约定兜底。
if (NOT DEFINED PDF_FIXTURE_DIR)
    set(PDF_FIXTURE_DIR "${CMAKE_BINARY_DIR}/tests/fixtures")
endif()

qt_add_executable(SafeDocumentTests
    ${CMAKE_CURRENT_LIST_DIR}/../SafeDocumentTests.cpp
    ${AIPDF_REPO_ROOT}/src/app/controllers/SafeDocumentController.cpp
    ${AIPDF_REPO_ROOT}/src/app/controllers/SafeDocumentController.h
    ${AIPDF_REPO_ROOT}/src/app/dialogs/SafeDocumentViewerDialog.cpp
    ${AIPDF_REPO_ROOT}/src/app/dialogs/SafeDocumentViewerDialog.h
    ${AIPDF_REPO_ROOT}/src/ipc/IpcProtocol.cpp
    ${AIPDF_REPO_ROOT}/src/ipc/IpcProtocol.h
    ${AIPDF_REPO_ROOT}/src/ipc/WorkerClient.cpp
    ${AIPDF_REPO_ROOT}/src/ipc/WorkerClient.h
)

target_link_libraries(SafeDocumentTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test
)

target_include_directories(SafeDocumentTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)

target_compile_definitions(SafeDocumentTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
    AIPDF_VERSION="${PROJECT_VERSION}"
    AIPDF_PARSE_WORKER_EXE="$<TARGET_FILE:PdfParseWorker>"
)

set_target_properties(SafeDocumentTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(SafeDocumentTests)

add_dependencies(SafeDocumentTests PdfParseWorker)

if (TARGET pdf-fixtures)
    add_dependencies(SafeDocumentTests pdf-fixtures)
endif()

add_test(NAME SafeDocumentTests COMMAND SafeDocumentTests)
