// wave21：字体转曲「真正的矢量轮廓化」的专项用例。
//
// 这里测的是**轮廓化**，不是栅格化：文字变成路径对象（m / l / c + f），
// 产物里没有字体、没有可提取文字，但页面看起来还是原来那一页。
//
// 夹具是自己造的：内嵌 TrueType 字体的 PDF。
// 为什么不用仓库里的现成夹具：这份夹具必须钉死三件事——
// 一页、尺寸固定、字体程序真的嵌在文件里；自己造才能把这三件事都控制住。

#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>

#include <cmath>
#include <optional>
#include <vector>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

#include "doc/convert/FontFlattening.h"
#include "pdf/content/ContentStream.h"
#include "pdf/document/QpdfBackend.h"
#include "pdf/kernel/PdfiumAdapter.h"

using aipdf::doc::FlattenMode;
using aipdf::doc::FlattenOptions;
using aipdf::doc::FlattenReport;

namespace {

// ---------------------------------------------------------------------------
// 夹具：一份内嵌 TrueType 字体的 PDF
// ---------------------------------------------------------------------------

// 一份 TrueType 字体里我们要的三样东西。
struct TrueTypeInfo {
    int unitsPerEm = 0;
    int glyphCount = 0;
    // Unicode → 字形号。
    QHash<uint, int> unicodeToGid;
    // 字形号 → 前进宽度（字体单位）。
    QHash<int, int> gidToAdvance;

    int advanceForUnicode(uint unicode) const {
        const int gid = unicodeToGid.value(static_cast<int>(unicode), 0);
        return gidToAdvance.value(gid, 0);
    }
};

quint16 readU16(const QByteArray& data, int offset) {
    if (offset + 2 > data.size()) {
        return 0;
    }
    return static_cast<quint16>((static_cast<quint8>(data[offset]) << 8) |
                                 static_cast<quint8>(data[offset + 1]));
}

quint32 readU32(const QByteArray& data, int offset) {
    if (offset + 4 > data.size()) {
        return 0;
    }
    return (static_cast<quint32>(static_cast<quint8>(data[offset])) << 24) |
           (static_cast<quint32>(static_cast<quint8>(data[offset + 1])) << 16) |
           (static_cast<quint32>(static_cast<quint8>(data[offset + 2])) << 8) |
           static_cast<quint32>(static_cast<quint8>(data[offset + 3]));
}

// 解析 TrueType：只取 unitsPerEm、字形数、cmap（Unicode → 字形号）、hmtx（前进宽度）。
//
// 为什么自己解析：夹具里的 /Widths 必须与字体的真实前进宽度一致，
// 否则 PDFium 排出来的字符位置是错的，轮廓也会跟着错位。
bool parseTrueType(const QByteArray& data, TrueTypeInfo* info, QString* error) {
    if (data.size() < 12) {
        if (error) *error = QStringLiteral("字体文件太短");
        return false;
    }
    const int tableCount = readU16(data, 4);
    QHash<QByteArray, int> tableOffsets;
    for (int i = 0; i < tableCount; ++i) {
        const int entry = 12 + i * 16;
        if (entry + 16 > data.size()) {
            break;
        }
        tableOffsets.insert(data.mid(entry, 4), static_cast<int>(readU32(data, entry + 8)));
    }

    const int headOffset = tableOffsets.value("head", -1);
    const int maxpOffset = tableOffsets.value("maxp", -1);
    const int hheaOffset = tableOffsets.value("hhea", -1);
    const int hmtxOffset = tableOffsets.value("hmtx", -1);
    const int cmapOffset = tableOffsets.value("cmap", -1);
    if (headOffset < 0 || maxpOffset < 0 || hheaOffset < 0 || hmtxOffset < 0 || cmapOffset < 0) {
        if (error) *error = QStringLiteral("字体缺少必需的表");
        return false;
    }

    info->unitsPerEm = readU16(data, headOffset + 18);
    info->glyphCount = readU16(data, maxpOffset + 4);
    if (info->unitsPerEm <= 0) {
        if (error) *error = QStringLiteral("字体的 unitsPerEm 非法");
        return false;
    }

    const int metricCount = readU16(data, hheaOffset + 34);
    int lastAdvance = 0;
    for (int gid = 0; gid < info->glyphCount; ++gid) {
        int advance = lastAdvance;
        if (gid < metricCount) {
            advance = readU16(data, hmtxOffset + gid * 4);
        }
        lastAdvance = advance;
        info->gidToAdvance.insert(gid, advance);
    }

    // cmap：找一个 Unicode 子表（平台 3 编码 1，或平台 0）。
    const int subtableCount = readU16(data, cmapOffset + 2);
    int subtable = -1;
    for (int i = 0; i < subtableCount; ++i) {
        const int record = cmapOffset + 4 + i * 8;
        const int platform = readU16(data, record);
        const int encoding = readU16(data, record + 2);
        if ((platform == 3 && (encoding == 1 || encoding == 10)) || platform == 0) {
            subtable = cmapOffset + static_cast<int>(readU32(data, record + 4));
            break;
        }
    }
    if (subtable < 0 || readU16(data, subtable) != 4) {
        if (error) *error = QStringLiteral("字体的 cmap 不是可用的 format 4");
        return false;
    }

    const int segCount = readU16(data, subtable + 6) / 2;
    const int endsOffset = subtable + 14;
    const int startsOffset = endsOffset + segCount * 2;
    const int deltasOffset = startsOffset + segCount * 2;
    const int rangesOffset = deltasOffset + segCount * 2;
    for (int seg = 0; seg < segCount; ++seg) {
        const int end = readU16(data, endsOffset + seg * 2);
        const int start = readU16(data, startsOffset + seg * 2);
        const int delta = static_cast<qint16>(readU16(data, deltasOffset + seg * 2));
        const int rangeOffset = readU16(data, rangesOffset + seg * 2);
        if (start > end) {
            continue;
        }
        for (uint code = static_cast<uint>(start); code <= static_cast<uint>(end); ++code) {
            if (code > 0xFFFF) {
                break;
            }
            int gid = 0;
            if (rangeOffset == 0) {
                gid = (static_cast<int>(code) + delta) & 0xFFFF;
            } else {
                const int index = rangesOffset + seg * 2 + rangeOffset +
                                  (static_cast<int>(code) - start) * 2;
                gid = readU16(data, index);
                if (gid != 0) {
                    gid = (gid + delta) & 0xFFFF;
                }
            }
            if (gid != 0) {
                info->unicodeToGid.insert(code, gid);
            }
        }
    }
    return true;
}

// 找一个能用的系统 TrueType 字体。
//
// 为什么用系统字体：夹具必须带**真的内嵌字体程序**，
// 而仓库里没有现成的 .ttf 夹具文件；Windows 自带的字体是本仓库既有的做法
// （TrueTypeSubsetTests、VerticalWritingTests 都这么做）。
QString findSystemFontPath() {
    const QStringList candidates{
        QStringLiteral("C:/Windows/Fonts/arial.ttf"),
        QStringLiteral("C:/Windows/Fonts/segoeui.ttf"),
        QStringLiteral("C:/Windows/Fonts/calibri.ttf"),
        QStringLiteral("C:/Windows/Fonts/tahoma.ttf"),
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return QString();
}

// 把对象拼成一份带正确 xref 的 PDF。
QByteArray assemblePdf(const QVector<QPair<int, QByteArray>>& objects, int rootId) {
    QByteArray out("%PDF-1.7\n%\xe2\xe3\xcf\xd3\n");
    QHash<int, int> offsets;
    for (const auto& entry : objects) {
        offsets.insert(entry.first, out.size());
        out += QByteArray::number(entry.first) + " 0 obj\n";
        out += entry.second;
        out += "\nendobj\n";
    }
    const int xrefOffset = out.size();
    const int count = offsets.size() + 1;
    out += "xref\n0 " + QByteArray::number(count) + "\n";
    out += "0000000000 65535 f \n";
    for (int id = 1; id < count; ++id) {
        if (offsets.contains(id)) {
            out += QByteArray::number(offsets.value(id)).rightJustified(10, '0') + " 00000 n \n";
        } else {
            out += "0000000000 65535 f \n";
        }
    }
    out += "trailer\n<< /Size " + QByteArray::number(count) + " /Root " + QByteArray::number(rootId) +
           " 0 R >>\n";
    out += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return out;
}

QByteArray streamObject(const QByteArray& data) {
    return "<< /Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data + "\nendstream";
}

// 造一份内嵌 TrueType 字体的 PDF（简单字体 + WinAnsiEncoding）。
QByteArray makeEmbeddedFontPdf(const QByteArray& fontBytes, const TrueTypeInfo& info,
                                const QString& text, double fontSize = 24.0) {
    QByteArray widths;
    for (int code = 32; code <= 126; ++code) {
        const int advance = info.advanceForUnicode(static_cast<uint>(code));
        const int scaled = static_cast<int>(std::lround(advance * 1000.0 / info.unitsPerEm));
        widths += QByteArray::number(scaled) + " ";
    }
    widths.chop(1);

    const QByteArray content = "BT /F1 " + QByteArray::number(fontSize) + " Tf 20 100 Td (" +
                               text.toLatin1() + ") Tj ET";

    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")});
    objects.append({2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")});
    objects.append({3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200]"
                                  " /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>")});
    objects.append({4, streamObject(content)});
    objects.append({5, QByteArray("<< /Type /Font /Subtype /TrueType /BaseFont /ArialMT"
                                  " /FirstChar 32 /LastChar 126 /Widths [") + widths +
                                  QByteArray("] /Encoding /WinAnsiEncoding /FontDescriptor 6 0 R >>")});
    objects.append({6, QByteArray("<< /Type /FontDescriptor /FontName /ArialMT /Flags 32"
                                  " /FontBBox [-1000 -300 2000 900] /ItalicAngle 0 /Ascent 900"
                                  " /Descent -300 /CapHeight 700 /StemV 80 /FontFile2 7 0 R >>")});
    objects.append({7, streamObject(fontBytes)});
    return assemblePdf(objects, 1);
}

// 造一份 Type0 / Identity-H 的 PDF（中文 PDF 的常见形态）。
//
// 为什么要有这份夹具：CID 字体的字符码是 2 字节，Unicode 与字形号的对应
// 靠 /ToUnicode 来给。转曲要按 Unicode 找字形，所以 ToUnicode 在不在、
// 对不对，直接决定能不能转曲——这条边界必须被测到。
//
// withToUnicode 为假时故意不写 /ToUnicode：用来验证「转不了时如实报出来」。
QByteArray makeCidFontPdf(const QByteArray& fontBytes, const TrueTypeInfo& info,
                          const QString& text, bool withToUnicode) {
    QByteArray codes;
    QByteArray widths;
    QByteArray toUnicodeEntries;
    for (int i = 0; i < text.size(); ++i) {
        const uint unicode = text.at(i).unicode();
        const int gid = info.unicodeToGid.value(static_cast<int>(unicode), 0);
        codes += QByteArray::number(gid, 16).rightJustified(4, '0').toUpper() + " ";
        const int advance = info.advanceForUnicode(unicode);
        widths += QByteArray::number(gid) + " [" +
                  QByteArray::number(static_cast<int>(std::lround(advance * 1000.0 / info.unitsPerEm))) + "] ";
        toUnicodeEntries += "<" + QByteArray::number(gid, 16).rightJustified(4, '0').toUpper() + "> <" +
                            QByteArray::number(unicode, 16).rightJustified(4, '0').toUpper() + ">\n";
    }
    codes.chop(1);
    widths.chop(1);

    const QByteArray content = "BT /F1 24 Tf 20 100 Td <" + codes + "> Tj ET";
    // beginbfchar 前面必须写条目数：CMap 写坏了 PDFium 会整段忽略，
    // 于是字符的 Unicode 退化成原始字符码，轮廓就会取到 .notdef 方框。
    QByteArray toUnicode = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
                           "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
                           "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n" +
                           QByteArray::number(toUnicodeEntries.count('\n')) + " beginbfchar\n" +
                           toUnicodeEntries + "endbfchar\nendcmap\n"
                           "CMapName currentdict /CMap defineresource pop\nend\nend\n";

    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")});
    objects.append({2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")});
    objects.append({3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200]"
                                  " /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>")});
    objects.append({4, streamObject(content)});
    objects.append({5, QByteArray("<< /Type /Font /Subtype /Type0 /BaseFont /ArialMT"
                                  " /Encoding /Identity-H /DescendantFonts [8 0 R]") +
                        (withToUnicode ? QByteArray(" /ToUnicode 9 0 R") : QByteArray()) + QByteArray(" >>")});
    objects.append({6, QByteArray("<< /Type /FontDescriptor /FontName /ArialMT /Flags 32"
                                  " /FontBBox [-1000 -300 2000 900] /ItalicAngle 0 /Ascent 900"
                                  " /Descent -300 /CapHeight 700 /StemV 80 /FontFile2 7 0 R >>")});
    objects.append({7, streamObject(fontBytes)});
    objects.append({8, QByteArray("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /ArialMT"
                                  " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>"
                                  " /FontDescriptor 6 0 R /DW 1000 /W [") + widths +
                                QByteArray("] /CIDToGIDMap /Identity >>")});
    if (withToUnicode) {
        objects.append({9, streamObject(toUnicode)});
    }
    return assemblePdf(objects, 1);
}

// 造一份 Type3 字体的 PDF：字形是内容流，不是轮廓。
//
// 为什么要有这份夹具：Type3 字体的字形**没有轮廓数据**，
// 轮廓化一定转不了它——这条边界必须被测到，也必须被如实报出来。
QByteArray makeType3FontPdf(const QString& text) {
    const QByteArray glyphProc = "10 10 80 80 re f\n";
    const QByteArray content = "BT /F1 40 Tf 20 100 Td (" + text.toLatin1() + ") Tj ET";

    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")});
    objects.append({2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")});
    objects.append({3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200]"
                                  " /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>")});
    objects.append({4, streamObject(content)});
    objects.append({5, QByteArray("<< /Type /Font /Subtype /Type3 /FontBBox [0 0 100 100]"
                                  " /FontMatrix [0.01 0 0 0.01 0 0] /CharProcs << /square 6 0 R >>"
                                  " /Encoding << /Differences [65 /square] >> /FirstChar 65 /LastChar 65"
                                  " /Widths [100] /Resources << >> >>")});
    objects.append({6, streamObject(glyphProc)});
    return assemblePdf(objects, 1);
}

// ---------------------------------------------------------------------------
// 读产物
// ---------------------------------------------------------------------------

// 取出产物第一页的内容流（已解压）。
QByteArray readFirstPageContent(const QByteArray& pdfBytes) {
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("wave21.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        if (pages.empty()) {
            return QByteArray();
        }
        QPDFObjectHandle page = pages.front();
        page.coalesceContentStreams();
        QByteArray content;
        for (QPDFObjectHandle stream : page.getPageContents()) {
            if (!stream.isStream()) {
                continue;
            }
            const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
            if (!data) {
                continue;
            }
            content += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                  static_cast<qsizetype>(data->getSize()));
        }
        return content;
    } catch (const std::exception&) {
        return QByteArray();
    }
#else
    return QByteArray();
#endif
}

// 内容流里出现了哪些路径算子。
QSet<QByteArray> pathOperators(const QByteArray& content) {
    QSet<QByteArray> operators;
    const aipdf::pdf::ContentParseResult parsed = aipdf::pdf::ContentStream::tokenize(content);
    if (!parsed.isValid()) {
        return operators;
    }
    for (const aipdf::pdf::ContentToken& token : parsed.tokens) {
        if (token.type != aipdf::pdf::ContentTokenType::Operator) {
            continue;
        }
        if (token.raw == "m" || token.raw == "l" || token.raw == "c" || token.raw == "h" ||
            token.raw == "f") {
            operators.insert(token.raw);
        }
    }
    return operators;
}

// 两张图的平均绝对差（0..255）。
double meanAbsoluteDifference(const QImage& left, const QImage& right) {
    if (left.isNull() || right.isNull() || left.size() != right.size()) {
        return 255.0;
    }
    const QImage a = left.convertToFormat(QImage::Format_Grayscale8);
    const QImage b = right.convertToFormat(QImage::Format_Grayscale8);
    double total = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* rowA = a.constScanLine(y);
        const uchar* rowB = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x) {
            total += std::abs(static_cast<int>(rowA[x]) - static_cast<int>(rowB[x]));
        }
    }
    return total / static_cast<double>(a.width() * a.height());
}

// 非白像素占比（0..1）：墨量。
double inkRatio(const QImage& image) {
    if (image.isNull()) {
        return 0.0;
    }
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    qint64 inked = 0;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            if (row[x] < 250) {
                ++inked;
            }
        }
    }
    return static_cast<double>(inked) / static_cast<double>(gray.width() * gray.height());
}

// 渲染一页。
QImage renderPage(const QByteArray& pdfBytes, int pageIndex, double zoom) {
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(pdfBytes);
    if (!opened.document.isValid()) {
        return QImage();
    }
    const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(pageIndex);
    if (!page.has_value()) {
        return QImage();
    }
    return page->render(zoom);
}

}  // namespace

class Wave21FlattenTests : public QObject {
    Q_OBJECT

private slots:
    void defaultModeIsOutline();
    void outlineKeepsPageCountAndSize();
    void outlineRemovesExtractableText();
    void outlineProducesPathOperators();
    void outlineRemovesEmbeddedFont();
    void outlinePassesQpdfStructureCheck();
    void outlineKeepsVisualAppearance();
    void outlineReportCountsConvertedChars();
    void type3FontIsReportedAsUnconvertible();
    void cidFontWithToUnicodeIsOutlined();
    void cidFontWithoutToUnicodeIsReported();
    void unconvertibleTextCanBeKeptOnRequest();
    void rasterModeStillProducesAnImage();
    void invalidInputFailsWithChineseReason();
};

void Wave21FlattenTests::defaultModeIsOutline() {
    // 默认必须是轮廓化：默认给栅格化就等于「产品缺口没补上」。
    FlattenOptions options;
    QCOMPARE(options.mode, FlattenMode::Outline);
    QCOMPARE(options.keepTextWhenUnconvertible, false);
}

void Wave21FlattenTests::outlineKeepsPageCountAndSize() {
    const QString fontPath = findSystemFontPath();
    if (fontPath.isEmpty()) {
        QSKIP("这台机器上找不到可用的系统 TrueType 字体，跳过内嵌字体夹具。");
    }
    QFile fontFile(fontPath);
    QVERIFY(fontFile.open(QIODevice::ReadOnly));
    const QByteArray fontBytes = fontFile.readAll();
    TrueTypeInfo info;
    QString parseError;
    QVERIFY2(parseTrueType(fontBytes, &info, &parseError), qPrintable(parseError));

    const QByteArray source = makeEmbeddedFontPdf(fontBytes, info, QStringLiteral("Hello"));
    QVERIFY(!source.isEmpty());

    FlattenReport report;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, FlattenOptions{}, &report, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    QVERIFY(output.startsWith("%PDF"));

    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(output);
    QVERIFY(opened.document.isValid());
    QCOMPARE(opened.document.pageCount(), 1);

    const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->widthPoints(), 300.0);
    QCOMPARE(page->heightPoints(), 200.0);
}

void Wave21FlattenTests::outlineRemovesExtractableText() {
    const QString fontPath = findSystemFontPath();
    if (fontPath.isEmpty()) {
        QSKIP("这台机器上找不到可用的系统 TrueType 字体，跳过内嵌字体夹具。");
    }
    QFile fontFile(fontPath);
    QVERIFY(fontFile.open(QIODevice::ReadOnly));
    const QByteArray fontBytes = fontFile.readAll();
    TrueTypeInfo info;
    QVERIFY(parseTrueType(fontBytes, &info, nullptr));

    const QByteArray source = makeEmbeddedFontPdf(fontBytes, info, QStringLiteral("Hello"));
    QVERIFY(aipdf::doc::hasExtractableText(source));

    FlattenReport report;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, FlattenOptions{}, &report, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    QVERIFY2(!aipdf::doc::hasExtractableText(output), "轮廓化之后仍然能取到文字");
}

void Wave21FlattenTests::outlineProducesPathOperators() {
    const QString fontPath = findSystemFontPath();
    if (fontPath.isEmpty()) {
        QSKIP("这台机器上找不到可用的系统 TrueType 字体，跳过内嵌字体夹具。");
    }
    QFile fontFile(fontPath);
    QVERIFY(fontFile.open(QIODevice::ReadOnly));
    const QByteArray fontBytes = fontFile.readAll();
    TrueTypeInfo info;
    QVERIFY(parseTrueType(fontBytes, &info, nullptr));

    const QByteArray source = makeEmbeddedFontPdf(fontBytes, info, QStringLiteral("Hello"));
    FlattenReport report;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, FlattenOptions{}, &report, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QByteArray content = readFirstPageContent(output);
    QVERIFY2(!content.isEmpty(), "读不到产物第一页的内容流");
    const QSet<QByteArray> operators = pathOperators(content);
    QVERIFY2(operators.contains("m"), "产物里没有 moveto（m）路径算子");
    QVERIFY2(operators.contains("l"), "产物里没有 lineto（l）路径算子");
    QVERIFY2(operators.contains("c"), "产物里没有 curveto（c）路径算子");
    QVERIFY2(operators.contains("f"), "产物里没有填充算子（f）");
    // 文字算子必须已经不在了。
    QVERIFY(!content.contains("Tj"));
    QVERIFY(!content.contains("TJ"));
}

void Wave21FlattenTests::outlineRemovesEmbeddedFont() {
    const QString fontPath = findSystemFontPath();
    if (fontPath.isEmpty()) {
        QSKIP("这台机器上找不到可用的系统 TrueType 字体，跳过内嵌字体夹具。");
    }
    QFile fontFile(fontPath);
    QVERIFY(fontFile.open(QIODevice::ReadOnly));
    const QByteArray fontBytes = fontFile.readAll();
    TrueTypeInfo info;
    QVERIFY(parseTrueType(fontBytes, &info, nullptr));

    const QByteArray source = makeEmbeddedFontPdf(fontBytes, info, QStringLiteral("Hello"));
    QVERIFY(source.contains("/FontFile2"));

    FlattenReport report;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, FlattenOptions{}, &report, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    // 字体程序不能再在产物里：产物不该依赖任何字体。
    QVERIFY2(!output.contains("/FontFile"), "产物里仍然有字体程序（FontFile/FontFile2/FontFile3）");
    QVERIFY2(!output.contains("/FontFile2"), "产物里仍然有 FontFile2");
}

void Wave21FlattenTests::outlinePassesQpdfStructureCheck() {
    const QString fontPath = findSystemFontPath();
    if (fontPath.isEmpty()) {
        QSKIP("这台机器上找不到可用的系统 TrueType 字体，跳过内嵌字体夹具。");
    }
    QFile fontFile(fontPath);
    QVERIFY(fontFile.open(QIODevice::ReadOnly));
    const QByteArray fontBytes = fontFile.readAll();
    TrueTypeInfo info;
    QVERIFY(parseTrueType(fontBytes, &info, nullptr));

    const QByteArray source = makeEmbeddedFontPdf(fontBytes, info, QStringLiteral("Hello"));
    FlattenReport report;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, FlattenOptions{}, &report, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    // 产物必须能被独立解析器（qpdf）回读，而且**一条警告都不能有**：
    // 我们自己写出的文件不该带任何「修复过」的痕迹。
    QString verifyError;
    QVERIFY2(aipdf::pdf::verifyWithQpdf(output, &verifyError),
             qPrintable(QStringLiteral("qpdf 结构校验没过：%1").arg(verifyError)));
}

void Wave21FlattenTests::outlineKeepsVisualAppearance() {
    const QString fontPath = findSystemFontPath();
    if (fontPath.isEmpty()) {
        QSKIP("这台机器上找不到可用的系统 TrueType 字体，跳过内嵌字体夹具。");
    }
    QFile fontFile(fontPath);
    QVERIFY(fontFile.open(QIODevice::ReadOnly));
    const QByteArray fontBytes = fontFile.readAll();
    TrueTypeInfo info;
    QVERIFY(parseTrueType(fontBytes, &info, nullptr));

    const QByteArray source = makeEmbeddedFontPdf(fontBytes, info, QStringLiteral("Hello"));
    FlattenReport report;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, FlattenOptions{}, &report, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QImage original = renderPage(source, 0, 2.0);
    const QImage flattened = renderPage(output, 0, 2.0);
    QVERIFY(!original.isNull());
    QVERIFY(!flattened.isNull());
    QCOMPARE(original.size(), flattened.size());

    // 阈值依据：轮廓与原文字出自同一份字体程序，只是从「字」变成「路径」，
    // 差异只应来自抗锯齿边缘。实测平均灰阶差 0.19（满量程 255），
    // 阈值取 1.5（约 8 倍实测值）。
    // 这条阈值不是摆设：如果轮廓根本没画出来（整页空白），
    // 同一张夹具的灰阶差约 1.8，正好被挡在门外；
    // 墨量那条断言再兜一层（整页空白时墨量比是 0）。
    const double difference = meanAbsoluteDifference(original, flattened);
    const double originalInk = inkRatio(original);
    const double flattenedInk = inkRatio(flattened);
    QVERIFY2(difference < 1.5,
             qPrintable(QStringLiteral("轮廓化后的页面和原页面差太多：平均灰阶差 %1").arg(difference)));

    // 墨量也要对得上：转曲不能把字变没，也不能把字糊成一团。
    QVERIFY2(originalInk > 0.005, "夹具本身几乎没有墨，测不出差别");
    QVERIFY2(std::abs(originalInk - flattenedInk) < originalInk * 0.2,
             qPrintable(QStringLiteral("墨量对不上：原页面 %1，轮廓化后 %2")
                            .arg(originalInk)
                            .arg(flattenedInk)));
}

void Wave21FlattenTests::outlineReportCountsConvertedChars() {
    const QString fontPath = findSystemFontPath();
    if (fontPath.isEmpty()) {
        QSKIP("这台机器上找不到可用的系统 TrueType 字体，跳过内嵌字体夹具。");
    }
    QFile fontFile(fontPath);
    QVERIFY(fontFile.open(QIODevice::ReadOnly));
    const QByteArray fontBytes = fontFile.readAll();
    TrueTypeInfo info;
    QVERIFY(parseTrueType(fontBytes, &info, nullptr));

    const QByteArray source = makeEmbeddedFontPdf(fontBytes, info, QStringLiteral("Hello"));
    FlattenReport report;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, FlattenOptions{}, &report, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    QVERIFY(report.ok);
    QCOMPARE(report.pageCount, 1);
    QCOMPARE(report.outlinedCharCount, 5);
    QCOMPARE(report.droppedCharCount, 0);
    QVERIFY2(report.skipped.isEmpty(), qPrintable(report.skipped.join(QStringLiteral("；"))));
}

void Wave21FlattenTests::type3FontIsReportedAsUnconvertible() {
    const QByteArray source = makeType3FontPdf(QStringLiteral("A"));
    QVERIFY(aipdf::doc::hasExtractableText(source));

    FlattenReport report;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, FlattenOptions{}, &report, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    QVERIFY(report.ok);
    // Type3 字体没有字形轮廓：转不了，而且必须**逐条报出来**，
    // 不许悄悄跳过后宣称「全部转曲」。
    QCOMPARE(report.outlinedCharCount, 0);
    QVERIFY2(report.droppedCharCount > 0, "Type3 字体的字符没有被报成转不了");
    QVERIFY2(!report.skipped.isEmpty(), "报告里没有逐条说明");
    QVERIFY2(report.skipped.join(QStringLiteral("；")).contains(QStringLiteral("Type3")),
             qPrintable(report.skipped.join(QStringLiteral("；"))));
    // 产物里同样不能有可提取文字。
    QVERIFY(!aipdf::doc::hasExtractableText(output));
}

void Wave21FlattenTests::cidFontWithToUnicodeIsOutlined() {
    const QString fontPath = findSystemFontPath();
    if (fontPath.isEmpty()) {
        QSKIP("这台机器上找不到可用的系统 TrueType 字体，跳过内嵌字体夹具。");
    }
    QFile fontFile(fontPath);
    QVERIFY(fontFile.open(QIODevice::ReadOnly));
    const QByteArray fontBytes = fontFile.readAll();
    TrueTypeInfo info;
    QVERIFY(parseTrueType(fontBytes, &info, nullptr));

    // Type0 / Identity-H：中文 PDF 的常见形态。
    const QByteArray source = makeCidFontPdf(fontBytes, info, QStringLiteral("Hello"), true);
    QVERIFY(aipdf::doc::hasExtractableText(source));

    FlattenReport report;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, FlattenOptions{}, &report, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    QVERIFY2(!aipdf::doc::hasExtractableText(output), "轮廓化之后仍然能取到文字");
    QVERIFY2(!output.contains("/FontFile"), "产物里仍然有字体程序");
    QCOMPARE(report.outlinedCharCount, 5);
    QCOMPARE(report.droppedCharCount, 0);

    const QSet<QByteArray> operators = pathOperators(readFirstPageContent(output));
    QVERIFY2(operators.contains("m"), "产物里没有 moveto（m）路径算子");
    QVERIFY2(operators.contains("c"), "产物里没有 curveto（c）路径算子");

    // 视觉也要对得上。
    const QImage original = renderPage(source, 0, 2.0);
    const QImage flattened = renderPage(output, 0, 2.0);
    QVERIFY(!original.isNull());
    QVERIFY(!flattened.isNull());
    QVERIFY2(meanAbsoluteDifference(original, flattened) < 1.5,
             "轮廓化后的页面和原页面差太多");
}

void Wave21FlattenTests::cidFontWithoutToUnicodeIsReported() {
    const QString fontPath = findSystemFontPath();
    if (fontPath.isEmpty()) {
        QSKIP("这台机器上找不到可用的系统 TrueType 字体，跳过内嵌字体夹具。");
    }
    QFile fontFile(fontPath);
    QVERIFY(fontFile.open(QIODevice::ReadOnly));
    const QByteArray fontBytes = fontFile.readAll();
    TrueTypeInfo info;
    QVERIFY(parseTrueType(fontBytes, &info, nullptr));

    // 没有 /ToUnicode：PDFium 没法把字符码换回 Unicode，
    // 于是换不回字形轮廓——这条边界必须被如实报出来，不能悄悄当成「转曲成功」。
    const QByteArray source = makeCidFontPdf(fontBytes, info, QStringLiteral("Hello"), false);

    FlattenReport report;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, FlattenOptions{}, &report, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    QCOMPARE(report.outlinedCharCount, 0);
    QVERIFY2(report.droppedCharCount > 0, "没有 ToUnicode 的 CID 字体没有被报成转不了");
    QVERIFY2(!report.skipped.isEmpty(), "报告里没有逐条说明");
}

void Wave21FlattenTests::unconvertibleTextCanBeKeptOnRequest() {
    const QByteArray source = makeType3FontPdf(QStringLiteral("A"));

    FlattenOptions options;
    options.keepTextWhenUnconvertible = true;
    FlattenReport report;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, options, &report, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    // 走了回退策略：这一页保留原文字与字体，而且报告里写明了走的是哪一条。
    QVERIFY2(aipdf::doc::hasExtractableText(output), "回退策略下应当保留原文字");
    QVERIFY(report.keptCharCount > 0);
    QVERIFY2(report.notes.join(QStringLiteral("；")).contains(QStringLiteral("保留原文字")),
             qPrintable(report.notes.join(QStringLiteral("；"))));
}

void Wave21FlattenTests::rasterModeStillProducesAnImage() {
    const QString fontPath = findSystemFontPath();
    if (fontPath.isEmpty()) {
        QSKIP("这台机器上找不到可用的系统 TrueType 字体，跳过内嵌字体夹具。");
    }
    QFile fontFile(fontPath);
    QVERIFY(fontFile.open(QIODevice::ReadOnly));
    const QByteArray fontBytes = fontFile.readAll();
    TrueTypeInfo info;
    QVERIFY(parseTrueType(fontBytes, &info, nullptr));

    const QByteArray source = makeEmbeddedFontPdf(fontBytes, info, QStringLiteral("Hello"));
    FlattenOptions options;
    options.mode = FlattenMode::Raster;
    options.dpi = 96.0;
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(source, options, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    // 旧行为：产物里是一张位图。
    QVERIFY2(output.contains("/Image"), "栅格化产物里没有图像");
    QVERIFY(!aipdf::doc::hasExtractableText(output));
}

void Wave21FlattenTests::invalidInputFailsWithChineseReason() {
    QString error;
    const QByteArray output = aipdf::doc::flattenFontsToOutlines(
        QByteArrayLiteral("this is not a pdf at all"), FlattenOptions{}, &error);
    QVERIFY(output.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("字体转曲失败")), qPrintable(error));

    QString emptyError;
    QVERIFY(aipdf::doc::flattenFontsToOutlines(QByteArray(), FlattenOptions{}, &emptyError).isEmpty());
    QVERIFY(emptyError.contains(QStringLiteral("字体转曲失败")));
}

QTEST_MAIN(Wave21FlattenTests)
#include "Wave21FlattenTests.moc"
