# 2026-11-19 wave21：扫描件图像优化——去噪（连通域）与纠偏（投影法）。

qt_add_executable(Wave21ImagePrepTests
    ${CMAKE_CURRENT_LIST_DIR}/../Wave21ImagePrepTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ImagePreprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/pipeline/ImagePreprocess.h
)

target_link_libraries(Wave21ImagePrepTests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(Wave21ImagePrepTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(Wave21ImagePrepTests PROPERTIES WIN32_EXECUTABLE FALSE)
aipdf_deploy_qt_runtime(Wave21ImagePrepTests)

add_test(NAME Wave21ImagePrepTests COMMAND Wave21ImagePrepTests)
