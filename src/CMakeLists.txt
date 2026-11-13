qt_add_executable(AiPdfEditor WIN32
    app/main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
    app/Theme.cpp
    app/Theme.h
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
    doc/AnnotationModel.cpp
    doc/AnnotationModel.h
    doc/EditCommand.cpp
    doc/EditCommand.h
    doc/EditContext.h
    doc/PageCommand.cpp
    doc/PageCommand.h
    doc/RecoveryLog.cpp
    doc/RecoveryLog.h
    doc/SafeSave.cpp
    doc/SafeSave.h
    doc/UndoStack.cpp
    doc/UndoStack.h
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
        app/icons/send.svg
)

target_link_libraries(AiPdfEditor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    ${AIPDF_PDFIUM_LIBRARY}
)

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

