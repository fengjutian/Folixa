#include "app/controllers/ScannerCapture.h"
#include "pdf/convert/ImageToPdf.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QByteArray>
#include <QImage>
#include <QtTest>

#include <cstring>

using aipdf::app::ScanErrorKind;
using aipdf::app::ScanFrameSource;
using aipdf::app::ScanFrameStatus;
using aipdf::app::ScanOutcome;
using aipdf::app::ScannerCapture;
using aipdf::app::ScannerCaptureOptions;
using aipdf::app::ScannerDevice;

namespace {

// 造一张 24 位 BMP（每像素 3 字节 RGB，自下而上存储，行按 4 字节对齐）。
QByteArray makeBmp(int width, int height, const QByteArray& pixelsTopDown) {
    const int stride = ((width * 24 + 31) / 32) * 4;
    QByteArray pixels;
    pixels.resize(stride * height);
    std::memset(pixels.data(), 0, static_cast<size_t>(pixels.size()));
    for (int row = 0; row < height; ++row) {
        const int sourceRow = height - 1 - row;
        const char* source = pixelsTopDown.constData() + sourceRow * width * 3;
        char* target = pixels.data() + row * stride;
        for (int x = 0; x < width; ++x) {
            target[x * 3 + 0] = source[x * 3 + 2];  // B
            target[x * 3 + 1] = source[x * 3 + 1];  // G
            target[x * 3 + 2] = source[x * 3 + 0];  // R
        }
    }

    QByteArray header;
    header.append("BM");
    const auto appendU32 = [&header](quint32 value) {
        header.append(static_cast<char>(value & 0xFF));
        header.append(static_cast<char>((value >> 8) & 0xFF));
        header.append(static_cast<char>((value >> 16) & 0xFF));
        header.append(static_cast<char>((value >> 24) & 0xFF));
    };
    const auto appendU16 = [&header](quint16 value) {
        header.append(static_cast<char>(value & 0xFF));
        header.append(static_cast<char>((value >> 8) & 0xFF));
    };
    const int pixelOffset = 14 + 40;
    appendU32(static_cast<quint32>(pixelOffset + pixels.size()));
    appendU16(0);
    appendU16(0);
    appendU32(static_cast<quint32>(pixelOffset));
    appendU32(40);
    appendU32(static_cast<quint32>(width));
    appendU32(static_cast<quint32>(height));
    appendU16(1);
    appendU16(24);
    appendU32(0);  // BI_RGB
    appendU32(static_cast<quint32>(pixels.size()));
    appendU32(2835);
    appendU32(2835);
    appendU32(0);
    appendU32(0);
    return header + pixels;
}

// 假帧源：按预设剧本发帧。
//
// 为什么要有它：WIA 那条路在没有扫描仪的机器上根本走不到「取到帧之后」，
// 于是「取帧 → 排页序 → 拼 PDF」整条链从来没被验证过。
// 这个假源把那一段补上：真机路径仍然只能靠真机器验。
class FakeFrameSource final : public ScanFrameSource {
public:
    QVector<ScannerDevice> devices;
    QVector<QByteArray> frames;

    // 枚举失败时给的种类（None = 正常）。
    ScanErrorKind enumerateErrorKind = ScanErrorKind::None;
    // 打开设备时给的状态。
    ScanFrameStatus openStatus = ScanFrameStatus::Ok;
    // 取到第 failAtIndex 帧时给的状态（-1 = 一直正常）。
    ScanFrameStatus failStatus = ScanFrameStatus::Failed;
    int failAtIndex = -1;
    QString failDetail = QStringLiteral("fake frame source failure");

    int openCalls = 0;
    int nextCalls = 0;

    QVector<ScannerDevice> enumerateDevices(ScanErrorKind* errorKind, QString* detail) override {
        *errorKind = enumerateErrorKind;
        if (enumerateErrorKind != ScanErrorKind::None) {
            *detail = QStringLiteral("fake enumeration failure");
            return {};
        }
        *detail = QString();
        return devices;
    }

    ScanFrameStatus openDevice(const ScannerDevice& device, const ScannerCaptureOptions& options,
                               QString* detail) override {
        Q_UNUSED(device)
        Q_UNUSED(options)
        ++openCalls;
        if (openStatus != ScanFrameStatus::Ok) {
            *detail = QStringLiteral("fake open failure");
            return openStatus;
        }
        *detail = QString();
        // 每次打开都是一次新的取帧会话：帧序号与失败点都要从头数，
        // 否则同一个假源跑第二遍时 failAtIndex 对不上号。
        cursor_ = 0;
        nextCalls = 0;
        return ScanFrameStatus::Ok;
    }

    ScanFrameStatus nextFrame(QByteArray* frame, QString* detail) override {
        ++nextCalls;
        if (failAtIndex >= 0 && nextCalls - 1 == failAtIndex) {
            *detail = failDetail;
            return failStatus;
        }
        if (cursor_ >= frames.size()) {
            *detail = QString();
            return ScanFrameStatus::NoMoreFrames;
        }
        *frame = frames.at(cursor_);
        ++cursor_;
        *detail = QString();
        return ScanFrameStatus::Ok;
    }

private:
    int cursor_ = 0;
};

// 一台假设备。
ScannerDevice fakeDevice() {
    ScannerDevice device;
    device.deviceId = QStringLiteral("\\?\\usb#fake-scanner");
    device.name = QStringLiteral("Fake Scanner");
    device.deviceType = 1;
    return device;
}

// 造一张 8 位灰度 TIFF（小端、未压缩）。
//
// 为什么在这里现造：两个测试目标各自独立编译，不共享夹具；
// 真实文件（libtiff 写的那些）的对照由 ImageToPdfTests 负责。
QByteArray makeTiff(int width, int height, const QByteArray& pixels) {
    QByteArray out;
    out.append("II");
    const auto appendU16 = [&out](quint16 value) {
        out.append(static_cast<char>(value & 0xFF));
        out.append(static_cast<char>((value >> 8) & 0xFF));
    };
    const auto appendU32 = [&out](quint32 value) {
        for (int index = 0; index < 4; ++index) {
            out.append(static_cast<char>((value >> (8 * index)) & 0xFF));
        }
    };
    appendU16(42);
    appendU32(8);  // 第一个 IFD 的偏移
    const quint32 stripOffset = 8 + 2 + 5 * 12 + 4;
    appendU16(5);  // 条目数
    // 256 宽度 / 257 高度 / 258 位深 / 273 条偏移 / 279 条字节数
    const quint16 tags[] = {256, 257, 258, 273, 279};
    const quint16 types[] = {4, 4, 3, 4, 4};
    const quint32 values[] = {static_cast<quint32>(width), static_cast<quint32>(height), 8,
                               stripOffset, static_cast<quint32>(pixels.size())};
    for (int index = 0; index < 5; ++index) {
        appendU16(tags[index]);
        appendU16(types[index]);
        appendU32(1);
        if (types[index] == 3) {
            // SHORT 值左对齐装在 4 字节的 value 字段里。
            out.append(static_cast<char>(values[index] & 0xFF));
            out.append(static_cast<char>((values[index] >> 8) & 0xFF));
            out.append('\0');
            out.append('\0');
        } else {
            appendU32(values[index]);
        }
    }
    appendU32(0);  // 没有下一个 IFD
    out.append(pixels);
    return out;
}

}  // namespace

class ScannerCaptureTests final : public QObject {
    Q_OBJECT

private slots:
    void fakeSourceReturnsFramesInOrder();
    void scanToPdfBuildsPagesInOrder();
    void tiffFrameBecomesPdfPage();
    void transferFailureMapsToTransferFailed();
    void unsupportedFrameMapsToUnsupportedImage();
    void pdfAssemblyFailureMapsToPdfFailed();
    void userCancellationMapsToUserCancelled();
    void enumerationFailureIsReported();
    void maxPagesLimitsTheNumberOfFrames();
    void defaultCaptureStillUsesWia();
};

// --------------------------------------------------------- 全链路 ----

void ScannerCaptureTests::fakeSourceReturnsFramesInOrder() {
    FakeFrameSource source;
    source.devices = {fakeDevice()};
    source.frames = {
        makeBmp(2, 1, QByteArray::fromHex("FF0000")),
        makeBmp(4, 1, QByteArray::fromHex("00FF00")),
        makeBmp(6, 1, QByteArray::fromHex("0000FF")),
    };

    ScannerCapture capture(&source);
    ScannerCaptureOptions options;
    options.dpi = 72;
    const ScanOutcome outcome = capture.capture(source.devices.first(), options);

    QVERIFY2(outcome.ok, qPrintable(outcome.errorMessage));
    QCOMPARE(outcome.errorKind, ScanErrorKind::None);
    QCOMPARE(outcome.pageCount(), 3);
    // 走的是注入的假源，不是 WIA。
    QCOMPARE(source.openCalls, 1);
    // 页序必须与给出的顺序一致：扫描仪的自动进纸器一次出几页，
    // 顺序就是用户叠纸的顺序，弄错了等于把文件做废。
    for (int i = 0; i < source.frames.size(); ++i) {
        QCOMPARE(outcome.frames.at(i), source.frames.at(i));
    }
}

void ScannerCaptureTests::scanToPdfBuildsPagesInOrder() {
    FakeFrameSource source;
    source.devices = {fakeDevice()};
    source.frames = {
        makeBmp(2, 1, QByteArray::fromHex("FF0000")),
        makeBmp(4, 1, QByteArray::fromHex("00FF00")),
        makeBmp(6, 1, QByteArray::fromHex("0000FF")),
    };

    ScannerCapture capture(&source);
    ScannerCaptureOptions options;
    options.dpi = 72;  // 72 DPI 时 1 像素 = 1 点，页面尺寸就是像素尺寸
    QByteArray pdf;
    const ScanOutcome outcome = capture.scanToPdf(options, &pdf);

    QVERIFY2(outcome.ok, qPrintable(outcome.errorMessage));
    QCOMPARE(outcome.pageCount(), 3);
    QVERIFY(!pdf.isEmpty());

    // 拼出来的东西必须真的能打开、真的是三页。
    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(pdf, QStringLiteral("scan.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 3);

    // 页序：宽度 2 / 4 / 6 的三页，顺序不能乱。
    const QVector<double> expected{2.0, 4.0, 6.0};
    for (int i = 0; i < 3; ++i) {
        const auto page = opened.document.page(i);
        QVERIFY(page.has_value());
        QCOMPARE(page->widthPoints(), expected.at(i));
    }

    // 第一页的颜色也要对上（页序对了但内容串了，同样是错的）。
    const auto first = opened.document.page(0);
    QVERIFY(first.has_value());
    const QImage rendered = first->render(1.0);
    QVERIFY(!rendered.isNull());
    QCOMPARE(qRed(rendered.pixel(0, 0)), 255);
    QCOMPARE(qGreen(rendered.pixel(0, 0)), 0);
    QCOMPARE(qBlue(rendered.pixel(0, 0)), 0);
}

void ScannerCaptureTests::tiffFrameBecomesPdfPage() {
    FakeFrameSource source;
    source.devices = {fakeDevice()};
    // 有些扫描仪默认就交 TIFF：这条链路以前会报「格式不支持」，
    // 现在必须能一路走到 PDF。
    source.frames = {makeTiff(2, 1, QByteArray::fromHex("40FF"))};

    ScannerCapture capture(&source);
    ScannerCaptureOptions options;
    options.dpi = 72;
    const ScanOutcome outcome = capture.capture(source.devices.first(), options);
    QVERIFY2(outcome.ok, qPrintable(outcome.errorMessage));
    QCOMPARE(outcome.pageCount(), 1);

    QByteArray pdf;
    const ScanOutcome viaPdf = capture.scanToPdf(options, &pdf);
    QVERIFY2(viaPdf.ok, qPrintable(viaPdf.errorMessage));
    QCOMPARE(viaPdf.pageCount(), 1);
    QVERIFY(!pdf.isEmpty());

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(pdf, QStringLiteral("scan-tiff.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);

    // 像素也要对上：TIFF 解成 RGB 再进 PDF，不能串色或串行。
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const QImage rendered = page->render(1.0);
    QVERIFY(!rendered.isNull());
    QCOMPARE(qRed(rendered.pixel(0, 0)), 0x40);
    QCOMPARE(qRed(rendered.pixel(1, 0)), 0xFF);
}

// --------------------------------------------------------- 错误映射 ----
void ScannerCaptureTests::transferFailureMapsToTransferFailed() {
    FakeFrameSource source;
    source.devices = {fakeDevice()};
    source.frames = {makeBmp(2, 1, QByteArray::fromHex("FF0000"))};
    source.failAtIndex = 1;  // 第一帧正常，第二帧取不到
    source.failStatus = ScanFrameStatus::Failed;

    ScannerCapture capture(&source);
    ScannerCaptureOptions options;
    options.dpi = 72;

    const ScanOutcome outcome = capture.capture(source.devices.first(), options);
    QVERIFY(!outcome.ok);
    QCOMPARE(outcome.errorKind, ScanErrorKind::TransferFailed);
    QVERIFY(!outcome.errorMessage.isEmpty());
    QVERIFY(!outcome.errorDetail.isEmpty());
    // 取到一半失败时不能把半成品留给上层。
    QVERIFY(outcome.frames.isEmpty());

    // 同样的故障走 scanToPdf 也必须报「取图失败」，不是「转 PDF 失败」。
    QByteArray pdf;
    const ScanOutcome viaPdf = capture.scanToPdf(options, &pdf);
    QVERIFY(!viaPdf.ok);
    QCOMPARE(viaPdf.errorKind, ScanErrorKind::TransferFailed);
    QVERIFY(pdf.isEmpty());
}

void ScannerCaptureTests::unsupportedFrameMapsToUnsupportedImage() {
    FakeFrameSource source;
    source.devices = {fakeDevice()};
    // GIF 头：格式明确不支持，必须点名「GIF」，而不是笼统地说「格式不对」。
    //
    // （这里原来用的是 TIFF 头——TIFF 现在是支持的格式了，
    // 拿它当「不支持」的样本，这条用例就名不副实了。）
    source.frames = {QByteArrayLiteral("GIF89a\x01\x00\x01\x00\x00\x00\x00")};

    ScannerCapture capture(&source);
    ScannerCaptureOptions options;
    options.dpi = 72;

    const ScanOutcome outcome = capture.capture(source.devices.first(), options);
    QVERIFY(!outcome.ok);
    QCOMPARE(outcome.errorKind, ScanErrorKind::UnsupportedImage);
    QVERIFY(!outcome.errorMessage.isEmpty());
    QVERIFY(outcome.errorDetail.contains(QStringLiteral("GIF")));
    QVERIFY(outcome.frames.isEmpty());

    QByteArray pdf;
    const ScanOutcome viaPdf = capture.scanToPdf(options, &pdf);
    QVERIFY(!viaPdf.ok);
    QCOMPARE(viaPdf.errorKind, ScanErrorKind::UnsupportedImage);
    QVERIFY(pdf.isEmpty());
}

void ScannerCaptureTests::pdfAssemblyFailureMapsToPdfFailed() {
    FakeFrameSource source;
    source.devices = {fakeDevice()};
    // 格式认得出来（BMP），但数据是坏的 → 失败发生在「拼 PDF」这一步。
    QByteArray broken = makeBmp(4, 1, QByteArray::fromHex("FF0000"));
    broken.chop(20);
    source.frames = {broken};

    ScannerCapture capture(&source);
    ScannerCaptureOptions options;
    options.dpi = 72;
    QByteArray pdf;
    const ScanOutcome outcome = capture.scanToPdf(options, &pdf);

    QVERIFY(!outcome.ok);
    QCOMPARE(outcome.errorKind, ScanErrorKind::PdfFailed);
    QVERIFY(!outcome.errorMessage.isEmpty());
    // 失败的 PDF 不能是半成品。
    QVERIFY(pdf.isEmpty());
    QVERIFY(outcome.frames.isEmpty());
}

void ScannerCaptureTests::userCancellationMapsToUserCancelled() {
    FakeFrameSource source;
    source.devices = {fakeDevice()};
    source.frames = {makeBmp(2, 1, QByteArray::fromHex("FF0000"))};
    source.failAtIndex = 0;
    source.failStatus = ScanFrameStatus::Cancelled;

    ScannerCapture capture(&source);
    ScannerCaptureOptions options;
    options.dpi = 72;

    const ScanOutcome outcome = capture.capture(source.devices.first(), options);
    QVERIFY(!outcome.ok);
    QCOMPARE(outcome.errorKind, ScanErrorKind::UserCancelled);
    QVERIFY(!outcome.errorMessage.isEmpty());
    QVERIFY(outcome.frames.isEmpty());
}

void ScannerCaptureTests::enumerationFailureIsReported() {
    FakeFrameSource source;
    source.devices = {fakeDevice()};
    source.enumerateErrorKind = ScanErrorKind::EnumerationFailed;

    ScannerCapture capture(&source);
    ScannerCaptureOptions options;
    options.dpi = 72;
    QByteArray pdf;
    const ScanOutcome outcome = capture.scanToPdf(options, &pdf);

    QVERIFY(!outcome.ok);
    QCOMPARE(outcome.errorKind, ScanErrorKind::EnumerationFailed);
    QVERIFY(!outcome.errorMessage.isEmpty());
    QVERIFY(pdf.isEmpty());
}

void ScannerCaptureTests::maxPagesLimitsTheNumberOfFrames() {
    FakeFrameSource source;
    source.devices = {fakeDevice()};
    source.frames = {
        makeBmp(2, 1, QByteArray::fromHex("FF0000")),
        makeBmp(4, 1, QByteArray::fromHex("00FF00")),
        makeBmp(6, 1, QByteArray::fromHex("0000FF")),
    };

    ScannerCapture capture(&source);
    ScannerCaptureOptions options;
    options.dpi = 72;
    options.maxPages = 2;
    const ScanOutcome outcome = capture.capture(source.devices.first(), options);

    QVERIFY2(outcome.ok, qPrintable(outcome.errorMessage));
    QCOMPARE(outcome.pageCount(), 2);
    QCOMPARE(outcome.frames.at(0), source.frames.at(0));
    QCOMPARE(outcome.frames.at(1), source.frames.at(1));
}

// --------------------------------------------------------- 默认路径 ----

void ScannerCaptureTests::defaultCaptureStillUsesWia() {
    // 默认构造必须还是走 WIA（本轮没有改这条路径的行为）：
    // 本机没有扫描仪时，给出的是「说得清的原因」，不是空字符串。
    ScannerCapture capture;
    QString error;
    const QVector<ScannerDevice> devices = capture.listDevices(&error);
    if (devices.isEmpty()) {
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(QStringLiteral("扫描仪")) || error.contains(QStringLiteral("WIA")));
        return;
    }
    for (const ScannerDevice& device : devices) {
        QVERIFY(!device.deviceId.isEmpty());
        QVERIFY(!device.displayName().isEmpty());
    }
}

QTEST_MAIN(ScannerCaptureTests)
#include "ScannerCaptureTests.moc"
