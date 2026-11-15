#include "app/AudioRecorder.h"

#include <QVector>

#include <windows.h>
#include <mmsystem.h>

namespace aipdf::app {

namespace {

constexpr int kSampleRate = 16000;
constexpr int kChannels = 1;
constexpr int kBitsPerSample = 16;
constexpr int kBufferMs = 200;

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

struct AudioRecorder::Impl {
    HWAVEIN handle = nullptr;
    WAVEHDR header{};
    QByteArray buffer;
    QByteArray pcm;
    bool recording = false;
};

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

    const MMRESULT opened = waveInOpen(&impl_->handle, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (opened != MMSYSERR_NOERROR) {
        if (error != nullptr) {
            *error = QStringLiteral("打不开录音设备（错误码 %1）。").arg(int(opened));
        }
        impl_->handle = nullptr;
        return false;
    }

    impl_->buffer = QByteArray(kSampleRate * kChannels * kBitsPerSample / 8 * kBufferMs / 1000, 0);
    impl_->header = WAVEHDR{};
    impl_->header.lpData = impl_->buffer.data();
    impl_->header.dwBufferLength = static_cast<DWORD>(impl_->buffer.size());
    waveInPrepareHeader(impl_->handle, &impl_->header, sizeof(WAVEHDR));
    waveInAddBuffer(impl_->handle, &impl_->header, sizeof(WAVEHDR));
    if (waveInStart(impl_->handle) != MMSYSERR_NOERROR) {
        if (error != nullptr) {
            *error = QStringLiteral("录音启动失败。");
        }
        waveInClose(impl_->handle);
        impl_->handle = nullptr;
        return false;
    }
    impl_->recording = true;
    return true;
}

QByteArray AudioRecorder::stop() {
    if (impl_ == nullptr || !impl_->recording) {
        return {};
    }
    waveInStop(impl_->handle);
    waveInReset(impl_->handle);
    waveInUnprepareHeader(impl_->handle, &impl_->header, sizeof(WAVEHDR));
    waveInClose(impl_->handle);
    impl_->handle = nullptr;
    impl_->recording = false;

    // 单缓冲录音：拿到的是最后一段。真实产品要环形多缓冲，
    // 这里的定位是"快速留个语音备注"，够用且不复杂。
    const DWORD recorded = impl_->header.dwBytesRecorded;
    QByteArray pcm = impl_->buffer.left(static_cast<int>(recorded));
    return buildWav(pcm);
}

bool AudioRecorder::isRecording() const {
    return impl_ != nullptr && impl_->recording;
}

}  // namespace aipdf::app
