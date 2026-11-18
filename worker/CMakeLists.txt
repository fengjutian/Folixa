qt_add_executable(PdfAiWorker
    main.cpp
    ParentWatchdog.cpp
    ParentWatchdog.h
    WorkerServer.cpp
    WorkerServer.h
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.h
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ChatTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ChatTypes.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/CredentialStore.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/chat/ModelProvider.h
    ${CMAKE_SOURCE_DIR}/src/ai/provider/MockProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/provider/MockProvider.h
    ${CMAKE_SOURCE_DIR}/src/ai/provider/OpenAiProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/provider/OpenAiProvider.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/DataPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/DataPolicy.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderConfig.h
    ${CMAKE_SOURCE_DIR}/src/ai/provider/ProviderFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/provider/ProviderFactory.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderPreset.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderPreset.h
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderSelection.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/settings/ProviderSelection.h
    ${CMAKE_SOURCE_DIR}/src/ai/chat/SseParser.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/chat/SseParser.h
)

target_link_libraries(PdfAiWorker PRIVATE Qt6::Core Qt6::Network)

# Windows 凭据管理器（API 密钥）：advapi32。
if (WIN32)
    target_link_libraries(PdfAiWorker PRIVATE advapi32)
endif()
target_include_directories(PdfAiWorker PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(PdfAiWorker PRIVATE AIPDF_VERSION="${PROJECT_VERSION}")

set(AIPDF_FILE_DESCRIPTION "AI PDF Editor AI Worker")
set(AIPDF_INTERNAL_NAME "PdfAiWorker")
set(AIPDF_ORIGINAL_FILENAME "PdfAiWorker.exe")
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY
)
target_sources(PdfAiWorker PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)

aipdf_deploy_qt_runtime(PdfAiWorker)

