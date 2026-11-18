#include "app/controllers/OneNoteExporter.h"

#include <QImage>
#include <QTest>
#include <QXmlStreamReader>

#include <memory>

using namespace aipdf::app;

namespace {

// 假后端：记录调用，按脚本返回结果。
//
// 为什么不让单测真的去调 OneNote：COM 调用会把 OneNote 进程拉起来、
// 往用户的笔记本里写页面，单测绝对不能干这个。
// 抽成接口之后，「没装」「COM 起不来」「写页失败」三条路径都能在单测里走一遍。
class FakeBackend final : public OneNoteBackend {
public:
    // findTargetSection 的脚本
    bool sectionFound = true;
    OneNoteErrorKind findKind = OneNoteErrorKind::None;
    QString findDetail;
    // createPageWithImage 的脚本
    OneNoteErrorKind createKind = OneNoteErrorKind::None;
    QString createDetail;
    // 第几次调用开始失败（0 = 一直成功）。
    int failFromCall = 0;

    // 记录下来的调用
    int findCalls = 0;
    int createCalls = 0;
    QString sectionIdUsed;
    QStringList createdTitles;
    QVector<QByteArray> createdImages;
    // 每一页送过去的文字块（空 = 这一页只送了图片）。
    QVector<QVector<OneNoteTextBlock>> createdTextBlocks;

    bool findTargetSection(OneNoteTarget* target, OneNoteErrorKind* errorKind,
                           QString* detail) override {
        ++findCalls;
        if (!sectionFound) {
            *errorKind = findKind;
            *detail = findDetail;
            return false;
        }
        target->notebookName = QStringLiteral("测试笔记本");
        target->sectionName = QStringLiteral("测试分区");
        target->sectionId = QStringLiteral("{SECTION-ID}");
        return true;
    }

    bool createPageWithImage(const QString& sectionId, const QString& pageTitle,
                             const QByteArray& imageFile, OneNoteImageFormat format,
                             const QSizeF& displaySizePoints,
                             const QVector<OneNoteTextBlock>& textBlocks,
                             OneNotePageWriteReport* report, OneNoteErrorKind* errorKind,
                             QString* detail) override {
        ++createCalls;
        sectionIdUsed = sectionId;
        const bool scheduledFail = failFromCall > 0 && createCalls >= failFromCall;
        if (scheduledFail || createKind != OneNoteErrorKind::None) {
            *errorKind = createKind != OneNoteErrorKind::None ? createKind
                                                              : OneNoteErrorKind::ExportFailed;
            *detail = createDetail.isEmpty() ? QStringLiteral("写页失败") : createDetail;
            return false;
        }
        createdTitles.append(pageTitle);
        createdImages.append(imageFile);
        createdTextBlocks.append(textBlocks);
        if (report != nullptr) {
            report->textLayerIncluded = !textBlocks.isEmpty();
        }
        return true;
    }
};

// 造一张有内容的图。
QImage makeTestImage(int width = 40, int height = 30) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::white);
    image.setPixel(0, 0, qRgb(255, 0, 0));
    return image;
}

// 造一个「装了 OneNote」的环境。
OneNoteEnvironment installedEnvironment() {
    OneNoteEnvironment environment;
    environment.installed = true;
    environment.executablePath = QStringLiteral("C:/Program Files/OneNote/ONENOTE.EXE");
    return environment;
}

}  // namespace

class OneNoteExporterTests final : public QObject {
    Q_OBJECT

private slots:
    void normalizeEmptySelectionMeansAllPages();
    void normalizeDropsOutOfRangePages();
    void normalizeRemovesDuplicatesAndSorts();
    void normalizeEmptyDocumentHasNoPages();
    void planKeepsPagesFormatAndDpi();
    void planRejectsInvalidDpi();
    void pageRangeCollapsesContinuousRuns();
    void pageRangeListsSparsePages();
    void pageRangeEmptySaysNothingToExport();
    void imageFormatLabelsDiffer();
    void encodePngProducesPngBytes();
    void encodeJpegProducesJpegBytes();
    void encodeNullImageFails();
    void encodeJpegFlattensAlphaChannel();
    void pageXmlCarriesImageTitleAndId();
    void pageXmlStaysWellFormedWithCdataEnd();
    void pageXmlCarriesTextLayerRuns();
    void textRunXmlEscapesMarkup();
    void textRunXmlSkipsBlankBlocks();
    void exportFailsWhenOneNoteNotInstalled();
    void exportReportsComUnavailable();
    void exportReportsWriteFailure();
    void exportFailsWhenNothingPlanned();
    void exportDoesNotTouchBackendWhenRenderingFails();
    void exportReportsHowManyPagesWereWritten();
    void exportWritesTextLayerToBackend();
    void exportDegradesToImageOnlyWhenTextLayerIsBroken();
    void exportWithoutTextProviderSaysNoTextLayer();
    void exportSendsEveryPlannedPage();
    void successMessageExplainsWhatWasSent();
    void defaultExporterOnlyProbesRegistry();
};

// ------------------------------------------------------------------
// 页码归一化
// ------------------------------------------------------------------

void OneNoteExporterTests::normalizeEmptySelectionMeansAllPages() {
    const QVector<int> pages = normalizeOneNotePageNumbers(QVector<int>(), 4);
    QCOMPARE(pages, QVector<int>({1, 2, 3, 4}));
}

void OneNoteExporterTests::normalizeDropsOutOfRangePages() {
    // 负例：越界页码（0、负数、比总页数大）必须被丢掉，不能带进 OneNote。
    const QVector<int> pages = normalizeOneNotePageNumbers(QVector<int>({0, -2, 2, 9}), 3);
    QCOMPARE(pages, QVector<int>({2}));
}

void OneNoteExporterTests::normalizeRemovesDuplicatesAndSorts() {
    // 负例：同一页重复选多次只能送一次。
    const QVector<int> pages = normalizeOneNotePageNumbers(QVector<int>({3, 1, 3, 2, 1}), 5);
    QCOMPARE(pages, QVector<int>({1, 2, 3}));
}

void OneNoteExporterTests::normalizeEmptyDocumentHasNoPages() {
    // 负例：空文档没有页可导。
    QVERIFY(normalizeOneNotePageNumbers(QVector<int>(), 0).isEmpty());
    QVERIFY(normalizeOneNotePageNumbers(QVector<int>({1}), 0).isEmpty());
}

// ------------------------------------------------------------------
// 导出计划
// ------------------------------------------------------------------

void OneNoteExporterTests::planKeepsPagesFormatAndDpi() {
    OneNoteExportOptions options;
    options.pageNumbers = QVector<int>({2, 1});
    options.format = OneNoteImageFormat::Jpeg;
    options.dpi = 200;

    const OneNoteExportPlan plan =
        buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 5, options);
    QCOMPARE(plan.documentTitle, QStringLiteral("报告.pdf"));
    QCOMPARE(plan.pageNumbers, QVector<int>({1, 2}));
    QCOMPARE(plan.format, OneNoteImageFormat::Jpeg);
    QCOMPARE(plan.dpi, 200);
    QCOMPARE(plan.pageCount(), 2);
}

void OneNoteExporterTests::planRejectsInvalidDpi() {
    // 负例：dpi 不合法时回落到默认值，而不是拿 0 去渲染（0 DPI 会得到空图）。
    OneNoteExportOptions options;
    options.dpi = 0;
    const OneNoteExportPlan plan =
        buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 1, options);
    QCOMPARE(plan.dpi, 144);
}

// ------------------------------------------------------------------
// 人话与图片
// ------------------------------------------------------------------

void OneNoteExporterTests::pageRangeCollapsesContinuousRuns() {
    QCOMPARE(formatOneNotePageRange(QVector<int>({1, 2, 3})), QStringLiteral("第 1-3 页"));
    QCOMPARE(formatOneNotePageRange(QVector<int>({4})), QStringLiteral("第 4 页"));
}

void OneNoteExporterTests::pageRangeListsSparsePages() {
    QCOMPARE(formatOneNotePageRange(QVector<int>({1, 3, 5})),
             QStringLiteral("第 1、3、5 页"));
    QCOMPARE(formatOneNotePageRange(QVector<int>({1, 2, 4})), QStringLiteral("第 1-2、4 页"));
}

void OneNoteExporterTests::pageRangeEmptySaysNothingToExport() {
    // 负例：没有页时不能说成「第 1-0 页」。
    QVERIFY(!formatOneNotePageRange(QVector<int>()).contains(QStringLiteral("第 1")));
}

void OneNoteExporterTests::imageFormatLabelsDiffer() {
    QVERIFY(oneNoteImageFormatLabel(OneNoteImageFormat::Png) !=
            oneNoteImageFormatLabel(OneNoteImageFormat::Jpeg));
    QVERIFY(oneNoteImageFormatLabel(OneNoteImageFormat::Png).contains(QStringLiteral("PNG")));
    QVERIFY(oneNoteImageFormatLabel(OneNoteImageFormat::Jpeg).contains(QStringLiteral("JPEG")));
}

void OneNoteExporterTests::encodePngProducesPngBytes() {
    const QByteArray bytes =
        encodeOneNoteImageBytes(makeTestImage(), OneNoteImageFormat::Png, 144);
    QVERIFY(!bytes.isEmpty());
    QVERIFY(bytes.startsWith(QByteArray("\x89PNG", 4)));
}

void OneNoteExporterTests::encodeJpegProducesJpegBytes() {
    const QByteArray bytes =
        encodeOneNoteImageBytes(makeTestImage(), OneNoteImageFormat::Jpeg, 144);
    QVERIFY(!bytes.isEmpty());
    QVERIFY(bytes.startsWith(QByteArray("\xFF\xD8\xFF", 3)));
}

void OneNoteExporterTests::encodeNullImageFails() {
    // 负例：空图编码不出东西，必须让上层知道失败了。
    QVERIFY(encodeOneNoteImageBytes(QImage(), OneNoteImageFormat::Png, 144).isEmpty());
}

void OneNoteExporterTests::encodeJpegFlattensAlphaChannel() {
    // 负例（边界）：JPEG 没有透明通道，带 alpha 的图也得能编码，不能写出一张黑图。
    QImage image(10, 10, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    image.setPixel(5, 5, qRgba(255, 0, 0, 128));
    const QByteArray bytes = encodeOneNoteImageBytes(image, OneNoteImageFormat::Jpeg, 144);
    QVERIFY(!bytes.isEmpty());
    QVERIFY(bytes.startsWith(QByteArray("\xFF\xD8\xFF", 3)));
}

// ------------------------------------------------------------------
// 送进 OneNote 的 XML
// ------------------------------------------------------------------

void OneNoteExporterTests::pageXmlCarriesImageTitleAndId() {
    const QByteArray imageFile =
        encodeOneNoteImageBytes(makeTestImage(), OneNoteImageFormat::Png, 144);
    QVERIFY(!imageFile.isEmpty());

    const QString xml = buildOneNotePageXml(QStringLiteral("{PAGE-ID}"), QStringLiteral("第 1 页"),
                                             imageFile, OneNoteImageFormat::Png, QSizeF(300, 400));
    QVERIFY(xml.contains(
        QStringLiteral("http://schemas.microsoft.com/office/onenote/2013/onenote")));
    QVERIFY(xml.contains(QStringLiteral("ID=\"{PAGE-ID}\"")));
    QVERIFY(xml.contains(QStringLiteral("第 1 页")));
    QVERIFY(xml.contains(QStringLiteral("format=\"png\"")));
    QVERIFY(xml.contains(QString::fromLatin1(imageFile.toBase64())));
    QVERIFY(xml.contains(QStringLiteral("width=\"300.0\"")));
    QVERIFY(xml.contains(QStringLiteral("height=\"400.0\"")));
}

void OneNoteExporterTests::pageXmlStaysWellFormedWithCdataEnd() {
    // 负例（边界）：标题里出现 "]]>" 会把 CDATA 提前关掉，XML 直接坏掉。
    const QByteArray imageFile =
        encodeOneNoteImageBytes(makeTestImage(), OneNoteImageFormat::Png, 144);
    const QString xml =
        buildOneNotePageXml(QStringLiteral("{PAGE-ID}"), QStringLiteral("标题里有 ]]> 危险字符"),
                             imageFile, OneNoteImageFormat::Png, QSizeF(300, 400));
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
    }
    QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
}

void OneNoteExporterTests::pageXmlCarriesTextLayerRuns() {
    // 正例：页面 XML 里要有一整条文字层，OneNote 里才能搜到这页的文字。
    const QByteArray imageFile =
        encodeOneNoteImageBytes(makeTestImage(), OneNoteImageFormat::Png, 144);
    QVERIFY(!imageFile.isEmpty());

    QVector<OneNoteTextBlock> blocks;
    blocks.append(OneNoteTextBlock{QStringLiteral("第一行文字")});
    blocks.append(OneNoteTextBlock{QStringLiteral("Second line")});

    const QString xml = buildOneNotePageXml(QStringLiteral("{PAGE-ID}"), QStringLiteral("第 1 页"),
                                             imageFile, OneNoteImageFormat::Png, QSizeF(300, 400),
                                             blocks);
    QVERIFY(xml.contains(QStringLiteral("<one:Text>")));
    QVERIFY(xml.contains(QStringLiteral("<one:r>")));
    QVERIFY(xml.contains(QStringLiteral("<one:t>")));
    QVERIFY(xml.contains(QStringLiteral("第一行文字")));
    QVERIFY(xml.contains(QStringLiteral("Second line")));
    // 结构：图片那个 one:OE 后面，同一个 one:OEChildren 里还跟着装文字的 one:OE。
    QVERIFY(xml.contains(
        QStringLiteral("</one:Image></one:OE><one:OE alignment=\"left\"><one:Text>")));

    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
    }
    QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
}

void OneNoteExporterTests::textRunXmlEscapesMarkup() {
    // 负例：文字里有 XML 元字符 —— 必须转义，否则整页 XML 坏掉。
    QVector<OneNoteTextBlock> blocks;
    blocks.append(OneNoteTextBlock{QStringLiteral("a<b>&\"c\"")});
    const QString xml = buildOneNoteTextRunsXml(blocks);
    QVERIFY(xml.contains(QStringLiteral("&lt;")));
    QVERIFY(xml.contains(QStringLiteral("&amp;")));
    QVERIFY(xml.contains(QStringLiteral("&quot;")));

    const int start = xml.indexOf(QStringLiteral("<one:t>"));
    const int end = xml.indexOf(QStringLiteral("</one:t>"));
    QVERIFY(start >= 0);
    QVERIFY(end > start);
    const QString inner = xml.mid(start + 7, end - start - 7);
    // one:t 里面不能有没转义的元字符。
    QVERIFY(!inner.contains(QLatin1Char('<')));
    QVERIFY(!inner.contains(QLatin1Char('>')));
}

void OneNoteExporterTests::textRunXmlSkipsBlankBlocks() {
    // 负例：没有文字就没有 one:Text（空壳的 one:Text 只是噪音）。
    QVector<OneNoteTextBlock> blocks;
    blocks.append(OneNoteTextBlock{QStringLiteral("   ")});
    QVERIFY(buildOneNoteTextRunsXml(blocks).isEmpty());
    QVERIFY(buildOneNoteTextRunsXml(QVector<OneNoteTextBlock>()).isEmpty());
}

// ------------------------------------------------------------------
// 三条失败路径 + 成功路径
// ------------------------------------------------------------------

void OneNoteExporterTests::exportFailsWhenOneNoteNotInstalled() {
    OneNoteEnvironment environment;
    environment.installed = false;
    environment.detail = QStringLiteral("注册表 HKCR\\OneNote.Application\\CLSID 未找到");

    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* fake = backend.get();
    OneNoteExporter exporter(environment, std::move(backend));

    bool rendererCalled = false;
    auto renderer = [&rendererCalled](int, int) {
        rendererCalled = true;
        return makeTestImage();
    };

    OneNoteExportOptions options;
    const OneNoteExportPlan plan = buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 3, options);
    const OneNoteExportResult result = exporter.exportToOneNote(plan, renderer);

    QVERIFY(!result.ok);
    QCOMPARE(result.errorKind, OneNoteErrorKind::NotInstalled);
    QVERIFY(result.message.contains(QStringLiteral("未检测到本机安装 OneNote")));
    QVERIFY(result.message.contains(QStringLiteral("CLSID")));
    // 没装 OneNote 时不该白渲染一遍页面，也不该去碰 OneNote。
    QVERIFY(!rendererCalled);
    QCOMPARE(fake->findCalls, 0);
}

void OneNoteExporterTests::exportReportsComUnavailable() {
    auto backend = std::make_unique<FakeBackend>();
    backend->sectionFound = false;
    backend->findKind = OneNoteErrorKind::ComUnavailable;
    backend->findDetail = QStringLiteral("无法启动 OneNote 进程（0x80040154）");
    FakeBackend* fake = backend.get();
    OneNoteExporter exporter(installedEnvironment(), std::move(backend));

    auto renderer = [](int, int) { return makeTestImage(); };
    OneNoteExportOptions options;
    const OneNoteExportPlan plan = buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 3, options);
    const OneNoteExportResult result = exporter.exportToOneNote(plan, renderer);

    QVERIFY(!result.ok);
    QCOMPARE(result.errorKind, OneNoteErrorKind::ComUnavailable);
    QVERIFY(result.message.contains(QStringLiteral("无法连接 OneNote 的后台接口")));
    QVERIFY(result.message.contains(QStringLiteral("0x80040154")));
    QCOMPARE(fake->createCalls, 0);
}

void OneNoteExporterTests::exportReportsWriteFailure() {
    auto backend = std::make_unique<FakeBackend>();
    backend->createKind = OneNoteErrorKind::ExportFailed;
    backend->createDetail = QStringLiteral("把页面写入 OneNote 失败（0x80042001）");
    FakeBackend* fake = backend.get();
    OneNoteExporter exporter(installedEnvironment(), std::move(backend));

    auto renderer = [](int, int) { return makeTestImage(); };
    OneNoteExportOptions options;
    const OneNoteExportPlan plan = buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 3, options);
    const OneNoteExportResult result = exporter.exportToOneNote(plan, renderer);

    QVERIFY(!result.ok);
    QCOMPARE(result.errorKind, OneNoteErrorKind::ExportFailed);
    QVERIFY(result.message.contains(QStringLiteral("发送到 OneNote 失败")));
    QVERIFY(result.message.contains(QStringLiteral("0x80042001")));
    QCOMPARE(fake->createCalls, 1);
    QVERIFY(result.createdPageTitles.isEmpty());
}

void OneNoteExporterTests::exportFailsWhenNothingPlanned() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* fake = backend.get();
    OneNoteExporter exporter(installedEnvironment(), std::move(backend));

    bool rendererCalled = false;
    auto renderer = [&rendererCalled](int, int) {
        rendererCalled = true;
        return makeTestImage();
    };

    // 负例：选中的页全部越界 → 归一化后没有页 → 明确失败，不允许静默送出空内容。
    OneNoteExportOptions options;
    options.pageNumbers = QVector<int>({99});
    const OneNoteExportPlan plan = buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 3, options);
    QVERIFY(plan.pageNumbers.isEmpty());

    const OneNoteExportResult result = exporter.exportToOneNote(plan, renderer);
    QVERIFY(!result.ok);
    QCOMPARE(result.errorKind, OneNoteErrorKind::ExportFailed);
    QVERIFY(result.message.contains(QStringLiteral("没有可导出的页面")));
    QVERIFY(!rendererCalled);
    QCOMPARE(fake->findCalls, 0);
}

void OneNoteExporterTests::exportDoesNotTouchBackendWhenRenderingFails() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* fake = backend.get();
    OneNoteExporter exporter(installedEnvironment(), std::move(backend));

    // 负例：第 2 页渲染不出图 → 要说清是哪一页，不能笼统报「发送失败」。
    auto renderer = [](int pageIndex, int) {
        return pageIndex == 1 ? QImage() : makeTestImage();
    };

    OneNoteExportOptions options;
    options.pageNumbers = QVector<int>({1, 2, 3});
    const OneNoteExportPlan plan = buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 3, options);
    const OneNoteExportResult result = exporter.exportToOneNote(plan, renderer);

    QVERIFY(!result.ok);
    QCOMPARE(result.errorKind, OneNoteErrorKind::ExportFailed);
    QVERIFY(result.message.contains(QStringLiteral("第 2 页")));
    // 渲染阶段失败 → 一页都不写：假后端一次都没被调用，
    // 用户的笔记本里不会留下半截残页。
    QCOMPARE(fake->createCalls, 0);
    QCOMPARE(fake->findCalls, 0);
    QCOMPARE(result.createdPageTitles.size(), 0);
    // 也不能说「已经写进去几页」—— 一页都没写。
    QVERIFY(!result.message.contains(QStringLiteral("已经写进")));
}

void OneNoteExporterTests::exportReportsHowManyPagesWereWritten() {
    auto backend = std::make_unique<FakeBackend>();
    // 负例：写到第 2 页失败 —— 前 1 页已经进用户的笔记本了。
    backend->failFromCall = 2;
    backend->createDetail = QStringLiteral("把页面写入 OneNote 失败（0x80042001）");
    FakeBackend* fake = backend.get();
    OneNoteExporter exporter(installedEnvironment(), std::move(backend));

    auto renderer = [](int, int) { return makeTestImage(); };
    OneNoteExportOptions options;
    options.pageNumbers = QVector<int>({1, 2, 3});
    const OneNoteExportPlan plan = buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 3, options);
    const OneNoteExportResult result = exporter.exportToOneNote(plan, renderer);

    QVERIFY(!result.ok);
    QCOMPARE(result.errorKind, OneNoteErrorKind::ExportFailed);
    QCOMPARE(fake->createCalls, 2);
    // 如实说清已经写进去几页，不假装全失败。
    QCOMPARE(result.createdPageCount(), 1);
    QVERIFY(result.message.contains(QStringLiteral("已经写进 OneNote 1 页")));
    // 也不能声称会自动删除（本程序没有删页的能力）。
    QVERIFY(!result.message.contains(QStringLiteral("删除")));
}

void OneNoteExporterTests::exportWritesTextLayerToBackend() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* fake = backend.get();
    OneNoteExporter exporter(installedEnvironment(), std::move(backend));

    auto renderer = [](int, int) { return makeTestImage(); };
    OneNotePageTextProvider provider = [](int pageIndex) {
        QVector<OneNoteTextBlock> blocks;
        blocks.append(OneNoteTextBlock{QStringLiteral("第 %1 页的可搜索文字").arg(pageIndex + 1)});
        return blocks;
    };

    OneNoteExportOptions options;
    options.pageNumbers = QVector<int>({1, 2});
    const OneNoteExportPlan plan = buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 2, options);
    const OneNoteExportResult result = exporter.exportToOneNote(plan, renderer, provider);

    QVERIFY(result.ok);
    QCOMPARE(result.createdPageCount(), 2);
    QVERIFY(result.textLayerWritten);
    QVERIFY(!result.degradedToImageOnly);
    QCOMPARE(result.degradedPageCount, 0);
    // 后端每一页都拿到了文字块。
    QCOMPARE(fake->createdTextBlocks.size(), 2);
    QCOMPARE(fake->createdTextBlocks.at(0).size(), 1);
    QCOMPARE(fake->createdTextBlocks.at(0).at(0).text, QStringLiteral("第 1 页的可搜索文字"));
    QCOMPARE(fake->createdTextBlocks.at(1).at(0).text, QStringLiteral("第 2 页的可搜索文字"));
}

void OneNoteExporterTests::exportDegradesToImageOnlyWhenTextLayerIsBroken() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* fake = backend.get();
    OneNoteExporter exporter(installedEnvironment(), std::move(backend));

    auto renderer = [](int, int) { return makeTestImage(); };
    // 负例：文字里有 XML 1.0 不允许的控制字符 → 文字层写坏。
    OneNotePageTextProvider provider = [](int) {
        QVector<OneNoteTextBlock> blocks;
        blocks.append(OneNoteTextBlock{QStringLiteral("坏文字") + QChar(0x01)});
        return blocks;
    };

    OneNoteExportOptions options;
    options.pageNumbers = QVector<int>({1});
    const OneNoteExportPlan plan = buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 1, options);
    const OneNoteExportResult result = exporter.exportToOneNote(plan, renderer, provider);

    // 宁可少功能，不要功能全废：这一页照样送进去了，只是没有文字层。
    QVERIFY(result.ok);
    QCOMPARE(result.createdPageCount(), 1);
    QVERIFY(result.degradedToImageOnly);
    QCOMPARE(result.degradedPageCount, 1);
    QVERIFY(!result.textLayerDetail.isEmpty());
    // 后端拿到的是**空的**文字块：这一页只送图片。
    QCOMPARE(fake->createdTextBlocks.size(), 1);
    QVERIFY(fake->createdTextBlocks.at(0).isEmpty());
    QVERIFY(result.message.contains(QStringLiteral("只送图片")));
}

void OneNoteExporterTests::exportWithoutTextProviderSaysNoTextLayer() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* fake = backend.get();
    OneNoteExporter exporter(installedEnvironment(), std::move(backend));

    auto renderer = [](int, int) { return makeTestImage(); };
    OneNoteExportOptions options;
    options.pageNumbers = QVector<int>({1});
    const OneNoteExportPlan plan = buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 1, options);
    const OneNoteExportResult result = exporter.exportToOneNote(plan, renderer);

    QVERIFY(result.ok);
    QVERIFY(!result.textLayerWritten);
    QVERIFY(!result.degradedToImageOnly);
    QCOMPARE(fake->createdTextBlocks.size(), 1);
    QVERIFY(fake->createdTextBlocks.at(0).isEmpty());
    // 结果里要说明白：这次 OneNote 里搜不到文字。
    QVERIFY(result.message.contains(QStringLiteral("没有文字层")));
}

void OneNoteExporterTests::exportSendsEveryPlannedPage() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* fake = backend.get();
    OneNoteExporter exporter(installedEnvironment(), std::move(backend));

    QVector<int> requestedPages;
    QVector<int> requestedDpis;
    auto renderer = [&requestedPages, &requestedDpis](int pageIndex, int dpi) {
        requestedPages.append(pageIndex);
        requestedDpis.append(dpi);
        return makeTestImage();
    };

    OneNoteExportOptions options;
    options.pageNumbers = QVector<int>({1, 2, 3});
    options.format = OneNoteImageFormat::Png;
    options.dpi = 144;
    const OneNoteExportPlan plan = buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 5, options);

    const OneNoteExportResult result = exporter.exportToOneNote(plan, renderer);
    QVERIFY(result.ok);
    QCOMPARE(result.errorKind, OneNoteErrorKind::None);
    QCOMPARE(result.createdPageCount(), 3);
    QCOMPARE(requestedPages, QVector<int>({0, 1, 2}));
    QCOMPARE(requestedDpis, QVector<int>({144, 144, 144}));
    QCOMPARE(fake->createCalls, 3);
    QCOMPARE(fake->sectionIdUsed, QStringLiteral("{SECTION-ID}"));
    QCOMPARE(fake->createdTitles,
             QStringList({QStringLiteral("报告.pdf - 第 1 页"), QStringLiteral("报告.pdf - 第 2 页"),
                           QStringLiteral("报告.pdf - 第 3 页")}));
    QVERIFY(fake->createdImages.first().startsWith(QByteArray("\x89PNG", 4)));
    QCOMPARE(result.notebookName, QStringLiteral("测试笔记本"));
    QCOMPARE(result.sectionName, QStringLiteral("测试分区"));
    // 成功文案要说清：导哪几页、导成什么、送到 OneNote 的哪一页。
    QVERIFY(result.message.contains(QStringLiteral("第 1-3 页")));
    QVERIFY(result.message.contains(QStringLiteral("PNG 图片")));
    QVERIFY(result.message.contains(QStringLiteral("测试分区")));
}

void OneNoteExporterTests::successMessageExplainsWhatWasSent() {
    OneNoteExportOptions options;
    options.format = OneNoteImageFormat::Jpeg;
    options.dpi = 200;
    OneNoteExportPlan plan = buildOneNoteExportPlan(QStringLiteral("报告.pdf"), 3, options);
    plan.notebookName = QStringLiteral("我的笔记本");
    plan.sectionName = QStringLiteral("我的分区");

    const QString message = oneNoteSuccessMessage(plan, 3);
    QVERIFY(message.contains(QStringLiteral("第 1-3 页")));
    QVERIFY(message.contains(QStringLiteral("JPEG 图片")));
    QVERIFY(message.contains(QStringLiteral("200 DPI")));
    QVERIFY(message.contains(QStringLiteral("我的笔记本 / 我的分区")));
    QVERIFY(message.contains(QStringLiteral("3 页")));
}

void OneNoteExporterTests::defaultExporterOnlyProbesRegistry() {
    // 构造导出器只读注册表，不该启动 OneNote（也不该在用户桌面上弹东西）。
    OneNoteExporter exporter;
    const OneNoteEnvironment environment = exporter.environment();
    QCOMPARE(environment.installed, !environment.executablePath.isEmpty());
}

QTEST_MAIN(OneNoteExporterTests)
#include "OneNoteExporterTests.moc"
