#include "pdf/annot/ScreenAnnotation.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QDir>
#include <QFile>
#include <QtTest>

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <memory>

using aipdf::pdf::readScreenMediaAnnotations;
using aipdf::pdf::ScreenMediaAnnotation;
using aipdf::pdf::ScreenMediaSpec;
using aipdf::pdf::writeScreenMediaAnnotation;

namespace {

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

QByteArray readFixture(const QString& name) {
    QFile file(fixturePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// 往字节串尾部追加一个 4 字节大端整数。
//
// RIFF 与 ISO BMFF 的长度字段都是这个排法（大端），所以三个夹具共用它。
void appendU32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>((value >> 24) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

// 造一段 MP4 字节。
//
// 为什么不用「假字节」：写页面内播放注解时会看媒体文件头——不在容器白名单里
// 就失败，所以夹具必须真的像那种容器才测得到结构。
// 这里只造到「文件头是 ftyp 盒」的程度：字节一致性测试要的是「原样搬运」，
// 不是「MP4 能不能播」。
QByteArray makeMp4(const QByteArray& payload) {
    QByteArray out;
    QByteArray brands("isom");
    brands.append(QByteArray(4, '\0'));  // minor version
    brands.append("isom");
    brands.append("iso2");
    brands.append("avc1");
    brands.append("mp41");
    appendU32(out, static_cast<quint32>(8 + brands.size()));
    out.append("ftyp");
    out.append(brands);
    appendU32(out, static_cast<quint32>(8 + payload.size()));
    out.append("mdat");
    out.append(payload);
    return out;
}

// 造一段 AVI 字节（RIFF 容器，形式类型 'AVI '）。
//
// 只造到「文件头能被认出来」的程度：AVI 内部真正的轨结构不影响本模块的判定，
// 本模块只认容器，不解析 AVI。
QByteArray makeAvi(const QByteArray& payload) {
    QByteArray out;
    out.append("RIFF");
    appendU32(out, static_cast<quint32>(4 + 8 + payload.size()));  // RIFF 剩余长度
    out.append("AVI ");
    out.append("LIST");
    appendU32(out, static_cast<quint32>(4 + payload.size()));
    out.append("movi");
    out.append(payload);
    return out;
}

// 造一段 WAV 字节（RIFF 容器，形式类型 'WAVE'）。
//
// 同上：只造到文件头能被认出来的程度。
QByteArray makeWav(const QByteArray& payload) {
    QByteArray out;
    out.append("RIFF");
    appendU32(out, static_cast<quint32>(4 + 8 + 16 + payload.size()));  // RIFF 剩余长度
    out.append("WAVE");
    out.append("fmt ");
    appendU32(out, 16);
    out.append(QByteArray(16, '\0'));  // PCM 参数（占位）
    out.append("data");
    appendU32(out, static_cast<quint32>(payload.size()));
    out.append(payload);
    return out;
}

// 一个 /Assets 名字树条目的探测结果。
//
// 为什么用 qpdf 直接探，而不是调本模块自己的读接口：
// 读接口和写接口共用同一套理解，接口自己错了测试也跟着错；
// 直接看对象图才是对「写出来的结构对不对」的独立检查。
struct AssetProbe {
    QString key;             // 名字树里的键
    QString clipType;        // MediaClip 的 /Type
    QString clipSubtype;     // MediaClip 的 /S
    QString mime;            // MediaClip 的 /M
    QString filespecName;    // 文件说明的 /F
    QString streamSubtype;   // 嵌入流字典的 /Subtype
    bool hasFilespec = false;
    bool hasStream = false;
    qint64 paramsSize = 0;
    QByteArray bytes;        // 嵌入流里的字节
};

// 读文档 catalog 的 /Assets 名字树。
QVector<AssetProbe> probeAssets(const QByteArray& pdfBytes) {
    QVector<AssetProbe> probes;
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("probe.pdf", pdfBytes.constData(),
                          static_cast<size_t>(pdfBytes.size()));
    const QPDFObjectHandle catalog = pdf.getRoot();
    const QPDFObjectHandle assets = catalog.getKey("/Assets");
    if (!assets.isDictionary()) return probes;
    const QPDFObjectHandle names = assets.getKey("/Names");
    if (!names.isArray()) return probes;

    const std::vector<QPDFObjectHandle> items = names.getArrayAsVector();
    for (size_t i = 0; i + 1 < items.size(); i += 2) {
        AssetProbe probe;
        const QPDFObjectHandle keyHandle = items.at(i);
        if (keyHandle.isString()) {
            probe.key = QString::fromUtf8(keyHandle.getUTF8Value().c_str());
        }
        const QPDFObjectHandle clip = items.at(i + 1);
        if (clip.isDictionary()) {
            const QPDFObjectHandle type = clip.getKey("/Type");
            if (type.isName()) probe.clipType = QString::fromStdString(type.getName());
            const QPDFObjectHandle subtype = clip.getKey("/S");
            if (subtype.isName()) probe.clipSubtype = QString::fromStdString(subtype.getName());
            const QPDFObjectHandle mime = clip.getKey("/M");
            if (mime.isString()) probe.mime = QString::fromUtf8(mime.getUTF8Value().c_str());

            const QPDFObjectHandle filespec = clip.getKey("/D");
            if (filespec.isDictionary()) {
                probe.hasFilespec = true;
                const QPDFObjectHandle name = filespec.getKey("/F");
                if (name.isString()) {
                    probe.filespecName = QString::fromUtf8(name.getUTF8Value().c_str());
                }
                const QPDFObjectHandle embedded = filespec.getKey("/EF");
                if (embedded.isDictionary()) {
                    QPDFObjectHandle stream = embedded.getKey("/F");
                    if (stream.isStream()) {
                        probe.hasStream = true;
                        const QPDFObjectHandle dictionary = stream.getDict();
                        const QPDFObjectHandle streamSubtype = dictionary.getKey("/Subtype");
                        if (streamSubtype.isName()) {
                            probe.streamSubtype = QString::fromStdString(streamSubtype.getName());
                        }
                        const QPDFObjectHandle params = dictionary.getKey("/Params");
                        if (params.isDictionary()) {
                            const QPDFObjectHandle size = params.getKey("/Size");
                            if (size.isInteger()) probe.paramsSize = size.getIntValue();
                        }
                        const std::shared_ptr<Buffer> data =
                            stream.getStreamData(qpdf_dl_generalized);
                        if (data) {
                            probe.bytes = QByteArray(
                                reinterpret_cast<const char*>(data->getBuffer()),
                                static_cast<qsizetype>(data->getSize()));
                        }
                    }
                }
            }
        }
        probes.append(probe);
    }
    return probes;
}

// 页面上 /Screen 注解的条数（直接看页字典，不走本模块的读接口）。
int screenAnnotationCount(const QByteArray& pdfBytes, int pageIndex) {
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("probe.pdf", pdfBytes.constData(),
                          static_cast<size_t>(pdfBytes.size()));
    const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return -1;
    const QPDFObjectHandle annots = pages.at(static_cast<size_t>(pageIndex)).getKey("/Annots");
    if (!annots.isArray()) return 0;
    int count = 0;
    for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
        if (!annot.isDictionary()) continue;
        const QPDFObjectHandle subtype = annot.getKey("/Subtype");
        if (!subtype.isName()) continue;
        if (QString::fromStdString(subtype.getName()) == QStringLiteral("/Screen")) ++count;
    }
    return count;
}

// 页面上某个 /Screen 注解的 /Subtype、/F、/P、动作。
struct ScreenAnnotProbe {
    QString subtype;
    int flags = -1;
    bool hasPageRef = false;
    QString actionSubtype;
    bool hasAppearance = false;
    QByteArray rect;
};

ScreenAnnotProbe probeScreenAnnotation(const QByteArray& pdfBytes, int pageIndex) {
    ScreenAnnotProbe probe;
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("probe.pdf", pdfBytes.constData(),
                          static_cast<size_t>(pdfBytes.size()));
    const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return probe;
    const QPDFObjectHandle annots = pages.at(static_cast<size_t>(pageIndex)).getKey("/Annots");
    if (!annots.isArray()) return probe;
    for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
        if (!annot.isDictionary()) continue;
        const QPDFObjectHandle subtype = annot.getKey("/Subtype");
        if (!subtype.isName()) continue;
        if (QString::fromStdString(subtype.getName()) != QStringLiteral("/Screen")) continue;

        probe.subtype = QString::fromStdString(subtype.getName());
        const QPDFObjectHandle flags = annot.getKey("/F");
        if (flags.isInteger()) probe.flags = static_cast<int>(flags.getIntValue());
        const QPDFObjectHandle pageRef = annot.getKey("/P");
        probe.hasPageRef = pageRef.isDictionary() || pageRef.isIndirect();
        probe.hasAppearance = annot.hasKey("/AP");
        const QPDFObjectHandle action = annot.getKey("/A");
        if (action.isDictionary()) {
            const QPDFObjectHandle actionSubtype = action.getKey("/S");
            if (actionSubtype.isName()) {
                probe.actionSubtype = QString::fromStdString(actionSubtype.getName());
            }
        }
        return probe;
    }
    return probe;
}

bool sameRect(const QRectF& left, const QRectF& right) {
    return qAbs(left.left() - right.left()) < 0.01 && qAbs(left.top() - right.top()) < 0.01 &&
           qAbs(left.width() - right.width()) < 0.01 &&
           qAbs(left.height() - right.height()) < 0.01;
}

}  // namespace

class ScreenAnnotationTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void writesAndReadsBackScreenAnnotation();
    void keepsMediaBytesByteIdentical();
    void registersMediaClipInAssetsNameTree();
    void rejectsNonMp4Media();
    void keepsRestOfDocumentIntact();
    void keepsBothClipsWhenWrittenTwice();
    void acceptsAviContainer();
    void acceptsWavContainer();
    void derivesMimeFromContainerNotFileName();
    void rejectsUnknownContainer();
    void rejectsTruncatedContainerHeader();

private:
    bool hasNormal_ = false;
    bool hasNavigation_ = false;
};

void ScreenAnnotationTests::initTestCase() {
    hasNormal_ = QFile::exists(fixturePath(QStringLiteral("normal.pdf")));
    hasNavigation_ = QFile::exists(fixturePath(QStringLiteral("navigation.pdf")));
    if (!hasNormal_ || !hasNavigation_) {
        QSKIP("缺少 normal.pdf / navigation.pdf 夹具");
    }
}

void ScreenAnnotationTests::writesAndReadsBackScreenAnnotation() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!source.isEmpty());

    ScreenMediaSpec spec;
    spec.pageIndex = 0;
    spec.rect = QRectF(20.0, 20.0, 100.0, 50.0);
    spec.fileName = QStringLiteral("clip.mp4");
    spec.mediaBytes = makeMp4(QByteArray("first payload"));

    QString error;
    const QByteArray written = writeScreenMediaAnnotation(source, spec, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QVERIFY(error.isEmpty());

    // 读回来：注解在指定页上，矩形、名字、大小都对得上。
    const QVector<ScreenMediaAnnotation> found = readScreenMediaAnnotations(written, &error);
    QVERIFY2(!found.isEmpty(), qPrintable(error));
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().pageIndex, 0);
    QVERIFY(sameRect(found.first().rect, spec.rect));
    QCOMPARE(found.first().fileName, QStringLiteral("clip.mp4"));
    QCOMPARE(found.first().mimeType, QStringLiteral("video/mp4"));
    QCOMPARE(found.first().mediaSize, static_cast<qint64>(spec.mediaBytes.size()));

    // 对象层面：/Subtype 是 /Screen，/F 是 4（Print），/P 指向页面，
    // 动作是 /S /Rendition；隐藏型注解不该有 /AP。
    const ScreenAnnotProbe probe = probeScreenAnnotation(written, 0);
    QCOMPARE(probe.subtype, QStringLiteral("/Screen"));
    QCOMPARE(probe.flags, 4);
    QVERIFY(probe.hasPageRef);
    QCOMPARE(probe.actionSubtype, QStringLiteral("/Rendition"));
    QVERIFY(!probe.hasAppearance);
}

void ScreenAnnotationTests::keepsMediaBytesByteIdentical() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!source.isEmpty());

    // 负载里放不能被压缩/转码碰到的字节：任何一处改动都会在这里露出来。
    QByteArray payload;
    for (int i = 0; i < 512; ++i) {
        payload.append(static_cast<char>((i * 37) % 251));
    }
    const QByteArray media = makeMp4(payload);

    ScreenMediaSpec spec;
    spec.pageIndex = 0;
    spec.rect = QRectF(10.0, 10.0, 80.0, 60.0);
    spec.fileName = QStringLiteral("clip.mp4");
    spec.mediaBytes = media;

    QString error;
    const QByteArray written = writeScreenMediaAnnotation(source, spec, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const QVector<AssetProbe> assets = probeAssets(written);
    QCOMPARE(assets.size(), 1);
    QCOMPARE(assets.first().bytes, media);
    QCOMPARE(assets.first().paramsSize, static_cast<qint64>(media.size()));
    QCOMPARE(assets.first().streamSubtype, QStringLiteral("/video/mp4"));
}

void ScreenAnnotationTests::registersMediaClipInAssetsNameTree() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!source.isEmpty());

    ScreenMediaSpec spec;
    spec.pageIndex = 0;
    spec.rect = QRectF(10.0, 10.0, 80.0, 60.0);
    spec.fileName = QStringLiteral("clip.mp4");
    spec.mediaBytes = makeMp4(QByteArray("payload"));

    QString error;
    const QByteArray written = writeScreenMediaAnnotation(source, spec, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    // /Assets 名字树里要有这个 MediaClip；
    // MediaClip 的 /D 指向文件说明，文件说明的 /EF /F 指向嵌入流。
    const QVector<AssetProbe> assets = probeAssets(written);
    QCOMPARE(assets.size(), 1);
    const AssetProbe& probe = assets.first();
    QCOMPARE(probe.key, QStringLiteral("clip.mp4"));
    QCOMPARE(probe.clipType, QStringLiteral("/MediaClip"));
    QCOMPARE(probe.clipSubtype, QStringLiteral("/MCD"));
    QCOMPARE(probe.mime, QStringLiteral("video/mp4"));
    QVERIFY(probe.hasFilespec);
    QCOMPARE(probe.filespecName, QStringLiteral("clip.mp4"));
    QVERIFY(probe.hasStream);
}

void ScreenAnnotationTests::rejectsNonMp4Media() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!source.isEmpty());

    ScreenMediaSpec spec;
    spec.pageIndex = 0;
    spec.rect = QRectF(10.0, 10.0, 80.0, 60.0);
    spec.fileName = QStringLiteral("clip.mp4");
    // 一段普通文本：不是任何一种受支持的容器（MP4 / AVI / WAV）。
    spec.mediaBytes = QByteArray("this is definitely not an mp4 file");

    QString error;
    const QByteArray written = writeScreenMediaAnnotation(source, spec, &error);
    QVERIFY(written.isEmpty());
    QVERIFY(!error.isEmpty());
    // 错误信息要说清「只支持 MP4」和替代做法。
    QVERIFY(error.contains(QStringLiteral("MP4")));
    QVERIFY(error.contains(QStringLiteral("clip.mp4")));
    QVERIFY(error.contains(QStringLiteral("附件")));

    // 文档没被动过：失败时不该返回半成品。
    QCOMPARE(screenAnnotationCount(source, 0), 0);
}

void ScreenAnnotationTests::keepsRestOfDocumentIntact() {
    const QByteArray source = readFixture(QStringLiteral("navigation.pdf"));
    QVERIFY(!source.isEmpty());

    ScreenMediaSpec spec;
    spec.pageIndex = 1;  // 写到第 2 页
    spec.rect = QRectF(60.0, 60.0, 200.0, 120.0);
    spec.fileName = QStringLiteral("clip.mp4");
    spec.mediaBytes = makeMp4(QByteArray("payload"));

    QString error;
    const QByteArray written = writeScreenMediaAnnotation(source, spec, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    // 注解落在指定的第 2 页上，没跑到别的页去。
    QCOMPARE(screenAnnotationCount(written, 1), 1);
    QCOMPARE(screenAnnotationCount(written, 0), 0);
    QCOMPARE(screenAnnotationCount(written, 2), 0);

    // 页数不变，原来的文字还能提取出来（PDFium 说了算）。
    const auto opened = aipdf::pdf::openDocumentFromBytes(written, QStringLiteral("out.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 3);

    const std::optional<aipdf::pdf::PdfPage> first = opened.document.page(0);
    QVERIFY(first.has_value());
    const std::optional<aipdf::pdf::TextPage> firstText = first->extractText();
    QVERIFY(firstText.has_value());
    QVERIFY(firstText->plainText().contains(QStringLiteral("Navigation demo")));

    const std::optional<aipdf::pdf::PdfPage> third = opened.document.page(2);
    QVERIFY(third.has_value());
    const std::optional<aipdf::pdf::TextPage> thirdText = third->extractText();
    QVERIFY(thirdText.has_value());
    QVERIFY(thirdText->plainText().contains(QStringLiteral("Page three")));
}

void ScreenAnnotationTests::keepsBothClipsWhenWrittenTwice() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!source.isEmpty());

    const QByteArray firstMedia = makeMp4(QByteArray("first payload"));
    const QByteArray secondMedia = makeMp4(QByteArray("second payload, longer than the first one"));

    ScreenMediaSpec first;
    first.pageIndex = 0;
    first.rect = QRectF(10.0, 10.0, 80.0, 60.0);
    first.fileName = QStringLiteral("clip.mp4");
    first.mediaBytes = firstMedia;

    QString error;
    const QByteArray once = writeScreenMediaAnnotation(source, first, &error);
    QVERIFY2(!once.isEmpty(), qPrintable(error));

    // 第二次写用同一个文件名：不能互相覆盖，要多出一个媒体片段。
    ScreenMediaSpec second = first;
    second.rect = QRectF(10.0, 10.0, 80.0, 60.0);
    second.mediaBytes = secondMedia;
    const QByteArray twice = writeScreenMediaAnnotation(once, second, &error);
    QVERIFY2(!twice.isEmpty(), qPrintable(error));

    const QVector<ScreenMediaAnnotation> found = readScreenMediaAnnotations(twice, &error);
    QCOMPARE(found.size(), 2);
    // 两个片段是两份不同的媒体，不是同一份被改了两次。
    QVERIFY(found.at(0).mediaClipId != found.at(1).mediaClipId);
    QVERIFY(found.at(0).mediaSize != found.at(1).mediaSize);
    QCOMPARE(found.at(0).mediaSize, static_cast<qint64>(firstMedia.size()));
    QCOMPARE(found.at(1).mediaSize, static_cast<qint64>(secondMedia.size()));

    // 名字树里有两个条目，名字不同（同名时程序自动加序号避开）。
    const QVector<AssetProbe> assets = probeAssets(twice);
    QCOMPARE(assets.size(), 2);
    QVERIFY(assets.at(0).key != assets.at(1).key);
    QVERIFY(assets.at(0).bytes != assets.at(1).bytes);

    // 页面上有两个注解。
    QCOMPARE(screenAnnotationCount(twice, 0), 2);
}

// AVI 是白名单里的容器：写得出、读得回，MIME 是 video/x-msvideo。
void ScreenAnnotationTests::acceptsAviContainer() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!source.isEmpty());

    const QByteArray media = makeAvi(QByteArray("avi payload"));
    ScreenMediaSpec spec;
    spec.pageIndex = 0;
    spec.rect = QRectF(10.0, 10.0, 80.0, 60.0);
    spec.fileName = QStringLiteral("clip.avi");
    spec.mediaBytes = media;

    QString error;
    const QByteArray written = writeScreenMediaAnnotation(source, spec, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QVERIFY(error.isEmpty());

    // MIME 由容器推出：嵌入流的 /Subtype 与 MediaClip 的 /M 都要是 video/x-msvideo。
    const QVector<AssetProbe> assets = probeAssets(written);
    QCOMPARE(assets.size(), 1);
    QCOMPARE(assets.first().streamSubtype, QStringLiteral("/video/x-msvideo"));
    QCOMPARE(assets.first().mime, QStringLiteral("video/x-msvideo"));
    // 字节仍然是原样搬运的。
    QCOMPARE(assets.first().bytes, media);

    // 读回来也是同一个 MIME。
    const QVector<ScreenMediaAnnotation> found = readScreenMediaAnnotations(written, &error);
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().mimeType, QStringLiteral("video/x-msvideo"));
    QCOMPARE(found.first().fileName, QStringLiteral("clip.avi"));
    QCOMPARE(found.first().mediaSize, static_cast<qint64>(media.size()));
}

// WAV 是白名单里的容器：MIME 是 audio/wav。
void ScreenAnnotationTests::acceptsWavContainer() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!source.isEmpty());

    const QByteArray media = makeWav(QByteArray("wav payload"));
    ScreenMediaSpec spec;
    spec.pageIndex = 0;
    spec.rect = QRectF(10.0, 10.0, 80.0, 60.0);
    spec.fileName = QStringLiteral("voice.wav");
    spec.mediaBytes = media;

    QString error;
    const QByteArray written = writeScreenMediaAnnotation(source, spec, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QVERIFY(error.isEmpty());

    const QVector<AssetProbe> assets = probeAssets(written);
    QCOMPARE(assets.size(), 1);
    QCOMPARE(assets.first().streamSubtype, QStringLiteral("/audio/wav"));
    QCOMPARE(assets.first().mime, QStringLiteral("audio/wav"));
    QCOMPARE(assets.first().bytes, media);

    const QVector<ScreenMediaAnnotation> found = readScreenMediaAnnotations(written, &error);
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().mimeType, QStringLiteral("audio/wav"));
}

// MIME 由**容器**决定，不由文件名决定：名字叫 .bin，内容是 MP4，写出去还是 video/mp4。
void ScreenAnnotationTests::derivesMimeFromContainerNotFileName() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!source.isEmpty());

    ScreenMediaSpec spec;
    spec.pageIndex = 0;
    spec.rect = QRectF(10.0, 10.0, 80.0, 60.0);
    spec.fileName = QStringLiteral("clip.bin");  // 名字故意不对
    spec.mediaBytes = makeMp4(QByteArray("payload"));

    QString error;
    const QByteArray written = writeScreenMediaAnnotation(source, spec, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const QVector<AssetProbe> assets = probeAssets(written);
    QCOMPARE(assets.size(), 1);
    QCOMPARE(assets.first().streamSubtype, QStringLiteral("/video/mp4"));
    QCOMPARE(assets.first().mime, QStringLiteral("video/mp4"));
    // 文件名照旧原样写进 filespec。
    QCOMPARE(assets.first().filespecName, QStringLiteral("clip.bin"));
}

// 白名单之外的容器要**明确失败**，并说清支持哪几种。
void ScreenAnnotationTests::rejectsUnknownContainer() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!source.isEmpty());

    // RIFF 容器，但形式类型不是 AVI / WAVE：认不出来。
    QByteArray riff("RIFF");
    appendU32(riff, 32);
    riff.append("WEBP");
    riff.append("payload");

    ScreenMediaSpec spec;
    spec.pageIndex = 0;
    spec.rect = QRectF(10.0, 10.0, 80.0, 60.0);
    spec.fileName = QStringLiteral("clip.bin");
    spec.mediaBytes = riff;

    QString error;
    const QByteArray written = writeScreenMediaAnnotation(source, spec, &error);
    QVERIFY(written.isEmpty());
    QVERIFY(!error.isEmpty());
    // 错误信息要列出支持哪几种容器，以及替代做法。
    QVERIFY(error.contains(QStringLiteral("MP4")));
    QVERIFY(error.contains(QStringLiteral("AVI")));
    QVERIFY(error.contains(QStringLiteral("WAV")));
    QVERIFY(error.contains(QStringLiteral("附件")));
    QVERIFY(error.contains(QStringLiteral("clip.bin")));

    // 文档没被动过：失败时不该返回半成品。
    QCOMPARE(screenAnnotationCount(source, 0), 0);
}

// 截断的文件头要判失败，且不能越界读。
void ScreenAnnotationTests::rejectsTruncatedContainerHeader() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!source.isEmpty());

    // 各种「长度不够认容器」的头：只有 'RIFF'、RIFF 缺形式类型、
    // AVI / WAV 的形式类型只剩三个字节。
    const QVector<QByteArray> truncated = {
        QByteArray("RIFF", 4),
        QByteArray("RIFF\x10\x00\x00\x00", 8),
        QByteArray("RIFF\x10\x00\x00\x00AVI", 11),
        QByteArray("RIFF\x10\x00\x00\x00WAV", 11),
    };

    for (const QByteArray& media : truncated) {
        ScreenMediaSpec spec;
        spec.pageIndex = 0;
        spec.rect = QRectF(10.0, 10.0, 80.0, 60.0);
        spec.fileName = QStringLiteral("clip.bin");
        spec.mediaBytes = media;

        QString error;
        const QByteArray written = writeScreenMediaAnnotation(source, spec, &error);
        QVERIFY2(written.isEmpty(), "截断的文件头不该被放行");
        QVERIFY(!error.isEmpty());
        // 失败信息同样要说清支持哪几种容器。
        QVERIFY(error.contains(QStringLiteral("AVI")));
        QVERIFY(error.contains(QStringLiteral("WAV")));
    }

    QCOMPARE(screenAnnotationCount(source, 0), 0);
}

QTEST_GUILESS_MAIN(ScreenAnnotationTests)
#include "ScreenAnnotationTests.moc"
