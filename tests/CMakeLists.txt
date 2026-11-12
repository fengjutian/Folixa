qt_add_executable(IpcProtocolTests
    IpcProtocolTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.h
)

target_link_libraries(IpcProtocolTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(IpcProtocolTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(IpcProtocolTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(IpcProtocolTests)

add_test(NAME IpcProtocolTests COMMAND IpcProtocolTests)

qt_add_executable(WorkerIntegrationTests
    WorkerIntegrationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.h
    ${CMAKE_SOURCE_DIR}/src/ipc/WorkerClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/WorkerClient.h
)

target_link_libraries(WorkerIntegrationTests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
target_include_directories(WorkerIntegrationTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(WorkerIntegrationTests PRIVATE
    AIPDF_VERSION="${PROJECT_VERSION}"
    AIPDF_WORKER_EXE="$<TARGET_FILE:PdfAiWorker>"
)
set_target_properties(WorkerIntegrationTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(WorkerIntegrationTests)

add_dependencies(WorkerIntegrationTests PdfAiWorker)

add_test(NAME WorkerIntegrationTests COMMAND WorkerIntegrationTests)

# ---------------------------------------------------------------- PDFium ----

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(PDF_FIXTURE_DIR ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
add_custom_target(pdf-fixtures
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/generate-test-pdfs.py
            ${PDF_FIXTURE_DIR} --quiet
    COMMENT "Generating PDF test fixtures"
    VERBATIM
)

qt_add_executable(PdfiumAdapterTests
    PdfiumAdapterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
)

target_link_libraries(PdfiumAdapterTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(PdfiumAdapterTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(PdfiumAdapterTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
    AIPDF_PDF_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
set_target_properties(PdfiumAdapterTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(PdfiumAdapterTests pdf-fixtures)
aipdf_deploy_qt_runtime(PdfiumAdapterTests)
aipdf_deploy_pdfium(PdfiumAdapterTests)

add_test(NAME PdfiumAdapterTests COMMAND PdfiumAdapterTests)

# ------------------------------------------------------------ 视口与缓存 ----

qt_add_executable(PdfViewportTests
    PdfViewportTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PageLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PageLayout.h
    ${CMAKE_SOURCE_DIR}/src/app/TileCache.cpp
    ${CMAKE_SOURCE_DIR}/src/app/TileCache.h
)

target_link_libraries(PdfViewportTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(PdfViewportTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(PdfViewportTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PdfViewportTests)

add_test(NAME PdfViewportTests COMMAND PdfViewportTests)
