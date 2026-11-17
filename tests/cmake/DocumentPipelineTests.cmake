# 端到端流水线集成测试（DocumentPipelineTests）的目标定义。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：片段文件本身在 tests/cmake/ 下，
# 这样无论谁 include、从哪个目录 configure，路径都对，不写死绝对路径。
#
# 这个目标与其它单测目标的**根本区别**在于它把二十多个领域层模块串成一条流水线：
# 印前检查 → 图层 → 不可见文字层 → 结构树 → PDF/UA → 命名目标 → 页面标签
# → 合并 → 三种导出 → 活页夹。每一步的产物都必须仍然是一份能打开、
# 页数没变、内容没丢的 PDF。各模块的专项单测守不到这一层：
# 「各自都对」不等于「串起来也对」。
#
# 为什么既要 qpdf 又要 PDFium：
#   * PDFium 是「读者」——它说这份文件能不能打开、页数多少、文字还在不在；
#   * qpdf 是「结构校验」——它说这份文件的对象级结构有没有坏。
#   两条路都得真的跑一遍，流水线才算验过。没有 qpdf 的构建里，
#   依赖 qpdf 的用例会 QSKIP，其余用例照跑。
#
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、AIPDF_PDFIUM_LIBRARY、
# AIPDF_PDFIUM_INCLUDE_DIR）与函数（aipdf_deploy_qt_runtime、aipdf_deploy_pdfium）
# 都在父作用域里定义好了，include 进来直接可用。
#
# 注意：include 必须放在 tests/CMakeLists.txt 里 **pdf-fixtures 目标之后**，
# 否则 PDF_FIXTURE_DIR 还是空的（与 PreflightCheckerTests.cmake /
# ExportFormatsTests.cmake 同一套要求）。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(DocumentPipelineTests
    ${CMAKE_CURRENT_LIST_DIR}/../DocumentPipelineTests.cpp
    # ---- PDFium 适配层与它的依赖 ----
    ${AIPDF_REPO_ROOT}/src/pdf/PdfiumAdapter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/PdfiumAdapter.h
    ${AIPDF_REPO_ROOT}/src/pdf/Annotation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/Annotation.h
    ${AIPDF_REPO_ROOT}/src/pdf/TextPage.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/TextPage.h
    ${AIPDF_REPO_ROOT}/src/pdf/Navigation.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/Navigation.h
    ${AIPDF_REPO_ROOT}/src/pdf/TableStructure.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/TableStructure.h
    ${AIPDF_REPO_ROOT}/src/pdf/ContentStream.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ContentStream.h
    # ---- 流水线主流程涉及的领域层模块 ----
    ${AIPDF_REPO_ROOT}/src/pdf/PreflightChecker.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/PreflightChecker.h
    ${AIPDF_REPO_ROOT}/src/pdf/OptionalContent.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/OptionalContent.h
    ${AIPDF_REPO_ROOT}/src/pdf/InvisibleTextLayer.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/InvisibleTextLayer.h
    ${AIPDF_REPO_ROOT}/src/pdf/StructTreeReader.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/StructTreeReader.h
    ${AIPDF_REPO_ROOT}/src/pdf/TagTreeWriter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/TagTreeWriter.h
    ${AIPDF_REPO_ROOT}/src/pdf/PdfUaChecker.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/PdfUaChecker.h
    ${AIPDF_REPO_ROOT}/src/pdf/NamedDestination.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/NamedDestination.h
    ${AIPDF_REPO_ROOT}/src/pdf/PageLabels.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/PageLabels.h
    ${AIPDF_REPO_ROOT}/src/pdf/DocumentMerger.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/DocumentMerger.h
    ${AIPDF_REPO_ROOT}/src/pdf/ExportFormats.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ExportFormats.h
    ${AIPDF_REPO_ROOT}/src/pdf/PortfolioWriter.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/PortfolioWriter.h
    # ---- 分支流程涉及的领域层模块 ----
    ${AIPDF_REPO_ROOT}/src/pdf/DocumentReflow.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/DocumentReflow.h
    ${AIPDF_REPO_ROOT}/src/pdf/ParagraphFormat.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/ParagraphFormat.h
    ${AIPDF_REPO_ROOT}/src/pdf/TextReflow.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/TextReflow.h
    ${AIPDF_REPO_ROOT}/src/pdf/RichMedia3d.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/RichMedia3d.h
    ${AIPDF_REPO_ROOT}/src/doc/UniversalConverter.cpp
    ${AIPDF_REPO_ROOT}/src/doc/UniversalConverter.h
    ${AIPDF_REPO_ROOT}/src/doc/BlankDocument.cpp
    ${AIPDF_REPO_ROOT}/src/doc/BlankDocument.h
    ${AIPDF_REPO_ROOT}/src/ai/OfflineQa.cpp
    ${AIPDF_REPO_ROOT}/src/ai/OfflineQa.h
    ${AIPDF_REPO_ROOT}/src/ai/LocalHashEmbedding.cpp
    ${AIPDF_REPO_ROOT}/src/ai/LocalHashEmbedding.h
    ${AIPDF_REPO_ROOT}/src/ai/EmbeddingProvider.h
    ${AIPDF_REPO_ROOT}/src/doc/CitationVerifier.cpp
    ${AIPDF_REPO_ROOT}/src/doc/CitationVerifier.h
    ${AIPDF_REPO_ROOT}/src/doc/ChunkBuilder.cpp
    ${AIPDF_REPO_ROOT}/src/doc/ChunkBuilder.h
    ${AIPDF_REPO_ROOT}/src/doc/DocumentChunk.h
    ${AIPDF_REPO_ROOT}/src/doc/HybridRetriever.cpp
    ${AIPDF_REPO_ROOT}/src/doc/HybridRetriever.h
    ${AIPDF_REPO_ROOT}/src/doc/LocalIndex.cpp
    ${AIPDF_REPO_ROOT}/src/doc/LocalIndex.h
    ${AIPDF_REPO_ROOT}/src/doc/VectorIndex.cpp
    ${AIPDF_REPO_ROOT}/src/doc/VectorIndex.h
    ${AIPDF_REPO_ROOT}/src/i18n/Translate.h
)

target_link_libraries(DocumentPipelineTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    ${AIPDF_PDFIUM_LIBRARY}
)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    target_link_libraries(DocumentPipelineTests PRIVATE qpdf::libqpdf)
    target_compile_definitions(DocumentPipelineTests PRIVATE AIPDF_WITH_QPDF=1)
    add_custom_command(TARGET DocumentPipelineTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:DocumentPipelineTests>"
        COMMENT "Copying qpdf runtime for DocumentPipelineTests"
    )
endif()

target_include_directories(DocumentPipelineTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
    ${AIPDF_PDFIUM_INCLUDE_DIR}
)

# qpdf 命令行：用来跑一次 qpdf --check（结构校验）。
# 找不到就传空串，用例里会跳过那一条。
if(NOT DEFINED AIPDF_QPDF_CLI)
    set(AIPDF_QPDF_CLI "${AIPDF_REPO_ROOT}/tools/qpdf/bin/qpdf.exe")
    if(NOT EXISTS "${AIPDF_QPDF_CLI}")
        set(AIPDF_QPDF_CLI "")
    endif()
endif()

target_compile_definitions(DocumentPipelineTests PRIVATE
    AIPDF_PDF_FIXTURES_DIR="${PDF_FIXTURE_DIR}"
    AIPDF_QPDF_CLI="${AIPDF_QPDF_CLI}"
)
set_target_properties(DocumentPipelineTests PROPERTIES WIN32_EXECUTABLE FALSE)

add_dependencies(DocumentPipelineTests pdf-fixtures)

# 部署 Qt 运行时与 PDFium 的函数定义在仓库根作用域里；
# 独立的自检工程不一定有它们，所以先问一句再用。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(DocumentPipelineTests)
endif()
if (COMMAND aipdf_deploy_pdfium)
    aipdf_deploy_pdfium(DocumentPipelineTests)
endif()

add_test(NAME DocumentPipelineTests COMMAND DocumentPipelineTests)
