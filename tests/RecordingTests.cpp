// 录制中心的三层：AVI 容器写入、录制清单、录制会话。
//
// 这一层不碰界面：录制中心对话框只是把这里的三个件摆到界面上，
// 真正要守住的是「产出的文件确实是合法视频」「清单与磁盘对得上」
// 「录到一半停下来也能收尾」这三件事。
//
// AVI 的校验用一个**独立的解析器**（下面那个 parseAvi）：它只按 RIFF 规范
// 走一遍字节，不复用写入器里的任何代码——否则写入器写错了，解析器会跟着错，
// 两边一起绿灯，坏文件就永远出不了厂。
//
// 负例盯的是「坏字节进得去」与「坏文件出得来」：
//   * 不是 JPEG 的帧必须被拒（半张图会在播放器里花屏）；
//   * 抽掉收尾竖线 / 少写索引这类坏文件，解析器必须能看出来。

#include "app/recording/MotionJpegAviWriter.h"
#include "app/recording/RecordingLibrary.h"
#include "app/recording/ScreenRecorder.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

using aipdf::app::MotionJpegAviWriter;
using aipdf::app::RecordingEntry;
using aipdf::app::RecordingKind;
using aipdf::app::RecordingLibrary;
using aipdf::app::ScreenRecorder;

namespace {

quint32 readU32(const QByteArray& data, int position) {
    return static_cast<quint8>(data.at(position))
         | (static_cast<quint32>(static_cast<quint8>(data.at(position + 1))) << 8)
         | (static_cast<quint32>(static_cast<quint8>(data.at(position + 2))) << 16)
         | (static_cast<quint32>(static_cast<quint8>(data.at(position + 3))) << 24);
}

QString fourCc(const QByteArray& data, int position) {
    return QString::fromLatin1(data.mid(position, 4));
}

bool looksLikeJpeg(const QByteArray& payload) {
    if (payload.size() < 4) return false;
    return static_cast<quint8>(payload.at(0)) == 0xFFu
        && static_cast<quint8>(payload.at(1)) == 0xD8u
        && static_cast<quint8>(payload.at(payload.size() - 2)) == 0xFFu
        && static_cast<quint8>(payload.at(payload.size() - 1)) == 0xD9u;
}

// 独立解析器：按 RIFF/AVI 规范走一遍字节。
struct AviFacts {
    bool riffOk = false;
    quint32 riffSize = 0;
    quint32 totalFrames = 0;
    int width = 0;
    int height = 0;
    int frameChunks = 0;
    int brokenJpegChunks = 0;
    int indexEntries = 0;
    QVector<quint32> indexOffsets;
    QVector<quint32> indexLengths;
    int moviPosition = -1;
};

AviFacts parseAvi(const QByteArray& data) {
    AviFacts facts;
    if (data.size() < 12) return facts;
    if (fourCc(data, 0) != QLatin1String("RIFF")) return facts;
    facts.riffSize = readU32(data, 4);
    if (fourCc(data, 8) != QLatin1String("AVI ")) return facts;
    facts.riffOk = true;

    int position = 12;
    while (position + 8 <= data.size()) {
        const QString id = fourCc(data, position);
        const quint32 size = readU32(data, position + 4);
        if (id == QLatin1String("LIST")) {
            const QString listType = fourCc(data, position + 8);
            const int listEnd = position + 8 + static_cast<int>(size);
            if (listType == QLatin1String("movi")) {
                facts.moviPosition = position + 8;
                int cursor = position + 12;
                while (cursor + 8 <= listEnd) {
                    const QString chunkId = fourCc(data, cursor);
                    const quint32 chunkSize = readU32(data, cursor + 4);
                    if (chunkId == QLatin1String("00dc")) {
                        ++facts.frameChunks;
                        if (!looksLikeJpeg(data.mid(cursor + 8, static_cast<int>(chunkSize)))) {
                            ++facts.brokenJpegChunks;
                        }
                    }
                    cursor += 8 + static_cast<int>(chunkSize) + (chunkSize % 2);
                }
            } else if (listType == QLatin1String("hdrl")) {
                int cursor = position + 12;
                while (cursor + 8 <= listEnd) {
                    const QString chunkId = fourCc(data, cursor);
                    const quint32 chunkSize = readU32(data, cursor + 4);
                    if (chunkId == QLatin1String("avih") && chunkSize >= 40) {
                        facts.totalFrames = readU32(data, cursor + 8 + 16);
                        facts.width = static_cast<int>(readU32(data, cursor + 8 + 32));
                        facts.height = static_cast<int>(readU32(data, cursor + 8 + 36));
                    }
                    cursor += 8 + static_cast<int>(chunkSize) + (chunkSize % 2);
                }
            }
            position += 8 + static_cast<int>(size) + (size % 2);
        } else if (id == QLatin1String("idx1")) {
            const int count = static_cast<int>(size) / 16;
            for (int i = 0; i < count; ++i) {
                const int entry = position + 8 + i * 16;
                facts.indexOffsets.append(readU32(data, entry + 8));
                facts.indexLengths.append(readU32(data, entry + 12));
            }
            facts.indexEntries = count;
            position += 8 + static_cast<int>(size) + (size % 2);
        } else {
            position += 8 + static_cast<int>(size) + (size % 2);
        }
    }
    return facts;
}

// 一段最小的合法 JPEG 字节（只有 SOI 与 EOI，中间是假数据）。
//
// 为什么可以造假 JPEG：写入器只验「是不是完整的 JPEG 帧」，不解析内容；
// 真帧由 Qt 的编码器在录制那一路产生。
QByteArray fakeJpeg(int size = 64) {
    QByteArray data;
    data.append(static_cast<char>(0xFF));
    data.append(static_cast<char>(0xD8));
    data.append(QByteArray(size - 4, 'x'));
    data.append(static_cast<char>(0xFF));
    data.append(static_cast<char>(0xD9));
    return data;
}

// 从 AVI 字节里取出第一帧的画面数据。
//
// 为什么单独写一个：光看文件头里的宽高只能证明「尺寸对」，
// 证明不了「裁的是那一块」——得把帧解出来看颜色才对得上。
QByteArray firstFramePayload(const QByteArray& data) {
    const int position = data.indexOf("00dc");
    if (position < 0) return QByteArray();
    const int size = static_cast<int>(readU32(data, position + 4));
    return data.mid(position + 8, size);
}

}  // namespace

class RecordingTests : public QObject {
    Q_OBJECT

private slots:
    void aviHeaderFramesAndIndexAreConsistent();
    void aviRejectsFrameThatIsNotJpeg();
    void aviRejectsTruncatedJpeg();
    void aviRejectsAppendAfterFinish();
    void aviRejectsInvalidFrameSize();
    void aviWithNoFrameIsStillStructurallyValid();
    void libraryRoundTripsEntries();
    void libraryUpdatesSamePathInsteadOfDuplicating();
    void libraryRemovesEntryAndFile();
    void libraryReportsBrokenIndex();
    void recorderWritesAviFromInjectedFrames();
    void recorderCropsToSelectedRegion();
    void recorderFailsWhenRegionIsOutsideTheFrame();
    void recorderCancelRemovesPartialFile();
    void recorderStartFailsWithoutAnyFrame();
    void recorderStopsCleanlyAndReportsFrameCount();
    void writesSamplesForIndependentVerification();
};

// 写三帧 → 头部、帧块、索引三处必须互相说得通。
void RecordingTests::aviHeaderFramesAndIndexAreConsistent() {
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));

    MotionJpegAviWriter writer(&buffer, 320, 240, 10);
    QString error;
    QVERIFY2(writer.begin(&error), qPrintable(error));
    for (int i = 0; i < 3; ++i) {
        QVERIFY2(writer.appendFrame(fakeJpeg(100 + i), &error), qPrintable(error));
    }
    QVERIFY2(writer.finish(&error), qPrintable(error));
    QCOMPARE(writer.frameCount(), 3);

    const QByteArray data = buffer.data();
    const AviFacts facts = parseAvi(data);
    QVERIFY2(facts.riffOk, "不是合法的 RIFF/AVI 文件");
    QCOMPARE(facts.riffSize, static_cast<quint32>(data.size() - 8));
    QCOMPARE(facts.totalFrames, 3u);
    QCOMPARE(facts.width, 320);
    QCOMPARE(facts.height, 240);
    QCOMPARE(facts.frameChunks, 3);
    QCOMPARE(facts.brokenJpegChunks, 0);
    QCOMPARE(facts.indexEntries, 3);

    // 索引里的偏移必须真的指向帧块（偏移是相对 movi 四字节码的）。
    QVERIFY(facts.moviPosition > 0);
    for (int i = 0; i < facts.indexOffsets.size(); ++i) {
        const int absolute = facts.moviPosition + static_cast<int>(facts.indexOffsets.at(i));
        QVERIFY(absolute + 8 <= data.size());
        QCOMPARE(fourCc(data, absolute), QLatin1String("00dc"));
        QCOMPARE(readU32(data, absolute + 4), facts.indexLengths.at(i));
    }
}

// 不是 JPEG 的帧必须被拒：写进去会在播放器里花屏。
void RecordingTests::aviRejectsFrameThatIsNotJpeg() {
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    MotionJpegAviWriter writer(&buffer, 64, 48, 10);
    QString error;
    QVERIFY(writer.begin(&error));

    QVERIFY(!writer.appendFrame(QByteArray("not a jpeg"), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(writer.frameCount(), 0);
}

// 有头没尾的半张图同样要拒。
void RecordingTests::aviRejectsTruncatedJpeg() {
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    MotionJpegAviWriter writer(&buffer, 64, 48, 10);
    QString error;
    QVERIFY(writer.begin(&error));

    QByteArray truncated;
    truncated.append(static_cast<char>(0xFF));
    truncated.append(static_cast<char>(0xD8));
    truncated.append(QByteArray(32, 'x'));
    QVERIFY(!writer.appendFrame(truncated, &error));
    QCOMPARE(writer.frameCount(), 0);
}

// 收尾之后不能再写帧。
void RecordingTests::aviRejectsAppendAfterFinish() {
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    MotionJpegAviWriter writer(&buffer, 64, 48, 10);
    QString error;
    QVERIFY(writer.begin(&error));
    QVERIFY(writer.appendFrame(fakeJpeg(), &error));
    QVERIFY(writer.finish(&error));

    QVERIFY(!writer.appendFrame(fakeJpeg(), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(writer.frameCount(), 1);
}

// 尺寸不合法时连文件头都不该写。
void RecordingTests::aviRejectsInvalidFrameSize() {
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    MotionJpegAviWriter writer(&buffer, 0, 48, 10);
    QString error;
    QVERIFY(!writer.begin(&error));
    QVERIFY(!error.isEmpty());
}

// 一帧都没有也要收出一个结构合法的文件（调用方通常不该走到这一步，但别崩）。
void RecordingTests::aviWithNoFrameIsStillStructurallyValid() {
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    MotionJpegAviWriter writer(&buffer, 320, 240, 10);
    QString error;
    QVERIFY(writer.begin(&error));
    QVERIFY(writer.finish(&error));

    const AviFacts facts = parseAvi(buffer.data());
    QVERIFY(facts.riffOk);
    QCOMPARE(facts.totalFrames, 0u);
    QCOMPARE(facts.frameChunks, 0);
    QCOMPARE(facts.indexEntries, 0);
}

// 清单存盘再读回来，内容不能少。
void RecordingTests::libraryRoundTripsEntries() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString indexPath = dir.filePath(QStringLiteral("recordings.json"));

    RecordingLibrary library(indexPath);
    RecordingEntry screen;
    screen.kind = RecordingKind::Screen;
    screen.path = dir.filePath(QStringLiteral("a.avi"));
    screen.createdAt = QDateTime::fromString(QStringLiteral("2026-11-20T10:00:00"), Qt::ISODate);
    screen.sizeBytes = 1234;
    screen.width = 1280;
    screen.height = 720;
    screen.durationMs = 5000;

    RecordingEntry audio;
    audio.kind = RecordingKind::Audio;
    audio.path = dir.filePath(QStringLiteral("b.wav"));
    audio.createdAt = QDateTime::fromString(QStringLiteral("2026-11-20T11:00:00"), Qt::ISODate);

    QString error;
    QVERIFY2(library.add(screen, &error), qPrintable(error));
    QVERIFY2(library.add(audio, &error), qPrintable(error));

    RecordingLibrary reloaded(indexPath);
    QVERIFY2(reloaded.load(&error), qPrintable(error));
    const QVector<RecordingEntry> entries = reloaded.entries();
    QCOMPARE(entries.size(), 2);
    // 新的在前。
    QCOMPARE(entries.at(0).path, audio.path);
    QCOMPARE(entries.at(0).kind, RecordingKind::Audio);
    QCOMPARE(entries.at(1).path, screen.path);
    QCOMPARE(entries.at(1).width, 1280);
    QCOMPARE(entries.at(1).durationMs, 5000);
}

// 同一个路径登记两次只能有一条。
void RecordingTests::libraryUpdatesSamePathInsteadOfDuplicating() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingLibrary library(dir.filePath(QStringLiteral("recordings.json")));

    RecordingEntry entry;
    entry.path = dir.filePath(QStringLiteral("x.avi"));
    entry.kind = RecordingKind::Screen;
    entry.sizeBytes = 10;
    QString error;
    QVERIFY(library.add(entry, &error));

    entry.sizeBytes = 20;
    QVERIFY(library.add(entry, &error));
    QCOMPARE(library.entries().size(), 1);
    QCOMPARE(library.entries().at(0).sizeBytes, 20);
}

// 删条目时要把磁盘上的文件一起删掉。
void RecordingTests::libraryRemovesEntryAndFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("clip.avi"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    media.write("data");
    media.close();

    RecordingLibrary library(dir.filePath(QStringLiteral("recordings.json")));
    RecordingEntry entry;
    entry.path = mediaPath;
    entry.kind = RecordingKind::Screen;
    QString error;
    QVERIFY(library.add(entry, &error));
    const QString id = library.entries().at(0).id;

    QVERIFY2(library.remove(id, true, &error), qPrintable(error));
    QVERIFY(library.entries().isEmpty());
    QVERIFY(!QFileInfo::exists(mediaPath));
}

// 索引坏了要报错，不能当成空清单悄悄吞掉。
void RecordingTests::libraryReportsBrokenIndex() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString indexPath = dir.filePath(QStringLiteral("recordings.json"));
    QFile file(indexPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ this is not json");
    file.close();

    RecordingLibrary library(indexPath);
    QString error;
    QVERIFY(!library.load(&error));
    QVERIFY(!error.isEmpty());
}

// 注入假帧源：整条「抓帧 → 编码 → 写 AVI → 收尾」要在 CI 上跑得通。
void RecordingTests::recorderWritesAviFromInjectedFrames() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output = dir.filePath(QStringLiteral("out.avi"));

    ScreenRecorder recorder;
    recorder.setFrameProvider([]() {
        QImage image(64, 48, QImage::Format_RGB32);
        image.fill(QColor(20, 120, 200));
        return image;
    });

    ScreenRecorder::Options options;
    options.framesPerSecond = 10;
    QString error;
    QVERIFY2(recorder.start(options, output, &error), qPrintable(error));
    QVERIFY(recorder.captureFrame(&error));
    QVERIFY(recorder.captureFrame(&error));
    QVERIFY2(recorder.stop(&error), qPrintable(error));

    QVERIFY(QFileInfo::exists(output));
    QFile file(output);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    file.close();

    const AviFacts facts = parseAvi(data);
    QVERIFY(facts.riffOk);
    QCOMPARE(facts.frameChunks, 3);
    QCOMPARE(facts.totalFrames, 3u);
    QCOMPARE(facts.width, 64);
    QCOMPARE(facts.height, 48);
    QCOMPARE(facts.brokenJpegChunks, 0);
    QCOMPARE(facts.indexEntries, 3);
}

// 区域框选：录到的必须就是框里的那一块，不是整张画面。
//
// 为什么要验像素而不只是尺寸：尺寸对了也可能是「另一块同样大小的区域」，
// 把颜色也解出来看，才算真的裁对了。
void RecordingTests::recorderCropsToSelectedRegion() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output = dir.filePath(QStringLiteral("crop.avi"));

    ScreenRecorder recorder;
    recorder.setFrameProvider([]() {
        QImage image(200, 100, QImage::Format_RGB32);
        image.fill(QColor(255, 0, 0)); // 框外是红
        QPainter painter(&image);
        painter.fillRect(QRect(20, 10, 60, 30), QColor(0, 255, 0)); // 框内是绿
        return image;
    });

    ScreenRecorder::Options options;
    options.region = QRect(20, 10, 60, 30);
    QString error;
    QVERIFY2(recorder.start(options, output, &error), qPrintable(error));
    QVERIFY2(recorder.stop(&error), qPrintable(error));

    QFile file(output);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    file.close();

    const AviFacts facts = parseAvi(data);
    QCOMPARE(facts.width, 60);
    QCOMPARE(facts.height, 30);

    const QImage frame = QImage::fromData(firstFramePayload(data), "JPG");
    QVERIFY(!frame.isNull());
    QCOMPARE(frame.width(), 60);
    QCOMPARE(frame.height(), 30);
    const QColor pixel = frame.pixelColor(30, 15);
    QVERIFY2(pixel.green() > pixel.red(),
              qPrintable(QStringLiteral("框内像素不是绿色：r=%1 g=%2 b=%3")
                             .arg(pixel.red()).arg(pixel.green()).arg(pixel.blue())));
}

// 选区完全在画面之外：没有画面可录就明确失败，不能悄悄录一片黑。
void RecordingTests::recorderFailsWhenRegionIsOutsideTheFrame() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output = dir.filePath(QStringLiteral("outside.avi"));

    ScreenRecorder recorder;
    recorder.setFrameProvider([]() {
        QImage image(64, 48, QImage::Format_RGB32);
        image.fill(Qt::black);
        return image;
    });

    ScreenRecorder::Options options;
    options.region = QRect(500, 500, 40, 30);
    QString error;
    QVERIFY(!recorder.start(options, output, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(output));
}

// 放弃录制不能留下半成品文件。
void RecordingTests::recorderCancelRemovesPartialFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output = dir.filePath(QStringLiteral("cancel.avi"));

    ScreenRecorder recorder;
    recorder.setFrameProvider([]() {
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::black);
        return image;
    });
    QString error;
    QVERIFY(recorder.start(ScreenRecorder::Options(), output, &error));
    recorder.cancel();
    QVERIFY(!QFileInfo::exists(output));
}

// 抓不到画面时不能开始录制，也不能留下空文件。
void RecordingTests::recorderStartFailsWithoutAnyFrame() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output = dir.filePath(QStringLiteral("nothing.avi"));

    ScreenRecorder recorder;
    recorder.setFrameProvider([]() { return QImage(); });
    QString error;
    QVERIFY(!recorder.start(ScreenRecorder::Options(), output, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(output));
}

// 停止之后状态要归位，帧数要报得出来。
void RecordingTests::recorderStopsCleanlyAndReportsFrameCount() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output = dir.filePath(QStringLiteral("count.avi"));

    ScreenRecorder recorder;
    recorder.setFrameProvider([]() {
        QImage image(16, 16, QImage::Format_RGB32);
        image.fill(Qt::white);
        return image;
    });
    QString error;
    QVERIFY(recorder.start(ScreenRecorder::Options(), output, &error));
    QCOMPARE(recorder.frameCount(), 1);
    QVERIFY(recorder.isRecording());
    QVERIFY(recorder.stop(&error));
    QVERIFY(!recorder.isRecording());
    QCOMPARE(recorder.frameCount(), 1);
    QCOMPARE(recorder.outputPath(), output);
}

// 写一份样例给独立校验脚本（scripts/verify-recordings.py）。
//
// 为什么还要另一道校验：C++ 侧读 AVI 的解析器与 C++ 侧写 AVI 的代码共享
// 同一套「对格式的理解」，写错了两边会一起错。这份样例交给 Python 那边
// 另写的解析器（外加 PIL 的 JPEG 解码器）再验一次，才算真的没问题。
void RecordingTests::writesSamplesForIndependentVerification() {
    const QString dir = QStringLiteral(AIPDF_RECORDING_DIR);
    QVERIFY(QDir().mkpath(dir));
    const QString output = QDir(dir).filePath(QStringLiteral("sample.avi"));

    ScreenRecorder recorder;
    int shade = 0;
    recorder.setFrameProvider([&shade]() {
        QImage image(160, 120, QImage::Format_RGB32);
        image.fill(QColor(shade % 255, 90, 200 - (shade % 200)));
        ++shade;
        return image;
    });

    ScreenRecorder::Options options;
    options.framesPerSecond = 10;
    options.maxWidth = 160;
    options.jpegQuality = 80;
    QString error;
    QVERIFY2(recorder.start(options, output, &error), qPrintable(error));
    for (int i = 0; i < 4; ++i) {
        QVERIFY2(recorder.captureFrame(&error), qPrintable(error));
    }
    QVERIFY2(recorder.stop(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(output));
    QVERIFY(QFileInfo(output).size() > 0);
}

QTEST_GUILESS_MAIN(RecordingTests)
#include "RecordingTests.moc"
