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

qt_add_executable(ContentEditingTests
    ContentEditingTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TextReflow.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TextReflow.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TableStructure.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TableStructure.h
)
target_link_libraries(ContentEditingTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(ContentEditingTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(ContentEditingTests PRIVATE
        AIPDF_WITH_QPDF=1
        AIPDF_PDF_FIXTURES_DIR="${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    add_custom_command(TARGET ContentEditingTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:ContentEditingTests>")
    add_dependencies(ContentEditingTests pdf-fixtures)
endif()
target_include_directories(ContentEditingTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ContentEditingTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(ContentEditingTests)
add_test(NAME ContentEditingTests COMMAND ContentEditingTests)

qt_add_executable(ShareUtilsTests
    ShareUtilsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/util/ShareUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/app/util/ShareUtils.h
)
target_link_libraries(ShareUtilsTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(ShareUtilsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ShareUtilsTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(ShareUtilsTests)
add_test(NAME ShareUtilsTests COMMAND ShareUtilsTests)

qt_add_executable(BatchRenameTests
    BatchRenameTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/util/BatchRename.cpp
    ${CMAKE_SOURCE_DIR}/src/app/util/BatchRename.h
)
target_link_libraries(BatchRenameTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(BatchRenameTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(BatchRenameTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(BatchRenameTests)
add_test(NAME BatchRenameTests COMMAND BatchRenameTests)

qt_add_executable(DocumentConverterTests
    DocumentConverterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/DocumentConverter.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/DocumentConverter.h
)
target_link_libraries(DocumentConverterTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(DocumentConverterTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(DocumentConverterTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(DocumentConverterTests)
add_test(NAME DocumentConverterTests COMMAND DocumentConverterTests)

qt_add_executable(DigitalSignatureBackendTests
    DigitalSignatureBackendTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/DigitalSignatureBackend.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/DigitalSignatureBackend.h
)
target_link_libraries(DigitalSignatureBackendTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(DigitalSignatureBackendTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(DigitalSignatureBackendTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(DigitalSignatureBackendTests)
add_test(NAME DigitalSignatureBackendTests COMMAND DigitalSignatureBackendTests)

# ------------------------------------------------------------ 国际化底座 ----
#
# .ts 目录在主程序里是资源，测试要自己再打一份，才能验证「目录真的进包了」。

qt_add_executable(I18nTests
    I18nTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Language.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Language.h
    ${CMAKE_SOURCE_DIR}/src/i18n/Translate.h
)

target_link_libraries(I18nTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(I18nTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(I18nTests PROPERTIES WIN32_EXECUTABLE FALSE)

file(GLOB AIPDF_TEST_I18N_CATALOGS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/i18n/*.ts")
qt_add_resources(I18nTests "i18n_test_catalogs"
    PREFIX "/i18n"
    BASE "${CMAKE_SOURCE_DIR}/src/i18n"
    FILES ${AIPDF_TEST_I18N_CATALOGS}
)

aipdf_deploy_qt_runtime(I18nTests)

add_test(NAME I18nTests COMMAND I18nTests)

# 覆盖率守卫：源码里每一条 tr()/translate()/QT_TRANSLATE_NOOP 都要有英文翻译。
# 用 Python 跑，CI 与本地一条命令都能复现。Python3 在这里单独找一次：
# 文件后半段（PDF 夹具）才 find_package，此处用得到这个变量。
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME I18nCoverageTests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check-i18n.py
            --src ${CMAKE_SOURCE_DIR}/src
            --ts ${CMAKE_SOURCE_DIR}/src/i18n/ai-pdf-editor_en.ts
)
set_tests_properties(I18nCoverageTests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 窗口级：真建一个控件、切语言、看文字有没有就地变（不重建窗口）。
qt_add_executable(I18nWidgetTests
    I18nWidgetTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/WelcomePage.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/WelcomePage.h
    ${CMAKE_SOURCE_DIR}/src/app/ui/BrandMark.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/BrandMark.h
    ${CMAKE_SOURCE_DIR}/src/app/ui/Language.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Language.h
    ${CMAKE_SOURCE_DIR}/src/i18n/Translate.h
)

target_link_libraries(I18nWidgetTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_include_directories(I18nWidgetTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(I18nWidgetTests PROPERTIES WIN32_EXECUTABLE FALSE)

file(GLOB AIPDF_WIDGET_TEST_ICON_SVGS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/app/icons/*.svg")
qt_add_resources(I18nWidgetTests "i18n_widget_test_icons"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/src/app/icons"
    FILES ${AIPDF_WIDGET_TEST_ICON_SVGS}
)
qt_add_resources(I18nWidgetTests "i18n_widget_test_catalogs"
    PREFIX "/i18n"
    BASE "${CMAKE_SOURCE_DIR}/src/i18n"
    FILES ${AIPDF_TEST_I18N_CATALOGS}
)

aipdf_deploy_qt_runtime(I18nWidgetTests)

add_test(NAME I18nWidgetTests COMMAND I18nWidgetTests)

# 加密对话框：只建控件、改状态、读回参数，不碰文档与 PDFium。
qt_add_executable(EncryptionDialogTests
    EncryptionDialogTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/EncryptionDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/EncryptionDialog.h
    ${CMAKE_SOURCE_DIR}/src/app/ui/Language.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Language.h
    ${CMAKE_SOURCE_DIR}/src/i18n/Translate.h
)
target_link_libraries(EncryptionDialogTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_include_directories(EncryptionDialogTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(EncryptionDialogTests PROPERTIES WIN32_EXECUTABLE FALSE)
qt_add_resources(EncryptionDialogTests "encryption_dialog_catalogs"
    PREFIX "/i18n"
    BASE "${CMAKE_SOURCE_DIR}/src/i18n"
    FILES ${AIPDF_TEST_I18N_CATALOGS}
)
aipdf_deploy_qt_runtime(EncryptionDialogTests)
add_test(NAME EncryptionDialogTests COMMAND EncryptionDialogTests)

# Ribbon 中央布局：固定 96 高度、标签条可见、窗口不被撑宽。
qt_add_executable(RibbonLayoutTests
    RibbonLayoutTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/RibbonBar.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/RibbonBar.h
    ${CMAKE_SOURCE_DIR}/src/app/ui/Theme.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Theme.h
)
target_link_libraries(RibbonLayoutTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_include_directories(RibbonLayoutTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(RibbonLayoutTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(RibbonLayoutTests)
add_test(NAME RibbonLayoutTests COMMAND RibbonLayoutTests)
set_tests_properties(RibbonLayoutTests PROPERTIES
    ENVIRONMENT "AIPDF_UI_SNAPSHOT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ui-snapshots"
)

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

include(${CMAKE_SOURCE_DIR}/tests/cmake/DocumentPipelineTests.cmake)

qt_add_executable(PdfiumAdapterTests
    PdfiumAdapterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormFieldWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormFieldWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/BarcodeEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/BarcodeEncoder.h
)

target_link_libraries(PdfiumAdapterTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(PdfiumAdapterTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(PdfiumAdapterTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET PdfiumAdapterTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:PdfiumAdapterTests>"
    )
endif()
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

# ------------------------------------------------------------ PDF 安全 ----
# AES-256 加密与权限位依赖 qpdf；没有 qpdf 的构建不编这组测试。

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    qt_add_executable(PdfSecurityTests
        PdfSecurityTests.cpp
        ${CMAKE_SOURCE_DIR}/src/pdf/security/PdfSecurity.cpp
        ${CMAKE_SOURCE_DIR}/src/pdf/security/PdfSecurity.h
        ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfRevisionCounter.cpp
        ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfRevisionCounter.h
        ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
        ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
        ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
        ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
        ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
        ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    )

    target_link_libraries(PdfSecurityTests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Test
        ${AIPDF_PDFIUM_LIBRARY}
        qpdf::libqpdf
    )
    target_include_directories(PdfSecurityTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${AIPDF_PDFIUM_INCLUDE_DIR}
    )
    target_compile_definitions(PdfSecurityTests PRIVATE
        AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
        AIPDF_WITH_QPDF=1
    )
    set_target_properties(PdfSecurityTests PROPERTIES WIN32_EXECUTABLE FALSE)
    add_dependencies(PdfSecurityTests pdf-fixtures)
    aipdf_deploy_qt_runtime(PdfSecurityTests)
    aipdf_deploy_pdfium(PdfSecurityTests)
    add_custom_command(TARGET PdfSecurityTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:PdfSecurityTests>"
        COMMENT "Copying qpdf runtime for tests")

    add_test(NAME PdfSecurityTests COMMAND PdfSecurityTests)
endif()

# ------------------------------------------------------------ 视口与缓存 ----

qt_add_executable(PdfViewportTests
    PdfViewportTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageLayout.h
    ${CMAKE_SOURCE_DIR}/src/app/editor/TileCache.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/TileCache.h
)

target_link_libraries(PdfViewportTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(PdfViewportTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(PdfViewportTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PdfViewportTests)

add_test(NAME PdfViewportTests COMMAND PdfViewportTests)

# ------------------------------------------------------------ 文本与选区 ----

qt_add_executable(TextSelectionTests
    TextSelectionTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/TextLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/TextLayout.h
)

target_link_libraries(TextSelectionTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(TextSelectionTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(TextSelectionTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(TextSelectionTests)

add_test(NAME TextSelectionTests COMMAND TextSelectionTests)

# 鼠标快捷操作：滚轮手势、缩放倍率、连击计数与锚点滚动都是纯函数，
# 这里逐条钉死，界面层只负责接线。
qt_add_executable(MouseShortcutsTests
    MouseShortcutsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/MouseShortcuts.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/MouseShortcuts.h
)

target_link_libraries(MouseShortcutsTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(MouseShortcutsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(MouseShortcutsTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(MouseShortcutsTests)

add_test(NAME MouseShortcutsTests COMMAND MouseShortcutsTests)

qt_add_executable(TextExtractionTests
    TextExtractionTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.h
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
    ${CMAKE_SOURCE_DIR}/src/app/layout/LinkHitTest.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/LinkHitTest.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.h
)

target_link_libraries(NavigationTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(NavigationTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(NavigationTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(NavigationTests)

add_test(NAME NavigationTests COMMAND NavigationTests)

qt_add_executable(PdfNavigationTests
    PdfNavigationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OutlineWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OutlineWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
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
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditContext.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/PageCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/PageCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/UndoStack.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/UndoStack.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(AnnotationTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(AnnotationTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AnnotationTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(AnnotationTests)

add_test(NAME AnnotationTests COMMAND AnnotationTests)

qt_add_executable(PdfAnnotationTests
    PdfAnnotationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/app/controllers/AnnotationController.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/SoundFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/PageAnnotsInjector.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/MeasureGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/AnnotationController.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PdfPageOperations.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PdfPageOperations.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditContext.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/PageCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/PageCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/UndoStack.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/UndoStack.h
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

# ------------------------------------------------------------ 批注外观流 ----
#
# 纯函数：不吃文档、不碰 PDFium，测的是"生成的 content stream 会不会越界、
# 该透明的透没透、该描边的有没有描"。

qt_add_executable(AppearanceStreamTests
    AppearanceStreamTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AppearanceStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AppearanceStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(AppearanceStreamTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(AppearanceStreamTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AppearanceStreamTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(AppearanceStreamTests)

add_test(NAME AppearanceStreamTests COMMAND AppearanceStreamTests)

# ------------------------------------------------------------ 增量写入 ----

qt_add_executable(IncrementalWriterTests
    IncrementalWriterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/IncrementalWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/IncrementalWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AppearanceStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AppearanceStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(IncrementalWriterTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(IncrementalWriterTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(IncrementalWriterTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(IncrementalWriterTests PROPERTIES WIN32_EXECUTABLE FALSE)
add_dependencies(IncrementalWriterTests pdf-fixtures)
aipdf_deploy_qt_runtime(IncrementalWriterTests)
aipdf_deploy_pdfium(IncrementalWriterTests)

add_test(NAME IncrementalWriterTests COMMAND IncrementalWriterTests)

# -------------------------------------------------------- 保存路径（外观流） ----

qt_add_executable(DocumentWriterTests
    DocumentWriterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/DocumentWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/PageAnnotsInjector.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/PageAnnotsInjector.h
    ${CMAKE_SOURCE_DIR}/src/pdf/security/PdfSecurity.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/security/PdfSecurity.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfRevisionCounter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfRevisionCounter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationDictionary.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationDictionary.h
    ${CMAKE_SOURCE_DIR}/src/pdf/document/DocumentWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/document/QpdfBackend.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/QpdfBackend.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/SafeSave.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/SafeSave.h
    ${CMAKE_SOURCE_DIR}/src/pdf/document/IncrementalWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/IncrementalWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AppearanceStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AppearanceStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(DocumentWriterTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(DocumentWriterTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(DocumentWriterTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(DocumentWriterTests PROPERTIES WIN32_EXECUTABLE FALSE)
add_dependencies(DocumentWriterTests pdf-fixtures)
aipdf_deploy_qt_runtime(DocumentWriterTests)
aipdf_deploy_pdfium(DocumentWriterTests)

# 开了 qpdf 就让这组测试也走真实的 qpdf 校验，否则测的只是空操作。
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(DocumentWriterTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(DocumentWriterTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET DocumentWriterTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:DocumentWriterTests>"
        COMMENT "Copying qpdf runtime for tests")
endif()

add_test(NAME DocumentWriterTests COMMAND DocumentWriterTests)

# ------------------------------------------------------- 页面注释注入 ----

qt_add_executable(PageAnnotsInjectorTests
    PageAnnotsInjectorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/PageAnnotsInjector.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/PageAnnotsInjector.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(PageAnnotsInjectorTests PRIVATE
    Qt6::Gui
    Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(PageAnnotsInjectorTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tools/pdfium/include
)
target_compile_definitions(PageAnnotsInjectorTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(PageAnnotsInjectorTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PageAnnotsInjectorTests)
aipdf_deploy_pdfium(PageAnnotsInjectorTests)
add_dependencies(PageAnnotsInjectorTests pdf-fixtures)

add_test(NAME PageAnnotsInjectorTests COMMAND PageAnnotsInjectorTests)
# ------------------------------------------------------------ 录音 --------

qt_add_executable(AudioRecorderTests
    AudioRecorderTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/AudioRecorder.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/AudioRecorder.h
)

target_link_libraries(AudioRecorderTests PRIVATE Qt6::Core Qt6::Test winmm)
target_include_directories(AudioRecorderTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AudioRecorderTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(AudioRecorderTests)

add_test(NAME AudioRecorderTests COMMAND AudioRecorderTests)
# ------------------------------------------------------------ 表单数据 ----

qt_add_executable(FormDataTests
    FormDataTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormData.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormData.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(FormDataTests PRIVATE
    Qt6::Gui
    Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(FormDataTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tools/pdfium/include
)
set_target_properties(FormDataTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(FormDataTests)
aipdf_deploy_pdfium(FormDataTests)

add_test(NAME FormDataTests COMMAND FormDataTests)
# ------------------------------------------------------------ 页面装饰 ----

qt_add_executable(PageDecorationsTests
    PageDecorationsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageDecorations.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageDecorations.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(PageDecorationsTests PRIVATE
    Qt6::Gui
    Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(PageDecorationsTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tools/pdfium/include
)
target_compile_definitions(PageDecorationsTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(PageDecorationsTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PageDecorationsTests)
aipdf_deploy_pdfium(PageDecorationsTests)
add_dependencies(PageDecorationsTests pdf-fixtures)

add_test(NAME PageDecorationsTests COMMAND PageDecorationsTests)
# ------------------------------------------------------------ WAV 解析 ----

qt_add_executable(SoundFormatTests
    SoundFormatTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/SoundFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/SoundFormat.h
)

target_link_libraries(SoundFormatTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(SoundFormatTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(SoundFormatTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(SoundFormatTests)

add_test(NAME SoundFormatTests COMMAND SoundFormatTests)
# ------------------------------------------------------------ 批注摘要 ----

qt_add_executable(AnnotationSummaryTests
    AnnotationSummaryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationSummary.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationSummary.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(AnnotationSummaryTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(AnnotationSummaryTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AnnotationSummaryTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(AnnotationSummaryTests)

add_test(NAME AnnotationSummaryTests COMMAND AnnotationSummaryTests)
# ------------------------------------------------------------ 测量几何 ----

qt_add_executable(MeasureGeometryTests
    MeasureGeometryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/MeasureGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/MeasureGeometry.h
)

target_link_libraries(MeasureGeometryTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(MeasureGeometryTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(MeasureGeometryTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(MeasureGeometryTests)

add_test(NAME MeasureGeometryTests COMMAND MeasureGeometryTests)
# ------------------------------------------------------------ XFDF ----

qt_add_executable(XfdfTests
    XfdfTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Xfdf.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Xfdf.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(XfdfTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(XfdfTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(XfdfTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(XfdfTests)

add_test(NAME XfdfTests COMMAND XfdfTests)
# ------------------------------------------------------------ 批注字典 ----

qt_add_executable(AnnotationDictionaryTests
    AnnotationDictionaryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationDictionary.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationDictionary.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(AnnotationDictionaryTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(AnnotationDictionaryTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(AnnotationDictionaryTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(AnnotationDictionaryTests)

add_test(NAME AnnotationDictionaryTests COMMAND AnnotationDictionaryTests)

# ------------------------------------------------------------ 橡皮几何 ----

qt_add_executable(InkGeometryTests
    InkGeometryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/InkGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/InkGeometry.h
)

target_link_libraries(InkGeometryTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(InkGeometryTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(InkGeometryTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(InkGeometryTests)

add_test(NAME InkGeometryTests COMMAND InkGeometryTests)

# ------------------------------------------------------------ 缩放记忆 ----

qt_add_executable(ZoomMemoryTests
    ZoomMemoryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/ZoomMemory.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/ZoomMemory.h
)

target_link_libraries(ZoomMemoryTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(ZoomMemoryTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ZoomMemoryTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(ZoomMemoryTests)

add_test(NAME ZoomMemoryTests COMMAND ZoomMemoryTests)

# ------------------------------------------------------------ 放映适配 ----

qt_add_executable(ViewFitTests
    ViewFitTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/ViewFit.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/ViewFit.h
)

target_link_libraries(ViewFitTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(ViewFitTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ViewFitTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(ViewFitTests)

add_test(NAME ViewFitTests COMMAND ViewFitTests)

# ------------------------------------------------------------ 视图历史 ----

qt_add_executable(NavigationHistoryTests
    NavigationHistoryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/NavigationHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/NavigationHistory.h
)

target_link_libraries(NavigationHistoryTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(NavigationHistoryTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(NavigationHistoryTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(NavigationHistoryTests)

add_test(NAME NavigationHistoryTests COMMAND NavigationHistoryTests)

# ------------------------------------------------------------ 检索匹配 ----

qt_add_executable(SearchMatcherTests
    SearchMatcherTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/util/SearchMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/app/util/SearchMatcher.h
)

target_link_libraries(SearchMatcherTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(SearchMatcherTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(SearchMatcherTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(SearchMatcherTests)

add_test(NAME SearchMatcherTests COMMAND SearchMatcherTests)

# ------------------------------------------------------------ 导出与打印 ----

qt_add_executable(DocumentOutputTests
    DocumentOutputTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/DocumentOutput.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/DocumentOutput.h
    ${CMAKE_SOURCE_DIR}/src/pdf/document/DocumentWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/PageAnnotsInjector.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/PageAnnotsInjector.h
    ${CMAKE_SOURCE_DIR}/src/pdf/security/PdfSecurity.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/security/PdfSecurity.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfRevisionCounter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfRevisionCounter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationDictionary.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AnnotationDictionary.h
    ${CMAKE_SOURCE_DIR}/src/pdf/document/DocumentWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/document/QpdfBackend.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/QpdfBackend.h
    ${CMAKE_SOURCE_DIR}/src/pdf/document/IncrementalWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/document/IncrementalWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AppearanceStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/AppearanceStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/SafeSave.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/SafeSave.h
)

target_link_libraries(DocumentOutputTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test Qt6::PrintSupport
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(DocumentOutputTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(DocumentOutputTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(DocumentOutputTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(DocumentOutputTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET DocumentOutputTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:DocumentOutputTests>"
        COMMENT "Copying qpdf runtime for tests")
endif()
set_target_properties(DocumentOutputTests PROPERTIES WIN32_EXECUTABLE FALSE)
add_dependencies(DocumentOutputTests pdf-fixtures)
aipdf_deploy_qt_runtime(DocumentOutputTests)
aipdf_deploy_pdfium(DocumentOutputTests)

add_test(NAME DocumentOutputTests COMMAND DocumentOutputTests)

# ---------------------------------------------------- 页面管理与安全保存 ----

qt_add_executable(PageEditingTests
    PageEditingTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageDecorations.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/PageDecorations.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TableStructure.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TableStructure.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TableIntegration.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/AnnotationController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/AnnotationController.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PdfPageOperations.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PdfPageOperations.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditContext.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/PageCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/PageCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/UndoStack.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/UndoStack.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/SoundFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/PageAnnotsInjector.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/MeasureGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormFieldWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormFieldWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/BarcodeEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/BarcodeEncoder.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormScriptStore.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormScriptStore.h
)

target_link_libraries(PageEditingTests PRIVATE
    Qt6::Gui
    Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(PageEditingTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tools/pdfium/include
)
target_compile_definitions(PageEditingTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
# 表单脚本写回走 qpdf 的对象级接口：开了 qpdf 才测得到写回与 PDFium 往返。
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(PageEditingTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(PageEditingTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET PageEditingTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:PageEditingTests>"
        COMMENT "Copying qpdf runtime for tests")
endif()
set_target_properties(PageEditingTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PageEditingTests)
aipdf_deploy_pdfium(PageEditingTests)
add_dependencies(PageEditingTests pdf-fixtures)

add_test(NAME PageEditingTests COMMAND PageEditingTests)

# ---------------------------------------------- 表单域 JS 子集求值器 ----

qt_add_executable(FormJsEvaluatorTests
    FormJsEvaluatorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormJsEvaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormJsEvaluator.h
)

target_link_libraries(FormJsEvaluatorTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(FormJsEvaluatorTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(FormJsEvaluatorTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(FormJsEvaluatorTests)

add_test(NAME FormJsEvaluatorTests COMMAND FormJsEvaluatorTests)

# ------------------------------------------------------ 表单模板仓库 ----

qt_add_executable(FormTemplateStoreTests
    FormTemplateStoreTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormTemplateStore.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormTemplateStore.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormFieldWriter.h
)

target_link_libraries(FormTemplateStoreTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(FormTemplateStoreTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(FormTemplateStoreTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(FormTemplateStoreTests)

add_test(NAME FormTemplateStoreTests COMMAND FormTemplateStoreTests)

# ------------------------------------------------ 表单脚本与默认值读写 ----

qt_add_executable(FormScriptStoreTests
    FormScriptStoreTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormScriptStore.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormScriptStore.h
)

target_link_libraries(FormScriptStoreTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(FormScriptStoreTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(FormScriptStoreTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(FormScriptStoreTests)
# 写脚本走 qpdf 的对象级接口：开了 qpdf 才测得到写回，没开就只测读取。
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(FormScriptStoreTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(FormScriptStoreTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET FormScriptStoreTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:FormScriptStoreTests>"
        COMMENT "Copying qpdf runtime")
endif()

add_test(NAME FormScriptStoreTests COMMAND FormScriptStoreTests)

# ---------------------------------------------------------- Code128 条形码 ----

qt_add_executable(BarcodeEncoderTests
    BarcodeEncoderTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/BarcodeEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/BarcodeEncoder.h
)

target_link_libraries(BarcodeEncoderTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(BarcodeEncoderTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(BarcodeEncoderTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(BarcodeEncoderTests)

add_test(NAME BarcodeEncoderTests COMMAND BarcodeEncoderTests)

# ------------------------------------------------------ 表单建域写入 ----

qt_add_executable(FormFieldWriterTests
    FormFieldWriterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormFieldWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/FormFieldWriter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/BarcodeEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/BarcodeEncoder.h
)

target_link_libraries(FormFieldWriterTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(FormFieldWriterTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(FormFieldWriterTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(FormFieldWriterTests)

add_test(NAME FormFieldWriterTests COMMAND FormFieldWriterTests)

qt_add_executable(PageOperationTests
    PageOperationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/app/controllers/AnnotationController.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/SoundFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/PageAnnotsInjector.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/MeasureGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/AnnotationController.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PdfPageOperations.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PdfPageOperations.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditContext.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/PageCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/PageCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/SafeSave.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/SafeSave.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/UndoStack.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/UndoStack.h
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
    ${CMAKE_SOURCE_DIR}/src/doc/persist/RecoveryLog.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/persist/RecoveryLog.h
)

target_link_libraries(RecoveryLogTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(RecoveryLogTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(RecoveryLogTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(RecoveryLogTests)

add_test(NAME RecoveryLogTests COMMAND RecoveryLogTests)

# ---------------------------------------------------------- 文档索引底座 ----

qt_add_executable(DocumentIndexTests
    DocumentIndexTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/DocumentChunk.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.h
)

target_link_libraries(DocumentIndexTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(DocumentIndexTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(DocumentIndexTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(DocumentIndexTests)

add_test(NAME DocumentIndexTests COMMAND DocumentIndexTests)

qt_add_executable(PdfIndexTests
    PdfIndexTests.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/TextLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/TextLayout.h
    ${CMAKE_SOURCE_DIR}/src/app/controllers/IndexController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/IndexController.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/DocumentChunk.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.h
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
    ${CMAKE_SOURCE_DIR}/src/app/editor/RenderThread.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/RenderThread.h
    ${CMAKE_SOURCE_DIR}/src/app/editor/TileCache.cpp
    ${CMAKE_SOURCE_DIR}/src/app/editor/TileCache.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
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
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ChatTypes.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/CredentialStore.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderConfig.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/DataPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/DataPolicy.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderPreset.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderPreset.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/UploadPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/UploadPreview.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/AuditLog.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/AuditLog.h
    ${CMAKE_SOURCE_DIR}/src/ai/chat/SseParser.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/chat/SseParser.h
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
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ChatTypes.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/EmbeddingProvider.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalHashEmbedding.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalHashEmbedding.h
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ModelProvider.h
    ${CMAKE_SOURCE_DIR}/src/ai/provider/MockProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/provider/MockProvider.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/CitationVerifier.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/CitationVerifier.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/DocumentChunk.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/HybridRetriever.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/HybridRetriever.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/VectorIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/VectorIndex.h
)

target_link_libraries(DocumentQaTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(DocumentQaTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(DocumentQaTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(DocumentQaTests)

add_test(NAME DocumentQaTests COMMAND DocumentQaTests)

# ------------------------------------------------- AI 面板的交互守卫 ----

qt_add_executable(AiPanelTests
    AiPanelTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/panels/AiPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Language.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/ModalScrim.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/ModalScrim.h
    ${CMAKE_SOURCE_DIR}/src/app/ui/Theme.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Theme.h
    ${CMAKE_SOURCE_DIR}/src/app/panels/AiPanel.h
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/AiSettingsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/UploadPreviewDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/UploadPreviewDialog.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/UploadPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/UploadPreview.h
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/AiSettingsDialog.h
    ${CMAKE_SOURCE_DIR}/src/app/controllers/DocumentQaController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/DocumentQaController.h
    ${CMAKE_SOURCE_DIR}/src/app/controllers/IndexController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/IndexController.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/app/controllers/TextController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/TextController.h
    ${CMAKE_SOURCE_DIR}/src/app/util/SearchMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/app/util/SearchMatcher.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/TextLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/TextLayout.h
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/PageTransform.h
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ChatTypes.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/CredentialStore.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/EmbeddingProvider.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalHashEmbedding.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalHashEmbedding.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderConfig.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderSelection.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderSelection.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderPreset.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderPreset.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/DataPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/DataPolicy.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/UploadPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/UploadPreview.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/AuditLog.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/AuditLog.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/CitationVerifier.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/CitationVerifier.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/DocumentChunk.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/HybridRetriever.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/HybridRetriever.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/VectorIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/VectorIndex.h
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.h
    ${CMAKE_SOURCE_DIR}/src/ipc/WorkerClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/WorkerClient.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/Navigation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
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
    ${CMAKE_SOURCE_DIR}/src/ai/settings/DataPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/DataPolicy.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderSelection.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderSelection.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderPreset.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderPreset.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/UploadPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/UploadPreview.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/AuditLog.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/AuditLog.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderConfig.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/CredentialStore.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/CredentialStore.h
)

target_link_libraries(DataPolicyTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(DataPolicyTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(DataPolicyTests PROPERTIES WIN32_EXECUTABLE FALSE)
if (WIN32)
    target_link_libraries(DataPolicyTests PRIVATE advapi32)
endif()
aipdf_deploy_qt_runtime(DataPolicyTests)

add_test(NAME DataPolicyTests COMMAND DataPolicyTests)

# ------------------------------------------------- AI 服务商预设 ----

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProviderPresetTests.cmake)

# ------------------------------------------------- 本机模型请求链路 ----

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OllamaProviderTests.cmake)

# ------------------------------------------------- TrueType 子集化 ----

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/TrueTypeSubsetTests.cmake)

# --------------------------------------------- PDF → Excel / PPT / Word ----

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PdfToOfficeTests.cmake)

# ------------------------------------------------- 提示注入防护 ----

qt_add_executable(PromptInjectionTests
    PromptInjectionTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ChatTypes.h
)

target_link_libraries(PromptInjectionTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(PromptInjectionTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(PromptInjectionTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PromptInjectionTests)

add_test(NAME PromptInjectionTests COMMAND PromptInjectionTests)

# ------------------------------------------------- OCR 基础（v0.13.0） ----

qt_add_executable(OcrTests
    OcrTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrGeometry.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrTextUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrTextUtils.h
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ScannedPageDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ScannedPageDetector.h
)

target_link_libraries(OcrTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(OcrTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(OcrTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(OcrTests)

add_test(NAME OcrTests COMMAND OcrTests)

qt_add_executable(OcrServiceTests
    OcrServiceTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/OcrService.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/OcrService.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrGeometry.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrTypes.h
)

target_link_libraries(OcrServiceTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(OcrServiceTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(OcrServiceTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(OcrServiceTests)

add_test(NAME OcrServiceTests COMMAND OcrServiceTests)

qt_add_executable(ImagePreprocessTests
    ImagePreprocessTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ImagePreprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ImagePreprocess.h
)

target_link_libraries(ImagePreprocessTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(ImagePreprocessTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ImagePreprocessTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(ImagePreprocessTests)

add_test(NAME ImagePreprocessTests COMMAND ImagePreprocessTests)

qt_add_executable(OcrChunkBridgeTests
    OcrChunkBridgeTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/OcrChunkBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/OcrChunkBridge.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrTypes.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/DocumentChunk.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.h
)

target_link_libraries(OcrChunkBridgeTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(OcrChunkBridgeTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(OcrChunkBridgeTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(OcrChunkBridgeTests)

add_test(NAME OcrChunkBridgeTests COMMAND OcrChunkBridgeTests)

qt_add_executable(TesseractCliEngineTests
    TesseractCliEngineTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/engines/TesseractCliEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/engines/TesseractCliEngine.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrTextUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrTextUtils.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrEngine.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrTypes.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrGeometry.h
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ImagePreprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ImagePreprocess.h
)

target_link_libraries(TesseractCliEngineTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(TesseractCliEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(TesseractCliEngineTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(TesseractCliEngineTests)

add_test(NAME TesseractCliEngineTests COMMAND TesseractCliEngineTests)

qt_add_executable(WindowsOcrEngineTests
    WindowsOcrEngineTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/engines/WindowsOcrEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/engines/WindowsOcrEngine.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrEngine.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrTypes.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrGeometry.h
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrTextUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/core/OcrTextUtils.h
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ImagePreprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ImagePreprocess.h
)

target_link_libraries(WindowsOcrEngineTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(WindowsOcrEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(WindowsOcrEngineTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(WindowsOcrEngineTests)

add_test(NAME WindowsOcrEngineTests COMMAND WindowsOcrEngineTests)

qt_add_executable(ToolRegistryTests
    ToolRegistryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolRegistry.h
    ${CMAKE_SOURCE_DIR}/src/tools/ProposalValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ProposalValidator.h
)

target_link_libraries(ToolRegistryTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(ToolRegistryTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ToolRegistryTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(ToolRegistryTests)

add_test(NAME ToolRegistryTests COMMAND ToolRegistryTests)

qt_add_executable(ProposalRunnerTests
    ProposalRunnerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/ProposalRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/ProposalRunner.h
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/EditProposalDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Language.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/EditProposalDialog.h
    ${CMAKE_SOURCE_DIR}/src/app/util/BlankPageDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/app/util/BlankPageDetector.h
    ${CMAKE_SOURCE_DIR}/src/tools/ProposalValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ProposalValidator.h
    ${CMAKE_SOURCE_DIR}/src/tools/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolRegistry.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/PageCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/PageCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/AnnotationModel.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditCommand.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/UndoStack.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/edit/UndoStack.h
    ${CMAKE_SOURCE_DIR}/src/doc/edit/EditContext.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(ProposalRunnerTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_include_directories(ProposalRunnerTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ProposalRunnerTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(ProposalRunnerTests)

add_test(NAME ProposalRunnerTests COMMAND ProposalRunnerTests)

# ------------------------------------------------- 性能基线（v0.15.0） ----

qt_add_executable(PerfTests
    PerfTests.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/ChunkBuilder.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/DocumentChunk.h
    ${CMAKE_SOURCE_DIR}/src/app/util/BlankPageDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/app/util/BlankPageDetector.h
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ImagePreprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ImagePreprocess.h
    ${CMAKE_SOURCE_DIR}/src/tools/ProposalValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ProposalValidator.h
    ${CMAKE_SOURCE_DIR}/src/tools/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolRegistry.h
)

target_link_libraries(PerfTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(PerfTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(PerfTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(PerfTests)

add_test(NAME PerfTests COMMAND PerfTests)
set_tests_properties(PerfTests PROPERTIES LABELS "perf")

# ------------------------------------------------- 模糊测试（v0.15.0） ----

qt_add_executable(FuzzTests
    FuzzTests.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ProposalValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ProposalValidator.h
    ${CMAKE_SOURCE_DIR}/src/tools/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolRegistry.h
    ${CMAKE_SOURCE_DIR}/src/doc/persist/RecoveryLog.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/persist/RecoveryLog.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/PdfiumAdapter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/Annotation.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
)

target_link_libraries(FuzzTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)
target_include_directories(FuzzTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)
target_compile_definitions(FuzzTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
)
set_target_properties(FuzzTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(FuzzTests)

add_test(NAME FuzzTests COMMAND FuzzTests)
set_tests_properties(FuzzTests PROPERTIES LABELS "fuzz")

# ------------------------------------------------------------ 新增能力 ----
#
# 每个测试目标一个片段：片段是完整的可 include 定义，
# 这份清单里只留一行 include，不必再往这里塞细节。

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FormDesignParserTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FormToolSettingsTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DocumentStatisticsTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DocumentPropertiesTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OutlineGeneratorTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OutlineEditorTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BookmarkTreePanelTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PdfRevisionCounterTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LanguagePackTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThemeTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ModalScrimTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OfficePackageTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PageAssemblyTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/RedactionTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedFileTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HiddenDataInventoryTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpeechTextTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpeechEngineTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BatchReplaceTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpellCheckerTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignatureInspectorTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/TagTreeWriterTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LargeFileOpenTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PdfACheckerTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ScannerCaptureTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OneNoteExporterTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PresentationLoopTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/RecordingTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FormAutoTagTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ImageToPdfTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ConversionInputsTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BatchEncryptTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DocumentComparerTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LastWaveUiTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiWiringTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PageTextLinesTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BatchProcessorTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ReadingSchemeTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PageLabelsTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FontResourceTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FormRuntimeTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/TextSpliceTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJsFormScriptTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FormAppearanceTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ObjectInventoryTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AnchorPlannerTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AnchorMoverTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/NamedDestinationTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DocumentMergerTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AnnotationShapesTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DocumentReflowTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/RichMedia3dTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/XfaSupportTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PdfUaCheckerTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OfflineQaTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/UniversalConverterTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CustomAppearanceTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/StructTreeReaderTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/InvisibleTextLayerTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ParagraphFormatTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OptionalContentTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/InvoiceLayoutTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ActionWizardTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OcrSuspicionTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PortfolioWriterTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreflightCheckerTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExportFormatsTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FontFlatteningTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OfdExportTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OfficeExportTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AnnotationToolkitTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AnnotationFilterTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ScreenCaptureTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ViewModesTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/RsaVerifierTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PrintTemplateTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PrintBatchTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BlankDocumentTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PptGenerationTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AiNarrationTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AiContentDetectionTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AiImageGenerationTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignatureWorkflowTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignatureVerifierTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AccessibilityWizardTests.cmake)

# 欠账整改批次（2026-11-18）：这几组是 A8 / A12 / H5 / J5-J7 / L7 的专项测试。
# 已落地的片段一律无条件 include —— 片段文件跟测试源码同在版本库里，
# 用 EXISTS 判断挂载会让「漏提交片段」变成静默不注册测试。
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PageObjectModelTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ReflowWriteBackTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerticalWritingTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LayoutDiffTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SemanticTagWriterTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/UaGeneratorTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/VeraPdfCheckerTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BrowserPdfPrinterTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MediaSourceTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CertificateChainTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MediaPreviewPanelTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AttachmentPanelTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/WizardActionsTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/WizardPrintRangeTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/WizardDocumentActionsTests.cmake)

# H5 的片段由 Office 导出任务一并落地，落地后同样去掉 EXISTS 判断。
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/OfficeImagesTests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OfficeImagesTests.cmake)
endif()

# 2026-11-19 wave21：把「部分对齐」的命令补齐到对齐，这几组各自一个片段。
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave21ExportHtmlTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave21FlattenTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave21ImagePrepTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave21SpellTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave21XfaTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave21NavTests.cmake)
# wave21 的流式编辑 / 跨块合并 / 整篇翻译由 Wave21StreamTests 覆盖（原来那个
# Wave21TextTests 只是占位：一个 placeholder() 用例，不算验收）。
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave21FormTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave21OfdTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave213dTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave21DxfTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave21RtfTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Wave21StreamTests.cmake)
