#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::pdf {

// 一段可以直接写进 PDF /Sound 流的音频。
//
// PDF 的 /Sound 是裸 PCM（/R 采样率、/C 声道数、/B 位深），不是 WAV 容器：
// 把整个 WAV 塞进去，阅读器会按格式去解释文件头，放出来是噪音。所以必须先把
// 44 字节的 RIFF 头剥掉，并把真正的格式参数写进字典。
struct SoundPayload {
    QByteArray pcm;
    int rate = 8000;
    int channels = 1;
    int bits = 8;
    // 解析失败时的原因（成功时为空）。
    QString error;

    bool isValid() const { return !pcm.isEmpty() && error.isEmpty(); }
};

// 解析 WAV（RIFF/PCM）。只接受 PCM（format=1），其余格式明确报错——
// 悄悄当成 PCM 写进去，用户听到的就是噪声，比失败更糟。
SoundPayload parseWav(const QByteArray& bytes);

}  // namespace aipdf::pdf
