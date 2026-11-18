# 本机模型（Ollama）的端到端请求链路。
#
# 起一个假的 OpenAI 兼容服务器，让程序按真实路径把请求打出去：
# 核对 URL、请求体、**没有 Authorization 头**，以及 SSE 分片能读成完整回答。
# 这条链路与云端服务商共用同一套 OpenAiProvider 实现。

qt_add_executable(OllamaProviderTests
    ${CMAKE_CURRENT_LIST_DIR}/../OllamaProviderTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ChatTypes.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/DataPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/DataPolicy.h
    ${CMAKE_SOURCE_DIR}/src/ai/MockProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/MockProvider.h
    ${CMAKE_SOURCE_DIR}/src/ai/ModelProvider.h
    ${CMAKE_SOURCE_DIR}/src/ai/OpenAiProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/OpenAiProvider.h
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderConfig.h
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderFactory.h
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderPreset.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderPreset.h
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderSelection.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderSelection.h
    ${CMAKE_SOURCE_DIR}/src/ai/SseParser.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/SseParser.h
)

target_link_libraries(OllamaProviderTests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
target_include_directories(OllamaProviderTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(OllamaProviderTests PROPERTIES WIN32_EXECUTABLE FALSE)
if (WIN32)
    # CredentialStore 走 Windows 凭据管理器（advapi32）。
    target_link_libraries(OllamaProviderTests PRIVATE advapi32)
endif()
aipdf_deploy_qt_runtime(OllamaProviderTests)

add_test(NAME OllamaProviderTests COMMAND OllamaProviderTests)
