qt_add_executable(AiPdfEditor WIN32
    app/main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
    app/Language.cpp
    app/Language.h
    i18n/Translate.h
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
    app/OcrController.cpp
    app/OcrController.h
    app/IndexController.h
    app/IndexController.cpp
    app/IndexPanel.h
    app/IndexPanel.cpp
    app/DocumentQaController.h
    app/DocumentQaController.cpp
    app/DocumentOutput.h
    app/SearchMatcher.h
    app/NavigationHistory.h
    app/NavigationHistory.cpp
    app/SearchMatcher.cpp
    app/PresentationWindow.h
    app/PresentationWindow.cpp
    app/ViewFit.h
    app/ViewFit.cpp
    app/ZoomMemory.h
    app/ZoomMemory.cpp
    app/InkGeometry.h
    app/InkGeometry.cpp
    app/MeasureGeometry.h
    app/MeasureGeometry.cpp
    app/DocumentOutput.cpp
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
    pdf/AnnotationDictionary.h
    pdf/Xfdf.h
    pdf/AnnotationSummary.h
    pdf/SoundFormat.h
    pdf/PageDecorations.h
    pdf/PageDecorations.cpp
    pdf/SoundFormat.cpp
    pdf/AnnotationSummary.cpp
    pdf/Xfdf.cpp
    pdf/AnnotationDictionary.cpp
    pdf/AppearanceStream.cpp
    pdf/AppearanceStream.h
    pdf/IncrementalWriter.cpp
    pdf/IncrementalWriter.h
    pdf/DocumentWriter.cpp
    pdf/DocumentWriter.h
    pdf/QpdfBackend.cpp
    pdf/QpdfBackend.h
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
    ai/ProviderSelection.cpp
    ai/ProviderSelection.h
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

    ocr/OcrEngine.h
    ocr/OcrTypes.h
    ocr/OcrGeometry.cpp
    ocr/OcrGeometry.h
    ocr/ScannedPageDetector.cpp
    ocr/ScannedPageDetector.h
    ocr/OcrService.cpp
    ocr/OcrService.h
    ocr/ImagePreprocess.cpp
    ocr/ImagePreprocess.h
    ocr/OcrChunkBridge.cpp
    ocr/OcrChunkBridge.h
    ocr/TesseractCliEngine.cpp
    ocr/TesseractCliEngine.h
    ocr/OcrEngineFactory.cpp
    ocr/OcrEngineFactory.h
    ocr/WindowsOcrEngine.cpp
    ocr/WindowsOcrEngine.h
    ocr/OcrTextUtils.cpp
    ocr/OcrTextUtils.h
    tools/ToolRegistry.cpp
    tools/ToolRegistry.h
    tools/ProposalValidator.cpp
    tools/ProposalValidator.h
    app/ProposalRunner.cpp
    app/ProposalRunner.h
    app/EditProposalDialog.cpp
    app/EditProposalDialog.h
    app/BlankPageDetector.cpp
    app/BlankPageDetector.h
    doc/VectorIndex.h
)

# 图标资源按目录收集：新增一个图标只要放文件，不用同时改应用和测试两份清单。
file(GLOB AIPDF_ICON_SVGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app/icons/*.svg")
qt_add_resources(AiPdfEditor "app_icons"
    PREFIX "/icons"
    BASE "app/icons"
    FILES ${AIPDF_ICON_SVGS}
)

# 翻译目录：标准 .ts 直接进资源，运行时由 TsTranslator 解析。
#
# 不走 qt_add_translations / lrelease：固定工具链里没有 Qt LinguistTools，
# 加一个编译期依赖会把「装好工具链就能构建」这条前提打破。.ts 仍是标准
# 格式，Qt Linguist 照样能打开编辑；打包也只是多几百 KB 的文本。
file(GLOB AIPDF_I18N_CATALOGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/*.ts")
qt_add_resources(AiPdfEditor "app_i18n"
    PREFIX "/i18n"
    BASE "i18n"
    FILES ${AIPDF_I18N_CATALOGS}
)

target_link_libraries(AiPdfEditor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    # 导航栏与发送按钮的图标是 SVG（qt_add_resources 打包）：没有 QSvg 插件
    # 时 QIcon 拿到的是空图标，按钮就只剩下一个白方块。
    Qt6::Svg
    # 打印：QPrinter / QPrintDialog。用渲染成图的方式打印，不走系统 PDF 打印栈。
    Qt6::PrintSupport
    ${AIPDF_PDFIUM_LIBRARY}
)

# Windows 凭据管理器（API 密钥）：advapi32。
if (WIN32)
    target_link_libraries(AiPdfEditor PRIVATE advapi32)
endif()

target_include_directories(AiPdfEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(AiPdfEditor PRIVATE ${AIPDF_PDFIUM_INCLUDE_DIR})
target_compile_definitions(AiPdfEditor PRIVATE AIPDF_VERSION="${PROJECT_VERSION}")

# 可选的 qpdf 结构校验：包在根作用域已找好（找不到时 qpdf_FOUND 为假，这里自动跳过）。
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(AiPdfEditor PRIVATE qpdf::libqpdf)
    target_compile_definitions(AiPdfEditor PRIVATE AIPDF_WITH_QPDF=1)
    message(STATUS "qpdf ${qpdf_VERSION}: 保存前会做结构校验")
    # 运行时需要 qpdf30.dll 与主程序同目录，否则程序根本起不来。
    add_custom_command(TARGET AiPdfEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:AiPdfEditor>"
        COMMENT "Copying qpdf runtime")
endif()

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

