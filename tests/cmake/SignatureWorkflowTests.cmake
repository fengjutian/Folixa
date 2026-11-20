# 电子签章「签署流程 / 签署报告 / 签署记录」的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的变量（aipdf_deploy_qt_runtime 函数）在父作用域里定义好了，include 进来直接可用。
#
# 只依赖 Qt6::Core 与 Qt6::Test：签署流程是纯数据层，
# 不读 PDF、不碰 PDFium、不碰 qpdf。

qt_add_executable(SignatureWorkflowTests
    ${CMAKE_CURRENT_LIST_DIR}/../SignatureWorkflowTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/SignatureWorkflowController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/SignatureWorkflowController.h
    ${CMAKE_SOURCE_DIR}/src/pdf/security/SignatureWorkflow.cpp
    ${CMAKE_SOURCE_DIR}/src/pdf/security/SignatureWorkflow.h
)

target_link_libraries(SignatureWorkflowTests PRIVATE
    Qt6::Core
    Qt6::Test
)

target_include_directories(SignatureWorkflowTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(SignatureWorkflowTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(SignatureWorkflowTests)

add_test(NAME SignatureWorkflowTests COMMAND SignatureWorkflowTests)
