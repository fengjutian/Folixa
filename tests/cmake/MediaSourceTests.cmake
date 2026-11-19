# 音视频解码（Media Foundation）与播放状态机的测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份已经很长的清单里塞细节。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：这样这个片段既能被
# tests/CMakeLists.txt include，也能被仓库外的自检工程 include，
# 不必写死绝对路径。
#
# 为什么要链 Media Foundation：音视频解码走的是 Windows 自带的 MF，
# 头文件在 MinGW 套件里就有（mfapi.h / mfidl.h / mfobjects.h / mfreadwrite.h），
# 运行库是系统自带的 mfplat.dll / mfreadwrite.dll / mf.dll。
# MF 的 GUID（MF_MT_* 之类）没有从 DLL 导出，MediaSource.cpp 里用
# INITGUID 在本翻译单元内定义，所以链接期不需要额外的 GUID 库。
#
# ole32 是 COM：CoInitializeEx / CoUninitialize 都在它里面。
# COM apartment 的生命周期由 MediaSource.cpp 里的线程级守卫保证，
# 详见那里的注释。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

qt_add_executable(MediaSourceTests
    ${CMAKE_CURRENT_LIST_DIR}/../MediaSourceTests.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/media/MediaSource.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/media/MediaSource.h
    ${AIPDF_REPO_ROOT}/src/pdf/media/MediaPlayback.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/media/MediaPlayback.h
)

target_link_libraries(MediaSourceTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
)

if (WIN32)
    # mfplat：MFStartup / MFShutdown / 媒体类型。
    # mfreadwrite：源阅读器（MFCreateSourceReaderFromURL）。
    # mf：解码器与视频处理（源阅读器内部会用到）。
    # ole32：COM apartment。
    target_link_libraries(MediaSourceTests PRIVATE mfplat mfreadwrite mf ole32)
else()
    message(WARNING "MediaSourceTests 依赖 Windows 的 Media Foundation；非 Windows 平台跑不了这一组。")
endif()

target_include_directories(MediaSourceTests PRIVATE ${AIPDF_REPO_ROOT}/src)
set_target_properties(MediaSourceTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(MediaSourceTests)

add_test(NAME MediaSourceTests COMMAND MediaSourceTests)

# 真机解码用的样本可以覆盖路径；不设置就用系统自带的样本。
#   AIPDF_MEDIA_AUDIO_SAMPLE —— WAV
#   AIPDF_MEDIA_MP3_SAMPLE   —— MP3
#   AIPDF_MEDIA_VIDEO_SAMPLE —— 视频
set_tests_properties(MediaSourceTests PROPERTIES
    ENVIRONMENT "AIPDF_MEDIA_AUDIO_SAMPLE=$ENV{AIPDF_MEDIA_AUDIO_SAMPLE};AIPDF_MEDIA_MP3_SAMPLE=$ENV{AIPDF_MEDIA_MP3_SAMPLE};AIPDF_MEDIA_VIDEO_SAMPLE=$ENV{AIPDF_MEDIA_VIDEO_SAMPLE}"
)
