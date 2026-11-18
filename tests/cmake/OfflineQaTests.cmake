# 本地离线问答（领域层模块 OfflineQa）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量与函数（aipdf_deploy_qt_runtime）在父作用域里定义好了，
# include 进来直接可用。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：片段文件本身在 tests/cmake/ 下，
# 这样无论谁 include、从哪个目录 configure，路径都对，不写死绝对路径。
#
# 这一组不碰 PDFium、也不碰 PDF 夹具：它测的是「本地检索 + 抽取式回答」，
# 全部在内存里完成（LocalIndex / VectorIndex 都能不落盘地建）。

set(AIPDF_OFFLINEQA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

qt_add_executable(OfflineQaTests
    ${CMAKE_CURRENT_LIST_DIR}/../OfflineQaTests.cpp
    ${AIPDF_OFFLINEQA_ROOT}/src/ai/rag/OfflineQa.cpp
    ${AIPDF_OFFLINEQA_ROOT}/src/ai/rag/OfflineQa.h
    ${AIPDF_OFFLINEQA_ROOT}/src/ai/rag/LocalHashEmbedding.cpp
    ${AIPDF_OFFLINEQA_ROOT}/src/ai/rag/LocalHashEmbedding.h
    ${AIPDF_OFFLINEQA_ROOT}/src/ai/chat/EmbeddingProvider.h
    ${AIPDF_OFFLINEQA_ROOT}/src/doc/LocalIndex.cpp
    ${AIPDF_OFFLINEQA_ROOT}/src/doc/LocalIndex.h
    ${AIPDF_OFFLINEQA_ROOT}/src/doc/VectorIndex.cpp
    ${AIPDF_OFFLINEQA_ROOT}/src/doc/VectorIndex.h
    ${AIPDF_OFFLINEQA_ROOT}/src/doc/ChunkBuilder.cpp
    ${AIPDF_OFFLINEQA_ROOT}/src/doc/ChunkBuilder.h
    ${AIPDF_OFFLINEQA_ROOT}/src/doc/DocumentChunk.h
    ${AIPDF_OFFLINEQA_ROOT}/src/doc/HybridRetriever.cpp
    ${AIPDF_OFFLINEQA_ROOT}/src/doc/HybridRetriever.h
    ${AIPDF_OFFLINEQA_ROOT}/src/doc/CitationVerifier.cpp
    ${AIPDF_OFFLINEQA_ROOT}/src/doc/CitationVerifier.h
)

target_link_libraries(OfflineQaTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

target_include_directories(OfflineQaTests PRIVATE ${AIPDF_OFFLINEQA_ROOT}/src)
set_target_properties(OfflineQaTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(OfflineQaTests)

add_test(NAME OfflineQaTests COMMAND OfflineQaTests)
