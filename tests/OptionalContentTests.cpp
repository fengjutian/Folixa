#include "pdf/OptionalContent.h"

#include "pdf/PdfiumAdapter.h"

#include <QFile>
#include <QString>
#include <QTest>
#include <QVector>

#include <optional>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::OptionalContentGroup;
using aipdf::pdf::OptionalContentLayer;
using aipdf::pdf::OptionalContentMark;
using aipdf::pdf::OptionalContentPageCommand;
using aipdf::pdf::OptionalContentPlan;
using aipdf::pdf::OptionalContentSummary;
using aipdf::pdf::PdfOpenResult;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 读夹具 PDF。
QByteArray fixtureBytes(const QString& name) {
    QFile file(QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

// 页面的原始内容流（直接问 qpdf，不经过本模块的读回函数）。
//
// 为什么要有这个：读回函数是我们自己写的，用它验证自己写的东西，
// 万一两边一起错就发现不了。这里绕开读回函数，直接看文件里的原始字节。
QByteArray rawContentOf(const QByteArray& bytes, int pageIndex) {
#ifdef AIPDF_WITH_QPDF
    QPDF pdf;
    pdf.processMemoryFile("test.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return {};
    QPDFObjectHandle contents = pages.at(static_cast<size_t>(pageIndex)).getKey("/Contents");
    QByteArray result;
    if (contents.isArray()) {
        for (QPDFObjectHandle item : contents.getArrayAsVector()) {
            if (!item.isStream()) continue;
            const std::shared_ptr<Buffer> data = item.getStreamData(qpdf_dl_generalized);
            if (!data) continue;
            result += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                 static_cast<qsizetype>(data->getSize()));
        }
    } else if (contents.isStream()) {
        const std::shared_ptr<Buffer> data = contents.getStreamData(qpdf_dl_generalized);
        if (data) {
            result = QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                static_cast<qsizetype>(data->getSize()));
        }
    }
#endif
    return result;
}

// catalog 的 /PageMode。
QString rawPageMode(const QByteArray& bytes) {
#ifdef AIPDF_WITH_QPDF
    QPDF pdf;
    pdf.processMemoryFile("test.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    const QPDFObjectHandle mode = pdf.getRoot().getKey("/PageMode");
    if (!mode.isName()) return {};
    return QString::fromStdString(mode.getName());
#else
    Q_UNUSED(bytes);
    return {};
#endif
}

// 一份方案：两个图层，第一个可见、第二个不可见。
OptionalContentPlan makePlan() {
    OptionalContentPlan plan;
    OptionalContentLayer visible;
    visible.name = QStringLiteral("Watermark");
    visible.defaultVisible = true;
    plan.layers.append(visible);

    OptionalContentLayer hidden;
    hidden.name = QStringLiteral("Hidden");
    hidden.defaultVisible = false;
    plan.layers.append(hidden);
    return plan;
}

}  // namespace

class OptionalContentTests : public QObject {
    Q_OBJECT

private slots:
    void documentWithoutOptionalContentReadsEmpty();
    void emptyInputIsRejected();
    void brokenInputIsRejected();
    void writeCreatesLayers();
    void writeKeepsVisibility();
    void writeMarksContentWithBdcEmc();
    void writeAddsPageOverride();
    void writtenPdfOpensInPdfium();
    void writtenPdfKeepsText();
    void writeCanOpenLayerPanel();
    void writeRejectsEmptyPlan();
    void writeRejectsDuplicateLayerNames();
    void writeRejectsUnknownLayerReference();
    void writeRejectsOutOfRangePage();
    void writeRejectsTwoWholePageMarks();
    void writeRejectsUnknownPolicy();
    void writeRejectsPageWithoutContentStream();
};

void OptionalContentTests::documentWithoutOptionalContentReadsEmpty() {
    REQUIRE_QPDF();
    const QByteArray bytes = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!bytes.isEmpty());

    QString error;
    const OptionalContentSummary summary = aipdf::pdf::readOptionalContent(bytes, &error);
    // 只有文本、没有图层的 PDF 不是错误：返回空结果就行。
    QVERIFY(error.isEmpty());
    QVERIFY(!summary.hasOptionalContent);
    QVERIFY(summary.groups.isEmpty());
    QVERIFY(summary.pages.isEmpty());
}

void OptionalContentTests::emptyInputIsRejected() {
    QString error;
    const OptionalContentSummary summary = aipdf::pdf::readOptionalContent(QByteArray(), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(!summary.hasOptionalContent);
}

void OptionalContentTests::brokenInputIsRejected() {
    REQUIRE_QPDF();
    const QByteArray bytes = fixtureBytes(QStringLiteral("corrupt.pdf"));
    QVERIFY(!bytes.isEmpty());
    QString error;
    const OptionalContentSummary summary = aipdf::pdf::readOptionalContent(bytes, &error);
    // 坏文件必须报错，而不是崩掉或者假装「没有图层」。
    QVERIFY(!error.isEmpty());
    QVERIFY(!summary.hasOptionalContent);
}

void OptionalContentTests::writeCreatesLayers() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const OptionalContentSummary summary = aipdf::pdf::readOptionalContent(written, nullptr);
    QVERIFY(summary.hasOptionalContent);
    QCOMPARE(summary.groups.size(), 2);
    QCOMPARE(summary.groups.at(0).name, QStringLiteral("Watermark"));
    QCOMPARE(summary.groups.at(1).name, QStringLiteral("Hidden"));
    // 图层面板里的次序就是写入的次序。
    QCOMPARE(summary.groups.at(0).orderIndex, 0);
    QCOMPARE(summary.groups.at(1).orderIndex, 1);
}

void OptionalContentTests::writeKeepsVisibility() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const OptionalContentSummary summary = aipdf::pdf::readOptionalContent(written, nullptr);
    QCOMPARE(summary.groups.size(), 2);
    QCOMPARE(summary.groups.at(0).visible, true);
    QCOMPARE(summary.groups.at(1).visible, false);
}

void OptionalContentTests::writeMarksContentWithBdcEmc() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    OptionalContentMark mark;
    mark.pageIndex = 0;
    mark.layerName = QStringLiteral("Watermark");
    plan.marks.append(mark);

    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    // 原始字节里必须真的有标记内容：/OC /<属性键名> BDC … EMC。
    const QByteArray content = rawContentOf(written, 0);
    QVERIFY(!content.isEmpty());
    QVERIFY(content.contains("/OC "));
    QVERIFY(content.contains(" BDC "));
    QVERIFY(content.contains(" EMC"));
    // 原来的内容还在（标记只是把它包起来）。
    QVERIFY(content.contains("Folixa"));

    // 页面 /Resources /Properties 里要有「键名 → 图层」的对应关系。
    const OptionalContentSummary summary = aipdf::pdf::readOptionalContent(written, nullptr);
    QCOMPARE(summary.pages.size(), 1);
    QCOMPARE(summary.pages.at(0).properties.size(), 1);
    QCOMPARE(summary.pages.at(0).properties.at(0).groupName, QStringLiteral("Watermark"));
}

void OptionalContentTests::writeAddsPageOverride() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    OptionalContentPageCommand command;
    command.pageIndex = 1;
    command.groupNames = QStringList{QStringLiteral("Watermark")};
    command.policy = QStringLiteral("AllOn");
    plan.pageCommands.append(command);

    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const OptionalContentSummary summary = aipdf::pdf::readOptionalContent(written, nullptr);
    bool found = false;
    for (const aipdf::pdf::OptionalContentPage& page : summary.pages) {
        if (page.pageIndex != 1) continue;
        found = true;
        QCOMPARE(page.commands.size(), 1);
        QCOMPARE(page.commands.at(0).policy, QStringLiteral("AllOn"));
        QCOMPARE(page.commands.at(0).groupNames, QStringList{QStringLiteral("Watermark")});
    }
    QVERIFY(found);
}

void OptionalContentTests::writtenPdfOpensInPdfium() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    // 写出来的文件必须能被 PDFium 打开。
    const PdfOpenResult result = aipdf::pdf::openDocumentFromBytes(written);
    QVERIFY2(result.document.isValid(), qPrintable(result.errorMessage));
    QCOMPARE(result.document.pageCount(), 2);
}

void OptionalContentTests::writtenPdfKeepsText() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    OptionalContentMark mark;
    mark.pageIndex = 0;
    mark.layerName = QStringLiteral("Watermark");
    plan.marks.append(mark);

    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    // 标记内容不能把文字弄丢：文本层还在。
    const PdfOpenResult result = aipdf::pdf::openDocumentFromBytes(written);
    QVERIFY(result.document.isValid());
    const std::optional<aipdf::pdf::PdfPage> page = result.document.page(0);
    QVERIFY(page.has_value());
    const std::optional<aipdf::pdf::TextPage> textPage = page->extractText();
    QVERIFY(textPage.has_value());
    QVERIFY(textPage->plainText().contains(QStringLiteral("Folixa")));
}

void OptionalContentTests::writeCanOpenLayerPanel() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    plan.openLayerPanel = true;
    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QCOMPARE(rawPageMode(written), QStringLiteral("/UseOC"));
}

void OptionalContentTests::writeRejectsEmptyPlan() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    QString error;
    const QByteArray written =
        aipdf::pdf::writeOptionalContent(source, OptionalContentPlan(), &error);
    QVERIFY(written.isEmpty());
    QVERIFY(!error.isEmpty());
}

void OptionalContentTests::writeRejectsDuplicateLayerNames() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    OptionalContentLayer duplicate;
    duplicate.name = QStringLiteral("Watermark");
    plan.layers.append(duplicate);

    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY(written.isEmpty());
    QVERIFY(error.contains(QStringLiteral("重复")));
}

void OptionalContentTests::writeRejectsUnknownLayerReference() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    OptionalContentMark mark;
    mark.pageIndex = 0;
    mark.layerName = QStringLiteral("NotDefined");
    plan.marks.append(mark);

    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY(written.isEmpty());
    QVERIFY(error.contains(QStringLiteral("NotDefined")));
}

void OptionalContentTests::writeRejectsOutOfRangePage() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    OptionalContentMark mark;
    mark.pageIndex = 9;
    mark.layerName = QStringLiteral("Watermark");
    plan.marks.append(mark);

    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY(written.isEmpty());
    QVERIFY(error.contains(QStringLiteral("页码")));
}

void OptionalContentTests::writeRejectsTwoWholePageMarks() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    OptionalContentMark first;
    first.pageIndex = 0;
    first.layerName = QStringLiteral("Watermark");
    plan.marks.append(first);
    OptionalContentMark second;
    second.pageIndex = 0;
    second.layerName = QStringLiteral("Hidden");
    plan.marks.append(second);

    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY(written.isEmpty());
    QVERIFY(error.contains(QStringLiteral("整页")));
}

void OptionalContentTests::writeRejectsUnknownPolicy() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    OptionalContentPageCommand command;
    command.pageIndex = 0;
    command.groupNames = QStringList{QStringLiteral("Watermark")};
    command.policy = QStringLiteral("Sometimes");
    plan.pageCommands.append(command);

    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY(written.isEmpty());
    QVERIFY(error.contains(QStringLiteral("Sometimes")));
}

void OptionalContentTests::writeRejectsPageWithoutContentStream() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("empty.pdf"));
    QVERIFY(!source.isEmpty());

    OptionalContentPlan plan = makePlan();
    OptionalContentMark mark;
    mark.pageIndex = 0;
    mark.layerName = QStringLiteral("Watermark");
    plan.marks.append(mark);

    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    // 空白页没有内容流：必须报错，不能悄悄跳过。
    QVERIFY(written.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("内容流")));
}

QTEST_GUILESS_MAIN(OptionalContentTests)
#include "OptionalContentTests.moc"
