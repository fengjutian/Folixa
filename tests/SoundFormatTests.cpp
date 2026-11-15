// WAV 解析的单元测试。
//
// 声音批注的关键不是"能不能存"，而是"存进去的字节阅读器能不能放"：
// PDF 的 /Sound 要裸 PCM，把 WAV 容器整个塞进去放出来就是噪音。

#include "pdf/SoundFormat.h"

#include <QTest>

using aipdf::pdf::parseWav;
using aipdf::pdf::SoundPayload;

namespace {

void appendU32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 24) & 0xFF));
}

void appendU16(QByteArray& out, quint16 value) {
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
}

// 造一段最小可用的 WAV：16 位单声道 8000Hz，样本是 4 个字节。
QByteArray makeWav() {
    QByteArray wav;
    wav.append("RIFF");
    appendU32(wav, 36 + 4);
    wav.append("WAVE");
    wav.append("fmt ");
    appendU32(wav, 16);
    appendU16(wav, 1);      // PCM
    appendU16(wav, 1);      // 单声道
    appendU32(wav, 8000);   // 采样率
    appendU32(wav, 16000);  // 字节率
    appendU16(wav, 2);      // 块对齐
    appendU16(wav, 16);     // 位深
    wav.append("data");
    appendU32(wav, 4);
    wav.append(QByteArray("\x01\x02\x03\x04", 4));
    return wav;
}

}  // namespace

class SoundFormatTests : public QObject {
    Q_OBJECT

private slots:
    void parsesPcmHeaderAndPayload();
    void rejectsNonRiff();
    void rejectsNonPcm();
    void rejectsTruncated();
};

void SoundFormatTests::parsesPcmHeaderAndPayload() {
    const SoundPayload payload = parseWav(makeWav());
    QVERIFY2(payload.isValid(), qPrintable(payload.error));
    QCOMPARE(payload.rate, 8000);
    QCOMPARE(payload.channels, 1);
    QCOMPARE(payload.bits, 16);
    // 只留 data 里的样本：RIFF 头必须被剥掉。
    QCOMPARE(payload.pcm, QByteArray("\x01\x02\x03\x04", 4));
}

void SoundFormatTests::rejectsNonRiff() {
    QByteArray notWav("这是一段中文文本，不是音频文件", 42);
    QVERIFY(!parseWav(notWav).isValid());
}

void SoundFormatTests::rejectsNonPcm() {
    QByteArray wav = makeWav();
    const int formatAt = 20;  // "fmt " 之后 16 字节结构里第一个字段
    wav[formatAt] = 3;        // IEEE float
    wav[formatAt + 1] = 0;
    const SoundPayload payload = parseWav(wav);
    QVERIFY(!payload.isValid());
    QVERIFY(payload.error.contains(QStringLiteral("PCM")));
}

void SoundFormatTests::rejectsTruncated() {
    QVERIFY(!parseWav(QByteArray("RIFF")).isValid());
}

QTEST_MAIN(SoundFormatTests)
#include "SoundFormatTests.moc"
