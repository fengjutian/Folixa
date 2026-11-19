#include "doc/export/OfficeExport.h"

#include "doc/export/OfficeMedia.h"

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QTest>
#include <QVector>

using aipdf::doc::ExportFormat;
using aipdf::doc::OfficePageContent;
using aipdf::doc::TextBand;
using aipdf::doc::TextExportLine;
using aipdf::doc::office::ImagePlacement;
using aipdf::doc::office::MediaSet;
using aipdf::doc::office::TableGrid;

namespace {

// 一张最小的 PNG（只有签名与 IHDR，够 RTF 读出像素尺寸）。
//
// 为什么自己造：真 PNG 还得带 IDAT 与 CRC，造起来啰嗦；而 RTF 这边
// 只用到「签名 + IHDR 里的宽高」这两样。
QByteArray tinyPng(int width, int height) {
    QByteArray out;
    out.append(QByteArray::fromHex("89504E470D0A1A0A"));
    out.append(QByteArray::fromHex("0000000D"));
    out.append(QByteArray("IHDR"));
    // IHDR 里的宽高是大端序的四字节整数。
    auto appendBigEndian = [&out](int value) {
        out.append(static_cast<char>((value >> 24) & 0xFF));
        out.append(static_cast<char>((value >> 16) & 0xFF));
        out.append(static_cast<char>((value >> 8) & 0xFF));
        out.append(static_cast<char>(value & 0xFF));
    };
    appendBigEndian(width);
    appendBigEndian(height);
    out.append(QByteArray::fromHex("0806000000"));
    return out;
}

QByteArray toHex(const QByteArray& bytes) {
    static const char kDigits[] = "0123456789abcdef";
    QByteArray out;
    out.reserve(bytes.size() * 2);
    for (const char raw : bytes) {
        const unsigned char value = static_cast<unsigned char>(raw);
        out.append(kDigits[value >> 4]);
        out.append(kDigits[value & 0x0F]);
    }
    return out;
}

// 一页内容：一行文字 + 一张图 + 一张表。
OfficePageContent samplePage(const MediaSet& media, int mediaIndex) {
    OfficePageContent page;

    TextExportLine line;
    line.text = QStringLiteral("Hello RTF");
    TextBand band;
    band.lines.append(line);
    band.left = 40.0;
    band.right = 300.0;
    band.bottom = 700.0;
    band.top = 720.0;
    band.fullWidth = true;
    page.bands.append(band);

    ImagePlacement image;
    image.mediaIndex = mediaIndex;
    image.leftPoints = 40.0;
    image.bottomPoints = 500.0;
    image.widthPoints = 120.0;
    image.heightPoints = 60.0;
    image.bandIndex = 0;
    page.images.append(image);

    TableGrid table;
    table.rows.append({QStringLiteral("Name"), QStringLiteral("Qty")});
    table.rows.append({QStringLiteral("Bolt"), QStringLiteral("12")});
    table.columnWidthsPoints = {80.0, 60.0};
    table.leftPoints = 40.0;
    table.bottomPoints = 200.0;
    table.widthPoints = 140.0;
    table.heightPoints = 40.0;
    table.bandIndex = 0;
    page.tables.append(table);

    return page;
}

}  // namespace

class Wave21RtfTests : public QObject {
    Q_OBJECT

private slots:
    void rtfCarriesImageAsPictureGroup();
    void rtfPictureSizeFollowsPlacement();
    void rtfCarriesTableAsRealRows();
    void rtfWithoutMediaHasNoPictureNorTable();
    void rtfKeepsTextAndPageBreak();
};

void Wave21RtfTests::rtfCarriesImageAsPictureGroup() {
    MediaSet media;
    const QByteArray png = tinyPng(64, 32);
    const int mediaIndex = media.add(png, QStringLiteral("image/png"), QStringLiteral("png"));
    const OfficePageContent page = samplePage(media, mediaIndex);

    const aipdf::doc::ExportResult result =
        aipdf::doc::exportPagesTo(ExportFormat::Rtf, {page}, QSizeF(595.0, 842.0), media);
    QVERIFY2(result.ok, qPrintable(result.error));
    const QString rtf = QString::fromUtf8(result.bytes);

    QVERIFY2(rtf.contains(QStringLiteral("\\pict")), "RTF 里要有图片组（\\pict）");
    QVERIFY2(rtf.contains(QStringLiteral("\\pngblip")), "PNG 要用 \\pngblip 声明");
    QVERIFY2(rtf.contains(toHex(png)), "图片字节要真的写进去（十六进制）");
    // 像素尺寸来自 PNG 的 IHDR，不是页面上的显示尺寸。
    QVERIFY2(rtf.contains(QStringLiteral("\\picw64\\pich32")), qPrintable(rtf.left(200)));
}

void Wave21RtfTests::rtfPictureSizeFollowsPlacement() {
    MediaSet media;
    const int mediaIndex = media.add(tinyPng(10, 10), QStringLiteral("image/png"),
                                     QStringLiteral("png"));
    const OfficePageContent page = samplePage(media, mediaIndex);

    const aipdf::doc::ExportResult result =
        aipdf::doc::exportPagesTo(ExportFormat::Rtf, {page}, QSizeF(595.0, 842.0), media);
    QVERIFY(result.ok);
    const QString rtf = QString::fromUtf8(result.bytes);

    // 显示尺寸用缇（1 点 = 20 缇）：120 点宽 → 2400，60 点高 → 1200。
    QVERIFY2(rtf.contains(QStringLiteral("\\picwgoal2400")), qPrintable(rtf.left(200)));
    QVERIFY2(rtf.contains(QStringLiteral("\\pichgoal1200")), qPrintable(rtf.left(200)));
}

void Wave21RtfTests::rtfCarriesTableAsRealRows() {
    MediaSet media;
    const int mediaIndex = media.add(tinyPng(10, 10), QStringLiteral("image/png"),
                                     QStringLiteral("png"));
    const OfficePageContent page = samplePage(media, mediaIndex);

    const aipdf::doc::ExportResult result =
        aipdf::doc::exportPagesTo(ExportFormat::Rtf, {page}, QSizeF(595.0, 842.0), media);
    QVERIFY(result.ok);
    const QString rtf = QString::fromUtf8(result.bytes);

    QVERIFY2(rtf.contains(QStringLiteral("\\trowd")), "RTF 里要有表格行定义");
    QVERIFY2(rtf.contains(QStringLiteral("\\cellx")), "表格行要有列边界");
    QVERIFY2(rtf.contains(QStringLiteral("\\intbl")), "单元格要在表格里");
    QVERIFY2(rtf.contains(QStringLiteral("\\row")), "表格行要收尾");
    // 列边界是累加的列宽：80 点 → 1600 缇，140 点 → 2800 缇。
    QVERIFY2(rtf.contains(QStringLiteral("\\cellx1600\\cellx2800")), qPrintable(rtf.left(400)));
    QVERIFY2(rtf.contains(QStringLiteral("Bolt")), "单元格文字要真的进去");
    QVERIFY2(rtf.contains(QStringLiteral("12")), "单元格文字要真的进去");
}

void Wave21RtfTests::rtfWithoutMediaHasNoPictureNorTable() {
    MediaSet media;
    OfficePageContent page;
    TextExportLine line;
    line.text = QStringLiteral("Plain text only");
    TextBand band;
    band.lines.append(line);
    page.bands.append(band);

    const aipdf::doc::ExportResult result =
        aipdf::doc::exportPagesTo(ExportFormat::Rtf, {page}, QSizeF(595.0, 842.0), media);
    QVERIFY(result.ok);
    const QString rtf = QString::fromUtf8(result.bytes);

    QVERIFY2(!rtf.contains(QStringLiteral("\\pict")), "没有图片就不该留图片组");
    QVERIFY2(!rtf.contains(QStringLiteral("\\trowd")), "没有表格就不该留表格行");
    QVERIFY(rtf.contains(QStringLiteral("Plain text only")));
}

void Wave21RtfTests::rtfKeepsTextAndPageBreak() {
    MediaSet media;
    const int mediaIndex = media.add(tinyPng(10, 10), QStringLiteral("image/png"),
                                     QStringLiteral("png"));
    const OfficePageContent page = samplePage(media, mediaIndex);

    const aipdf::doc::ExportResult result =
        aipdf::doc::exportPagesTo(ExportFormat::Rtf, {page, page}, QSizeF(595.0, 842.0), media);
    QVERIFY(result.ok);
    const QString rtf = QString::fromUtf8(result.bytes);

    QVERIFY(rtf.startsWith(QStringLiteral("{\\rtf1")));
    QVERIFY(rtf.trimmed().endsWith(QStringLiteral("}")));
    QVERIFY2(rtf.contains(QStringLiteral("Hello RTF")), "文字不能丢");
    QVERIFY2(rtf.contains(QStringLiteral("\\page")), "页与页之间要有换页");
    // 换页只出现一次（两页 → 一个分页符）。
    QCOMPARE(rtf.count(QStringLiteral("\\page")), 1);
}

QTEST_MAIN(Wave21RtfTests)
#include "Wave21RtfTests.moc"
