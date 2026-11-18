# 第三批界面接线（任务 J~U）：Ribbon 入口、无障碍名、界面文案纪律的机械可测部分。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（aipdf_deploy_qt_runtime / aipdf_deploy_pdfium 函数）在父作用域里定义好了，
# include 进来直接可用。
#
# 编进来的分两半：
#   - 界面层：第三批新加的桥接文案与面板（src/app/LastWaveBridge.*、src/app/LastWavePanels.*）
#     以及 Ribbon（src/app/RibbonBar.*）；
#   - 领域层：这一批任务真正调用的那几块（不可见文字层、段落排版、标签树、图层、
#     任意文件转 PDF、离线问答、PDF/UA、XFA、3D/多媒体、整篇重排、动作向导、发票）。
# 领域层那一半需要 qpdf（结构树、图层、PDF/UA、XFA、3D 都是对象级信息）。

qt_add_executable(LastWaveUiTests
    ${CMAKE_CURRENT_LIST_DIR}/../LastWaveUiTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/RibbonBar.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/RibbonBar.h
    ${CMAKE_SOURCE_DIR}/src/app/ui/Theme.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/Theme.h
    ${CMAKE_SOURCE_DIR}/src/app/controllers/LastWaveBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/LastWaveBridge.h
    ${CMAKE_SOURCE_DIR}/src/app/panels/LastWavePanels.cpp
    ${CMAKE_SOURCE_DIR}/src/app/panels/LastWavePanels.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/InvisibleTextLayer.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/InvisibleTextLayer.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ParagraphFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ParagraphFormat.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TextReflow.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TextReflow.h
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/kernel/TextPage.h
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/StructTreeReader.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/StructTreeReader.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/ContentStream.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OptionalContent.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/OptionalContent.h
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/PdfUaChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/PdfUaChecker.h
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/XfaSupport.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/forms/XfaSupport.h
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/RichMedia3d.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/annot/RichMedia3d.h
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/DocumentReflow.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/ops/DocumentReflow.h
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/TagTreeWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/compliance/TagTreeWriter.h
    ${CMAKE_SOURCE_DIR}/src/doc/convert/UniversalConverter.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/convert/UniversalConverter.h
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TrueTypeSubset.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/content/TrueTypeSubset.h
    ${CMAKE_SOURCE_DIR}/src/doc/model/BlankDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/model/BlankDocument.h
    ${CMAKE_SOURCE_DIR}/src/doc/batch/ActionWizard.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/batch/ActionWizard.h
    ${CMAKE_SOURCE_DIR}/src/doc/convert/InvoiceLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/convert/InvoiceLayout.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/LocalIndex.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/VectorIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/VectorIndex.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/HybridRetriever.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/HybridRetriever.h
    ${CMAKE_SOURCE_DIR}/src/doc/index/CitationVerifier.cpp
    ${CMAKE_SOURCE_DIR}/src/doc/index/CitationVerifier.h
    ${CMAKE_SOURCE_DIR}/src/ai/rag/OfflineQa.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/rag/OfflineQa.h
    ${CMAKE_SOURCE_DIR}/src/ai/rag/LocalHashEmbedding.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/rag/LocalHashEmbedding.h
    ${CMAKE_SOURCE_DIR}/src/i18n/Translate.h
)

target_link_libraries(LastWaveUiTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
)

# 这一批任务里有一半是对象级信息（结构树、图层、PDF/UA、XFA、3D/多媒体），
# 只有真正的 PDF 解析器能给答案，所以必须链 qpdf。
if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(LastWaveUiTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(LastWaveUiTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET LastWaveUiTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:LastWaveUiTests>"
        COMMENT "Copying qpdf runtime for LastWaveUiTests"
    )
endif()

target_include_directories(LastWaveUiTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# 翻译目录的路径：测试要核对「新增的用户可见文案都登记进去了」。
target_compile_definitions(LastWaveUiTests PRIVATE
    AIPDF_I18N_TS_FILE="${CMAKE_SOURCE_DIR}/src/i18n/ai-pdf-editor_en.ts"
)

set_target_properties(LastWaveUiTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(LastWaveUiTests)

add_test(NAME LastWaveUiTests COMMAND LastWaveUiTests)
