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

# ---------------------------------------------------------- 文档索引底座 ----

qt_add_executable(DocumentIndexTests
    DocumentIndexTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/ChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/ChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/doc/DocumentChunk.h
    ${CMAKE_SOURCE_DIR}/src/doc/LocalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/LocalIndex.h
)

target_link_libraries(DocumentIndexTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(DocumentIndexTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(DocumentIndexTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(DocumentIndexTests)

add_test(NAME DocumentIndexTests COMMAND DocumentIndexTests)

qt_add_executable(PdfIndexTests
    PdfIndexTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.h
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/app/TextLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/app/TextLayout.h
    ${CMAKE_SOURCE_DIR}/src/app/IndexController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/IndexController.h
    ${CMAKE_SOURCE_DIR}/src/doc/ChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/ChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/doc/DocumentChunk.h
    ${CMAKE_SOURCE_DIR}/src/doc/LocalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/LocalIndex.h
)

target_link_libraries(PdfIndexTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(PdfIndexTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(PdfIndexTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(PdfIndexTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(PdfIndexTests pdf-fixtures)
aipdf_deploy_qt_runtime(PdfIndexTests)
aipdf_deploy_pdfium(PdfIndexTests)

add_test(NAME PdfIndexTests COMMAND PdfIndexTests)

# ---------------------------------------------------------- 渲染线程收工 ----
qt_add_executable(RenderThreadTests
    RenderThreadTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/RenderThread.cpp
    ${CMAKE_SOURCE_DIR}/src/app/RenderThread.h
    ${CMAKE_SOURCE_DIR}/src/app/TileCache.cpp
    ${CMAKE_SOURCE_DIR}/src/app/TileCache.h
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
)

target_link_libraries(RenderThreadTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(RenderThreadTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(RenderThreadTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(RenderThreadTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(RenderThreadTests pdf-fixtures)
aipdf_deploy_qt_runtime(RenderThreadTests)
aipdf_deploy_pdfium(RenderThreadTests)

add_test(NAME RenderThreadTests COMMAND RenderThreadTests)

# ------------------------------------------------------- 模型接入与流式 ----

qt_add_executable(AiProviderTests
    AiProviderTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderConfig.h
    ${CMAKE_SOURCE_DIR}/src/ai/DataPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/DataPolicy.h
    ${CMAKE_SOURCE_DIR}/src/ai/UploadPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/UploadPreview.h
    ${CMAKE_SOURCE_DIR}/src/ai/AuditLog.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AuditLog.h
    ${CMAKE_SOURCE_DIR}/src/ai/SseParser.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/SseParser.h
)

target_link_libraries(AiProviderTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(AiProviderTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AiProviderTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 凭据管理器在 advapi32 上。
if (WIN32)
    target_link_libraries(AiProviderTests PRIVATE advapi32)
endif()

aipdf_deploy_qt_runtime(AiProviderTests)

add_test(NAME AiProviderTests COMMAND AiProviderTests)

# ------------------------------------------------- 可验证文档问答底座 ----

qt_add_executable(DocumentQaTests
    DocumentQaTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.h
    ${CMAKE_SOURCE_DIR}/src/ai/EmbeddingProvider.h
    ${CMAKE_SOURCE_DIR}/src/ai/LocalHashEmbedding.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/LocalHashEmbedding.h
    ${CMAKE_SOURCE_DIR}/src/ai/ModelProvider.h
    ${CMAKE_SOURCE_DIR}/src/ai/MockProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/MockProvider.h
    ${CMAKE_SOURCE_DIR}/src/doc/CitationVerifier.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/CitationVerifier.h
    ${CMAKE_SOURCE_DIR}/src/doc/DocumentChunk.h
    ${CMAKE_SOURCE_DIR}/src/doc/HybridRetriever.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/HybridRetriever.h
    ${CMAKE_SOURCE_DIR}/src/doc/LocalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/LocalIndex.h
    ${CMAKE_SOURCE_DIR}/src/doc/VectorIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/VectorIndex.h
)

target_link_libraries(DocumentQaTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(DocumentQaTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(DocumentQaTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(DocumentQaTests)

add_test(NAME DocumentQaTests COMMAND DocumentQaTests)

# ------------------------------------------------- AI 面板的交互守卫 ----

qt_add_executable(AiPanelTests
    AiPanelTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/AiPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/app/AiPanel.h
    ${CMAKE_SOURCE_DIR}/src/app/AiSettingsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/UploadPreviewDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/UploadPreviewDialog.h
    ${CMAKE_SOURCE_DIR}/src/ai/UploadPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/UploadPreview.h
    ${CMAKE_SOURCE_DIR}/src/app/AiSettingsDialog.h
    ${CMAKE_SOURCE_DIR}/src/app/DocumentQaController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/DocumentQaController.h
    ${CMAKE_SOURCE_DIR}/src/app/IndexController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/IndexController.h
    ${CMAKE_SOURCE_DIR}/src/app/TextController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/TextController.h
    ${CMAKE_SOURCE_DIR}/src/app/TextLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/app/TextLayout.h
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/EmbeddingProvider.h
    ${CMAKE_SOURCE_DIR}/src/ai/LocalHashEmbedding.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/LocalHashEmbedding.h
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderConfig.h
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderSelection.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderSelection.h
    ${CMAKE_SOURCE_DIR}/src/ai/DataPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/DataPolicy.h
    ${CMAKE_SOURCE_DIR}/src/ai/UploadPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/UploadPreview.h
    ${CMAKE_SOURCE_DIR}/src/ai/AuditLog.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AuditLog.h
    ${CMAKE_SOURCE_DIR}/src/doc/CitationVerifier.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/CitationVerifier.h
    ${CMAKE_SOURCE_DIR}/src/doc/ChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/ChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/doc/DocumentChunk.h
    ${CMAKE_SOURCE_DIR}/src/doc/HybridRetriever.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/HybridRetriever.h
    ${CMAKE_SOURCE_DIR}/src/doc/LocalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/LocalIndex.h
    ${CMAKE_SOURCE_DIR}/src/doc/VectorIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/VectorIndex.h
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.h
    ${CMAKE_SOURCE_DIR}/src/ipc/WorkerClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/WorkerClient.h
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/Navigation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/TextPage.h
)

target_link_libraries(AiPanelTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(AiPanelTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(AiPanelTests PRIVATE ${AIPDF_PDFIUM_INCLUDE_DIR})
set_target_properties(AiPanelTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(AiPanelTests)
aipdf_deploy_pdfium(AiPanelTests)

add_test(NAME AiPanelTests COMMAND AiPanelTests)

# ------------------------------------------------- 图标资源的回归守卫 ----

qt_add_executable(IconResourceTests
    IconResourceTests.cpp
)

# 图标资源只打包进主程序，测试要自己再打一份，才能验证「资源在」这一半。
# 图标资源按目录收集：与主程序保持同一份集合，新增图标不用改测试清单。
file(GLOB AIPDF_TEST_ICON_SVGS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/app/icons/*.svg")
qt_add_resources(IconResourceTests "icon_test_resources"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/src/app/icons"
    FILES ${AIPDF_TEST_ICON_SVGS}
)

# Qt6::Svg 必须链上：windeployqt 靠它判断要部署 imageformats/qsvg.dll，
# 少了这个插件图标就是空的——这正是本用例守的那个回归。
target_link_libraries(IconResourceTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test Qt6::Svg)
set_target_properties(IconResourceTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(IconResourceTests)

add_test(NAME IconResourceTests COMMAND IconResourceTests)

# ------------------------------------------------- AI 数据策略与审计 ----

qt_add_executable(DataPolicyTests
    DataPolicyTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/DataPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/DataPolicy.h
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderSelection.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderSelection.h
    ${CMAKE_SOURCE_DIR}/src/ai/UploadPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/UploadPreview.h
    ${CMAKE_SOURCE_DIR}/src/ai/AuditLog.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AuditLog.h
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderConfig.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
)

target_link_libraries(DataPolicyTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(DataPolicyTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(DataPolicyTests PROPERTIES WIN32_EXECUTABLE FALSE)
if (WIN32)
    target_link_libraries(DataPolicyTests PRIVATE advapi32)
endif()
aipdf_deploy_qt_runtime(DataPolicyTests)

add_test(NAME DataPolicyTests COMMAND DataPolicyTests)

# ------------------------------------------------- 提示注入防护 ----

qt_add_executable(PromptInjectionTests
    PromptInjectionTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.h
)

target_link_libraries(PromptInjectionTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(PromptInjectionTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(PromptInjectionTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PromptInjectionTests)

add_test(NAME PromptInjectionTests COMMAND PromptInjectionTests)
