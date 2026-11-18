#include "pdf/document/NamedDestination.h"

#include "pdf/kernel/PdfiumAdapter.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

#include <optional>

using aipdf::pdf::NamedDestination;
using aipdf::pdf::PdfOpenResult;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 错误信息必须是中文：领域层不接 i18n，错误文案直接写中文。
bool containsChinese(const QString& text) {
    for (const QChar character : text) {
        if (character.unicode() >= 0x4E00 && character.unicode() <= 0x9FFF) return true;
    }
    return false;
}

// 造一份结构完整的 PDF：对象表 + 交叉引用表 + trailer。
//
// 为什么自己造而不是只用夹具：命名目标要断言「落在第几页、哪个坐标」，
// 需要页面内容完全可控；夹具 normal.pdf 只有一页，multipage.pdf 又有 12 页。
QByteArray buildPdf(const QList<QPair<int, QByteArray>>& objects) {
    QByteArray out("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = offsets.size() + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        // 注意 QString::arg 的重载：arg(int, fieldWidth, base, fillChar)。
        // 只传三个参数时第三个是**进制**不是填充字符——写成 48 会把偏移
        // 按 48 进制输出，qpdf 会判定 xref 表损坏。
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id, 0), 10, 10, QLatin1Char('0'))
                   .toLatin1();
    }
    // QString::arg 不会把 %% 还原成 %，所以 EOF 标记这里直接写两个百分号。
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// 两页样例：每页几行文字，页号固定（第一页是 3 号对象，第二页是 5 号对象）。
QByteArray samplePdf() {
    const QByteArray pageOne =
        "BT /F1 12 Tf 40 250 Td (Alpha) Tj ET\n"
        "BT /F1 12 Tf 40 200 Td (Bravo) Tj ET\n"
        "BT /F1 12 Tf 40 150 Td (Charlie) Tj ET";
    const QByteArray pageTwo =
        "BT /F1 12 Tf 40 250 Td (Delta) Tj ET\n"
        "BT /F1 12 Tf 40 200 Td (Echo) Tj ET";

    QList<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R >>")));
    objects.append(qMakePair(4, QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
                                   .arg(pageOne.size())
                                   .arg(QString::fromLatin1(pageOne))
                                   .toLatin1()));
    objects.append(qMakePair(5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 6 0 R >>")));
    objects.append(qMakePair(6, QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
                                   .arg(pageTwo.size())
                                   .arg(QString::fromLatin1(pageTwo))
                                   .toLatin1()));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    return buildPdf(objects);
}

// 名字树写法（catalog 的 /Names /Dests）的样例。
//
// 这一份是给「读」的：命名目标也可以写成名称树，别的工具这么写时我们也得认。
// 三种名字编码都覆盖到：UTF-8 字节串、UTF-16BE（带 BOM）十六进制串、
// 以及放在 /Kids 子节点里的名字。
QByteArray nameTreePdf() {
    QByteArray pageOne = "BT /F1 12 Tf 40 250 Td (Alpha) Tj ET";
    QByteArray pageTwo = "BT /F1 12 Tf 40 250 Td (Delta) Tj ET";

    // 中文名字要写成 PDF 文本串里的原始 UTF-8 字节。
    //
    // 为什么不能用 "\xE5\xBA\xAB" 这种写法：那是窄字符串里的**码点**，
    // 编译成 UTF-8 之后每个字节都变成了两字节，PDF 里存的就不是 UTF-8 了。
    const QByteArray coverName = QStringLiteral("封面").toUtf8();
    const QByteArray chapterTwoName = QStringLiteral("第二章").toUtf8();
    const QByteArray chapterFourName = QStringLiteral("第四章").toUtf8();

    QByteArray catalog = "<< /Type /Catalog /Pages 2 0 R /Names << /Dests << /Names [(";
    catalog += coverName;
    catalog += ") [3 0 R /XYZ 10 20 1] <FEFF96445F550041> [5 0 R /Fit] (";
    catalog += chapterTwoName;
    catalog += ") [3 0 R /XYZ 1 2 3]] /Kids [8 0 R] >> >> >>";

    QByteArray kid = "<< /Names [(";
    kid += chapterFourName;
    kid += ") [5 0 R /XYZ 30 40 2]] >>";

    QList<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, catalog));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R >>")));
    objects.append(qMakePair(4, QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
                                   .arg(pageOne.size())
                                   .arg(QString::fromLatin1(pageOne))
                                   .toLatin1()));
    objects.append(qMakePair(5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 6 0 R >>")));
    objects.append(qMakePair(6, QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
                                   .arg(pageTwo.size())
                                   .arg(QString::fromLatin1(pageTwo))
                                   .toLatin1()));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(8, kid));
    return buildPdf(objects);
}

// 文本层（PDFium 读出来的纯文本）。
QString plainTextOf(const QByteArray& bytes) {
    const PdfOpenResult result = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!result.document.isValid()) return {};
    QString text;
    for (int i = 0; i < result.document.pageCount(); ++i) {
        const std::optional<aipdf::pdf::PdfPage> page = result.document.page(i);
        if (!page.has_value()) continue;
        const std::optional<aipdf::pdf::TextPage> textPage = page->extractText();
        if (textPage.has_value()) text += textPage->plainText();
    }
    return text;
}

// 按名字找一条命名目标。
const NamedDestination* findByName(const QVector<NamedDestination>& destinations, const QString& name) {
    for (const NamedDestination& destination : destinations) {
        if (destination.name == name) return &destination;
    }
    return nullptr;
}

// 两个落点是否一致（浮点比较留一点余量）。
bool samePosition(const QPointF& left, const QPointF& right) {
    return qFuzzyCompare(left.x() + 1.0, right.x() + 1.0) &&
           qFuzzyCompare(left.y() + 1.0, right.y() + 1.0);
}

}  // namespace

class NamedDestinationTests : public QObject {
    Q_OBJECT

private slots:
    void writeOneDestinationRoundTrips();
    void multipleDestinationsRoundTrip();
    void writtenFileOpensInPdfium();
    void writtenFileKeepsTextLayer();
    void readsNameTreeForm();
    void documentWithoutDestinationsReadsEmpty();
    void emptyInputIsRejected();
    void brokenInputIsRejected();
    void chineseAndSpaceNamesRoundTrip();
    void duplicateNameKeepsLast();
    void pageIndexOfMissingNameIsMinusOne();
    void fitPageAndZoomFormsRoundTrip();
    void fixtureDocumentCanCarryDestinations();
};

// 1. 写一个命名目标再读回来：名字、页码、落点、缩放都要一致。
void NamedDestinationTests::writeOneDestinationRoundTrips() {
    REQUIRE_QPDF();
    QVector<NamedDestination> destinations;
    NamedDestination destination;
    destination.name = QStringLiteral("第一章");
    destination.pageIndex = 1;
    destination.position = QPointF(12.5, 240.0);
    destination.zoom = 1.5;
    destinations.append(destination);

    QString error;
    const QByteArray output = aipdf::pdf::writeNamedDestinations(samplePdf(), destinations, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<NamedDestination> read = aipdf::pdf::readNamedDestinations(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(read.size(), 1);
    QCOMPARE(read.at(0).name, QStringLiteral("第一章"));
    QCOMPARE(read.at(0).pageIndex, 1);
    QVERIFY2(samePosition(read.at(0).position, QPointF(12.5, 240.0)),
             qPrintable(QStringLiteral("落点不对：%1,%2")
                            .arg(read.at(0).position.x())
                            .arg(read.at(0).position.y())));
    QVERIFY(qFuzzyCompare(read.at(0).zoom + 1.0, 2.5));
    QVERIFY(!read.at(0).fitPage);
}

// 2. 写多个：全部读回来，且两次读取的顺序一致（顺序稳定）。
void NamedDestinationTests::multipleDestinationsRoundTrip() {
    REQUIRE_QPDF();
    QVector<NamedDestination> destinations;
    NamedDestination cover;
    cover.name = QStringLiteral("封面");
    cover.pageIndex = 0;
    cover.fitPage = true;
    destinations.append(cover);

    NamedDestination appendix;
    appendix.name = QStringLiteral("附录A");
    appendix.pageIndex = 1;
    appendix.position = QPointF(30.0, 40.0);
    appendix.zoom = 2.0;
    destinations.append(appendix);

    NamedDestination chapter;
    chapter.name = QStringLiteral("第一章");
    chapter.pageIndex = 0;
    chapter.position = QPointF(0.0, 300.0);
    destinations.append(chapter);

    QString error;
    const QByteArray output = aipdf::pdf::writeNamedDestinations(samplePdf(), destinations, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<NamedDestination> first = aipdf::pdf::readNamedDestinations(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(first.size(), 3);

    QStringList names;
    for (const NamedDestination& destination : first) names.append(destination.name);
    names.sort();
    QCOMPARE(names, QStringList({QStringLiteral("封面"), QStringLiteral("第一章"),
                                 QStringLiteral("附录A")}));

    const NamedDestination* appendixEntry = findByName(first, QStringLiteral("附录A"));
    QVERIFY(appendixEntry != nullptr);
    QCOMPARE(appendixEntry->pageIndex, 1);
    QVERIFY(qFuzzyCompare(appendixEntry->zoom + 1.0, 3.0));

    const NamedDestination* coverEntry = findByName(first, QStringLiteral("封面"));
    QVERIFY(coverEntry != nullptr);
    QVERIFY(coverEntry->fitPage);

    // 再读一遍：顺序必须一样（界面列表不能每读一次就重排）。
    const QVector<NamedDestination> second = aipdf::pdf::readNamedDestinations(output, &error);
    QCOMPARE(second.size(), first.size());
    for (int i = 0; i < first.size(); ++i) {
        QCOMPARE(second.at(i).name, first.at(i).name);
        QCOMPARE(second.at(i).pageIndex, first.at(i).pageIndex);
    }
}

// 3. 写完后 PDFium 能打开这份 PDF，页数不变。
void NamedDestinationTests::writtenFileOpensInPdfium() {
    REQUIRE_QPDF();
    QVector<NamedDestination> destinations;
    NamedDestination destination;
    destination.name = QStringLiteral("封面");
    destination.pageIndex = 0;
    destination.fitPage = true;
    destinations.append(destination);

    QString error;
    const QByteArray output = aipdf::pdf::writeNamedDestinations(samplePdf(), destinations, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const PdfOpenResult result = aipdf::pdf::openDocumentFromBytes(output);
    QVERIFY2(result.document.isValid(),
             qPrintable(QStringLiteral("PDFium 打不开：%1").arg(result.errorMessage)));
    QCOMPARE(result.document.pageCount(), 2);
}

// 4. 写完后 PDFium 读出的正文一字不差。
void NamedDestinationTests::writtenFileKeepsTextLayer() {
    REQUIRE_QPDF();
    QVector<NamedDestination> destinations;
    NamedDestination destination;
    destination.name = QStringLiteral("第一章");
    destination.pageIndex = 1;
    destination.position = QPointF(12.5, 240.0);
    destination.zoom = 1.5;
    destinations.append(destination);

    const QByteArray input = samplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeNamedDestinations(input, destinations, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QString before = plainTextOf(input);
    QVERIFY(!before.isEmpty());
    QCOMPARE(plainTextOf(output), before);
}

// 5. 回读兼容 /Names /Dests 名称树写法（自己构造这种 PDF）。
void NamedDestinationTests::readsNameTreeForm() {
    REQUIRE_QPDF();
    QString error;
    const QVector<NamedDestination> read = aipdf::pdf::readNamedDestinations(nameTreePdf(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(read.size(), 4);

    // UTF-8 字节串写的名字。
    const NamedDestination* cover = findByName(read, QStringLiteral("封面"));
    QVERIFY(cover != nullptr);
    QCOMPARE(cover->pageIndex, 0);
    QVERIFY2(samePosition(cover->position, QPointF(10.0, 20.0)),
             qPrintable(QStringLiteral("落点不对：%1,%2").arg(cover->position.x()).arg(cover->position.y())));
    QVERIFY(qFuzzyCompare(cover->zoom + 1.0, 2.0));

    // UTF-16BE（带 BOM）十六进制串写的名字。
    const NamedDestination* appendix = findByName(read, QStringLiteral("附录A"));
    QVERIFY(appendix != nullptr);
    QCOMPARE(appendix->pageIndex, 1);
    QVERIFY(appendix->fitPage);

    const NamedDestination* chapterTwo = findByName(read, QStringLiteral("第二章"));
    QVERIFY(chapterTwo != nullptr);
    QCOMPARE(chapterTwo->pageIndex, 0);
    QVERIFY2(samePosition(chapterTwo->position, QPointF(1.0, 2.0)),
             qPrintable(QStringLiteral("落点不对：%1,%2")
                            .arg(chapterTwo->position.x())
                            .arg(chapterTwo->position.y())));

    // 放在 /Kids 子节点里的名字也要读得到。
    const NamedDestination* chapterFour = findByName(read, QStringLiteral("第四章"));
    QVERIFY(chapterFour != nullptr);
    QCOMPARE(chapterFour->pageIndex, 1);
}

// 6. 没有命名目标的文档：回读返回空列表，且不算错误。
void NamedDestinationTests::documentWithoutDestinationsReadsEmpty() {
    REQUIRE_QPDF();
    QString error;
    const QVector<NamedDestination> read = aipdf::pdf::readNamedDestinations(samplePdf(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(read.isEmpty());
}

// 7. 空输入：写入与回读都返回空 + 中文错误，不崩。
void NamedDestinationTests::emptyInputIsRejected() {
    QVector<NamedDestination> destinations;
    NamedDestination destination;
    destination.name = QStringLiteral("封面");
    destination.pageIndex = 0;
    destinations.append(destination);

    QString error;
    QVERIFY(aipdf::pdf::writeNamedDestinations({}, destinations, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(containsChinese(error), qPrintable(QStringLiteral("错误信息不是中文：%1").arg(error)));

    error.clear();
    QVERIFY(aipdf::pdf::readNamedDestinations({}, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(containsChinese(error), qPrintable(QStringLiteral("错误信息不是中文：%1").arg(error)));
}

// 8. 坏输入：返回空 + 中文错误，不崩。
void NamedDestinationTests::brokenInputIsRejected() {
    REQUIRE_QPDF();
    const QByteArray broken = QByteArray("this is definitely not a pdf file");

    QString error;
    QVERIFY(aipdf::pdf::writeNamedDestinations(broken, {}, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(containsChinese(error), qPrintable(QStringLiteral("错误信息不是中文：%1").arg(error)));

    error.clear();
    QVERIFY(aipdf::pdf::readNamedDestinations(broken, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(containsChinese(error), qPrintable(QStringLiteral("错误信息不是中文：%1").arg(error)));
}

// 9. 名字含中文与空格：写入与读回都要一字不差。
void NamedDestinationTests::chineseAndSpaceNamesRoundTrip() {
    REQUIRE_QPDF();
    const QStringList names = {QStringLiteral("封面"), QStringLiteral("第一章"),
                               QStringLiteral("附录 A"), QStringLiteral("带#号"),
                               QStringLiteral("A/B")};
    QVector<NamedDestination> destinations;
    for (int i = 0; i < names.size(); ++i) {
        NamedDestination destination;
        destination.name = names.at(i);
        destination.pageIndex = i % 2;
        destination.position = QPointF(10.0 * i, 20.0 * i);
        destinations.append(destination);
    }

    QString error;
    const QByteArray output = aipdf::pdf::writeNamedDestinations(samplePdf(), destinations, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<NamedDestination> read = aipdf::pdf::readNamedDestinations(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(read.size(), names.size());
    for (int i = 0; i < names.size(); ++i) {
        const NamedDestination* entry = findByName(read, names.at(i));
        QVERIFY2(entry != nullptr, qPrintable(QStringLiteral("名字丢了：%1").arg(names.at(i))));
        QCOMPARE(entry->pageIndex, i % 2);
    }
}

// 10. 同一个名字重复写入：后写覆盖前写，只留一条。
void NamedDestinationTests::duplicateNameKeepsLast() {
    REQUIRE_QPDF();
    QVector<NamedDestination> destinations;
    NamedDestination first;
    first.name = QStringLiteral("同名");
    first.pageIndex = 0;
    destinations.append(first);

    NamedDestination second;
    second.name = QStringLiteral("同名");
    second.pageIndex = 1;
    second.position = QPointF(5.0, 6.0);
    destinations.append(second);

    QString error;
    const QByteArray output = aipdf::pdf::writeNamedDestinations(samplePdf(), destinations, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<NamedDestination> read = aipdf::pdf::readNamedDestinations(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(read.size(), 1);
    QCOMPARE(read.at(0).name, QStringLiteral("同名"));
    QCOMPARE(read.at(0).pageIndex, 1);
}

// 11. 按名字查页码：找不到返回 -1，找到返回页号。
void NamedDestinationTests::pageIndexOfMissingNameIsMinusOne() {
    REQUIRE_QPDF();
    QVector<NamedDestination> destinations;
    NamedDestination destination;
    destination.name = QStringLiteral("封面");
    destination.pageIndex = 1;
    destinations.append(destination);

    QString error;
    const QByteArray output = aipdf::pdf::writeNamedDestinations(samplePdf(), destinations, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    QCOMPARE(aipdf::pdf::pageIndexOfNamedDestination(output, QStringLiteral("封面"), &error), 1);
    QCOMPARE(aipdf::pdf::pageIndexOfNamedDestination(output, QStringLiteral("没有这个"), &error), -1);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

// 12. /Fit 与带 zoom 的 /XYZ 两种形态都能正确往返。
void NamedDestinationTests::fitPageAndZoomFormsRoundTrip() {
    REQUIRE_QPDF();
    QVector<NamedDestination> destinations;
    NamedDestination fit;
    fit.name = QStringLiteral("整页");
    fit.pageIndex = 0;
    fit.fitPage = true;
    destinations.append(fit);

    NamedDestination zoomed;
    zoomed.name = QStringLiteral("放大");
    zoomed.pageIndex = 1;
    zoomed.position = QPointF(72.0, 144.0);
    zoomed.zoom = 1.75;
    destinations.append(zoomed);

    QString error;
    const QByteArray output = aipdf::pdf::writeNamedDestinations(samplePdf(), destinations, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<NamedDestination> read = aipdf::pdf::readNamedDestinations(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(read.size(), 2);

    const NamedDestination* fitEntry = findByName(read, QStringLiteral("整页"));
    QVERIFY(fitEntry != nullptr);
    QVERIFY(fitEntry->fitPage);
    QCOMPARE(fitEntry->pageIndex, 0);

    const NamedDestination* zoomEntry = findByName(read, QStringLiteral("放大"));
    QVERIFY(zoomEntry != nullptr);
    QVERIFY(!zoomEntry->fitPage);
    QCOMPARE(zoomEntry->pageIndex, 1);
    QVERIFY2(samePosition(zoomEntry->position, QPointF(72.0, 144.0)),
             qPrintable(QStringLiteral("落点不对：%1,%2")
                            .arg(zoomEntry->position.x())
                            .arg(zoomEntry->position.y())));
    QVERIFY(qFuzzyCompare(zoomEntry->zoom + 1.0, 2.75));
}

// 13. 仓库自己的夹具文档也能挂命名目标（证明不是只对手工构造的 PDF 有效）。
void NamedDestinationTests::fixtureDocumentCanCarryDestinations() {
    REQUIRE_QPDF();
#ifdef AIPDF_PDF_FIXTURES_DIR
    QFile file(QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QStringLiteral("/multipage.pdf"));
    if (!file.open(QIODevice::ReadOnly)) {
        QSKIP("夹具 multipage.pdf 不可用");
    }
    const QByteArray input = file.readAll();
    file.close();
#else
    QSKIP("没有定义 AIPDF_PDF_FIXTURES_DIR");
#endif

    QVector<NamedDestination> destinations;
    NamedDestination destination;
    destination.name = QStringLiteral("附录A");
    destination.pageIndex = 5;
    destination.position = QPointF(20.0, 300.0);
    destination.zoom = 1.0;
    destinations.append(destination);

    QString error;
    const QByteArray output = aipdf::pdf::writeNamedDestinations(input, destinations, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<NamedDestination> read = aipdf::pdf::readNamedDestinations(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(read.size(), 1);
    QCOMPARE(read.at(0).pageIndex, 5);

    const PdfOpenResult result = aipdf::pdf::openDocumentFromBytes(output);
    QVERIFY2(result.document.isValid(),
             qPrintable(QStringLiteral("PDFium 打不开：%1").arg(result.errorMessage)));
    QCOMPARE(result.document.pageCount(), 12);
    QCOMPARE(plainTextOf(output), plainTextOf(input));
}

QTEST_GUILESS_MAIN(NamedDestinationTests)
#include "NamedDestinationTests.moc"
