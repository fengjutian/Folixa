qt_add_executable(AiPdfEditor WIN32
    app/main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
    app/Theme.cpp
    app/Theme.h
    app/BrandMark.cpp
    app/BrandMark.h
    app/WelcomePage.cpp
    app/WelcomePage.h
    app/PdfView.h
    app/PdfView.cpp
    app/PageCanvas.h
    app/PageCanvas.cpp
    app/PageLayout.h
    app/PageLayout.cpp
    app/PageTransform.h
    app/PageTransform.cpp
    app/TextLayout.h
    app/TextLayout.cpp
    app/TextController.h
    app/TextController.cpp
    app/NavigationController.h
    app/NavigationController.cpp
    app/AnnotationController.h
    app/AnnotationController.cpp
    app/PdfPageOperations.h
    app/PdfPageOperations.cpp
    app/LinkHitTest.h
    app/LinkHitTest.cpp
    app/SearchPanel.h
    app/SearchPanel.cpp
    app/AiPanel.h
    app/AiPanel.cpp
    app/AiSettingsDialog.h
    app/AiSettingsDialog.cpp
    app/UploadPreviewDialog.h
    app/UploadPreviewDialog.cpp
    app/IndexController.h
    app/IndexController.cpp
    app/IndexPanel.h
    app/IndexPanel.cpp
    app/DocumentQaController.h
    app/DocumentQaController.cpp
    app/RenderThread.h
    app/RenderThread.cpp
    app/TileCache.h
    app/TileCache.cpp
    ipc/WorkerClient.cpp
    ipc/WorkerClient.h
    ipc/IpcProtocol.cpp
    ipc/IpcProtocol.h
    pdf/PdfiumAdapter.cpp
    pdf/PdfiumAdapter.h
    pdf/Annotation.cpp
    pdf/Annotation.h
    pdf/Navigation.cpp
    pdf/Navigation.h
    pdf/TextPage.cpp
    pdf/TextPage.h
    ai/ChatTypes.cpp
    ai/ChatTypes.h
    ai/CredentialStore.cpp
    ai/CredentialStore.h
    ai/ProviderConfig.cpp
    ai/ProviderConfig.h
    ai/UploadPreview.cpp
    ai/UploadPreview.h
    ai/DataPolicy.cpp
    ai/DataPolicy.h
    ai/AuditLog.cpp
    ai/AuditLog.h
    ai/EmbeddingProvider.h
    ai/LocalHashEmbedding.cpp
    ai/LocalHashEmbedding.h
    doc/AnnotationModel.cpp
    doc/AnnotationModel.h
    doc/ChunkBuilder.cpp
    doc/ChunkBuilder.h
    doc/DocumentChunk.h
    doc/EditCommand.cpp
    doc/EditCommand.h
    doc/EditContext.h
    doc/CitationVerifier.cpp
    doc/CitationVerifier.h
    doc/HybridRetriever.cpp
    doc/HybridRetriever.h
    doc/LocalIndex.cpp
    doc/LocalIndex.h
    doc/PageCommand.cpp
    doc/PageCommand.h
    doc/RecoveryLog.cpp
    doc/RecoveryLog.h
    doc/SafeSave.cpp
    doc/SafeSave.h
    doc/UndoStack.cpp
    doc/UndoStack.h
    doc/VectorIndex.cpp
    doc/VectorIndex.h
)

qt_add_resources(AiPdfEditor "app_icons"
    PREFIX "/icons"
    BASE "app/icons"
    FILES
        app/icons/pages.svg
        app/icons/outline.svg
        app/icons/bookmark.svg
        app/icons/search.svg
        app/icons/annotation.svg
        app/icons/index.svg
        app/icons/send.svg
        app/icons/chat.svg
        app/icons/rewrite.svg
        app/icons/proofread.svg
        app/icons/explain.svg
        app/icons/summarize.svg
        app/icons/zoom-out.svg
        app/icons/zoom-in.svg
        app/icons/fit-width.svg
        app/icons/fit-page.svg
        app/icons/rotate-left.svg
        app/icons/rotate-right.svg
        app/icons/save.svg
        app/icons/highlight.svg
        app/icons/underline.svg
        app/icons/strikethrough.svg
        app/icons/note.svg
        app/icons/square.svg
        app/icons/ink.svg
        app/icons/undo.svg
        app/icons/redo.svg
        app/icons/app.svg
        app/icons/close.svg
        app/icons/book.svg
)

target_link_libraries(AiPdfEditor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    # 导航栏与发送按钮的图标是 SVG（qt_add_resources 打包）：没有 QSvg 插件
    # 时 QIcon 拿到的是空图标，按钮就只剩下一个白方块。
    Qt6::Svg
    ${AIPDF_PDFIUM_LIBRARY}
)

# Windows 凭据管理器（API 密钥）：advapi32。
if (WIN32)
    target_link_libraries(AiPdfEditor PRIVATE advapi32)
endif()

target_include_directories(AiPdfEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(AiPdfEditor PRIVATE ${AIPDF_PDFIUM_INCLUDE_DIR})
target_compile_definitions(AiPdfEditor PRIVATE AIPDF_VERSION="${PROJECT_VERSION}")

set_target_properties(AiPdfEditor PROPERTIES
    OUTPUT_NAME "AiPdfEditor"
)

set(AIPDF_FILE_DESCRIPTION "AI PDF Editor")
set(AIPDF_INTERNAL_NAME "AiPdfEditor")
set(AIPDF_ORIGINAL_FILENAME "AiPdfEditor.exe")
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY
)
target_sources(AiPdfEditor PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)

aipdf_deploy_qt_runtime(AiPdfEditor)
aipdf_deploy_pdfium(AiPdfEditor)

