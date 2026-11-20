#include "app/recording/MotionJpegAviWriter.h"

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QIODevice>

namespace aipdf::app {
namespace {

constexpr quint32 kAviFlagHasIndex = 0x00000010u;  // AVIF_HASINDEX
constexpr quint32 kIndexFlagKeyFrame = 0x00000010u;  // AVIIF_KEYFRAME
constexpr int kAvihSize = 56;
constexpr int kStrhSize = 56;
constexpr int kStrfSize = 40;

void appendU16(QByteArray& out, quint16 value) {
    char bytes[2];
    bytes[0] = static_cast<char>(value & 0xFFu);
    bytes[1] = static_cast<char>((value >> 8) & 0xFFu);
    out.append(bytes, 2);
}

void appendU32(QByteArray& out, quint32 value) {
    char bytes[4];
    for (int i = 0; i < 4; ++i) {
        bytes[i] = static_cast<char>((value >> (8 * i)) & 0xFFu);
    }
    out.append(bytes, 4);
}

void appendFourCC(QByteArray& out, const char* code) {
    out.append(code, 4);
}

}  // namespace

MotionJpegAviWriter::MotionJpegAviWriter(QIODevice* device, int width, int height,
                                         int framesPerSecond)
    : device_(device), width_(width), height_(height), fps_(framesPerSecond) {}

bool MotionJpegAviWriter::isJpeg(const QByteArray& data) {
    // SOI (FFD8) 开头、EOI (FFD9) 结尾才算一段完整的 JPEG。
    //
    // 为什么查两头而不是只查头：编码器中途失败会留下「有头没尾」的半张图，
    // 那种字节喂给播放器会在那一帧花屏，而文件本身仍然是「合法可打开」的。
    if (data.size() < 4) return false;
    const unsigned char* raw = reinterpret_cast<const unsigned char*>(data.constData());
    if (raw[0] != 0xFFu || raw[1] != 0xD8u) return false;
    const qint64 tail = data.size() - 2;
    if (raw[tail] != 0xFFu || raw[tail + 1] != 0xD9u) return false;
    return true;
}

void MotionJpegAviWriter::putU16(char* target, quint16 value) const {
    target[0] = static_cast<char>(value & 0xFFu);
    target[1] = static_cast<char>((value >> 8) & 0xFFu);
}

void MotionJpegAviWriter::putU32(char* target, quint32 value) const {
    for (int i = 0; i < 4; ++i) {
        target[i] = static_cast<char>((value >> (8 * i)) & 0xFFu);
    }
}

bool MotionJpegAviWriter::patchU32(qint64 position, quint32 value) {
    if (position < 0) return true;
    const qint64 saved = device_->pos();
    if (!device_->seek(position)) return false;
    char bytes[4];
    putU32(bytes, value);
    const bool ok = device_->write(bytes, 4) == 4;
    device_->seek(saved);
    return ok;
}

bool MotionJpegAviWriter::begin(QString* error) {
    if (opened_) {
        error_ = QStringLiteral("AVI 文件头已经写过了。");
        if (error != nullptr) *error = error_;
        return false;
    }
    if (device_ == nullptr || !device_->isOpen()) {
        error_ = QStringLiteral("输出设备没有打开。");
        if (error != nullptr) *error = error_;
        return false;
    }
    if (device_->isSequential()) {
        error_ = QStringLiteral("输出设备不能回写，AVI 的长度字段无处可填。");
        if (error != nullptr) *error = error_;
        return false;
    }
    if (width_ <= 0 || height_ <= 0) {
        error_ = QStringLiteral("画面尺寸不合法。");
        if (error != nullptr) *error = error_;
        return false;
    }
    if (fps_ <= 0) {
        error_ = QStringLiteral("帧率不合法。");
        if (error != nullptr) *error = error_;
        return false;
    }

    device_->seek(0);
    // 清空目标：QFile 与 QBuffer 各有各的清法，QIODevice 上没有统一入口。
    if (auto* file = qobject_cast<QFile*>(device_)) {
        file->resize(0);
    } else if (auto* buffer = qobject_cast<QBuffer*>(device_)) {
        buffer->buffer().clear();
    }

    const int avihChunk = 8 + kAvihSize;
    const int strhChunk = 8 + kStrhSize;
    const int strfChunk = 8 + kStrfSize;
    const int strlPayload = 4 + strhChunk + strfChunk;
    const int hdrlPayload = 4 + avihChunk + (8 + strlPayload);

    QByteArray head;
    head.reserve(256);
    const qint64 base = device_->pos();

    appendFourCC(head, "RIFF");
    riffSizePos_ = base + head.size();
    appendU32(head, 0);  // 回填：整份文件长度 - 8
    appendFourCC(head, "AVI ");

    appendFourCC(head, "LIST");
    appendU32(head, static_cast<quint32>(hdrlPayload));
    appendFourCC(head, "hdrl");

    // ---- avih：主视频头 ----
    appendFourCC(head, "avih");
    appendU32(head, kAvihSize);
    appendU32(head, static_cast<quint32>(1000000 / fps_));  // dwMicroSecPerFrame
    avihMaxBytesPos_ = base + head.size();
    appendU32(head, 0);  // dwMaxBytesPerSec（回填）
    appendU32(head, 0);  // dwPaddingGranularity
    appendU32(head, kAviFlagHasIndex);
    avihTotalFramesPos_ = base + head.size();
    appendU32(head, 0);  // dwTotalFrames（回填）
    appendU32(head, 0);  // dwInitialFrames
    appendU32(head, 1);  // dwStreams
    avihSuggestedPos_ = base + head.size();
    appendU32(head, 0);  // dwSuggestedBufferSize（回填）
    appendU32(head, static_cast<quint32>(width_));
    appendU32(head, static_cast<quint32>(height_));
    appendU32(head, 0);
    appendU32(head, 0);
    appendU32(head, 0);
    appendU32(head, 0);

    // ---- strl：流列表 ----
    appendFourCC(head, "LIST");
    appendU32(head, static_cast<quint32>(strlPayload));
    appendFourCC(head, "strl");

    // ---- strh：流头 ----
    appendFourCC(head, "strh");
    appendU32(head, kStrhSize);
    appendFourCC(head, "vids");
    appendFourCC(head, "MJPG");
    appendU32(head, 0);  // dwFlags
    appendU16(head, 0);  // wPriority
    appendU16(head, 0);  // wLanguage
    appendU32(head, 0);  // dwInitialFrames
    appendU32(head, 1);  // dwScale
    appendU32(head, static_cast<quint32>(fps_));  // dwRate
    appendU32(head, 0);  // dwStart
    strhLengthPos_ = base + head.size();
    appendU32(head, 0);  // dwLength（回填）
    strhSuggestedPos_ = base + head.size();
    appendU32(head, 0);  // dwSuggestedBufferSize（回填）
    appendU32(head, 0xFFFFFFFFu);  // dwQuality：交给解码器默认
    appendU32(head, 0);  // dwSampleSize：每帧大小不固定
    appendU16(head, 0);  // rcFrame.left
    appendU16(head, 0);  // rcFrame.top
    appendU16(head, static_cast<quint16>(width_ & 0xFFFF));
    appendU16(head, static_cast<quint16>(height_ & 0xFFFF));

    // ---- strf：位图信息头 ----
    appendFourCC(head, "strf");
    appendU32(head, kStrfSize);
    appendU32(head, kStrfSize);  // biSize
    appendU32(head, static_cast<quint32>(width_));  // biWidth
    appendU32(head, static_cast<quint32>(height_));  // biHeight
    appendU16(head, 1);  // biPlanes
    appendU16(head, 24);  // biBitCount
    appendFourCC(head, "MJPG");  // biCompression
    appendU32(head, static_cast<quint32>(width_ * height_ * 3));  // biSizeImage
    appendU32(head, 0);  // biXPelsPerMeter
    appendU32(head, 0);  // biYPelsPerMeter
    appendU32(head, 0);  // biClrUsed
    appendU32(head, 0);  // biClrImportant

    // ---- movi：帧数据 ----
    appendFourCC(head, "LIST");
    moviListSizePos_ = base + head.size();
    appendU32(head, 0);  // 回填：movi 的负载长度
    moviFourccPos_ = base + head.size();
    appendFourCC(head, "movi");

    if (device_->write(head) != head.size()) {
        error_ = QStringLiteral("写 AVI 文件头失败。");
        if (error != nullptr) *error = error_;
        return false;
    }
    opened_ = true;
    return true;
}

bool MotionJpegAviWriter::appendFrame(const QByteArray& data, QString* error) {
    if (!opened_ || finished_) {
        error_ = QStringLiteral("AVI 还没开始写，或者已经收尾了。");
        if (error != nullptr) *error = error_;
        return false;
    }
    if (!isJpeg(data)) {
        error_ = QStringLiteral("这一帧不是完整的 JPEG（缺 SOI 或 EOI），不写进文件。");
        if (error != nullptr) *error = error_;
        return false;
    }

    const qint64 chunkPos = device_->pos();
    char header[8];
    header[0] = '0';
    header[1] = '0';
    header[2] = 'd';
    header[3] = 'c';  // 流 0 的压缩帧
    putU32(header + 4, static_cast<quint32>(data.size()));
    if (device_->write(header, 8) != 8 || device_->write(data) != data.size()) {
        error_ = QStringLiteral("写帧数据失败。");
        if (error != nullptr) *error = error_;
        return false;
    }
    if (data.size() % 2 != 0) {
        // AVI 的块按偶数字节对齐，多的那个字节补 0。
        const char pad = 0;
        device_->write(&pad, 1);
    }

    Chunk chunk;
    chunk.id = 0x30306463u;  // '00dc'
    chunk.offset = static_cast<quint32>(chunkPos - moviFourccPos_);
    chunk.length = static_cast<quint32>(data.size());
    chunks_.append(chunk);

    ++frameCount_;
    if (static_cast<quint32>(data.size()) > maxFrameSize_) {
        maxFrameSize_ = static_cast<quint32>(data.size());
    }
    return true;
}

bool MotionJpegAviWriter::finish(QString* error) {
    if (!opened_) {
        error_ = QStringLiteral("AVI 还没开始写。");
        if (error != nullptr) *error = error_;
        return false;
    }
    if (finished_) return true;

    // ---- idx1：帧索引 ----
    QByteArray index;
    index.reserve(chunks_.size() * 16);
    for (const Chunk& chunk : chunks_) {
        appendU32(index, chunk.id);
        appendU32(index, kIndexFlagKeyFrame);
        appendU32(index, chunk.offset);
        appendU32(index, chunk.length);
    }
    char indexHeader[8];
    indexHeader[0] = 'i';
    indexHeader[1] = 'd';
    indexHeader[2] = 'x';
    indexHeader[3] = '1';
    putU32(indexHeader + 4, static_cast<quint32>(index.size()));
    if (device_->write(indexHeader, 8) != 8 || device_->write(index) != index.size()) {
        error_ = QStringLiteral("写帧索引失败。");
        if (error != nullptr) *error = error_;
        return false;
    }

    const qint64 endPos = device_->pos();

    // ---- 回填长度字段 ----
    // movi 的负载长度：从 'movi' 四字节码到 movi 结束（含索引之前的全部帧）。
    if (!patchU32(moviListSizePos_, static_cast<quint32>(endPos - moviFourccPos_ - index.size() - 8))) {
        error_ = QStringLiteral("回填 movi 长度失败。");
        if (error != nullptr) *error = error_;
        return false;
    }
    if (!patchU32(riffSizePos_, static_cast<quint32>(endPos - 8))) {
        error_ = QStringLiteral("回填 RIFF 长度失败。");
        if (error != nullptr) *error = error_;
        return false;
    }
    if (!patchU32(avihTotalFramesPos_, static_cast<quint32>(frameCount_)) ||
        !patchU32(avihMaxBytesPos_, maxFrameSize_ * static_cast<quint32>(fps_)) ||
        !patchU32(avihSuggestedPos_, maxFrameSize_) ||
        !patchU32(strhLengthPos_, static_cast<quint32>(frameCount_)) ||
        !patchU32(strhSuggestedPos_, maxFrameSize_)) {
        error_ = QStringLiteral("回填视频头失败。");
        if (error != nullptr) *error = error_;
        return false;
    }

    finished_ = true;
    return true;
}

}  // namespace aipdf::app
