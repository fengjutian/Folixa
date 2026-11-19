# 媒体预览面板与 PCM 播放器（A11 欠账的 app 层）的测试目标。
#
# 为什么单独放一个片段：这是一个可以被 include 的完整目标定义，
# tests/CMakeLists.txt 里只要一行 include，不必再往那份很长的清单里塞细节。
# 依赖的变量与函数（aipdf_deploy_qt_runtime）都在父作用域里定义好了，
# include 进来直接可用。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：片段文件本身在 tests/cmake/ 下，
# 这样无论谁 include、从哪个目录 configure，路径都对，不写死绝对路径。
#
# 编进来的：
#   - app/panels/MediaPreviewPanel（界面与时序）
#   - app/controllers/PcmPlayer（waveOut 放声音）
#   - pdf/media/MediaSource、MediaPlayback（领域层：读信息、解帧、播放状态机）
#   - app/ui/Language（面板要跟着界面语言切换重译）
#
# 链接 winmm 是为了 waveOut（放声音）；mf / mfplat / mfreadwrite / mfuuid
# 是 Media Foundation（读信息、解帧）——都是 Windows 自带的系统库，
# 不引入任何第三方依赖。

if (NOT DEFINED AIPDF_REPO_ROOT)
    get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

qt_add_executable(MediaPreviewPanelTests
    ${CMAKE_CURRENT_LIST_DIR}/../MediaPreviewPanelTests.cpp
    ${AIPDF_REPO_ROOT}/src/app/panels/MediaPreviewPanel.cpp
    ${AIPDF_REPO_ROOT}/src/app/panels/MediaPreviewPanel.h
    ${AIPDF_REPO_ROOT}/src/app/controllers/PcmPlayer.cpp
    ${AIPDF_REPO_ROOT}/src/app/controllers/PcmPlayer.h
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.cpp
    ${AIPDF_REPO_ROOT}/src/app/ui/Language.h
    ${AIPDF_REPO_ROOT}/src/pdf/media/MediaSource.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/media/MediaSource.h
    ${AIPDF_REPO_ROOT}/src/pdf/media/MediaPlayback.cpp
    ${AIPDF_REPO_ROOT}/src/pdf/media/MediaPlayback.h
)

target_link_libraries(MediaPreviewPanelTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
    winmm
    mf mfplat mfreadwrite mfuuid
)

target_include_directories(MediaPreviewPanelTests PRIVATE
    ${AIPDF_REPO_ROOT}/src
)

set_target_properties(MediaPreviewPanelTests PROPERTIES WIN32_EXECUTABLE FALSE)

aipdf_deploy_qt_runtime(MediaPreviewPanelTests)

# 这一组**不依赖网络**，也不依赖本机一定有某个媒体文件：
# 系统示例文件（如 C:\Windows\Media\Alarm01.wav）不存在时用例会 QSKIP
# （跳过不算失败），其余分支一律用假数据覆盖。
add_test(NAME MediaPreviewPanelTests COMMAND MediaPreviewPanelTests)
