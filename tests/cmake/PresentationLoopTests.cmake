# 循环放映（翻页规则）的单元测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
# 依赖的函数（aipdf_deploy_qt_runtime）在父作用域里定义好了，include 进来直接可用。
#
# 为什么不用 QWidget：翻页规则被抽成了纯函数（app/controllers/PresentationLoop.h），
# 这一组只测那一个函数——建窗口、跑事件循环跟这条规则无关，
# 也不该由它来保证。

qt_add_executable(PresentationLoopTests
    ${CMAKE_CURRENT_LIST_DIR}/../PresentationLoopTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/PresentationLoop.cpp
    ${CMAKE_SOURCE_DIR}/src/app/controllers/PresentationLoop.h
    ${CMAKE_SOURCE_DIR}/src/i18n/Translate.h
)

target_link_libraries(PresentationLoopTests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(PresentationLoopTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(PresentationLoopTests PROPERTIES WIN32_EXECUTABLE FALSE)

# 部署 Qt 运行库的函数定义在仓库根作用域里；
# 独立的自检工程不一定有它们，所以先问一句再用。
if (COMMAND aipdf_deploy_qt_runtime)
    aipdf_deploy_qt_runtime(PresentationLoopTests)
endif()

add_test(NAME PresentationLoopTests COMMAND PresentationLoopTests)
