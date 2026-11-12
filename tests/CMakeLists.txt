qt_add_executable(IpcProtocolTests
    IpcProtocolTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/IpcProtocol.h
)

target_link_libraries(IpcProtocolTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(IpcProtocolTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME IpcProtocolTests COMMAND IpcProtocolTests)

