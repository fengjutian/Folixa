qt_add_executable(PdfAiWorker
    main.cpp
    WorkerServer.cpp
    WorkerServer.h
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.h
)

target_link_libraries(PdfAiWorker PRIVATE Qt6::Core Qt6::Network)
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

