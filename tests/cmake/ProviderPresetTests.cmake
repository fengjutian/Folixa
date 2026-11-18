# AI 服务商预设（DeepSeek / Kimi / Qwen / MiniMax / Ollama / 自定义）。
#
# 纯逻辑：不联网、不碰界面。守的是「服务商事实只有一处说了算」，
# 以及本机模型（Ollama）既不要密钥、也不被本地模式拦下。

qt_add_executable(ProviderPresetTests
    ${CMAKE_CURRENT_LIST_DIR}/../ProviderPresetTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderPreset.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderPreset.h
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderSelection.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderSelection.h
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ProviderConfig.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/DataPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/DataPolicy.h
    ${CMAKE_SOURCE_DIR}/src/ai/UploadPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/UploadPreview.h
)

target_link_libraries(ProviderPresetTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(ProviderPresetTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ProviderPresetTests PROPERTIES WIN32_EXECUTABLE FALSE)
if (WIN32)
    # CredentialStore 走 Windows 凭据管理器（advapi32）。
    target_link_libraries(ProviderPresetTests PRIVATE advapi32)
endif()
aipdf_deploy_qt_runtime(ProviderPresetTests)

add_test(NAME ProviderPresetTests COMMAND ProviderPresetTests)
