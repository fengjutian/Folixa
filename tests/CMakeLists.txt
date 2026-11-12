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
