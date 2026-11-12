qt_add_executable(PdfAiWorker
    main.cpp
    WorkerServer.cpp
    WorkerServer.h
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.h
)

target_link_libraries(PdfAiWorker PRIVATE Qt6::Core Qt6::Network)
target_include_directories(PdfAiWorker PRIVATE ${CMAKE_SOURCE_DIR}/src)

