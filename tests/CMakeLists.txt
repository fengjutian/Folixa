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
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
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

# ------------------------------------------------------------ 文本与选区 ----

qt_add_executable(TextSelectionTests
    TextSelectionTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/app/TextLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/app/TextLayout.h
)

target_link_libraries(TextSelectionTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(TextSelectionTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(TextSelectionTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(TextSelectionTests)

add_test(NAME TextSelectionTests COMMAND TextSelectionTests)

qt_add_executable(TextExtractionTests
    TextExtractionTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.h
)

target_link_libraries(TextExtractionTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(TextExtractionTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(TextExtractionTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(TextExtractionTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(TextExtractionTests pdf-fixtures)
aipdf_deploy_qt_runtime(TextExtractionTests)
aipdf_deploy_pdfium(TextExtractionTests)

add_test(NAME TextExtractionTests COMMAND TextExtractionTests)

# ------------------------------------------------------------ 导航与链接 ----

qt_add_executable(NavigationTests
    NavigationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/LinkHitTest.cpp
    ${CMAKE_SOURCE_DIR}/src/app/LinkHitTest.h
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.h
)

target_link_libraries(NavigationTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(NavigationTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(NavigationTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(NavigationTests)

add_test(NAME NavigationTests COMMAND NavigationTests)

qt_add_executable(PdfNavigationTests
    PdfNavigationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
)

target_link_libraries(PdfNavigationTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(PdfNavigationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(PdfNavigationTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(PdfNavigationTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(PdfNavigationTests pdf-fixtures)
aipdf_deploy_qt_runtime(PdfNavigationTests)
aipdf_deploy_pdfium(PdfNavigationTests)

add_test(NAME PdfNavigationTests COMMAND PdfNavigationTests)

# ------------------------------------------------------------ 批注与撤销 ----

qt_add_executable(AnnotationTests
    AnnotationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/AnnotationModel.h
    ${CMAKE_SOURCE_DIR}/src/doc/EditCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/EditCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/EditContext.h
    ${CMAKE_SOURCE_DIR}/src/doc/PageCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/PageCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/UndoStack.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/UndoStack.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
)

target_link_libraries(AnnotationTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(AnnotationTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AnnotationTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(AnnotationTests)

add_test(NAME AnnotationTests COMMAND AnnotationTests)

qt_add_executable(PdfAnnotationTests
    PdfAnnotationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/app/AnnotationController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/AnnotationController.h
    ${CMAKE_SOURCE_DIR}/src/app/PdfPageOperations.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PdfPageOperations.h
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/doc/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/AnnotationModel.h
    ${CMAKE_SOURCE_DIR}/src/doc/EditCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/EditCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/EditContext.h
    ${CMAKE_SOURCE_DIR}/src/doc/PageCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/PageCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/UndoStack.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/UndoStack.h
)

target_link_libraries(PdfAnnotationTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(PdfAnnotationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(PdfAnnotationTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(PdfAnnotationTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(PdfAnnotationTests pdf-fixtures)
aipdf_deploy_qt_runtime(PdfAnnotationTests)
aipdf_deploy_pdfium(PdfAnnotationTests)

add_test(NAME PdfAnnotationTests COMMAND PdfAnnotationTests)

# ---------------------------------------------------- 页面管理与安全保存 ----

qt_add_executable(PageOperationTests
    PageOperationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/app/AnnotationController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/AnnotationController.h
    ${CMAKE_SOURCE_DIR}/src/app/PdfPageOperations.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PdfPageOperations.h
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/doc/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/AnnotationModel.h
    ${CMAKE_SOURCE_DIR}/src/doc/EditCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/EditCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/EditContext.h
    ${CMAKE_SOURCE_DIR}/src/doc/PageCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/PageCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/SafeSave.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/SafeSave.h
    ${CMAKE_SOURCE_DIR}/src/doc/UndoStack.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/UndoStack.h
)

target_link_libraries(PageOperationTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(PageOperationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(PageOperationTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(PageOperationTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(PageOperationTests pdf-fixtures)
aipdf_deploy_qt_runtime(PageOperationTests)
aipdf_deploy_pdfium(PageOperationTests)

add_test(NAME PageOperationTests COMMAND PageOperationTests)

qt_add_executable(RecoveryLogTests
    RecoveryLogTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/RecoveryLog.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/RecoveryLog.h
)

target_link_libraries(RecoveryLogTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(RecoveryLogTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(RecoveryLogTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(RecoveryLogTests)

add_test(NAME RecoveryLogTests COMMAND RecoveryLogTests)
