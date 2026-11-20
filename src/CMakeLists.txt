add_library(aipdf_app_tasks STATIC
    app/controllers/ExternalProcessTask.cpp
    app/controllers/ExternalProcessTask.h
    app/controllers/OfdBatchConverter.cpp
    app/controllers/OfdBatchConverter.h
)
target_link_libraries(aipdf_app_tasks PUBLIC Qt6::Core)
target_include_directories(aipdf_app_tasks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_executable(AiPdfEditor WIN32
    app/shell/main.cpp
    app/shell/MainWindow.cpp
    app/shell/MainWindow.h
    app/dialogs/FormScriptDialog.h
    app/dialogs/FormScriptDialog.cpp
    app/dialogs/FormToolSettingsDialog.h
    app/dialogs/FormToolSettingsDialog.cpp
    app/dialogs/FormDesignAssistantDialog.h
    app/dialogs/FormDesignAssistantDialog.cpp
    app/dialogs/ThreeDViewerDialog.h
    app/dialogs/ThreeDViewerDialog.cpp
    app/dialogs/TextBlockMergeDialog.h
    app/dialogs/TextBlockMergeDialog.cpp
    app/util/WorkerTranslationProvider.h
    app/util/WorkerTranslationProvider.cpp
    app/ui/Language.cpp
    app/ui/Language.h
    app/ui/LanguagePackInstaller.cpp
    i18n/Translate.h
    app/ui/Theme.cpp
    app/ui/Theme.h
    app/ui/ModalScrim.cpp
    app/ui/ModalScrim.h
    app/ui/ReadingScheme.cpp
    app/ui/ReadingScheme.h
    app/ui/BrandMark.cpp
    app/ui/BrandMark.h
    app/shell/WelcomePage.cpp
    app/shell/WelcomePage.h
    app/editor/PdfView.h
    app/editor/PdfView.cpp
    app/editor/PageCanvas.h
    app/editor/PageCanvas.cpp
    app/layout/PageLayout.h
    app/layout/PageLayout.cpp
    app/layout/PageTransform.h
    app/layout/PageTransform.cpp
    app/layout/TextLayout.h
    app/layout/TextLayout.cpp
    app/editor/MouseShortcuts.h
    app/editor/MouseShortcuts.cpp
    app/controllers/TextController.h
    app/controllers/TextController.cpp
    app/controllers/NavigationController.h
    app/controllers/NavigationController.cpp
    app/controllers/AnnotationController.h
    app/controllers/AnnotationController.cpp
    app/layout/PdfPageOperations.h
    app/layout/PageChunkBuilder.h
    app/layout/PdfPageOperations.cpp
    app/layout/PageChunkBuilder.cpp
    app/layout/PageTextLines.h
    app/layout/PageTextLines.cpp
    app/layout/LinkHitTest.h
    app/layout/LinkHitTest.cpp
    app/panels/SearchPanel.h
    app/panels/SearchPanel.cpp
    app/panels/AiPanel.h
    app/panels/AiPanel.cpp
    app/dialogs/AiSettingsDialog.h
    app/dialogs/AiSettingsDialog.cpp
    app/dialogs/SettingsDialog.h
    app/dialogs/SettingsDialog.cpp
    app/dialogs/EncryptionDialog.h
    app/dialogs/EncryptionDialog.cpp
    app/dialogs/ViewSettingsDialog.h
    app/dialogs/ViewSettingsDialog.cpp
    app/dialogs/SignatureDialog.h
    app/dialogs/SignatureDialog.cpp
    app/shell/RibbonBar.h
    app/shell/RibbonBar.cpp
    app/controllers/LastWaveWriter.cpp
    app/controllers/LastWaveWriter.h
    app/panels/PdfAPreflightPanel.cpp
    app/panels/PdfAPreflightPanel.h
    app/dialogs/HiddenDataDialog.cpp
    app/dialogs/HiddenDataDialog.h
    app/dialogs/BookmarkTreeDialog.cpp
    app/dialogs/BookmarkTreeDialog.h
    app/controllers/OneNoteExporter.cpp
    app/controllers/OneNoteExporter.h
    app/controllers/PresentationLoop.cpp
    app/controllers/PresentationLoop.h
    app/controllers/ScannerCapture.cpp
    app/controllers/ScannerCapture.h
    app/dialogs/ConversionInputDialog.cpp
    app/dialogs/ConversionInputDialog.h
    app/dialogs/DocumentCompareDialog.cpp
    app/dialogs/DocumentCompareDialog.h
    app/panels/LastWavePanels.cpp
    app/panels/LastWavePanels.h
    app/panels/MediaPreviewPanel.cpp
    app/panels/MediaPreviewPanel.h
    app/controllers/PcmPlayer.cpp
    app/controllers/PcmPlayer.h
    app/controllers/LastWaveBridge.cpp
    app/controllers/LastWaveBridge.h
    app/shell/WiringActions.cpp
    app/shell/WiringActions.h
    app/dialogs/PrintToolsDialog.cpp
    app/dialogs/PrintToolsDialog.h
    app/dialogs/ConversionToolsDialog.cpp
    app/dialogs/ConversionToolsDialog.h
    app/util/ConversionReports.cpp
    app/util/ConversionReports.h
    app/util/ConversionExports.cpp
    app/util/ConversionExports.h
    app/util/WizardPageRange.cpp
    app/util/WizardPageRange.h
    app/dialogs/AnnotationToolboxDialog.cpp
    app/dialogs/AnnotationToolboxDialog.h
    app/dialogs/StampNumberDialog.cpp
    app/dialogs/StampNumberDialog.h
    app/dialogs/RegionPicker.cpp
    app/dialogs/RegionPicker.h
    app/dialogs/RecordingCenterDialog.cpp
    app/dialogs/RecordingCenterDialog.h
    app/controllers/ScreenCapture.cpp
    app/controllers/ScreenCapture.h
    app/recording/MotionJpegAviWriter.cpp
    app/recording/MotionJpegAviWriter.h
    app/recording/RecordingLibrary.cpp
    app/recording/RecordingLibrary.h
    app/recording/ScreenRecorder.cpp
    app/recording/ScreenRecorder.h
    app/dialogs/AccessibilityWizard.cpp
    app/dialogs/AccessibilityWizard.h
    app/dialogs/ReadingOptionsDialog.cpp
    app/dialogs/ReadingOptionsDialog.h
    app/dialogs/AiToolsDialog.cpp
    app/dialogs/AiToolsDialog.h
    app/controllers/AiToolService.cpp
    app/controllers/AiToolService.h
    app/dialogs/UploadPreviewDialog.h
    app/dialogs/UploadPreviewDialog.cpp
    app/editor/OcrController.cpp
    app/editor/OcrController.h
    app/controllers/IndexController.h
    app/controllers/IndexController.cpp
    app/panels/IndexPanel.h
    app/panels/IndexPanel.cpp
    app/panels/AttachmentPanel.h
    app/panels/AttachmentPanel.cpp
    app/controllers/DocumentQaController.h
    app/controllers/DocumentQaController.cpp
    app/controllers/DocumentOutput.h
    app/controllers/DocumentConverter.cpp
    app/controllers/DocumentConverter.h
    app/controllers/DigitalSignatureBackend.cpp
    app/controllers/DigitalSignatureBackend.h
    app/util/SearchMatcher.h
    app/editor/NavigationHistory.h
    app/editor/NavigationHistory.cpp
    app/util/SearchMatcher.cpp
    app/util/ShareUtils.cpp
    app/util/ShareUtils.h
    app/ui/SystemCjkFont.h
    app/util/BatchRename.cpp
    app/util/BatchRename.h
    app/editor/PresentationWindow.h
    app/editor/PresentationWindow.cpp
    app/editor/ViewFit.h
    app/editor/ViewFit.cpp
    app/editor/ZoomMemory.h
    app/editor/ZoomMemory.cpp
    app/editor/ViewMemory.h
    app/editor/ViewMemory.cpp
    app/layout/InkGeometry.h
    app/layout/InkGeometry.cpp
    app/layout/MeasureGeometry.h
    app/controllers/AudioRecorder.h
    app/controllers/AudioRecorder.cpp
    app/layout/MeasureGeometry.cpp
    app/controllers/DocumentOutput.cpp
    app/editor/RenderThread.h
    app/editor/RenderThread.cpp
    app/editor/TileCache.h
    app/editor/TileCache.cpp
    ipc/WorkerClient.cpp
    ipc/WorkerClient.h
    ipc/IpcProtocol.cpp
    ipc/IpcProtocol.h
    pdf/kernel/PdfiumAdapter.cpp
    pdf/kernel/PdfiumAdapter.h
    pdf/content/ContentStream.cpp
    pdf/content/ContentStream.h
    pdf/content/FontResource.cpp
    pdf/content/FontResource.h
    pdf/content/TextSplice.cpp
    pdf/content/ObjectInventory.cpp
    pdf/content/TrueTypeFace.cpp
    pdf/content/TextSplice.h
    pdf/content/ObjectInventory.h
    pdf/content/TrueTypeFace.h
    pdf/content/PageGraphics.cpp
    pdf/content/PageGraphics.h
    pdf/content/TrueTypeSubset.cpp
    pdf/content/TrueTypeSubset.h
    pdf/content/TextReflow.cpp
    pdf/content/TextReflow.h
    pdf/kernel/TableStructure.cpp
    pdf/kernel/TableStructure.h
    pdf/kernel/PageObjectModel.cpp
    pdf/kernel/PageObjectModel.h
    pdf/content/ReflowPlan.cpp
    pdf/content/ReflowPlan.h
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
    pdf/forms/FormRuntime.h
    pdf/forms/QuickJsFormScript.h
    pdf/forms/FormAppearance.h
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
    pdf/forms/FormRuntime.cpp
    pdf/forms/QuickJsFormScript.cpp
    pdf/forms/FormAppearance.cpp
    pdf/forms/FormTemplateStore.cpp
    pdf/forms/FormScriptStore.cpp
    pdf/forms/BarcodeEncoder.cpp
    pdf/forms/FormToolSettings.cpp
    pdf/forms/FormDesignParser.cpp
    pdf/forms/FormAutoDetect.cpp
    pdf/forms/FormAutoDetect.h
    pdf/document/DocumentProperties.cpp
    pdf/document/DocumentProperties.h
    doc/model/OutlineGenerator.cpp
    doc/model/OutlineGenerator.h
    pdf/ops/OutlineEditor.cpp
    pdf/ops/OutlineEditor.h
    pdf/ops/CrossReference.cpp
    pdf/ops/CrossReference.h
    pdf/ops/AnchorMover.cpp
    pdf/ops/AnchorMover.h
    pdf/ops/AnchorPlanner.cpp
    pdf/ops/AnchorPlanner.h
    pdf/ops/TablePagination.cpp
    pdf/ops/TablePagination.h
    pdf/ops/StreamingEdit.cpp
    pdf/ops/StreamingEdit.h
    pdf/ops/TranslationWriter.cpp
    pdf/ops/TranslationWriter.h
    pdf/convert/DxfExport.cpp
    pdf/convert/DxfExport.h
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
    pdf/compliance/StructTreeModel.cpp
    pdf/compliance/StructTreeModel.h
    pdf/compliance/RoleInference.cpp
    pdf/compliance/RoleInference.h
    pdf/compliance/AltTextGenerator.cpp
    pdf/compliance/AltTextGenerator.h
    pdf/compliance/SemanticTagWriter.cpp
    pdf/compliance/SemanticTagWriter.h
    pdf/compliance/UaGenerator.cpp
    pdf/compliance/UaGenerator.h
    pdf/annot/AnnotationShapes.cpp
    pdf/annot/AnnotationShapes.h
    pdf/ops/DocumentReflow.cpp
    pdf/ops/DocumentReflow.h
    pdf/annot/RichMedia3d.cpp
    pdf/annot/RichMedia3d.h
    pdf/annot/ThreeDModel.cpp
    pdf/annot/ThreeDModel.h
    pdf/forms/XfaSupport.cpp
    pdf/forms/XfaSupport.h
    pdf/forms/XfaScriptRunner.cpp
    pdf/forms/XfaScriptRunner.h
    pdf/compliance/PdfUaChecker.cpp
    pdf/compliance/PdfUaChecker.h
    doc/index/OfflineQa.cpp
    doc/index/OfflineQa.h
    doc/convert/UniversalConverter.cpp
    doc/convert/UniversalConverter.h
    pdf/compliance/StructTreeReader.cpp
    pdf/compliance/StructTreeReader.h
    pdf/content/InvisibleTextLayer.cpp
    pdf/content/InvisibleTextLayer.h
    pdf/content/ParagraphFormat.cpp
    pdf/content/ParagraphFormat.h
    pdf/ops/OptionalContent.cpp
    pdf/ops/OptionalContent.h
    doc/convert/InvoiceLayout.cpp
    doc/convert/InvoiceLayout.h
    doc/batch/ActionWizard.cpp
    doc/batch/ActionWizard.h
    doc/batch/WizardActions.cpp
    doc/batch/WizardActions.h
    pdf/ops/OcrSuspicion.cpp
    pdf/ops/OcrSuspicion.h
    pdf/convert/PortfolioWriter.cpp
    pdf/convert/PortfolioWriter.h
    pdf/compliance/PreflightChecker.cpp
    pdf/compliance/PreflightChecker.h
    pdf/convert/ExportFormats.cpp
    pdf/convert/ExportFormats.h
    doc/export/ZipWriter.cpp
    doc/export/ZipWriter.h
    doc/convert/BrowserPdfPrinter.cpp
    doc/convert/BrowserPdfPrinter.h
    doc/convert/ConversionInputs.cpp
    doc/convert/ConversionInputs.h
    doc/convert/FontFlattening.cpp
    doc/convert/FontFlattening.h
    doc/convert/ScannedPdfOptimizer.cpp
    doc/convert/ScannedPdfOptimizer.h
    doc/export/OfdExport.cpp
    doc/export/OfdExport.h
    doc/export/OfficeExport.cpp
    doc/export/OfficeExport.h
    doc/export/OfficeMedia.cpp
    doc/export/OfficeMedia.h
    doc/export/PdfToOffice.cpp
    doc/export/PdfToOffice.h
    doc/export/PptxWriter.cpp
    doc/export/PptxWriter.h
    doc/export/SpreadsheetExport.cpp
    doc/export/SpreadsheetExport.h
    doc/export/XmlText.h
    pdf/annot/AnnotationToolkit.cpp
    pdf/annot/AnnotationToolkit.h
    pdf/annot/AnnotationFilter.cpp
    pdf/annot/AnnotationFilter.h
    app/layout/ViewModes.cpp
    app/layout/ViewModes.h
    doc/print/PrintTemplate.cpp
    doc/print/PrintTemplate.h
    doc/print/PrintBatch.cpp
    doc/print/PrintBatch.h
    doc/model/BlankDocument.cpp
    doc/model/BlankDocument.h
    ai/content/PptGeneration.cpp
    ai/content/PptGeneration.h
    ai/content/PptxExport.cpp
    ai/content/PptxExport.h
    ai/content/AiNarration.cpp
    ai/content/AiNarration.h
    ai/content/AiContentDetection.cpp
    ai/content/AiContentDetection.h
    ai/content/AiImageGeneration.cpp
    ai/content/AiImageGeneration.h
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
    pdf/compliance/VeraPdfChecker.cpp
    pdf/compliance/VeraPdfChecker.h
    pdf/security/CertificateChain.cpp
    pdf/security/CertificateChain.h
    pdf/media/MediaSource.cpp
    pdf/media/MediaSource.h
    pdf/media/MediaPlayback.cpp
    pdf/media/MediaPlayback.h
    pdf/forms/FormAutoTag.cpp
    pdf/forms/FormAutoTag.h
    pdf/convert/ImageToPdf.cpp
    pdf/convert/ImageToPdf.h
    pdf/ops/DocumentComparer.cpp
    pdf/ops/DocumentComparer.h
    pdf/kernel/LayoutBlocks.cpp
    pdf/kernel/LayoutBlocks.h
    pdf/ops/LayoutDiff.cpp
    pdf/ops/LayoutDiff.h
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
    pdf/kernel/PdfRevisionCounter.cpp
    pdf/kernel/PdfRevisionCounter.h
    pdf/kernel/Navigation.cpp
    pdf/kernel/Navigation.h
    pdf/kernel/TextPage.cpp
    pdf/kernel/TextPage.h
    ai/chat/ChatTypes.cpp
    ai/chat/ChatTypes.h
    ai/settings/CredentialStore.cpp
    ai/settings/CredentialStore.h
    ai/settings/ProviderConfig.cpp
    ai/settings/ProviderConfig.h
    ai/settings/ProviderPreset.cpp
    ai/settings/ProviderPreset.h
    ai/settings/ProviderSelection.cpp
    ai/settings/ProviderSelection.h
    ai/settings/UploadPreview.cpp
    ai/settings/UploadPreview.h
    ai/settings/DataPolicy.cpp
    ai/settings/DataPolicy.h
    ai/settings/AuditLog.cpp
    ai/settings/AuditLog.h
    doc/index/EmbeddingProvider.h
    doc/index/LocalHashEmbedding.cpp
    doc/index/LocalHashEmbedding.h
    doc/edit/AnnotationModel.cpp
    doc/edit/AnnotationModel.h
    doc/model/ChunkBuilder.cpp
    doc/model/ChunkBuilder.h
    doc/model/DocumentChunk.h
    doc/model/DocumentStatistics.cpp
    doc/model/DocumentStatistics.h
    doc/batch/BatchProcessor.cpp
    doc/batch/BatchProcessor.h
    doc/edit/EditCommand.cpp
    doc/edit/EditCommand.h
    doc/edit/EditContext.h
    doc/index/CitationVerifier.cpp
    doc/index/CitationVerifier.h
    doc/index/HybridRetriever.cpp
    doc/index/HybridRetriever.h
    doc/index/LocalIndex.cpp
    doc/index/LocalIndex.h
    doc/edit/PageCommand.cpp
    doc/edit/PageCommand.h
    doc/persist/RecoveryLog.cpp
    doc/persist/RecoveryLog.h
    pdf/kernel/SafeSave.cpp
    pdf/kernel/SafeSave.h
    doc/edit/UndoStack.cpp
    doc/edit/UndoStack.h
    doc/index/VectorIndex.cpp

    ocr/core/OcrEngine.h
    ocr/core/OcrTypes.h
    ocr/core/OcrGeometry.cpp
    ocr/core/OcrGeometry.h
    ocr/pipeline/ScannedPageDetector.cpp
    ocr/pipeline/ScannedPageDetector.h
    ocr/pipeline/OcrService.cpp
    ocr/pipeline/OcrService.h
    ocr/pipeline/ImagePreprocess.cpp
    ocr/pipeline/ImagePreprocess.h
    ocr/pipeline/OcrChunkBridge.cpp
    ocr/pipeline/OcrChunkBridge.h
    ocr/engines/TesseractCliEngine.cpp
    ocr/engines/TesseractCliEngine.h
    ocr/engines/OcrEngineFactory.cpp
    ocr/engines/OcrEngineFactory.h
    ocr/engines/WindowsOcrEngine.cpp
    ocr/engines/WindowsOcrEngine.h
    ocr/core/OcrTextUtils.cpp
    ocr/core/OcrTextUtils.h
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
    app/controllers/ProposalRunner.cpp
    app/controllers/ProposalRunner.h
    app/dialogs/EditProposalDialog.cpp
    app/dialogs/EditProposalDialog.h
    app/util/BlankPageDetector.cpp
    app/util/BlankPageDetector.h
    doc/index/VectorIndex.h
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
    aipdf_app_tasks
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
    # Media Foundation（音视频解码，A11）：mfplat 是入口（MFStartup / 媒体类型），
    # mfreadwrite 是源读取器，mf 是解码器与视频处理。
    # 都是 Windows 自带的系统库，不引入新第三方依赖。
    # MF 的 GUID 在 MediaSource.cpp 里用 INITGUID 本地定义，不需要额外的 GUID 库。
    target_link_libraries(AiPdfEditor PRIVATE mf mfplat mfreadwrite)
    # OneNote 导出走 COM 晚绑定（CoCreateInstance + IDispatch）。
    # Qt6::Core 的接口库目前已经带了这两个，但显式写出来更稳：
    # Qt 哪天改成动态库、接口库不再传递它们时，链接不会莫名其妙地断。
    target_link_libraries(AiPdfEditor PRIVATE ole32 oleaut32)
endif()

target_include_directories(AiPdfEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(AiPdfEditor PRIVATE ${AIPDF_PDFIUM_INCLUDE_DIR})
target_compile_definitions(AiPdfEditor PRIVATE AIPDF_VERSION="${PROJECT_VERSION}")

# qpdf 是必需依赖：根作用域已经确认找到，这里无条件链接并打开编译开关。
# 不再有「没有 qpdf 的构建」——所有 #ifdef AIPDF_WITH_QPDF 的降级分支从此不可达。
target_link_libraries(AiPdfEditor PRIVATE qpdf::libqpdf)
target_compile_definitions(AiPdfEditor PRIVATE AIPDF_WITH_QPDF=1)
# QuickJS 同样是必需依赖：表单脚本引擎，源码一起编译（见根 CMakeLists）。
target_link_libraries(AiPdfEditor PRIVATE aipdf_quickjs)
message(STATUS "qpdf ${qpdf_VERSION}: 结构校验与对象级读写已启用")
# 运行时需要 qpdf30.dll 与主程序同目录，否则程序根本起不来。
add_custom_command(TARGET AiPdfEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:AiPdfEditor>"
    COMMENT "Copying qpdf runtime")

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

