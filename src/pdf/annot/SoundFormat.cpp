#include "pdf/annot/SoundFormat.h"

#include "i18n/Translate.h"

namespace aipdf::pdf {

namespace {

quint32 readU32(const QByteArray& data, int offset) {
    return static_cast<quint32>(static_cast<unsigned char>(data.at(offset))) |
           (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 1))) << 8) |
           (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 2))) << 16) |
           (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 3))) << 24);
}

quint16 readU16(const QByteArray& data, int offset) {
    return static_cast<quint16>(static_cast<unsigned char>(data.at(offset))) |
           (static_cast<quint16>(static_cast<unsigned char>(data.at(offset + 1))) << 8);
}

}  // namespace

SoundPayload parseWav(const QByteArray& bytes) {
    SoundPayload payload;
    if (bytes.size() < 44) {
        payload.error = i18n::tr("aipdf::pdf::SoundFormat", "文件太小，不是 WAV");
        return payload;
    }
    if (!bytes.startsWith("RIFF") || bytes.mid(8, 4) != "WAVE") {
        payload.error = i18n::tr("aipdf::pdf::SoundFormat", "不是 RIFF/WAVE 文件");
        return payload;
    }

    int offset = 12;
    bool sawFormat = false;
    while (offset + 8 <= bytes.size()) {
        const QByteArray chunkId = bytes.mid(offset, 4);
        const quint32 chunkSize = readU32(bytes, offset + 4);
        const int chunkStart = offset + 8;
        if (chunkId == "fmt ") {
            if (chunkSize < 16 || chunkStart + 16 > bytes.size()) {
                payload.error = i18n::tr("aipdf::pdf::SoundFormat", "fmt 块不完整");
                return payload;
            }
            const quint16 format = readU16(bytes, chunkStart);
            if (format != 1) {
                payload.error =
                    i18n::tr("aipdf::pdf::SoundFormat", "只支持 PCM（format=1），当前是 %1")
                        .arg(format);
                return payload;
            }
            payload.channels = readU16(bytes, chunkStart + 2);
            payload.rate = static_cast<int>(readU32(bytes, chunkStart + 4));
            payload.bits = readU16(bytes, chunkStart + 14);
            sawFormat = true;
        } else if (chunkId == "data") {
            if (!sawFormat) {
                payload.error = i18n::tr("aipdf::pdf::SoundFormat", "data 块出现在 fmt 之前");
                return payload;
            }
            const int available = bytes.size() - chunkStart;
            const int length = static_cast<int>(qMin<quint32>(chunkSize, static_cast<quint32>(available)));
            payload.pcm = bytes.mid(chunkStart, length);
            if (payload.rate <= 0 || payload.channels <= 0 || payload.bits <= 0) {
                payload.error = i18n::tr("aipdf::pdf::SoundFormat", "格式参数非法");
                payload.pcm.clear();
            }
            return payload;
        }
        // 块长度按偶数对齐（RIFF 规定）。
        offset = chunkStart + static_cast<int>(chunkSize) + (chunkSize % 2);
    }

    payload.error = i18n::tr("aipdf::pdf::SoundFormat", "没有找到 data 块");
    return payload;
}

}  // namespace aipdf::pdf
