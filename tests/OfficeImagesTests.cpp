// PDF → Word / PowerPoint 的图片与表格导出测试。
//
// 守的是三件事：
//  1. **图片真的进了包**：包里有 word/media/*（或 ppt/media/*）部件，
//     正文里有对应的引用，而且引用数与部件数对得上。
//     对不上是最常见的坏法：部件写了但正文没引用（图片看不见），
//     或者正文引用了但部件不存在（Office 判「文件已损坏」）。
//  2. **关系号指向真实存在的部件**：rId 与 rels 里的 Target 必须对得上。
//  3. **表格是真正的表格对象**：DOCX 里是 <w:tbl>，PPTX 里是 <a:tbl>，
//     而且行列数与 PDF 里认出来的一致。
//
// 另外守两条容易被忽略的不变量：
//  4. **颜色没反**：PDFium 给的是 BGRA，换字节序时最容易把红蓝弄反。
//  5. **双栏没串行**：左右两栏同基线的文字不能被并成一行。
//
// 这里**自己造 PDF**（不依赖外部夹具）：带图的 PDF 不好用现成夹具凑，
// 而且自己造才能把「图片的颜色」「表格的列位置」这些关键量定死。
#include "doc/export/PdfToOffice.h"
#include "doc/export/ZipWriter.h"

#include <QByteArray>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

using namespace aipdf::doc;

namespace {

// ---- 造 PDF 的小工具 ------------------------------------------------------

// 攒对象、最后统一写 xref。
class PdfBuilder {
public:
    int add(const QByteArray& body) {
        objects_.append(body);
        return objects_.size();
    }

    QByteArray serialize() const {
        QByteArray out("%PDF-1.7\n");
        QVector<int> offsets;
        for (int index = 0; index < objects_.size(); ++index) {
            offsets.append(out.size());
            out += QByteArray::number(index + 1);
            out += " 0 obj\n";
            out += objects_.at(index);
            out += "\nendobj\n";
        }
        const int xrefOffset = out.size();
        out += "xref\n0 ";
        out += QByteArray::number(objects_.size() + 1);
        out += "\n0000000000 65535 f \n";
        for (int offset : offsets) {
            out += QByteArray::number(offset).rightJustified(10, '0');
            out += " 00000 n \n";
        }
        out += "trailer\n<< /Size ";
        out += QByteArray::number(objects_.size() + 1);
        out += " /Root 1 0 R >>\nstartxref\n";
        out += QByteArray::number(xrefOffset);
        out += "\n%%EOF\n";
        return out;
    }

private:
    QVector<QByteArray> objects_;
};

// 一张 4×4 的测试图：四个象限分别是红、绿、蓝、白。
//
// 颜色是这一组测试的关键：PDFium 的位图是 BGRA，字节序换错的话
// 红蓝会互换——只看「图片导出成功了」是发现不了的。
QByteArray testImagePixels() {
    QByteArray pixels;
    // PDF 的图像数据按行从上往下；每行两个红、两个绿，再两行蓝、两行白。
    const unsigned char rows[4][12] = {
        {255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 255, 0},
        {255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 255, 0},
        {0, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255, 255},
        {0, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255, 255},
    };
    for (int row = 0; row < 4; ++row) {
        pixels.append(reinterpret_cast<const char*>(rows[row]), 12);
    }
    return pixels;
}

// 一页的版面：标题 + 双栏正文 + 两张图 + 一张表。
//
// 版面参数是定死的，理由：表格识别靠「列的 x 固定、行距固定」，
// 双栏识别靠「两栏之间有一条竖直空白带」。位置一变，认表与分栏
// 就可能不成立，测试会变成「测机器的心情」。
//
// 坐标（PDF user space，左下原点，页面 612×792）：
//   标题      y=760，18pt，x=50
//   左栏正文  x=50，12pt，8 行，y=700…560（行距 20）
//   右栏正文  x=340，12pt，8 行，同一批基线
//   图片甲    120×90，(60, 440)
//   图片乙    100×80，(340, 440)
//   表格      3 列 × 3 行，列 x=60/200/340，行 y=380/350/320
//
// title 可以换：换成**横跨右栏起点**的长标题时，标题的字会一直排到
// 栏口那一侧（这是双栏版面的常见形态：通栏大标题 + 下面两栏正文）。
// 那种标题在栏口那里是**连着的**（没有空隙），不该被当成两栏切开。
QByteArray buildSamplePdf(const QString& title = QStringLiteral("Quarterly export report")) {
    PdfBuilder builder;
    builder.add("<< /Type /Catalog /Pages 2 0 R >>");

    const QStringList columnLines = {
        QStringLiteral("Left column body text line one of eight"),
        QStringLiteral("Left column body text line two of eight"),
        QStringLiteral("Left column body text line three of eight"),
        QStringLiteral("Left column body text line four of eight"),
        QStringLiteral("Left column body text line five of eight"),
        QStringLiteral("Left column body text line six of eight"),
        QStringLiteral("Left column body text line seven of eight"),
        QStringLiteral("Left column body text line eight of eight"),
    };
    const QStringList rightLines = {
        QStringLiteral("Right column body text line one of eight"),
        QStringLiteral("Right column body text line two of eight"),
        QStringLiteral("Right column body text line three of eight"),
        QStringLiteral("Right column body text line four of eight"),
        QStringLiteral("Right column body text line five of eight"),
        QStringLiteral("Right column body text line six of eight"),
        QStringLiteral("Right column body text line seven of eight"),
        QStringLiteral("Right column body text line eight of eight"),
    };

    QString content;
    content += QStringLiteral("BT /F1 18 Tf 50 760 Td (%1) Tj ET\n").arg(title);
    for (int index = 0; index < columnLines.size(); ++index) {
        const int y = 700 - index * 20;
        content += QStringLiteral("BT /F1 12 Tf 50 %1 Td (%2) Tj ET\n")
                       .arg(y)
                       .arg(columnLines.at(index));
        content += QStringLiteral("BT /F1 12 Tf 340 %1 Td (%2) Tj ET\n")
                       .arg(y)
                       .arg(rightLines.at(index));
    }
    content += "q 120 0 0 90 60 440 cm /Im0 Do Q\n";
    content += "q 100 0 0 80 340 440 cm /Im1 Do Q\n";
    const QStringList rows[3] = {
        {QStringLiteral("Item"), QStringLiteral("Qty"), QStringLiteral("Price")},
        {QStringLiteral("Apple"), QStringLiteral("12"), QStringLiteral("3.50")},
        {QStringLiteral("Pear"), QStringLiteral("7"), QStringLiteral("5.25")},
    };
    for (int row = 0; row < 3; ++row) {
        const int y = 380 - row * 30;
        for (int column = 0; column < 3; ++column) {
            content += QStringLiteral("BT /F1 12 Tf %1 %2 Td (%3) Tj ET\n")
                           .arg(60 + column * 140)
                           .arg(y)
                           .arg(rows[row].at(column));
        }
    }

    const QByteArray pixels = testImagePixels();
    const int catalog = 1;
    const int pages = 2;
    const int page = 3;
    const int contentId = 4;
    const int imageOne = 5;
    const int imageTwo = 6;
    const int font = 7;
    Q_UNUSED(catalog);
    Q_UNUSED(pages);
    Q_UNUSED(page);
    Q_UNUSED(contentId);
    Q_UNUSED(imageOne);
    Q_UNUSED(imageTwo);
    Q_UNUSED(font);

    builder.add("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    builder.add(QStringLiteral(
                    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                    "/Resources << /Font << /F1 7 0 R >> /XObject << /Im0 5 0 R /Im1 6 0 R >> >> "
                    "/Contents 4 0 R >>")
                    .toLatin1());
    builder.add(QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
                    .arg(content.size())
                    .arg(content)
                    .toLatin1());
    for (int id = 5; id <= 6; ++id) {
        builder.add(QStringLiteral(
                        "<< /Type /XObject /Subtype /Image /Width 4 /Height 4 "
                        "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Length %1 >>\n"
                        "stream\n%2\nendstream")
                        .arg(pixels.size())
                        .arg(QString::fromLatin1(pixels))
                        .toLatin1());
    }
    builder.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    return builder.serialize();
}

// 把产物落到磁盘（给独立校验脚本用）。
//
// 单测里用本仓库自己的 zip 读回器检查自己写的包，等于自己给自己判卷。
// 真正的验收是「**别的软件**能打开它」：python-docx / python-pptx
// 打开得了吗。设了环境变量 AIPDF_OFFICE_DUMP_DIR 才落盘。
void dumpIfRequested(const QString& name, const QByteArray& bytes) {
    const QString directory = qEnvironmentVariable("AIPDF_OFFICE_DUMP_DIR");
    if (directory.isEmpty() || bytes.isEmpty()) {
        return;
    }
    QDir().mkpath(directory);
    QFile file(directory + QLatin1Char('/') + name);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(bytes);
    file.close();
}

// 统计一段 XML 里出现了多少次某个片段。
int countOf(const QByteArray& haystack, const QByteArray& needle) {
    int count = 0;
    int index = haystack.indexOf(needle);
    while (index >= 0) {
        ++count;
        index = haystack.indexOf(needle, index + needle.size());
    }
    return count;
}

// 从 rels 里取出 Id → Target 的对应关系。
QMap<QString, QString> relationshipsOf(const QByteArray& rels) {
    QMap<QString, QString> map;
    const QString text = QString::fromUtf8(rels);
    int index = 0;
    while (true) {
        const int idAt = text.indexOf(QStringLiteral("Id=\""), index);
        if (idAt < 0) {
            break;
        }
        const int idEnd = text.indexOf(QLatin1Char('"'), idAt + 4);
        const int targetAt = text.indexOf(QStringLiteral("Target=\""), idEnd);
        if (targetAt < 0) {
            break;
        }
        const int targetEnd = text.indexOf(QLatin1Char('"'), targetAt + 8);
        map.insert(text.mid(idAt + 4, idEnd - idAt - 4),
                   text.mid(targetAt + 8, targetEnd - targetAt - 8));
        index = targetEnd;
    }
    return map;
}

// 取出正文里所有 r:embed="rIdN" 的引用。
QStringList embedsOf(const QByteArray& xml) {
    QStringList embeds;
    const QString text = QString::fromUtf8(xml);
    int index = 0;
    while (true) {
        const int at = text.indexOf(QStringLiteral("r:embed=\""), index);
        if (at < 0) {
            break;
        }
        const int end = text.indexOf(QLatin1Char('"'), at + 9);
        embeds.append(text.mid(at + 9, end - at - 9));
        index = end;
    }
    return embeds;
}

// 写一个文件（落盘）。
bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const bool ok = file.write(bytes) == bytes.size();
    file.close();
    return ok;
}

// 语料批量导出：把语料目录里的每份 PDF 导成 docx / pptx / xlsx，落盘并写 manifest。
//
// 为什么要有这个模式：验收脚本要用**别的实现**（python-docx / python-pptx /
// openpyxl）把产物重新读一遍。判卷权不能落在写产物的这一侧——
// 自己写、自己读不算验证。所以这里只负责「批量导出 + 落盘 + 自报计数」，
// 读回来对不对由 scripts/verify-office-images.py 说了算。
//
// 用法：OfficeImagesTests --export-corpus <语料目录> <产物目录>
int exportCorpus(const QString& corpusDir, const QString& outDir) {
    QDir corpus(corpusDir);
    const QStringList pdfs =
        corpus.entryList(QStringList() << "*.pdf", QDir::Files, QDir::Name);
    if (pdfs.isEmpty()) {
        fprintf(stderr, "语料目录里没有 PDF：%s\n", qPrintable(corpusDir));
        return 2;
    }
    if (!QDir().mkpath(outDir)) {
        fprintf(stderr, "建不了产物目录：%s\n", qPrintable(outDir));
        return 2;
    }

    QJsonArray documents;
    int failed = 0;
    for (const QString& name : pdfs) {
        QFile file(corpus.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            fprintf(stderr, "读不了语料：%s\n", qPrintable(name));
            ++failed;
            continue;
        }
        const QByteArray bytes = file.readAll();
        file.close();

        const QString stem = QFileInfo(name).completeBaseName();
        QJsonObject entry;
        entry.insert("name", name);

        const WordExportResult word = exportPdfToWord(bytes);
        const QString docxName = stem + ".docx";
        if (word.ok && writeFile(QDir(outDir).filePath(docxName), word.bytes)) {
            entry.insert("docx", QJsonObject{{"file", docxName},
                                             {"ok", true},
                                             {"pages", word.pageCount},
                                             {"images", word.imageCount},
                                             {"tables", word.tableCount}});
        } else {
            ++failed;
            entry.insert("docx", QJsonObject{{"file", docxName},
                                             {"ok", false},
                                             {"pages", 0},
                                             {"images", 0},
                                             {"tables", 0},
                                             {"error", word.error}});
        }

        const PptxExportResult pptx = exportPdfToPptx(bytes);
        const QString pptxName = stem + ".pptx";
        if (pptx.ok && writeFile(QDir(outDir).filePath(pptxName), pptx.bytes)) {
            entry.insert("pptx", QJsonObject{{"file", pptxName},
                                             {"ok", true},
                                             {"slides", pptx.slideCount},
                                             {"images", pptx.imageCount},
                                             {"tables", pptx.tableCount}});
        } else {
            ++failed;
            entry.insert("pptx", QJsonObject{{"file", pptxName},
                                             {"ok", false},
                                             {"slides", 0},
                                             {"images", 0},
                                             {"tables", 0},
                                             {"error", pptx.error}});
        }

        const ExcelExportResult excel = exportPdfToExcel(bytes);
        const QString xlsxName = stem + ".xlsx";
        if (excel.ok && writeFile(QDir(outDir).filePath(xlsxName), excel.bytes)) {
            entry.insert("xlsx", QJsonObject{{"file", xlsxName},
                                             {"ok", true},
                                             {"pages", excel.pageCount},
                                             {"sheets", excel.sheetCount},
                                             {"tables", excel.tableCount}});
        } else {
            ++failed;
            entry.insert("xlsx", QJsonObject{{"file", xlsxName},
                                             {"ok", false},
                                             {"pages", 0},
                                             {"sheets", 0},
                                             {"tables", 0},
                                             {"error", excel.error}});
        }

        documents.append(entry);
    }

    QJsonObject root;
    root.insert("generator", QStringLiteral("tests/OfficeImagesTests.cpp --export-corpus"));
    root.insert("count", documents.size());
    root.insert("documents", documents);
    QFile manifest(QDir(outDir).filePath("manifest.json"));
    if (!manifest.open(QIODevice::WriteOnly)) {
        fprintf(stderr, "写不了 manifest：%s\n", qPrintable(manifest.fileName()));
        return 2;
    }
    manifest.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    manifest.close();

    fprintf(stderr, "语料导出：%d 份，失败 %d 份 → %s\n", documents.size(), failed,
            qPrintable(outDir));
    return failed == 0 ? 0 : 1;
}

}  // namespace

class OfficeImagesTests : public QObject {
    Q_OBJECT

private slots:
    void wordExportPutsEveryImageIntoThePackage();
    void pptxExportPutsEveryImageIntoThePackage();
    void wordRelationshipsPointToRealParts();
    void pptxRelationshipsPointToRealParts();
    void exportedImageKeepsItsColors();
    void wordExportWritesRealTables();
    void pptxExportWritesRealTables();
    void twoColumnTextIsNotMergedIntoOneLine();
    void fullWidthHeadingIsNotCutByTheColumnSeparator();
    void exportIsReproducible();
    void brokenPdfIsRejectedHonestly();
    void corpusExportsMatchTheDeclaredExpectations();
};

void OfficeImagesTests::wordExportPutsEveryImageIntoThePackage() {
    const WordExportResult result = exportPdfToWord(buildSamplePdf());
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.pageCount, 1);
    QCOMPARE(result.imageCount, 2);
    QCOMPARE(result.tableCount, 1);
    dumpIfRequested(QStringLiteral("images.docx"), result.bytes);

    const QStringList names = zip::listEntryNames(result.bytes);
    QVERIFY(names.contains(QStringLiteral("word/media/image1.png")));
    QVERIFY(names.contains(QStringLiteral("word/media/image2.png")));
    // 关系文件以前根本没有：没有它，正文里的 r:embed 无处可指。
    QVERIFY(names.contains(QStringLiteral("word/_rels/document.xml.rels")));

    const QByteArray document = zip::readEntry(result.bytes, QStringLiteral("word/document.xml"));
    QVERIFY(!document.isEmpty());
    QCOMPARE(countOf(document, QByteArray("<w:drawing>")), 2);
    QCOMPARE(countOf(document, QByteArray("<a:blip ")), 2);

    // 内容类型：少了 png 的 Default，Office 会判「文件已损坏」。
    const QByteArray types = zip::readEntry(result.bytes, QStringLiteral("[Content_Types].xml"));
    QVERIFY2(types.contains("Extension=\"png\""), types.constData());
    QVERIFY2(types.contains("image/png"), types.constData());
}

void OfficeImagesTests::pptxExportPutsEveryImageIntoThePackage() {
    const PptxExportResult result = exportPdfToPptx(buildSamplePdf());
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.imageCount, 2);
    QCOMPARE(result.tableCount, 1);
    QVERIFY(result.slideCount >= 1);
    dumpIfRequested(QStringLiteral("images.pptx"), result.bytes);

    const QStringList names = zip::listEntryNames(result.bytes);
    QVERIFY(names.contains(QStringLiteral("ppt/media/image1.png")));
    QVERIFY(names.contains(QStringLiteral("ppt/media/image2.png")));
    QVERIFY(names.contains(QStringLiteral("ppt/slides/_rels/slide1.xml.rels")));

    // 图片可能落在任意一张幻灯片上（一页文字多时会拆成「第 N 页（续）」），
    // 所以要**所有幻灯片加起来**数。
    int pictures = 0;
    for (int index = 1; index <= result.slideCount; ++index) {
        const QByteArray slide =
            zip::readEntry(result.bytes, QStringLiteral("ppt/slides/slide%1.xml").arg(index));
        QVERIFY(!slide.isEmpty());
        pictures += countOf(slide, QByteArray("<p:pic>"));
        QCOMPARE(countOf(slide, QByteArray("<a:blip ")),
                 countOf(slide, QByteArray("<p:pic>")));
    }
    QCOMPARE(pictures, 2);

    const QByteArray types = zip::readEntry(result.bytes, QStringLiteral("[Content_Types].xml"));
    QVERIFY2(types.contains("Extension=\"png\""), types.constData());
}

void OfficeImagesTests::wordRelationshipsPointToRealParts() {
    const WordExportResult result = exportPdfToWord(buildSamplePdf());
    QVERIFY(result.ok);
    const QByteArray rels =
        zip::readEntry(result.bytes, QStringLiteral("word/_rels/document.xml.rels"));
    QVERIFY(!rels.isEmpty());
    const QMap<QString, QString> map = relationshipsOf(rels);
    QCOMPARE(map.size(), 2);

    const QByteArray document = zip::readEntry(result.bytes, QStringLiteral("word/document.xml"));
    const QStringList embeds = embedsOf(document);
    QCOMPARE(embeds.size(), map.size());

    for (const QString& embed : embeds) {
        QVERIFY2(map.contains(embed), qPrintable(embed));
        // Target 是相对于 word/ 的，落到包里的路径是 "word/" + Target。
        const QString partName = QStringLiteral("word/") + map.value(embed);
        QVERIFY2(zip::listEntryNames(result.bytes).contains(partName), qPrintable(partName));
    }
}

void OfficeImagesTests::pptxRelationshipsPointToRealParts() {
    const PptxExportResult result = exportPdfToPptx(buildSamplePdf());
    QVERIFY(result.ok);
    int embeds = 0;
    for (int index = 1; index <= result.slideCount; ++index) {
        const QByteArray rels = zip::readEntry(
            result.bytes, QStringLiteral("ppt/slides/_rels/slide%1.xml.rels").arg(index));
        QVERIFY(!rels.isEmpty());
        const QMap<QString, QString> map = relationshipsOf(rels);
        const QByteArray slide =
            zip::readEntry(result.bytes, QStringLiteral("ppt/slides/slide%1.xml").arg(index));
        const QStringList used = embedsOf(slide);
        for (const QString& embed : used) {
            QVERIFY2(map.contains(embed), qPrintable(embed));
            // Target 是相对于 ppt/slides/ 的（"../media/image1.png"）。
            const QString target = map.value(embed);
            QVERIFY2(target.startsWith(QStringLiteral("../media/")), qPrintable(target));
            const QString partName = QStringLiteral("ppt/") + target.section(QLatin1Char('/'), 1);
            QVERIFY2(zip::listEntryNames(result.bytes).contains(partName), qPrintable(partName));
            // 图片的关系号不能撞版式与备注占的号。
            QVERIFY2(embed != QStringLiteral("rId1"), qPrintable(embed));
        }
        embeds += used.size();
    }
    QCOMPARE(embeds, 2);
}

void OfficeImagesTests::exportedImageKeepsItsColors() {
    const WordExportResult result = exportPdfToWord(buildSamplePdf());
    QVERIFY(result.ok);
    const QByteArray png = zip::readEntry(result.bytes, QStringLiteral("word/media/image1.png"));
    QVERIFY(!png.isEmpty());
    const QImage image = QImage::fromData(png, "PNG");
    QVERIFY2(!image.isNull(), "导出的图片解不开");
    // 至少要有原生分辨率（4×4），又不能超过 150 DPI 的上限
    // （落位 120×90 点 → 150 DPI 是 250×187）。
    QVERIFY2(image.width() >= 4 && image.height() >= 4, "分辨率比原生还小");
    QVERIFY2(image.width() <= 250 && image.height() <= 187, "分辨率超过了 DPI 上限");
    // 取四个象限的中心：不管导出时选的是原生位图还是渲染位图，
    // 象限的归属都一样。
    const int quarterWidth = image.width() / 4;
    const int quarterHeight = image.height() / 4;
    // PDFium 的位图是 BGRA：换错了字节序，红蓝就会互换。
    QCOMPARE(image.pixelColor(quarterWidth / 2, quarterHeight / 2), QColor(255, 0, 0));
    QCOMPARE(image.pixelColor(image.width() - quarterWidth / 2, quarterHeight / 2),
             QColor(0, 255, 0));
    QCOMPARE(image.pixelColor(quarterWidth / 2, image.height() - quarterHeight / 2),
             QColor(0, 0, 255));
    QCOMPARE(image.pixelColor(image.width() - quarterWidth / 2, image.height() - quarterHeight / 2),
             QColor(255, 255, 255));
}

void OfficeImagesTests::wordExportWritesRealTables() {
    const WordExportResult result = exportPdfToWord(buildSamplePdf());
    QVERIFY(result.ok);
    const QByteArray document = zip::readEntry(result.bytes, QStringLiteral("word/document.xml"));
    QCOMPARE(countOf(document, QByteArray("<w:tbl>")), 1);
    // 3 列 × 3 行。
    QCOMPARE(countOf(document, QByteArray("<w:gridCol ")), 3);
    QCOMPARE(countOf(document, QByteArray("<w:tr>")), 3);
    QCOMPARE(countOf(document, QByteArray("<w:tc>")), 9);
    QVERIFY2(document.contains("Apple"), document.constData());
    QVERIFY2(document.contains("5.25"), document.constData());
}

void OfficeImagesTests::pptxExportWritesRealTables() {
    const PptxExportResult result = exportPdfToPptx(buildSamplePdf());
    QVERIFY(result.ok);
    int tables = 0;
    for (int index = 1; index <= result.slideCount; ++index) {
        const QByteArray slide =
            zip::readEntry(result.bytes, QStringLiteral("ppt/slides/slide%1.xml").arg(index));
        tables += countOf(slide, QByteArray("<a:tbl>"));
        if (countOf(slide, QByteArray("<a:tbl>")) > 0) {
            // 3 列 × 3 行。
            QCOMPARE(countOf(slide, QByteArray("<a:gridCol ")), 3);
            QCOMPARE(countOf(slide, QByteArray("<a:tr ")), 3);
            QCOMPARE(countOf(slide, QByteArray("<a:tc>")), 9);
            QVERIFY2(slide.contains("Apple"), slide.constData());
            QVERIFY2(slide.contains("5.25"), slide.constData());
        }
    }
    QCOMPARE(tables, 1);
}

void OfficeImagesTests::twoColumnTextIsNotMergedIntoOneLine() {
    const WordExportResult result = exportPdfToWord(buildSamplePdf());
    QVERIFY(result.ok);
    const QByteArray document = zip::readEntry(result.bytes, QStringLiteral("word/document.xml"));
    // 左栏与右栏的同一行**不能**被并成一段。
    // 归行时词距会补一个空格，所以串起来会长这样（带空格）。
    QVERIFY2(!document.contains("Left column body text line one of eight Right column body text"),
              "左右两栏被串成一行了");
    QVERIFY2(!document.contains("Left column body text line one of eightRight column body text"),
              "左右两栏被串成一行了");
    QVERIFY2(document.contains("Left column body text line one of eight"), document.constData());
    QVERIFY2(document.contains("Right column body text line one of eight"), document.constData());
    // 两栏的行数都要在（各 8 行）。
    QCOMPARE(countOf(document, QByteArray("Left column body text line")), 8);
    QCOMPARE(countOf(document, QByteArray("Right column body text line")), 8);
}

// 通栏标题横跨栏口时，标题不能被切成两段。
//
// 为什么这条要单独守：双栏页面的大标题常常是通栏的（横跨两栏），
// 它的字会一直排到右栏起点那一侧。栏口在那里**没有空隙**
// （标题是连着排下来的），所以它不是「两栏的分界」，只是「一行字」。
// 按「字符中心越过栏口就断开」来切，标题会被切成两半——
// Word 里看到的就是「Quarterly export report for every region」
// 变成两段，第一段在左栏、第二段在右栏。
void OfficeImagesTests::fullWidthHeadingIsNotCutByTheColumnSeparator() {
    // 标题要**长到跨过栏口之后还剩下一大段**：只有右边那一段也够宽，
    // 「被切成两段」才会真的发生（右边只剩一两个字时，切了也不算错）。
    const QString title = QStringLiteral("Quarterly export report for every region of the company");
    const WordExportResult result = exportPdfToWord(buildSamplePdf(title));
    QVERIFY2(result.ok, qPrintable(result.error));
    const QByteArray document = zip::readEntry(result.bytes, QStringLiteral("word/document.xml"));
    QVERIFY(!document.isEmpty());

    // 标题必须**完整地**出现在某一段里。
    QVERIFY2(document.contains(title.toUtf8()), document.constData());
    // 而且只能出现一次（被切成两段的话，两半拼不到一起，各自都不完整）。
    QCOMPARE(countOf(document, title.toUtf8()), 1);

    // 左右两栏照旧要分开。
    QVERIFY2(document.contains("Left column body text line one of eight"), document.constData());
    QVERIFY2(document.contains("Right column body text line one of eight"), document.constData());
    QVERIFY2(!document.contains("Left column body text line one of eight Right column body text"),
              "左右两栏被串成一行了");
}

void OfficeImagesTests::exportIsReproducible() {
    const QByteArray pdf = buildSamplePdf();
    const WordExportResult first = exportPdfToWord(pdf);
    const WordExportResult second = exportPdfToWord(pdf);
    QVERIFY(first.ok);
    QVERIFY(second.ok);
    // 同样的输入必须得到同样的字节：PNG 编码要确定，ZIP 的时间戳也要固定。
    QCOMPARE(first.bytes, second.bytes);
}

void OfficeImagesTests::brokenPdfIsRejectedHonestly() {
    const QByteArray garbage("this is not a pdf at all");
    const WordExportResult word = exportPdfToWord(garbage);
    QVERIFY(!word.ok);
    QVERIFY(word.bytes.isEmpty());
    QCOMPARE(word.imageCount, 0);
    QCOMPARE(word.tableCount, 0);
}

// 语料导出的计数要与语料生成时写死的期望值一致。
//
// 为什么这条要在 C++ 侧再守一遍：语料生成器把「该有几页、几张图、几个表」
// 写进了 corpus.meta.json，那是**标准答案**。导出端数出来的必须一样，
// 不一样就说明导出丢了东西（或者多认了）。
//
// 没设 AIPDF_OFFICE_CORPUS_DIR 时跳过：语料是重夹具，
// 不该让「跑一遍单测」也拖上它。
void OfficeImagesTests::corpusExportsMatchTheDeclaredExpectations() {
    const QString corpusDir = qEnvironmentVariable("AIPDF_OFFICE_CORPUS_DIR");
    if (corpusDir.isEmpty()) {
        QSKIP("没设 AIPDF_OFFICE_CORPUS_DIR：这一轮没有语料可比对");
    }
    QFile metaFile(QDir(corpusDir).filePath("corpus.meta.json"));
    QVERIFY2(metaFile.open(QIODevice::ReadOnly),
             qPrintable(QDir(corpusDir).filePath("corpus.meta.json")));
    const QJsonObject meta = QJsonDocument::fromJson(metaFile.readAll()).object();
    metaFile.close();
    const QJsonArray documents = meta.value("documents").toArray();
    QVERIFY2(!documents.isEmpty(), "语料的 meta 里没有文档");

    for (const QJsonValue& value : documents) {
        const QJsonObject document = value.toObject();
        const QString name = document.value("name").toString();
        const int expectedPages = document.value("pages").toInt();
        const int expectedImages = document.value("images").toInt();
        const int expectedTables = document.value("tables").toInt();

        QFile pdf(QDir(corpusDir).filePath(name));
        QVERIFY2(pdf.open(QIODevice::ReadOnly), qPrintable(name));
        const QByteArray bytes = pdf.readAll();
        pdf.close();

        const WordExportResult word = exportPdfToWord(bytes);
        QVERIFY2(word.ok, qPrintable(name + QStringLiteral(": ") + word.error));
        QCOMPARE(word.pageCount, expectedPages);
        QCOMPARE(word.imageCount, expectedImages);
        QCOMPARE(word.tableCount, expectedTables);

        const PptxExportResult pptx = exportPdfToPptx(bytes);
        QVERIFY2(pptx.ok, qPrintable(name + QStringLiteral(": ") + pptx.error));
        QCOMPARE(pptx.imageCount, expectedImages);
        QCOMPARE(pptx.tableCount, expectedTables);
        QVERIFY2(pptx.slideCount >= expectedPages, qPrintable(name));

        const ExcelExportResult excel = exportPdfToExcel(bytes);
        QVERIFY2(excel.ok, qPrintable(name + QStringLiteral(": ") + excel.error));
        QCOMPARE(excel.tableCount, expectedTables);
    }
}

int main(int argc, char** argv) {
    // 语料批量导出模式：OfficeImagesTests --export-corpus <语料目录> <产物目录>
    //
    // 为什么用同一个二进制：导出这一侧只需要「读 PDF → 写包」，
    // 单测已经在编这些代码了；另开一个可执行文件就得再登记一遍源文件。
    if (argc >= 4 && QByteArray(argv[1]) == "--export-corpus") {
        return exportCorpus(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    OfficeImagesTests test;
    return QTest::qExec(&test, argc, argv);
}

#include "OfficeImagesTests.moc"
