#include "app/controllers/PcmPlayer.h"

#include "i18n/Translate.h"

#include <QMutex>
#include <QMutexLocker>

#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <mutex>
#include <set>
#include <vector>

namespace aipdf::app {

namespace {

// ============================================================================
// 内部状态
// ============================================================================

// 一块 PCM 的缓冲。
//
// 为什么每块单独分配：WAVEHDR 指向的内存必须在设备播完之前一直有效，
// 而播放是**异步**的——事先不知道它什么时候播完。改成预先分配固定数量的大缓冲，
// 缓冲用光就只能等，在界面线程上等就是卡界面。
// 所以这里按块分配：播完一块回收一块。
struct PcmChunk {
    QByteArray data;
    WAVEHDR header{};
};

// 播放器的内部状态。
struct PcmState {
    HWAVEOUT handle = nullptr;
    int sampleRate = 0;
    int channelCount = 0;
    bool paused = false;
    // 还在声卡队列里的字节数。
    qint64 queuedBytes = 0;
    // 保护上面这些字段与下面的缓冲表。
    QMutex mutex;
    std::vector<PcmChunk*> chunks;
};

// 全局登记表：记录哪些播放状态还有效。
//
// 为什么需要它：waveOut 的回调跑在驱动线程上，可能比 close() 晚一步
// 才把积压的 WOM_DONE 送回来。那一刻状态对象可能已经被释放了，
// 「读状态里的有效标志」本身就可能崩。
// 登记表是进程级的，活得比任何状态都久：回调先在登记表里找状态，
// 找不到就直接走人；close() 在释放状态之前先把状态从登记表里摘掉。
// 两边用同一把锁，于是「摘状态」和「回调正在处理状态」不会同时发生。
std::mutex g_stateRegistryMutex;
std::set<PcmState*> g_stateRegistry;

// 还开着的 waveOut 设备数：给「有没有漏句柄」一个能测的判据。
std::atomic<int> g_liveDeviceCount{0};

void CALLBACK waveOutCallback(HWAVEOUT handle, UINT message, DWORD_PTR instance,
                              DWORD_PTR parameter1, DWORD_PTR) {
    if (message != WOM_DONE || instance == 0 || parameter1 == 0) return;
    auto* state = reinterpret_cast<PcmState*>(instance);
    auto* header = reinterpret_cast<WAVEHDR*>(parameter1);

    PcmChunk* done = nullptr;
    {
        // 登记表的锁在整个处理过程里都持有：
        // 这样 close() 不可能在回调处理到一半时把状态与缓冲释放掉。
        std::lock_guard<std::mutex> registryLock(g_stateRegistryMutex);
        if (g_stateRegistry.find(state) == g_stateRegistry.end()) {
            // 设备已经关了：迟到的消息，丢掉。
            return;
        }
        QMutexLocker locker(&state->mutex);
        if (state->handle != handle) return;
        for (auto it = state->chunks.begin(); it != state->chunks.end(); ++it) {
            if (&(*it)->header == header) {
                done = *it;
                state->chunks.erase(it);
                break;
            }
        }
        if (done != nullptr) {
            state->queuedBytes -= static_cast<qint64>(done->header.dwBufferLength);
        }
    }
    if (done == nullptr) return;
    // 这一块已经播完：不 unprepare 的话，关闭句柄时会报「还有缓冲没放完」。
    waveOutUnprepareHeader(handle, &done->header, sizeof(WAVEHDR));
    delete done;
}

// 把失败原因写进调用方的字符串。
void writeError(QString* error, PcmPlayerErrorKind kind, int code) {
    if (error != nullptr) *error = pcmPlayerErrorText(kind, code);
}

}  // namespace

// ============================================================================
// 错误文案
// ============================================================================

QString pcmPlayerErrorText(PcmPlayerErrorKind kind, int code) {
    switch (kind) {
    case PcmPlayerErrorKind::None:
        return QString();
    case PcmPlayerErrorKind::NoDevice:
        return i18n::tr("aipdf::app::MediaPreview", "这台机器没有音频输出设备。");
    case PcmPlayerErrorKind::OpenFailed:
        return code > 0
                   ? i18n::tr("aipdf::app::MediaPreview", "打不开音频输出设备（错误码 %1）。").arg(code)
                   : i18n::tr("aipdf::app::MediaPreview", "打不开音频输出设备。");
    case PcmPlayerErrorKind::WriteFailed:
        return code > 0
                   ? i18n::tr("aipdf::app::MediaPreview", "写入音频数据失败（错误码 %1）。").arg(code)
                   : i18n::tr("aipdf::app::MediaPreview", "写入音频数据失败：播放器还没有打开。");
    }
    return QString();
}

// ============================================================================
// PcmPlayer
// ============================================================================

struct PcmPlayer::Impl : PcmState {};

PcmPlayer::PcmPlayer() : impl_(std::make_unique<Impl>()) {}

PcmPlayer::~PcmPlayer() { close(); }

bool PcmPlayer::open(int sampleRate, int channelCount, QString* error) {
    // 重新打开之前先把上一个设备关干净：句柄只有一个，留着旧的会漏。
    close();

    if (sampleRate <= 0 || channelCount <= 0) {
        writeError(error, PcmPlayerErrorKind::OpenFailed, 0);
        return false;
    }

    // 没有输出设备是一种**单独**的失败：和「设备在但打不开」要分开说。
    if (waveOutGetNumDevs() == 0) {
        writeError(error, PcmPlayerErrorKind::NoDevice, 0);
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(channelCount);
    format.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HWAVEOUT handle = nullptr;
    const MMRESULT opened = waveOutOpen(&handle, WAVE_MAPPER, &format,
                                        reinterpret_cast<DWORD_PTR>(&waveOutCallback),
                                        reinterpret_cast<DWORD_PTR>(impl_.get()),
                                        CALLBACK_FUNCTION);
    if (opened != MMSYSERR_NOERROR || handle == nullptr) {
        writeError(error, PcmPlayerErrorKind::OpenFailed, static_cast<int>(opened));
        return false;
    }

    {
        QMutexLocker locker(&impl_->mutex);
        impl_->handle = handle;
        impl_->sampleRate = sampleRate;
        impl_->channelCount = channelCount;
        impl_->paused = false;
        impl_->queuedBytes = 0;
    }
    {
        std::lock_guard<std::mutex> registryLock(g_stateRegistryMutex);
        g_stateRegistry.insert(impl_.get());
    }
    g_liveDeviceCount.fetch_add(1);

    writeError(error, PcmPlayerErrorKind::None, 0);
    return true;
}

bool PcmPlayer::pushPcm(const QByteArray& pcm, QString* error) {
    if (pcm.isEmpty()) return true;
    if (!isOpen()) {
        writeError(error, PcmPlayerErrorKind::WriteFailed, 0);
        return false;
    }

    HWAVEOUT handle = nullptr;
    {
        QMutexLocker locker(&impl_->mutex);
        handle = impl_->handle;
    }
    if (handle == nullptr) {
        writeError(error, PcmPlayerErrorKind::WriteFailed, 0);
        return false;
    }

    auto* chunk = new PcmChunk();
    chunk->data = pcm;
    chunk->header.lpData = chunk->data.data();
    chunk->header.dwBufferLength = static_cast<DWORD>(chunk->data.size());
    chunk->header.dwFlags = 0;

    if (waveOutPrepareHeader(handle, &chunk->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        delete chunk;
        writeError(error, PcmPlayerErrorKind::WriteFailed, 0);
        return false;
    }

    {
        // 先登记再写：写完之后设备可能**立刻**就播完这一块，
        // 回调会马上来要这块缓冲——晚登记就找不到它，那一块就漏了。
        QMutexLocker locker(&impl_->mutex);
        impl_->chunks.push_back(chunk);
        impl_->queuedBytes += static_cast<qint64>(chunk->header.dwBufferLength);
    }

    const MMRESULT written = waveOutWrite(handle, &chunk->header, sizeof(WAVEHDR));
    if (written != MMSYSERR_NOERROR) {
        {
            QMutexLocker locker(&impl_->mutex);
            for (auto it = impl_->chunks.begin(); it != impl_->chunks.end(); ++it) {
                if (*it == chunk) {
                    impl_->chunks.erase(it);
                    break;
                }
            }
            impl_->queuedBytes -= static_cast<qint64>(chunk->header.dwBufferLength);
        }
        waveOutUnprepareHeader(handle, &chunk->header, sizeof(WAVEHDR));
        delete chunk;
        writeError(error, PcmPlayerErrorKind::WriteFailed, static_cast<int>(written));
        return false;
    }

    writeError(error, PcmPlayerErrorKind::None, 0);
    return true;
}

bool PcmPlayer::pause() {
    QMutexLocker locker(&impl_->mutex);
    if (impl_->handle == nullptr) return false;
    if (impl_->paused) return true;
    const MMRESULT result = waveOutPause(impl_->handle);
    if (result != MMSYSERR_NOERROR) return false;
    impl_->paused = true;
    return true;
}

bool PcmPlayer::resume() {
    QMutexLocker locker(&impl_->mutex);
    if (impl_->handle == nullptr) return false;
    if (!impl_->paused) return true;
    const MMRESULT result = waveOutRestart(impl_->handle);
    if (result != MMSYSERR_NOERROR) return false;
    impl_->paused = false;
    return true;
}

void PcmPlayer::stop() {
    HWAVEOUT handle = nullptr;
    std::vector<PcmChunk*> chunks;
    {
        QMutexLocker locker(&impl_->mutex);
        handle = impl_->handle;
        chunks.swap(impl_->chunks);
        impl_->queuedBytes = 0;
        impl_->paused = false;
    }
    if (handle == nullptr) return;

    // 驱动调用不放在状态锁里：waveOutReset 可能等驱动线程，
    // 而驱动线程的回调会来拿状态锁——拿着锁等它就是死锁。
    waveOutReset(handle);
    for (auto* chunk : chunks) {
        waveOutUnprepareHeader(handle, &chunk->header, sizeof(WAVEHDR));
        delete chunk;
    }
}

void PcmPlayer::close() {
    // 先从登记表里摘掉：摘的时候要拿登记表的锁，
    // 于是「正在处理这个状态的回调」一定已经跑完了，
    // 后面释放状态与缓冲才是安全的。
    {
        std::lock_guard<std::mutex> registryLock(g_stateRegistryMutex);
        g_stateRegistry.erase(impl_.get());
    }

    HWAVEOUT handle = nullptr;
    std::vector<PcmChunk*> chunks;
    {
        QMutexLocker locker(&impl_->mutex);
        handle = impl_->handle;
        chunks.swap(impl_->chunks);
        impl_->queuedBytes = 0;
        impl_->paused = false;
    }
    if (handle == nullptr) return;

    // ---- 释放句柄与缓冲：这一段必须整体在，缺一步就是漏 ----
    // 顺序不能变：先 reset 让设备不再引用这些缓冲，
    // 再把每一块 unprepare 掉，最后关句柄。
    waveOutReset(handle);
    for (auto* chunk : chunks) {
        waveOutUnprepareHeader(handle, &chunk->header, sizeof(WAVEHDR));
        delete chunk;
    }
    waveOutClose(handle);
    {
        QMutexLocker locker(&impl_->mutex);
        impl_->handle = nullptr;
    }
    g_liveDeviceCount.fetch_sub(1);
    // ---- 释放段结束 ----
}

bool PcmPlayer::isOpen() const {
    QMutexLocker locker(&impl_->mutex);
    return impl_->handle != nullptr;
}

bool PcmPlayer::isPaused() const {
    QMutexLocker locker(&impl_->mutex);
    return impl_->paused;
}

qint64 PcmPlayer::queuedBytes() const {
    QMutexLocker locker(&impl_->mutex);
    return impl_->queuedBytes;
}

qint64 PcmPlayer::queuedDurationMs() const {
    QMutexLocker locker(&impl_->mutex);
    if (impl_->sampleRate <= 0 || impl_->channelCount <= 0) return 0;
    const qint64 bytesPerSecond =
        static_cast<qint64>(impl_->sampleRate) * impl_->channelCount * 16 / 8;
    if (bytesPerSecond <= 0) return 0;
    return impl_->queuedBytes * 1000 / bytesPerSecond;
}

int PcmPlayer::liveDeviceCount() { return g_liveDeviceCount.load(); }

}  // namespace aipdf::app
