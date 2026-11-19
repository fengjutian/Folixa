// Media Foundation 的接入。
//
// 这里有两条「踩过就很难查」的坑，代码里都做了处理并且写了原因：
//
// 1) COM apartment 的生命周期。
//    MF 的对象全是 COM 对象，必须有一个 apartment 撑着。如果在某个
//    「创建对象」的局部函数里 CoInitializeEx，函数一返回 apartment 就
//    被拆掉，后面拿着 COM 指针继续调用就是未定义行为——表现是**偶发**
//    段错误，不是稳定崩，复现率还低，排查成本极高。
//    所以这里用**线程级**的守卫对象：第一次用到时初始化，线程结束时
//    成对释放（CoUninitialize / MFShutdown）。apartment 在整个线程的
//    生命周期里都活着，覆盖了所有 MF 调用。
//
// 2) COM 与 MF 的句柄要在**所有路径**上成对释放。
//    早退分支最容易漏：打开失败、类型协商失败、读帧失败……
//    所以这里的每个 COM 指针都交给一个带析构的小包装类，
//    由作用域保证释放，不靠人记。

#ifndef QT_NO_MINMAX
// windows.h 会定义 min / max 宏，和 Qt 的 qMin / qMax 打架。
// 在包含任何 Qt 头文件**之前**关掉它，Qt 就不会定义 qMin / qMax，
// 本文件里改用 std::min / std::max。
#define QT_NO_MINMAX
#endif

#include "pdf/media/MediaSource.h"

#include "i18n/Translate.h"

#include <QFileInfo>
#include <QUrl>

// Media Foundation 的 GUID（MF_MT_* 之类）**没有**从 mfplat.dll 导出，
// 只能在头文件里定义。INITGUID 就是干这个的：不定义它，链接期会报
// 一堆 undefined reference。
#define INITGUID

#include <windows.h>

#include <mferror.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>

#include <string>

namespace aipdf::pdf {

namespace {


// ============================================================================
// 线程级的 COM + Media Foundation 运行时
// ============================================================================

class MediaRuntime {
public:
    // 取得本线程的运行时（第一次调用时初始化）。
    static MediaRuntime& instance() {
        // thread_local：apartment 是**线程**概念，不能跨线程共享。
        // 静态局部变量的析构函数在线程退出时跑，
        // 那时栈上的解码器早就析构完了，COM 指针不会被提前失效。
        static thread_local MediaRuntime runtime;
        return runtime;
    }

    // 能不能用 MF。
    bool ready() const { return ready_; }

    ~MediaRuntime() {
        // 顺序有讲究：先关 MF，再拆 apartment。
        if (mfStarted_) {
            MFShutdown();
            mfStarted_ = false;
        }
        if (comOwned_) {
            CoUninitialize();
            comOwned_ = false;
        }
    }

private:
    MediaRuntime() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            // 成功：这一下是我们自己的 apartment，退出时必须配一次
            // CoUninitialize。
            comOwned_ = true;
        } else if (hr == RPC_E_CHANGED_MODE) {
            // 线程已经是 STA（Qt 主线程常见）：沿用现有的 apartment，
            // 但**不能**再 CoUninitialize——那会把别人的 apartment 拆了。
            comOwned_ = false;
        } else {
            ready_ = false;
            return;
        }

        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) {
            ready_ = false;
            return;
        }
        mfStarted_ = true;
        ready_ = true;
    }

    bool comOwned_ = false;
    bool mfStarted_ = false;
    bool ready_ = false;
};

// COM 指针的小包装：作用域结束自动 Release。
//
// 为什么要这样：早退分支（「类型协商失败，直接 return」）最容易漏掉
// Release，漏了就是引用计数错乱——症状是对象永远不释放（内存涨），
// 或者多释放一次（随机崩）。交给作用域就不用记。
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* ptr) : ptr_(ptr) {}
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    void reset(T* ptr = nullptr) {
        if (ptr_ != nullptr) {
            ptr_->Release();
        }
        ptr_ = ptr;
    }

    // 取一个「可以往里填指针」的地址。
    // 先把旧指针放掉，MF 的 xxx(out) 接口要的就是这个。
    T** getAddressOf() {
        reset();
        return &ptr_;
    }

    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    T* ptr_ = nullptr;
};

// ============================================================================
// 错误信息
// ============================================================================

// 把 HRESULT 变成一句能给人看的说明。
QString describeHResult(HRESULT hr) {
    switch (hr) {
        case MF_E_UNSUPPORTED_BYTESTREAM_TYPE:
            return i18n::tr("aipdf::pdf::MediaSource", "系统不认识这个文件的格式");
        case MF_E_INVALIDREQUEST:
            return i18n::tr("aipdf::pdf::MediaSource", "系统不认识这个文件的格式");
        case MF_E_NOTACCEPTING:
            return i18n::tr("aipdf::pdf::MediaSource", "系统不接受这个文件格式");
        case MF_E_INVALID_FILE_FORMAT:
            return i18n::tr("aipdf::pdf::MediaSource", "文件格式不对");
        case MF_E_INVALID_FORMAT:
            return i18n::tr("aipdf::pdf::MediaSource", "文件格式不对");
        case MF_E_END_OF_STREAM:
            return i18n::tr("aipdf::pdf::MediaSource", "文件里的数据不完整");
        case MF_E_INVALID_STREAM_DATA:
            return i18n::tr("aipdf::pdf::MediaSource", "文件里的数据不完整");
        case MF_E_PLATFORM_NOT_INITIALIZED:
            return i18n::tr("aipdf::pdf::MediaSource", "系统媒体组件还没初始化");
        case MF_E_SHUTDOWN:
            return i18n::tr("aipdf::pdf::MediaSource", "系统媒体组件已经关闭");
        case E_INVALIDARG:
            return i18n::tr("aipdf::pdf::MediaSource", "参数不对");
        case E_OUTOFMEMORY:
            return i18n::tr("aipdf::pdf::MediaSource", "内存不够");
        default:
            break;
    }
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        return i18n::tr("aipdf::pdf::MediaSource", "找不到这个文件");
    }
    if (hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
        return i18n::tr("aipdf::pdf::MediaSource", "找不到这个路径");
    }
    if (hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) {
        return i18n::tr("aipdf::pdf::MediaSource", "没有读这个文件的权限");
    }
    if (hr == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)) {
        return i18n::tr("aipdf::pdf::MediaSource", "这个文件正被别的程序占用");
    }
    return i18n::tr("aipdf::pdf::MediaSource", "系统返回了一个未知的错误");
}

// 把 HRESULT 归到三类失败里。
//
// 规则很简单：**文件存在、能读，但系统认不出它是什么格式** → 「不是媒体文件」；
// **文件本身读不了**（找不到、没权限、被占用）或**系统自己出了问题**
// （没初始化、内存不够）→ 「文件打不开」。
//
// 为什么对「系统不认得格式」采取默认归类：同一个非媒体文件，MF 报的错
// 码会随内容变——本机实测，一个 46 字节的 .txt 报
// MF_E_UNSUPPORTED_BYTESTREAM_TYPE（0xC00D36C4），一个 10 字节的 .txt
// 报 MF_E_END_OF_STREAM（0xC00D3E84，嗅探时数据就没了）。
// 逐个列错误码必然漏，所以按「是不是文件的问题」来分。
MediaErrorKind classifyOpenFailure(HRESULT hr) {
    // 系统自己的问题：这话是在说「读不了」，不是在说「这不是媒体」。
    if (hr == MF_E_PLATFORM_NOT_INITIALIZED || hr == MF_E_SHUTDOWN || hr == E_OUTOFMEMORY) {
        return MediaErrorKind::OpenFailed;
    }
    // 文件本身读不了。
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
        hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) ||
        hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) ||
        hr == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) ||
        hr == HRESULT_FROM_WIN32(ERROR_INVALID_NAME)) {
        return MediaErrorKind::OpenFailed;
    }
    // 其余：系统认不出这个文件的格式。
    return MediaErrorKind::NotMedia;
}

// ============================================================================
// 媒体类型上的小工具
// ============================================================================

// 从媒体类型里读一个 UINT32 属性；读不到就用默认值。
UINT32 readUint32(IMFMediaType* type, REFGUID key, UINT32 fallback) {
    if (type == nullptr) return fallback;
    UINT32 value = fallback;
    if (FAILED(type->GetUINT32(key, &value))) return fallback;
    return value;
}

// 从媒体类型里读一个 GUID 属性；读不到就返回 false。
bool readGuid(IMFMediaType* type, REFGUID key, GUID* out) {
    if (type == nullptr) return false;
    return SUCCEEDED(type->GetGUID(key, out));
}

// 画面的宽高藏在 MF_MT_FRAME_SIZE 里。
//
// 这个属性有两种存法，MSDN 只写了 32 位那一种：
//   * 32 位：高 16 位是**高**，低 16 位是**宽**；
//   * 64 位：**高 32 位是宽、低 32 位是高**（本机实测：96x48 的片子读出来
//     就是 0x00000060_00000030）。
// 两种都认，认不出来就报「不知道多大」，不瞎猜。
void readFrameSize(IMFMediaType* type, int* width, int* height) {
    *width = 0;
    *height = 0;
    if (type == nullptr) return;

    UINT64 wide = 0;
    if (SUCCEEDED(type->GetUINT64(MF_MT_FRAME_SIZE, &wide)) && wide != 0) {
        if (wide > 0xffffffffull) {
            *width = static_cast<int>(wide >> 32);
            *height = static_cast<int>(wide & 0xffffffffull);
        } else {
            *width = static_cast<int>(wide & 0xffff);
            *height = static_cast<int>((wide >> 16) & 0xffff);
        }
        return;
    }

    UINT32 narrow = 0;
    if (SUCCEEDED(type->GetUINT32(MF_MT_FRAME_SIZE, &narrow)) && narrow != 0) {
        *width = static_cast<int>(narrow & 0xffff);
        *height = static_cast<int>((narrow >> 16) & 0xffff);
    }
}

// 帧率：MF_MT_FRAME_RATE 是个有理数。
//
// 本机实测它也存成 64 位：**高 32 位是分子、低 32 位是分母**
// （25 帧的片子读出来是 0x00000019_00000001）。
// 32 位那一种按惯例是高 16 位分子、低 16 位分母。
// 两种都认；认不出来就报 0（不知道），不瞎猜。
double readFrameRate(IMFMediaType* type) {
    if (type == nullptr) return 0.0;

    UINT64 wide = 0;
    if (SUCCEEDED(type->GetUINT64(MF_MT_FRAME_RATE, &wide)) && wide != 0) {
        const double numerator = static_cast<double>(wide >> 32);
        const double denominator = static_cast<double>(wide & 0xffffffffull);
        if (denominator > 0.0) return numerator / denominator;
        return numerator;
    }

    UINT32 narrow = 0;
    if (SUCCEEDED(type->GetUINT32(MF_MT_FRAME_RATE, &narrow)) && narrow != 0) {
        const double numerator = static_cast<double>((narrow >> 16) & 0xffff);
        const double denominator = static_cast<double>(narrow & 0xffff);
        if (denominator > 0.0) return numerator / denominator;
        return numerator;
    }
    return 0.0;
}

// 编码名：把 subtype 的 GUID 翻成人看得懂的名字。
//
// 只认常见的那几个；认不出来就写「未知编码」——
// 写一个编造的编码名比写「未知」更糟：用户会拿它去查资料。
QString codecNameFromSubtype(const GUID& subtype, bool video) {
    if (IsEqualGUID(subtype, MFAudioFormat_PCM)) return QStringLiteral("PCM");
    if (IsEqualGUID(subtype, MFAudioFormat_MP3)) return QStringLiteral("MP3");
    if (IsEqualGUID(subtype, MFVideoFormat_RGB32) || IsEqualGUID(subtype, MFVideoFormat_RGB24)) {
        return QStringLiteral("RGB");
    }
    // 几个常见编码的 FourCC 直接用 ASCII 拼出来读。
    // 注意字节序：FourCC 是四个字符按「低字节在前」存进一个 DWORD 的，
    // 也就是 'H264' 存成 0x34363248。写反了就会把 H.264 认成别的东西。
    const quint32 code = subtype.Data1;
    if (video) {
        switch (code) {
            case 0x34363248: return QStringLiteral("H.264");  // 'H264'
            case 0x43564548: return QStringLiteral("HEVC");   // 'HEVC'
            case 0x5634504D: return QStringLiteral("MPEG-4"); // 'MP4V'
            case 0x3334504D: return QStringLiteral("MPEG-4"); // 'MP43'
            case 0x31564D57: return QStringLiteral("WMV1");
            case 0x32564D57: return QStringLiteral("WMV2");
            case 0x33564D57: return QStringLiteral("WMV3");
            case 0x5632504D: return QStringLiteral("MPEG-2"); // 'MP2V'
            case 0x3235504D: return QStringLiteral("MPEG-2"); // 'MP52'
            default: break;
        }
    } else {
        switch (code) {
            case 0x00000055: return QStringLiteral("MP3");    // WAVE_FORMAT_MPEGLAYER3
            case 0x00000001: return QStringLiteral("PCM");
            default: break;
        }
    }
    return QStringLiteral("未知编码");
}

// ============================================================================
// 源阅读器
// ============================================================================

// 一个已经打开的媒体源。
struct OpenedSource {
    ComPtr<IMFSourceReader> reader;
    // 有没有视频轨 / 音频轨。
    bool hasVideo = false;
    bool hasAudio = false;
    // 视频轨的原生媒体类型（协商之前）。
    ComPtr<IMFMediaType> nativeVideoType;
    ComPtr<IMFMediaType> nativeAudioType;
};

// 打开一个媒体源。
//
// 失败时把原因写进 error，并返回对应的错误分类。
bool openSource(const QString& filePath, OpenedSource* out, MediaErrorKind* kind, QString* error) {
    *kind = MediaErrorKind::None;
    if (error != nullptr) error->clear();

    if (filePath.isEmpty()) {
        *kind = MediaErrorKind::OpenFailed;
        if (error != nullptr) *error = i18n::tr("aipdf::pdf::MediaSource", "没有给出文件路径");
        return false;
    }

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        // 路径不存在、是个目录：都是「打不开」，不是「不是媒体文件」。
        *kind = MediaErrorKind::OpenFailed;
        if (error != nullptr) {
            *error = i18n::tr("aipdf::pdf::MediaSource", "文件打不开：%1").arg(filePath);
        }
        return false;
    }
    if (!info.isReadable()) {
        *kind = MediaErrorKind::OpenFailed;
        if (error != nullptr) {
            *error = i18n::tr("aipdf::pdf::MediaSource", "文件打不开：没有读这个文件的权限（%1）").arg(filePath);
        }
        return false;
    }

    const QUrl url = QUrl::fromLocalFile(info.absoluteFilePath());
    const QString urlString = url.toString();
    const std::wstring wide = urlString.toStdWString();

    ComPtr<IMFSourceReader> reader;
    const HRESULT hr =
        MFCreateSourceReaderFromURL(wide.c_str(), nullptr, reader.getAddressOf());
    if (FAILED(hr)) {
        *kind = classifyOpenFailure(hr);
        if (error != nullptr) {
            *error = (*kind == MediaErrorKind::NotMedia)
                         ? i18n::tr("aipdf::pdf::MediaSource", "不是媒体文件：%1").arg(filePath)
                         : i18n::tr("aipdf::pdf::MediaSource", "文件打不开：%1（%2）").arg(filePath, describeHResult(hr));
        }
        return false;
    }

    // 找视频轨与音频轨。
    ComPtr<IMFMediaType> nativeVideo;
    const HRESULT videoHr = reader->GetNativeMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nativeVideo.getAddressOf());
    ComPtr<IMFMediaType> nativeAudio;
    const HRESULT audioHr = reader->GetNativeMediaType(
        MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nativeAudio.getAddressOf());

    const bool hasVideo = SUCCEEDED(videoHr) && nativeVideo;
    const bool hasAudio = SUCCEEDED(audioHr) && nativeAudio;
    if (!hasVideo && !hasAudio) {
        // 文件能打开，但里面没有 MF 认识的音视频轨：不是媒体文件。
        *kind = MediaErrorKind::NotMedia;
        if (error != nullptr) {
            *error = i18n::tr("aipdf::pdf::MediaSource", "不是媒体文件：%1").arg(filePath);
        }
        return false;
    }

    out->reader = std::move(reader);
    out->hasVideo = hasVideo;
    out->hasAudio = hasAudio;
    out->nativeVideoType = std::move(nativeVideo);
    out->nativeAudioType = std::move(nativeAudio);
    return true;
}

// 读媒体总时长（毫秒）。
//
// 时长是**展示层**的属性，不在流的媒体类型里，要在源阅读器上问。
// 问不到就返回 0（不知道有多长），不要瞎猜。
qint64 readDurationMs(IMFSourceReader* reader) {
    if (reader == nullptr) return 0;
    PROPVARIANT value;
    PropVariantInit(&value);
    const HRESULT hr =
        reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &value);
    qint64 durationMs = 0;
    if (SUCCEEDED(hr) && value.vt == VT_UI8) {
        // MF 的时长单位是 100 纳秒。
        durationMs = static_cast<qint64>(value.uhVal.QuadPart / 10000);
    }
    PropVariantClear(&value);
    return durationMs;
}

// 把一个已经打开的源填成 MediaInfo。
void fillInfo(const OpenedSource& source, const QString& filePath, MediaInfo* info) {
    info->ok = true;
    info->errorKind = MediaErrorKind::None;
    info->filePath = filePath;
    info->mediaType = source.hasVideo ? QStringLiteral("video") : QStringLiteral("audio");
    info->durationMs = readDurationMs(source.reader.get());

    if (source.hasVideo) {
        int width = 0;
        int height = 0;
        readFrameSize(source.nativeVideoType.get(), &width, &height);
        info->width = width;
        info->height = height;

        // 帧率：MF_MT_FRAME_RATE 是「每秒多少帧」（有理数）。
        info->frameRate = readFrameRate(source.nativeVideoType.get());

        GUID subtype{};
        if (readGuid(source.nativeVideoType.get(), MF_MT_SUBTYPE, &subtype)) {
            info->codecName = codecNameFromSubtype(subtype, true);
        }
    }

    if (source.hasAudio) {
        info->sampleRate = static_cast<int>(
            readUint32(source.nativeAudioType.get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0));
        info->channelCount =
            static_cast<int>(readUint32(source.nativeAudioType.get(), MF_MT_AUDIO_NUM_CHANNELS, 0));

        GUID subtype{};
        if (readGuid(source.nativeAudioType.get(), MF_MT_SUBTYPE, &subtype)) {
            const QString audioCodec = codecNameFromSubtype(subtype, false);
            // 有画面也有声音时，视频的编码名更有意义，别被音频覆盖。
            if (info->codecName.isEmpty()) info->codecName = audioCodec;
        }
    }

    if (info->codecName.isEmpty()) {
        info->codecName = QStringLiteral("未知编码");
    }
}

}  // namespace

// ============================================================================
// 公开接口
// ============================================================================

MediaInfo openMediaFile(const QString& filePath, QString* error) {
    MediaInfo info;
    info.filePath = filePath;

    if (!MediaRuntime::instance().ready()) {
        info.errorKind = MediaErrorKind::OpenFailed;
        info.error = i18n::tr("aipdf::pdf::MediaSource", "系统媒体组件（Media Foundation）初始化失败");
        if (error != nullptr) *error = info.error;
        return info;
    }

    OpenedSource source;
    MediaErrorKind kind = MediaErrorKind::None;
    QString reason;
    if (!openSource(filePath, &source, &kind, &reason)) {
        info.errorKind = kind;
        info.error = reason;
        if (error != nullptr) *error = reason;
        return info;
    }

    fillInfo(source, filePath, &info);
    if (error != nullptr) error->clear();
    return info;
}

// ============================================================================
// MediaDecoder
// ============================================================================

struct MediaDecoder::Private {
    QString filePath;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType> nativeVideoType;
    ComPtr<IMFMediaType> nativeAudioType;
    MediaDecoderOptions options;
    MediaInfo info;
    // 音频协商失败时的提示（不算整体失败，但要留着给界面）。
    QString note;

    bool hasVideo = false;
    bool hasAudio = false;

    // 视频输出格式（RGB32）协商出来的尺寸与行跨度。
    int videoWidth = 0;
    int videoHeight = 0;
    int videoStride = 0;

    // 手上的待发帧。
    bool hasPendingVideo = false;
    bool hasPendingAudio = false;
    MediaFrame pendingVideo;
    MediaFrame pendingAudio;

    // 两条流是不是都读完了。
    bool videoDone = false;
    bool audioDone = false;
    bool opened = false;

    // 协商视频轨：把输出定成 RGB32。
    bool negotiateVideo(QString* error) {
        ComPtr<IMFMediaType> output;
        if (FAILED(MFCreateMediaType(output.getAddressOf()))) {
            if (error != nullptr) *error = i18n::tr("aipdf::pdf::MediaSource", "创建媒体类型失败");
            return false;
        }
        output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        // 刻意**不**在这里写 MF_MT_FRAME_SIZE：
        // 原生类型里的宽高有 32 位与 64 位两种存法，写错了会让
        // 协商直接失败（MF_E_INVALIDMEDIATYPE）。协商成功之后
        // 从实际类型里读回来更准。

        const HRESULT hr = reader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output.get());
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::pdf::MediaSource", "无法把视频轨转换成 RGB32：%1").arg(describeHResult(hr));
            }
            return false;
        }

        // 协商完再看一次实际尺寸与行跨度：解码器可能给回不一样的值。
        ComPtr<IMFMediaType> negotiated;
        if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                   negotiated.getAddressOf()))) {
            readFrameSize(negotiated.get(), &videoWidth, &videoHeight);
            videoStride = static_cast<int>(readUint32(negotiated.get(), MF_MT_DEFAULT_STRIDE, 0));
        }
        if (videoStride <= 0) {
            videoStride = videoWidth * 4;
        }
        return true;
    }

    // 协商音频轨：输出定成 PCM。
    bool negotiateAudio(QString* error) {
        ComPtr<IMFMediaType> output;
        if (FAILED(MFCreateMediaType(output.getAddressOf()))) {
            if (error != nullptr) *error = i18n::tr("aipdf::pdf::MediaSource", "创建媒体类型失败");
            return false;
        }
        output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        output->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);

        // 原生类型是不是已经是 PCM。
        GUID nativeSubtype{};
        const bool nativeIsPcm =
            readGuid(nativeAudioType.get(), MF_MT_SUBTYPE, &nativeSubtype) &&
            IsEqualGUID(nativeSubtype, MFAudioFormat_PCM);

        // 采样率与声道数照着原生类型来，别硬写 44100/2：
        // 写死了会让 MF 去重采样，既慢又和文件对不上。
        const UINT32 rate = readUint32(nativeAudioType.get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
        const UINT32 channels = readUint32(nativeAudioType.get(), MF_MT_AUDIO_NUM_CHANNELS, 2);
        UINT32 bits = 16;
        if (nativeIsPcm) {
            // 只有原生就是 PCM 时，位深才有意义；压缩格式（MP3 等）
            // 根本没有「位深」这个属性，硬读会读到 0。
            const UINT32 nativeBits =
                readUint32(nativeAudioType.get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (nativeBits != 16) {
                // 只支持 16 位：别的位深要重采样，而重采样要自己写，不值当。
                if (error != nullptr) {
                    *error = i18n::tr("aipdf::pdf::MediaSource", "只支持 16 位 PCM 的音频");
                }
                return false;
            }
            bits = nativeBits;
        }
        output->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
        output->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
        output->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bits);
        output->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * (bits / 8));
        output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * (bits / 8));

        const HRESULT hr = reader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, output.get());
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::pdf::MediaSource", "无法把音频轨转换成 PCM：%1").arg(describeHResult(hr));
            }
            return false;
        }
        return true;
    }

    // 读一个视频样本并换算成图片。
    //
    // 返回 true 表示读到了（out 里有图）；返回 false 表示出错。
    // 流结束的情况不算出错：done 置真，out->endOfStream 置真。
    bool readVideoFrame(MediaFrame* out, bool* done, QString* error) {
        ComPtr<IMFSample> sample;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        const HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags,
                                              &timestamp, sample.getAddressOf());
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::pdf::MediaSource", "解码失败：%1").arg(describeHResult(hr));
            }
            return false;
        }
        if (!sample || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            *done = true;
            out->endOfStream = true;
            return true;
        }

        // MF 的时间戳单位是 100 纳秒，换成微秒。
        out->timestampUs = static_cast<qint64>(timestamp / 10);
        out->endOfStream = false;

        ComPtr<IMFMediaBuffer> buffer;
        const HRESULT bufferHr = sample->ConvertToContiguousBuffer(buffer.getAddressOf());
        if (FAILED(bufferHr) || !buffer) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::pdf::MediaSource", "解码失败：取不到画面数据（%1）").arg(describeHResult(bufferHr));
            }
            return false;
        }

        BYTE* bytes = nullptr;
        DWORD length = 0;
        const HRESULT lockHr = buffer->Lock(&bytes, &length, nullptr);
        if (FAILED(lockHr) || bytes == nullptr) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::pdf::MediaSource", "解码失败：读不出画面数据（%1）").arg(describeHResult(lockHr));
            }
            return false;
        }

        // 尺寸与行跨度：媒体类型里写了就用它写的；
        // 没写就从「行跨度 + 缓冲长度」倒推——这一步是兜底，
        // 因为不同来源的媒体类型给的字段并不一样。
        int stride = videoStride;
        int width = videoWidth;
        int height = videoHeight;
        if (stride <= 0 && width > 0) stride = width * 4;
        if (width <= 0 && stride > 0) width = stride / 4;
        if (height <= 0 && stride > 0 && length > 0) {
            height = static_cast<int>(static_cast<qint64>(length) / stride);
        }
        // 尺寸对不上就别硬读：越界读到别人的内存比报错难查得多。
        if (width <= 0 || height <= 0 || stride < width * 4 ||
            static_cast<qint64>(length) < static_cast<qint64>(stride) * height) {
            buffer->Unlock();
            if (error != nullptr) {
                *error = i18n::tr("aipdf::pdf::MediaSource", "解码失败：画面尺寸和实际数据对不上");
            }
            return false;
        }

        QImage image = rgb32BufferToImage(reinterpret_cast<const uchar*>(bytes), width, height, stride);
        buffer->Unlock();
        if (image.isNull()) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::pdf::MediaSource", "解码失败：画面换算失败");
            }
            return false;
        }

        // 超过上限就等比缩小：界面上媒体面板就那么大，
        // 解一张 4K 帧既慢又占内存。
        if (options.maxFrameWidth > 0 && image.width() > options.maxFrameWidth) {
            int scaledHeight = image.height() * options.maxFrameWidth / image.width();
            if (scaledHeight < 1) scaledHeight = 1;
            image = image.scaled(options.maxFrameWidth, scaledHeight, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        }
        out->image = image;
        return true;
    }

    // 读一个音频样本并取出 PCM。
    bool readAudioFrame(MediaFrame* out, bool* done, QString* error) {
        ComPtr<IMFSample> sample;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        const HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags,
                                              &timestamp, sample.getAddressOf());
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::pdf::MediaSource", "解码失败：%1").arg(describeHResult(hr));
            }
            return false;
        }
        if (!sample || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            *done = true;
            out->endOfStream = true;
            return true;
        }

        out->timestampUs = static_cast<qint64>(timestamp / 10);
        out->endOfStream = false;

        ComPtr<IMFMediaBuffer> buffer;
        const HRESULT bufferHr = sample->ConvertToContiguousBuffer(buffer.getAddressOf());
        if (FAILED(bufferHr) || !buffer) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::pdf::MediaSource", "解码失败：取不到声音数据（%1）").arg(describeHResult(bufferHr));
            }
            return false;
        }

        BYTE* bytes = nullptr;
        DWORD length = 0;
        const HRESULT lockHr = buffer->Lock(&bytes, &length, nullptr);
        if (FAILED(lockHr) || bytes == nullptr) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::pdf::MediaSource", "解码失败：读不出声音数据（%1）").arg(describeHResult(lockHr));
            }
            return false;
        }
        out->pcm = QByteArray(reinterpret_cast<const char*>(bytes), static_cast<int>(length));
        buffer->Unlock();
        return true;
    }
};

MediaDecoder::MediaDecoder(const QString& filePath) : d(new Private()) { d->filePath = filePath; }

MediaDecoder::~MediaDecoder() { close(); }

bool MediaDecoder::open(const MediaDecoderOptions& options, QString* error) {
    close();
    d->options = options;
    d->info = MediaInfo();
    d->info.filePath = d->filePath;
    d->note.clear();

    if (error != nullptr) error->clear();

    if (!MediaRuntime::instance().ready()) {
        const QString reason = i18n::tr("aipdf::pdf::MediaSource", "系统媒体组件（Media Foundation）初始化失败");
        d->info.errorKind = MediaErrorKind::OpenFailed;
        d->info.error = reason;
        if (error != nullptr) *error = reason;
        return false;
    }

    OpenedSource source;
    MediaErrorKind kind = MediaErrorKind::None;
    QString reason;
    if (!openSource(d->filePath, &source, &kind, &reason)) {
        d->info.errorKind = kind;
        d->info.error = reason;
        if (error != nullptr) *error = reason;
        return false;
    }

    // 先读信息（这时候 reader 还在 source 里），再把句柄搬过来。
    fillInfo(source, d->filePath, &d->info);

    d->reader = std::move(source.reader);
    d->hasVideo = source.hasVideo;
    d->hasAudio = source.hasAudio;
    d->nativeVideoType = std::move(source.nativeVideoType);
    d->nativeAudioType = std::move(source.nativeAudioType);
    d->videoDone = !d->hasVideo;
    d->audioDone = !d->hasAudio;

    if (d->hasVideo) {
        QString why;
        if (!d->negotiateVideo(&why)) {
            d->info.ok = false;
            d->info.errorKind = MediaErrorKind::DecodeFailed;
            d->info.error = why;
            if (error != nullptr) *error = why;
            close();
            return false;
        }
        // 协商之后的尺寸才是真正会解出来的尺寸。
        d->info.width = d->videoWidth;
        d->info.height = d->videoHeight;
    }

    if (d->hasAudio && options.decodeAudio) {
        QString why;
        if (!d->negotiateAudio(&why)) {
            // 音频协商不上不算致命：有画面就够了。
            // 但要记下来，界面上可以提示「这个文件的声音放不了」。
            d->note = why;
            d->audioDone = true;
        }
    } else if (d->hasAudio) {
        d->audioDone = true;
    }

    d->opened = true;
    return true;
}

void MediaDecoder::close() {
    d->reader.reset();
    d->nativeVideoType.reset();
    d->nativeAudioType.reset();
    d->hasPendingVideo = false;
    d->hasPendingAudio = false;
    d->videoDone = true;
    d->audioDone = true;
    d->opened = false;
}

MediaInfo MediaDecoder::info() const { return d->info; }

bool MediaDecoder::isOpen() const { return d->opened; }

QString MediaDecoder::filePath() const { return d->filePath; }

bool MediaDecoder::nextFrame(MediaFrame* frame, QString* error) {
    if (frame == nullptr) return false;
    *frame = MediaFrame();
    if (error != nullptr) error->clear();

    if (!d->opened || !d->reader) {
        if (error != nullptr) *error = i18n::tr("aipdf::pdf::MediaSource", "解码器还没有打开");
        return false;
    }

    // 先把两条流各取一帧到手上。
    if (d->hasVideo && !d->hasPendingVideo && !d->videoDone) {
        d->pendingVideo = MediaFrame();
        bool done = false;
        QString why;
        if (!d->readVideoFrame(&d->pendingVideo, &done, &why)) {
            if (error != nullptr) *error = why;
            d->info.errorKind = MediaErrorKind::DecodeFailed;
            d->info.error = why;
            return false;
        }
        if (done) {
            d->videoDone = true;
        } else {
            d->hasPendingVideo = true;
        }
    }

    const bool wantAudio = d->hasAudio && d->options.decodeAudio;
    if (wantAudio && !d->hasPendingAudio && !d->audioDone) {
        d->pendingAudio = MediaFrame();
        bool done = false;
        QString why;
        if (!d->readAudioFrame(&d->pendingAudio, &done, &why)) {
            if (error != nullptr) *error = why;
            d->info.errorKind = MediaErrorKind::DecodeFailed;
            d->info.error = why;
            return false;
        }
        if (done) {
            d->audioDone = true;
        } else {
            d->hasPendingAudio = true;
        }
    }

    // 两条流都读完了：给一个结束帧。
    if (!d->hasPendingVideo && !d->hasPendingAudio) {
        frame->endOfStream = true;
        return true;
    }

    // 谁的时间戳小先给谁：这样播放时音画不会各走各的。
    bool takeVideo = true;
    if (d->hasPendingVideo && d->hasPendingAudio) {
        takeVideo = d->pendingVideo.timestampUs <= d->pendingAudio.timestampUs;
    } else if (d->hasPendingAudio) {
        takeVideo = false;
    }

    if (takeVideo) {
        *frame = d->pendingVideo;
        d->hasPendingVideo = false;
    } else {
        *frame = d->pendingAudio;
        d->hasPendingAudio = false;
    }
    return true;
}

QImage rgb32BufferToImage(const uchar* src, int width, int height, int srcStride) {
    if (src == nullptr || width <= 0 || height <= 0) return QImage();

    const int stride = srcStride > 0 ? srcStride : width * 4;
    if (stride < width * 4) return QImage();

    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull()) return image;

    for (int y = 0; y < height; ++y) {
        const uchar* in = src + static_cast<qint64>(y) * stride;
        uchar* out = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            // MF 的 RGB32（D3DFMT_X8R8G8B8）在内存里是 B, G, R, X；
            // QImage 的 Format_RGB32（0xffRRGGBB）也是 B, G, R, 0xff。
            // 两边一致，所以这里逐通道搬，不靠 memcpy。
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
            out[3] = 0xff;
            in += 4;
            out += 4;
        }
    }
    return image;
}

}  // namespace aipdf::pdf
