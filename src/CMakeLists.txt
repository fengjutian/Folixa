qt_add_executable(AiPdfEditor WIN32
    app/main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
    app/FormScriptDialog.h
    app/FormScriptDialog.cpp
    app/FormToolSettingsDialog.h
    app/FormToolSettingsDialog.cpp
    app/FormDesignAssistantDialog.h
    app/FormDesignAssistantDialog.cpp
    app/Language.cpp
    app/Language.h
    app/LanguagePackInstaller.cpp
    i18n/Translate.h
    app/Theme.cpp
    app/Theme.h
    app/ModalScrim.cpp
    app/ModalScrim.h
    app/ReadingScheme.cpp
    app/ReadingScheme.h
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
    app/MouseShortcuts.h
    app/MouseShortcuts.cpp
    app/TextController.h
    app/TextController.cpp
    app/NavigationController.h
    app/NavigationController.cpp
    app/AnnotationController.h
    app/AnnotationController.cpp
    app/PdfPageOperations.h
    app/PageChunkBuilder.h
    app/PdfPageOperations.cpp
    app/PageChunkBuilder.cpp
    app/PageTextLines.h
    app/PageTextLines.cpp
    app/LinkHitTest.h
    app/LinkHitTest.cpp
    app/SearchPanel.h
    app/SearchPanel.cpp
    app/AiPanel.h
    app/AiPanel.cpp
    app/AiSettingsDialog.h
    app/AiSettingsDialog.cpp
    app/SettingsDialog.h
    app/SettingsDialog.cpp
    app/EncryptionDialog.h
    app/EncryptionDialog.cpp
    app/SignatureDialog.h
    app/SignatureDialog.cpp
    app/RibbonBar.h
    app/RibbonBar.cpp
    app/LastWaveWriter.cpp
    app/LastWaveWriter.h
    app/PdfAPreflightPanel.cpp
    app/PdfAPreflightPanel.h
    app/HiddenDataDialog.cpp
    app/HiddenDataDialog.h
    app/BookmarkTreeDialog.cpp
    app/BookmarkTreeDialog.h
    app/OneNoteExporter.cpp
    app/OneNoteExporter.h
    app/PresentationLoop.cpp
    app/PresentationLoop.h
    app/ScannerCapture.cpp
    app/ScannerCapture.h
    app/ConversionInputDialog.cpp
    app/ConversionInputDialog.h
    app/DocumentCompareDialog.cpp
    app/DocumentCompareDialog.h
    app/LastWavePanels.cpp
    app/LastWavePanels.h
    app/LastWaveBridge.cpp
    app/LastWaveBridge.h
    app/WiringActions.cpp
    app/WiringActions.h
    app/PrintToolsDialog.cpp
    app/PrintToolsDialog.h
    app/ConversionToolsDialog.cpp
    app/ConversionToolsDialog.h
    app/ConversionReports.cpp
    app/ConversionReports.h
    app/ConversionExports.cpp
    app/ConversionExports.h
    app/AnnotationToolboxDialog.cpp
    app/AnnotationToolboxDialog.h
    app/StampNumberDialog.cpp
    app/StampNumberDialog.h
    app/RegionPicker.cpp
    app/RegionPicker.h
    app/ScreenCapture.cpp
    app/ScreenCapture.h
    app/AccessibilityWizard.cpp
    app/AccessibilityWizard.h
    app/ReadingOptionsDialog.cpp
    app/ReadingOptionsDialog.h
    app/AiToolsDialog.cpp
    app/AiToolsDialog.h
    app/AiToolService.cpp
    app/AiToolService.h
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
    app/DocumentConverter.cpp
    app/DocumentConverter.h
    app/DigitalSignatureBackend.cpp
    app/DigitalSignatureBackend.h
    app/SearchMatcher.h
    app/NavigationHistory.h
    app/NavigationHistory.cpp
    app/SearchMatcher.cpp
    app/ShareUtils.cpp
    app/ShareUtils.h
    app/SystemCjkFont.h
    app/BatchRename.cpp
    app/BatchRename.h
    app/PresentationWindow.h
    app/PresentationWindow.cpp
    app/ViewFit.h
    app/ViewFit.cpp
    app/ZoomMemory.h
    app/ZoomMemory.cpp
    app/InkGeometry.h
    app/InkGeometry.cpp
    app/MeasureGeometry.h
    app/AudioRecorder.h
    app/AudioRecorder.cpp
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
    pdf/kernel/PdfiumAdapter.cpp
    pdf/kernel/PdfiumAdapter.h
    pdf/content/ContentStream.cpp
    pdf/content/ContentStream.h
    pdf/content/TrueTypeSubset.cpp
    pdf/content/TrueTypeSubset.h
    pdf/content/TextReflow.cpp
    pdf/content/TextReflow.h
    pdf/content/TableStructure.cpp
    pdf/content/TableStructure.h
    pdf/content/TableIntegration.cpp
    pdf/annot/Annotation.cpp
    pdf/annot/Annotation.h
    pdf/annot/AnnotationDictionary.h
    pdf/annot/Xfdf.h
    pdf/annot/AnnotationSummary.h
    pdf/annot/SoundFormat.h
    pdf/content/PageDecorations.h
    pdf/forms/FormData.h
    pdf/forms/FormFieldWriter.h
    pdf/forms/FormJsEvaluator.h
    pdf/forms/FormTemplateStore.h
    pdf/forms/FormScriptStore.h
    pdf/forms/BarcodeEncoder.h
    pdf/forms/FormToolSettings.h
    pdf/forms/FormDesignParser.h
    pdf/annot/PageAnnotsInjector.h
    pdf/annot/PageAnnotsInjector.cpp
    pdf/ops/OutlineWriter.h
    pdf/ops/OutlineWriter.cpp
    pdf/forms/FormData.cpp
    pdf/forms/FormFieldWriter.cpp
    pdf/forms/FormJsEvaluator.cpp
    pdf/forms/FormTemplateStore.cpp
    pdf/forms/FormScriptStore.cpp
    pdf/forms/BarcodeEncoder.cpp
    pdf/forms/FormToolSettings.cpp
    pdf/forms/FormDesignParser.cpp
    pdf/document/DocumentProperties.cpp
    pdf/document/DocumentProperties.h
    pdf/ops/OutlineGenerator.cpp
    pdf/ops/OutlineGenerator.h
    pdf/ops/OutlineEditor.cpp
    pdf/ops/OutlineEditor.h
    pdf/ops/PageSplitPlanner.cpp
    pdf/ops/PageSplitPlanner.h
    pdf/security/Redaction.cpp
    pdf/security/Redaction.h
    pdf/ops/BatchReplace.cpp
    pdf/ops/BatchReplace.h
    pdf/ops/SpellChecker.cpp
    pdf/ops/SpellChecker.h
    pdf/security/SignatureInspector.cpp
    pdf/security/SignatureInspector.h
    pdf/compliance/TagTreeWriter.cpp
    pdf/compliance/TagTreeWriter.h
    pdf/annot/AnnotationShapes.cpp
    pdf/annot/AnnotationShapes.h
    pdf/ops/DocumentReflow.cpp
    pdf/ops/DocumentReflow.h
    pdf/annot/RichMedia3d.cpp
    pdf/annot/RichMedia3d.h
    pdf/forms/XfaSupport.cpp
    pdf/forms/XfaSupport.h
    pdf/compliance/PdfUaChecker.cpp
    pdf/compliance/PdfUaChecker.h
    ai/OfflineQa.cpp
    ai/OfflineQa.h
    doc/UniversalConverter.cpp
    doc/UniversalConverter.h
    pdf/compliance/StructTreeReader.cpp
    pdf/compliance/StructTreeReader.h
    pdf/content/InvisibleTextLayer.cpp
    pdf/content/InvisibleTextLayer.h
    pdf/content/ParagraphFormat.cpp
    pdf/content/ParagraphFormat.h
    pdf/ops/OptionalContent.cpp
    pdf/ops/OptionalContent.h
    doc/InvoiceLayout.cpp
    doc/InvoiceLayout.h
    doc/ActionWizard.cpp
    doc/ActionWizard.h
    pdf/ops/OcrSuspicion.cpp
    pdf/ops/OcrSuspicion.h
    pdf/convert/PortfolioWriter.cpp
    pdf/convert/PortfolioWriter.h
    pdf/compliance/PreflightChecker.cpp
    pdf/compliance/PreflightChecker.h
    pdf/convert/ExportFormats.cpp
    pdf/convert/ExportFormats.h
    doc/ZipWriter.cpp
    doc/ZipWriter.h
    doc/ConversionInputs.cpp
    doc/ConversionInputs.h
    doc/FontFlattening.cpp
    doc/FontFlattening.h
    doc/OfdExport.cpp
    doc/OfdExport.h
    doc/OfficeExport.cpp
    doc/OfficeExport.h
    doc/PdfToOffice.cpp
    doc/PdfToOffice.h
    doc/PptxWriter.cpp
    doc/PptxWriter.h
    doc/SpreadsheetExport.cpp
    doc/SpreadsheetExport.h
    doc/XmlText.h
    pdf/annot/AnnotationToolkit.cpp
    pdf/annot/AnnotationToolkit.h
    pdf/annot/AnnotationFilter.cpp
    pdf/annot/AnnotationFilter.h
    app/ViewModes.cpp
    app/ViewModes.h
    doc/PrintTemplate.cpp
    doc/PrintTemplate.h
    doc/PrintBatch.cpp
    doc/PrintBatch.h
    doc/BlankDocument.cpp
    doc/BlankDocument.h
    ai/PptGeneration.cpp
    ai/PptGeneration.h
    ai/PptxExport.cpp
    ai/PptxExport.h
    ai/AiNarration.cpp
    ai/AiNarration.h
    ai/AiContentDetection.cpp
    ai/AiContentDetection.h
    ai/AiImageGeneration.cpp
    ai/AiImageGeneration.h
    pdf/security/SignatureWorkflow.cpp
    pdf/security/SignatureWorkflow.h
    pdf/security/SignatureVerifier.cpp
    pdf/security/RsaVerifier.cpp
    pdf/security/RsaVerifier.h
    pdf/security/SignatureVerifier.h
    pdf/ops/DocumentMerger.cpp
    pdf/ops/DocumentMerger.h
    pdf/document/NamedDestination.cpp
    pdf/document/NamedDestination.h
    pdf/ops/PageLabels.cpp
    pdf/ops/PageLabels.h
    pdf/compliance/PdfAChecker.cpp
    pdf/compliance/PdfAChecker.h
    pdf/forms/FormAutoTag.cpp
    pdf/forms/FormAutoTag.h
    pdf/convert/ImageToPdf.cpp
    pdf/convert/ImageToPdf.h
    pdf/ops/DocumentComparer.cpp
    pdf/ops/DocumentComparer.h
    pdf/content/PageDecorations.cpp
    pdf/annot/SoundFormat.cpp
    pdf/annot/AnnotationSummary.cpp
    pdf/annot/Xfdf.cpp
    pdf/annot/AnnotationDictionary.cpp
    pdf/annot/AppearanceStream.cpp
    pdf/annot/AppearanceStream.h
    pdf/document/IncrementalWriter.cpp
    pdf/document/IncrementalWriter.h
    pdf/document/DocumentWriter.cpp
    pdf/document/DocumentWriter.h
    pdf/document/QpdfBackend.cpp
    pdf/document/QpdfBackend.h
    pdf/security/PdfSecurity.cpp
    pdf/security/PdfSecurity.h
    pdf/document/PdfRevisionCounter.cpp
    pdf/document/PdfRevisionCounter.h
    pdf/document/Navigation.cpp
    pdf/document/Navigation.h
    pdf/kernel/TextPage.cpp
    pdf/kernel/TextPage.h
    ai/ChatTypes.cpp
    ai/ChatTypes.h
    ai/CredentialStore.cpp
    ai/CredentialStore.h
    ai/ProviderConfig.cpp
    ai/ProviderConfig.h
    ai/ProviderPreset.cpp
    ai/ProviderPreset.h
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
    doc/DocumentStatistics.cpp
    doc/DocumentStatistics.h
    doc/BatchProcessor.cpp
    doc/BatchProcessor.h
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
    speech/SpeechText.h
    speech/SpeechText.cpp
    speech/SpeechSettings.h
    speech/SpeechSettings.cpp
    speech/SpeechEngine.h
    speech/SpeechEngine.cpp
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
    winmm
    ${AIPDF_PDFIUM_LIBRARY}
)

# Windows 凭据管理器（API 密钥）：advapi32。
# 验签的证书链解析走 crypt32，证书 SHA-1 指纹走 bcrypt。
if (WIN32)
    target_link_libraries(AiPdfEditor PRIVATE advapi32 crypt32 bcrypt)
    # OneNote 导出走 COM 晚绑定（CoCreateInstance + IDispatch）。
    # Qt6::Core 的接口库目前已经带了这两个，但显式写出来更稳：
    # Qt 哪天改成动态库、接口库不再传递它们时，链接不会莫名其妙地断。
    target_link_libraries(AiPdfEditor PRIVATE ole32 oleaut32)
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
set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/version.rc APPEND PROPERTY
    OBJECT_DEPENDS "${AIPDF_APP_ICON}")

aipdf_deploy_qt_runtime(AiPdfEditor)
aipdf_deploy_pdfium(AiPdfEditor)

