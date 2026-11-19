#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include <memory>

namespace aipdf::pdf {

// ============================================================================
// 这个模块做什么、不做什么
// ============================================================================
//
// 做：用 Windows 自带的 Media Foundation（MF）把**音频 / 视频文件**打开、
//     读出基本信息（时长 / 尺寸 / 帧率 / 采样率 / 编码），
//     并能**顺序解出帧**：视频帧解成 QImage，音频解成 PCM 字节。
//
// 不做——这是**取舍**，不是缺陷：
//   * **不播放声音**。本模块只把 PCM 解出来，不接声卡（界面层的事）。
//   * **不做 3D 旋转渲染**。欠账 A11 的另一半（3D 运行时）不在这里，
//     本模块只管音视频这一半。
//   * **不支持拖动 / 不做进度条跳转**。这里是「顺序解码」：一帧一帧往前读。
//     播放的时序（播放 / 暂停 / 停止 / 拖动）由 MediaPlayback 那个
//     纯状态机负责，它不依赖 MF，可以单独测。
//
// 为什么要用 MF 而不是自己写解码器：Windows 自带 H.264 / MP3 / AAC /
// WMA 等解码器，自己写解码器既不现实也没必要。
//
// ============================================================================
// 错误分类：三类必须能分辨
// ============================================================================
//
// 界面层要按「打不开」「不是媒体」「解码失败」分别给不同的提示，
// 所以这里不只给一句中文，还给一个**分类**。分类是给程序看的，
// 中文是给人看的，两者都要有。
enum class MediaErrorKind {
    // 没有错误。
    None = 0,
    // 文件打不开：路径不存在、不是普通文件、没有读权限。
    OpenFailed,
    // 不是媒体文件：文件能打开，但里面没有 MF 认识的音视频轨道。
    NotMedia,
    // 解码中途失败：文件能打开、能读信息，但读帧的时候出错。
    DecodeFailed,
};

// 媒体文件的基本信息。
struct MediaInfo {
    // 是否成功读到了信息。为假时看 errorKind 与 error。
    bool ok = false;
    // 失败分类。成功时是 MediaErrorKind::None。
    MediaErrorKind errorKind = MediaErrorKind::None;
    // 中文失败原因。成功时为空。
    QString error;
    // 文件路径（原样保留调用方给的）。
    QString filePath;
    // "audio" / "video"。既有画面又有声音时按 "video" 算。
    QString mediaType;
    // 时长（毫秒）。读不到时是 0。
    qint64 durationMs = 0;
    // 画面尺寸。音频文件是 0。
    int width = 0;
    int height = 0;
    // 帧率（帧 / 秒）。音频文件是 0。
    double frameRate = 0.0;
    // 编码名，例如 "PCM" / "MP3" / "H.264"。
    QString codecName;
    // 采样率（Hz）。视频文件是 0。
    int sampleRate = 0;
    // 声道数。视频文件是 0。
    int channelCount = 0;
};

// 解出来的一帧。
//
// 视频帧：image 有内容、pcm 为空。
// 音频帧：pcm 有内容、image 为空。
// 两者都不是的「结束」帧：endOfStream 为真。
struct MediaFrame {
    // 视频帧。
    QImage image;
    // 音频 PCM（16 位小端，和 MF 的 MFAudioFormat_PCM 一致）。
    QByteArray pcm;
    // 这一帧在媒体时间轴上的位置（微秒）。
    qint64 timestampUs = 0;
    // 是不是「到此为止」的结束帧。
    bool endOfStream = false;
};

// 解码器的可调项。
struct MediaDecoderOptions {
    // 画面宽度超过这个值就等比缩小。
    // 为什么要限：界面上媒体面板就那么大，解一张 4K 帧既慢又占内存。
    int maxFrameWidth = 640;
    // 要不要解音频。纯看画面时可以关掉，省一半时间。
    bool decodeAudio = true;
};

// 打开一个媒体文件，读出基本信息（时长 / 尺寸 / 帧率 / 编码）。
//
// 失败时返回的 MediaInfo 里 ok 为假，errorKind 说明是哪一类失败，
// error 是中文原因。不抛异常、不崩：任何输入都要有话说。
MediaInfo openMediaFile(const QString& filePath, QString* error = nullptr);

// ============================================================================
// 顺序解码器
// ============================================================================
//
// 用法：
//
//     MediaDecoder decoder(path);
//     MediaDecoderOptions options;
//     if (!decoder.open(options, &why)) { ... }
//     MediaFrame frame;
//     while (decoder.nextFrame(&frame, &why) && !frame.endOfStream) { ... }
//     decoder.close();
//
// 生命周期：open() 成功之后必须 close()（析构里也会 close，多调一次安全）。
// 线程：同一个解码器不要跨线程用。COM apartment 是**线程级**的，
// 详见 MediaSource.cpp 里的说明。
class MediaDecoder {
public:
    explicit MediaDecoder(const QString& filePath);
    ~MediaDecoder();

    // 复制会踩到 COM 指针的引用计数，直接禁掉。
    MediaDecoder(const MediaDecoder&) = delete;
    MediaDecoder& operator=(const MediaDecoder&) = delete;

    // 打开文件并协商解码格式。
    // 失败时返回 false，error 里有中文原因。
    bool open(const MediaDecoderOptions& options, QString* error = nullptr);

    // 取下一帧。
    //
    // 返回 true：
    //   * frame->endOfStream 为真 → 媒体读完了，没有更多内容；
    //   * 否则 frame 里有一帧（视频帧或一块音频 PCM）。
    // 返回 false → 解码失败，error 里有中文原因。
    //
    // 视频与音频会**按时间戳交织**：哪一帧的时间戳小就先给哪一帧。
    bool nextFrame(MediaFrame* frame, QString* error = nullptr);

    // 关闭并释放所有系统句柄。可以重复调用。
    void close();

    // 已经读到的基本信息。open() 之后才有内容。
    MediaInfo info() const;

    // 是否处于打开状态。
    bool isOpen() const;

    // 文件路径。
    QString filePath() const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

// 把 MF 的 RGB32 缓冲区换算成 QImage。
//
// 为什么要单独暴露：色彩通道顺序是这类代码最常见的坑，
// 而它只有**一个像素时就能验**，没必要等真的解出一个视频帧。
// 单独暴露出来，测试就能直接喂一段已知字节、断言换出来的颜色对不对。
//
// 字节序（这是本模块最关键的一条约定）：
//   * MF 的 MFVideoFormat_RGB32 等价于 D3DFMT_X8R8G8B8，
//     在小端机器上内存里是 **B, G, R, X** 四个字节；
//   * Qt 的 QImage::Format_RGB32 是 0xffRRGGBB，
//     在小端机器上内存里同样是 **B, G, R, 0xff**。
// 两边一致，所以这里按 B/G/R 逐通道搬运。
//
// 参数：
//   src       —— 缓冲区第一行的第一个字节；
//   width     —— 画面宽度（像素）；
//   height    —— 画面高度（像素）；
//   srcStride —— 每行字节数；小于等于 0 时按 width * 4 算。
QImage rgb32BufferToImage(const uchar* src, int width, int height, int srcStride);

}  // namespace aipdf::pdf
