#include "app/AudioRecorder.h"

#include <QVector>
#include <QMutex>
#include <QMutexLocker>
#include <array>

#include <windows.h>
#include <mmsystem.h>

namespace aipdf::app {

namespace {

constexpr int kSampleRate = 16000;
constexpr int kChannels = 1;
constexpr int kBitsPerSample = 16;
constexpr int kBufferMs = 200;
constexpr int kBufferCount = 6;

QByteArray buildWav(const QByteArray& pcm) {
    const int byteRate = kSampleRate * kChannels * kBitsPerSample / 8;
    const int blockAlign = kChannels * kBitsPerSample / 8;
    QByteArray wav;
    wav.append("RIFF");
    auto appendU32 = [&wav](quint32 value) {
        wav.append(static_cast<char>(value & 0xFF));
        wav.append(static_cast<char>((value >> 8) & 0xFF));
        wav.append(static_cast<char>((value >> 16) & 0xFF));
        wav.append(static_cast<char>((value >> 24) & 0xFF));
    };
    auto appendU16 = [&wav](quint16 value) {
        wav.append(static_cast<char>(value & 0xFF));
        wav.append(static_cast<char>((value >> 8) & 0xFF));
    };
    appendU32(static_cast<quint32>(36 + pcm.size()));
    wav.append("WAVEfmt ");
    appendU32(16);
    appendU16(1);  // PCM
    appendU16(static_cast<quint16>(kChannels));
    appendU32(kSampleRate);
    appendU32(static_cast<quint32>(byteRate));
    appendU16(static_cast<quint16>(blockAlign));
    appendU16(static_cast<quint16>(kBitsPerSample));
    wav.append("data");
    appendU32(static_cast<quint32>(pcm.size()));
    wav.append(pcm);
    return wav;
}

}  // namespace

struct CaptureState {
    HWAVEIN handle = nullptr;
    std::array<WAVEHDR, kBufferCount> headers{};
    std::array<QByteArray, kBufferCount> buffers;
    QByteArray pcm;
    QMutex mutex;
    bool recording = false;
};

void CALLBACK waveInCallback(HWAVEIN handle, UINT message, DWORD_PTR instance,
                             DWORD_PTR parameter1, DWORD_PTR) {
    if (message != WIM_DATA || instance == 0 || parameter1 == 0) return;
    auto* state = reinterpret_cast<CaptureState*>(instance);
    auto* header = reinterpret_cast<WAVEHDR*>(parameter1);
    QMutexLocker locker(&state->mutex);
    if (header->dwBytesRecorded > 0) {
        state->pcm.append(header->lpData, static_cast<int>(header->dwBytesRecorded));
    }
    if (state->recording && state->handle == handle) {
        header->dwBytesRecorded = 0;
        waveInAddBuffer(handle, header, sizeof(WAVEHDR));
    }
}

struct AudioRecorder::Impl : CaptureState {};

AudioRecorder::~AudioRecorder() {
    if (impl_ != nullptr) {
        stop();
        delete impl_;
        impl_ = nullptr;
    }
}

bool AudioRecorder::start(QString* error) {
    if (impl_ != nullptr && impl_->recording) {
        return true;
    }
    if (impl_ == nullptr) {
        impl_ = new Impl();
    }

    if (waveInGetNumDevs() == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("这台机器没有录音设备。");
        }
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = kChannels;
    format.nSamplesPerSec = kSampleRate;
    format.wBitsPerSample = kBitsPerSample;
    format.nBlockAlign = kChannels * kBitsPerSample / 8;
    format.nAvgBytesPerSec = kSampleRate * format.nBlockAlign;

    impl_->pcm.clear();
    const MMRESULT opened = waveInOpen(
        &impl_->handle, WAVE_MAPPER, &format,
        reinterpret_cast<DWORD_PTR>(&waveInCallback),
        reinterpret_cast<DWORD_PTR>(static_cast<CaptureState*>(impl_)), CALLBACK_FUNCTION);
    if (opened != MMSYSERR_NOERROR) {
        if (error != nullptr) {
            *error = QStringLiteral("打不开录音设备（错误码 %1）。").arg(int(opened));
        }
        impl_->handle = nullptr;
        return false;
    }

    const int bufferBytes = kSampleRate * kChannels * kBitsPerSample / 8 * kBufferMs / 1000;
    impl_->recording = true;
    for (int i = 0; i < kBufferCount; ++i) {
        impl_->buffers[i] = QByteArray(bufferBytes, 0);
        impl_->headers[i] = WAVEHDR{};
        impl_->headers[i].lpData = impl_->buffers[i].data();
        impl_->headers[i].dwBufferLength = static_cast<DWORD>(bufferBytes);
        if (waveInPrepareHeader(impl_->handle, &impl_->headers[i], sizeof(WAVEHDR)) !=
                MMSYSERR_NOERROR ||
            waveInAddBuffer(impl_->handle, &impl_->headers[i], sizeof(WAVEHDR)) !=
                MMSYSERR_NOERROR) {
            impl_->recording = false;
            waveInReset(impl_->handle);
            for (int j = 0; j <= i; ++j) {
                waveInUnprepareHeader(impl_->handle, &impl_->headers[j], sizeof(WAVEHDR));
            }
            waveInClose(impl_->handle);
            impl_->handle = nullptr;
            if (error != nullptr) *error = QStringLiteral("无法准备录音缓冲区。");
            return false;
        }
    }
    if (waveInStart(impl_->handle) != MMSYSERR_NOERROR) {
        if (error != nullptr) {
            *error = QStringLiteral("录音启动失败。");
        }
        impl_->recording = false;
        waveInReset(impl_->handle);
        for (auto& header : impl_->headers) {
            waveInUnprepareHeader(impl_->handle, &header, sizeof(WAVEHDR));
        }
        waveInClose(impl_->handle);
        impl_->handle = nullptr;
        return false;
    }
    return true;
}

QByteArray AudioRecorder::stop() {
    if (impl_ == nullptr || !impl_->recording) {
        return {};
    }
    {
        QMutexLocker locker(&impl_->mutex);
        impl_->recording = false;
    }
    waveInStop(impl_->handle);
    waveInReset(impl_->handle);
    for (auto& header : impl_->headers) {
        waveInUnprepareHeader(impl_->handle, &header, sizeof(WAVEHDR));
    }
    waveInClose(impl_->handle);
    impl_->handle = nullptr;
    QMutexLocker locker(&impl_->mutex);
    const QByteArray pcm = impl_->pcm;
    impl_->pcm.clear();
    return buildWav(pcm);
}

bool AudioRecorder::isRecording() const {
    return impl_ != nullptr && impl_->recording;
}

}  // namespace aipdf::app
