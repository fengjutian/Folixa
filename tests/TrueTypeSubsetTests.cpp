// TrueType 子集化的单元测试。
//
// 守的是三件事：
//  1. **字形号不能变**——PDF 用 Type0 / Identity-H + /CIDToGIDMap /Identity，
//     字形号一变，内容流里的十六进制串与 ToUnicode 全得跟着改。
//  2. **复合字形的组件要一起留下**——汉字常常由几个部件组成，
//     组件不在子集里，那个字就画不出来。
//  3. **子集化做不了时要老实说不行**——退回整份嵌入，而不是把字弄丢。
//
// 另有一条端到端：转换器嵌进去的是子集后的字体，PDFium 仍能把中文抽回来、
// 也能把字画出来（证明子集没把字形写坏）。
#include "doc/convert/UniversalConverter.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/kernel/TextPage.h"
#include "pdf/content/TrueTypeSubset.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QtTest>

using namespace aipdf::pdf;

namespace {

// ======================================================== 合成 TrueType =====

void appendU16(QByteArray& out, quint16 value) {
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

void appendS16(QByteArray& out, qint16 value) {
    appendU16(out, static_cast<quint16>(value));
}

void appendU32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>((value >> 24) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

// 一个方块字形（一个轮廓、四个点）。
QByteArray squareGlyph() {
    QByteArray out;
    appendS16(out, 1);     // numberOfContours
    appendS16(out, 0);     // xMin
    appendS16(out, 0);     // yMin
    appendS16(out, 800);   // xMax
    appendS16(out, 800);   // yMax
    appendU16(out, 3);     // endPtsOfContours[0]
    appendU16(out, 0);     // instructionLength
    for (int index = 0; index < 4; ++index) {
        out.append(static_cast<char>(0x01));  // on-curve 标记
    }
    appendS16(out, 0);
    appendS16(out, 800);
    appendS16(out, 0);
    appendS16(out, -800);
    appendS16(out, 0);
    appendS16(out, 0);
    appendS16(out, 800);
    appendS16(out, 0);
    while (out.size() % 4 != 0) {
        out.append('\0');
    }
    return out;
}

// 一个复合字形：引用组件字形 componentGid（带缩放与偏移）。
QByteArray compositeGlyph(int componentGid) {
    QByteArray out;
    appendS16(out, -1);    // numberOfContours < 0 → 复合字形
    appendS16(out, 0);     // xMin
    appendS16(out, 0);     // yMin
    appendS16(out, 800);   // xMax
    appendS16(out, 800);   // yMax
    appendU16(out, 0x0001 | 0x0008 | 0x0020);  // ARG_1_AND_2_ARE_WORDS | WE_HAVE_A_SCALE
    appendU16(out, static_cast<quint16>(componentGid));
    appendS16(out, 0);     // dx
    appendS16(out, 0);     // dy
    appendS16(out, 0x4000);// scale = 1.0（F2Dot14）
    appendU16(out, 0x0001 | 0x0008);           // 最后一个组件：不再有 MORE_COMPONENTS
    appendU16(out, static_cast<quint16>(componentGid));
    appendS16(out, 400);   // dx
    appendS16(out, 400);   // dy
    appendS16(out, 0x4000);// scale = 1.0
    while (out.size() % 4 != 0) {
        out.append('\0');
    }
    return out;
}

// cmap 子表（格式 4）：空格、A、B、中、结束段。
QByteArray buildCmap() {
    const quint16 startCodes[5] = {0x0020, 0x0041, 0x0042, 0x4E2D, 0xFFFF};
    const quint16 endCodes[5] = {0x0020, 0x0041, 0x0042, 0x4E2D, 0xFFFF};
    const qint16 idDeltas[5] = {static_cast<qint16>(4 - 0x0020), static_cast<qint16>(1 - 0x0041),
                                static_cast<qint16>(2 - 0x0042), static_cast<qint16>(3 - 0x4E2D), 1};

    QByteArray subtable;
    appendU16(subtable, 4);
    appendU16(subtable, 56);
    appendU16(subtable, 0);
    appendU16(subtable, 10);
    appendU16(subtable, 10);
    appendU16(subtable, 3);
    appendU16(subtable, 0);
    for (int index = 0; index < 5; ++index) {
        appendU16(subtable, endCodes[index]);
    }
    appendU16(subtable, 0);
    for (int index = 0; index < 5; ++index) {
        appendU16(subtable, startCodes[index]);
    }
    for (int index = 0; index < 5; ++index) {
        appendS16(subtable, idDeltas[index]);
    }
    appendU16(subtable, 0);

    QByteArray cmap;
    appendU16(cmap, 0);
    appendU16(cmap, 1);
    appendU16(cmap, 3);
    appendU16(cmap, 1);
    appendU32(cmap, 12);
    cmap.append(subtable);
    return cmap;
}

// 造一份 5 个字形的 TrueType 字体：.notdef、空格、A、B、中。
QByteArray buildTestFont(bool withComposite = false) {
    // 先把每个字形的数据备好，再据此算 loca。
    QVector<QByteArray> glyphs;
    glyphs.append(QByteArray());  // gid 0：.notdef（空字形）
    glyphs.append(squareGlyph()); // gid 1：空格
    glyphs.append(squareGlyph()); // gid 2：A
    glyphs.append(squareGlyph()); // gid 3：B
    glyphs.append(withComposite ? compositeGlyph(2) : squareGlyph());  // gid 4：中

    QByteArray glyf;
    QByteArray loca;
    appendU32(loca, 0);
    for (const QByteArray& glyph : glyphs) {
        glyf.append(glyph);
        appendU32(loca, static_cast<quint32>(glyf.size()));
    }

    QByteArray hmtx;
    for (int index = 0; index < 5; ++index) {
        appendU16(hmtx, 1000);
        appendS16(hmtx, 0);
    }

    QByteArray head;
    appendU32(head, 0x00010000);  // version
    appendU32(head, 0x00010000);  // fontRevision
    appendU32(head, 0);           // checkSumAdjustment
    appendU32(head, 0x5F0F3CF5);  // magicNumber
    appendU16(head, 0x0003);      // flags
    appendU16(head, 1000);        // unitsPerEm
    // created / modified 各 8 字节（各 2 个 u32）。head 总长必须是 54 字节：
    // 少写或多写都会让 indexToLocFormat 落到别的字段上，子集化就会按错的
    // loca 格式去读偏移（症状是「复合字形的组件没被保留」）。
    appendU32(head, 0);           // created 高 4 字节
    appendU32(head, 0);           // created 低 4 字节
    appendU32(head, 0);           // modified 高 4 字节
    appendU32(head, 0);           // modified 低 4 字节
    appendS16(head, 0);           // xMin
    appendS16(head, 0);           // yMin
    appendS16(head, 800);         // xMax
    appendS16(head, 800);         // yMax
    appendU16(head, 0);           // macStyle
    appendU16(head, 8);           // lowestRecPPEM
    appendS16(head, 2);           // fontDirectionHint
    appendS16(head, 1);           // indexToLocFormat：长格式
    appendS16(head, 0);           // glyphDataFormat

    QByteArray hhea;
    appendU32(hhea, 0x00010000);
    appendS16(hhea, 800);   // ascender
    appendS16(hhea, -200);  // descender
    appendS16(hhea, 0);     // lineGap
    appendU16(hhea, 1000);  // advanceWidthMax
    appendS16(hhea, 0);
    appendS16(hhea, 0);
    appendS16(hhea, 800);   // xMaxExtent
    appendS16(hhea, 1);     // caretSlopeRise
    appendS16(hhea, 0);
    appendS16(hhea, 0);
    appendU16(hhea, 0);
    appendU16(hhea, 0);
    appendU16(hhea, 0);
    appendU16(hhea, 0);
    appendS16(hhea, 0);
    appendU16(hhea, 5);     // numberOfHMetrics
    appendU16(hhea, 0);

    QByteArray maxp;
    appendU32(maxp, 0x00010000);
    appendU16(maxp, 5);  // numGlyphs
    appendU16(maxp, 4);
    appendU16(maxp, 0);
    appendU16(maxp, 0);
    appendU16(maxp, 2);
    appendU16(maxp, 0);
    appendU16(maxp, 0);
    appendU16(maxp, 0);
    appendU16(maxp, 0);
    appendU16(maxp, 0);
    appendU16(maxp, 0);
    appendU16(maxp, 0);
    appendU16(maxp, 0);
    appendU16(maxp, 0);

    QByteArray out;
    appendU32(out, 0x00010000);  // sfnt version
    appendU16(out, 7);           // numTables
    appendU16(out, 64);          // searchRange（16 * 2^floor(log2(7)) = 64）
    appendU16(out, 2);           // entrySelector
    appendU16(out, 48);          // rangeShift（7 * 16 - 64）

    struct Entry {
        const char* tag;
        QByteArray data;
    };
    // 表目录必须按 tag 升序：cmap glyf head hhea hmtx loca maxp。
    const Entry entries[7] = {
        {"cmap", buildCmap()}, {"glyf", glyf}, {"head", head},
        {"hhea", hhea},           {"hmtx", hmtx},   {"loca", loca},
        {"maxp", maxp},
    };
    quint32 offset = 12 + 7 * 16;
    for (const Entry& entry : entries) {
        for (int index = 0; index < 4; ++index) {
            out.append(entry.tag[index]);
        }
        appendU32(out, 0);  // checksum（占位）
        appendU32(out, offset);
        appendU32(out, static_cast<quint32>(entry.data.size()));
        offset += static_cast<quint32>((entry.data.size() + 3) / 4 * 4);
    }
    for (const Entry& entry : entries) {
        QByteArray payload = entry.data;
        while (payload.size() % 4 != 0) {
            payload.append('\0');
        }
        out.append(payload);
    }
    return out;
}

// 读出一份字体里的表目录（只给测试用）。
struct TableView {
    QHash<QString, QByteArray> tables;
};

TableView readTables(const QByteArray& font) {
    TableView view;
    if (font.size() < 12) {
        return view;
    }
    const int tableCount = (static_cast<uchar>(font.at(4)) << 8) | static_cast<uchar>(font.at(5));
    for (int index = 0; index < tableCount; ++index) {
        const int entry = 12 + index * 16;
        if (entry + 16 > font.size()) {
            break;
        }
        const QString tag = QString::fromLatin1(font.mid(entry, 4));
        const int offset = (static_cast<uchar>(font.at(entry + 8)) << 24) |
                           (static_cast<uchar>(font.at(entry + 9)) << 16) |
                           (static_cast<uchar>(font.at(entry + 10)) << 8) |
                           static_cast<uchar>(font.at(entry + 11));
        const int length = (static_cast<uchar>(font.at(entry + 12)) << 24) |
                           (static_cast<uchar>(font.at(entry + 13)) << 16) |
                           (static_cast<uchar>(font.at(entry + 14)) << 8) |
                           static_cast<uchar>(font.at(entry + 15));
        if (offset >= 0 && length >= 0 && offset + length <= font.size()) {
            view.tables.insert(tag, font.mid(offset, length));
        }
    }
    return view;
}

// 头表里的 indexToLocFormat：0 = 短格式（偏移 / 2），1 = 长格式。
bool locaIsLongFormat(const QByteArray& head) {
    if (head.size() < 52) {
        return true;
    }
    const int format = (static_cast<quint8>(head.at(50)) << 8) | static_cast<quint8>(head.at(51));
    return format == 1;
}

QVector<quint32> readLocaEntries(const QByteArray& loca, int glyphCount, bool longFormat) {
    QVector<quint32> offsets;
    for (int gid = 0; gid <= glyphCount; ++gid) {
        const int at = longFormat ? gid * 4 : gid * 2;
        if (at + (longFormat ? 4 : 2) > loca.size()) {
            break;
        }
        quint32 value = 0;
        if (longFormat) {
            value = (static_cast<quint32>(static_cast<uchar>(loca.at(at))) << 24) |
                    (static_cast<quint32>(static_cast<uchar>(loca.at(at + 1))) << 16) |
                    (static_cast<quint32>(static_cast<uchar>(loca.at(at + 2))) << 8) |
                    static_cast<quint32>(static_cast<uchar>(loca.at(at + 3)));
        } else {
            const quint16 raw = static_cast<quint16>((static_cast<quint8>(loca.at(at)) << 8) |
                                                    static_cast<quint8>(loca.at(at + 1)));
            value = static_cast<quint32>(raw) * 2;
        }
        offsets.append(value);
    }
    return offsets;
}

// 把 head 表里的 indexToLocFormat 改成非法值（2）。
//
// 模拟「表目录错位 / 字体损坏」：子集化器读到 0 和 1 之外的值就必须报错，
// 而不是猜一个格式去读偏移——猜错会少搬几个字形，中文直接画成空白。
QByteArray corruptLocaFormat(const QByteArray& font) {
    QByteArray out = font;
    const int tableCount = (static_cast<uchar>(out.at(4)) << 8) | static_cast<uchar>(out.at(5));
    for (int index = 0; index < tableCount; ++index) {
        const int entry = 12 + index * 16;
        if (out.mid(entry, 4) != QByteArrayLiteral("head")) {
            continue;
        }
        const int offset = (static_cast<uchar>(out.at(entry + 8)) << 24) |
                           (static_cast<uchar>(out.at(entry + 9)) << 16) |
                           (static_cast<uchar>(out.at(entry + 10)) << 8) |
                           static_cast<uchar>(out.at(entry + 11));
        out[offset + 50] = '\0';
        out[offset + 51] = '\x02';
        return out;
    }
    return out;
}

}  // namespace

class TrueTypeSubsetTests : public QObject {
    Q_OBJECT

private slots:
    void subsetKeepsGlyphNumbersAndDropsUnusedGlyphs();
    void compositeGlyphKeepsItsComponents();
    void brokenFontIsRejectedWithoutCrash();
    void malformedLocaFormatIsRejectedHonestly();
    void realSystemFontShrinksToAFraction();
    void converterEmbedsSubsetAndTextSurvivesRoundTrip();
    void converterFallsBackToFullEmbeddingWhenSubsetFails();
};

void TrueTypeSubsetTests::subsetKeepsGlyphNumbersAndDropsUnusedGlyphs() {
    const QByteArray font = buildTestFont();
    const TrueTypeSubsetResult result = subsetTrueTypeFont(font, QSet<int>{4});
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.bytes.size() < font.size());
    QCOMPARE(result.glyphCount, 5);
    // .notdef + 用到的那个字形。
    QCOMPARE(result.keptGlyphCount, 2);

    const TableView original = readTables(font);
    const TableView subset = readTables(result.bytes);
    QVERIFY(subset.tables.contains(QStringLiteral("glyf")));
    QVERIFY(subset.tables.contains(QStringLiteral("loca")));
    QVERIFY(subset.tables.contains(QStringLiteral("head")));

    const QVector<quint32> originalLoca =
        readLocaEntries(original.tables.value(QStringLiteral("loca")), 5,
                        locaIsLongFormat(original.tables.value(QStringLiteral("head"))));
    const QVector<quint32> subsetLoca =
        readLocaEntries(subset.tables.value(QStringLiteral("loca")), 5,
                        locaIsLongFormat(subset.tables.value(QStringLiteral("head"))));
    QCOMPARE(subsetLoca.size(), 6);

    const QByteArray originalGlyf = original.tables.value(QStringLiteral("glyf"));
    const QByteArray subsetGlyf = subset.tables.value(QStringLiteral("glyf"));
    // 字形号没变：gid 4 仍然是原来那份数据（子集里它在别的位置）。
    const QByteArray originalGlyph4 = originalGlyf.mid(originalLoca.at(4), originalLoca.at(5) - originalLoca.at(4));
    const QByteArray subsetGlyph4 = subsetGlyf.mid(subsetLoca.at(4), subsetLoca.at(5) - subsetLoca.at(4));
    QCOMPARE(subsetGlyph4, originalGlyph4);
    // 没用到的字形变成空字形（loca 指向同一个位置）。
    QCOMPARE(subsetLoca.at(2), subsetLoca.at(3));
    QCOMPARE(subsetLoca.at(1), subsetLoca.at(2));
}

void TrueTypeSubsetTests::compositeGlyphKeepsItsComponents() {
    // gid 4 是复合字形，引用 gid 2。只留 gid 4 时，组件 gid 2 必须跟着留下。
    const QByteArray font = buildTestFont(true);
    const TrueTypeSubsetResult result = subsetTrueTypeFont(font, QSet<int>{4});
    QVERIFY2(result.ok, qPrintable(result.error));

    const TableView subset = readTables(result.bytes);
    const QVector<quint32> loca =
        readLocaEntries(subset.tables.value(QStringLiteral("loca")), 5,
                        locaIsLongFormat(subset.tables.value(QStringLiteral("head"))));
    const QByteArray glyf = subset.tables.value(QStringLiteral("glyf"));
    QVERIFY(loca.size() == 6);
    // 组件字形（非空）留下来了。
    QVERIFY(loca.at(3) > loca.at(2));
    // 复合字形本身也在。
    QVERIFY(loca.at(5) > loca.at(4));
    QVERIFY(glyf.size() > 0);
}

void TrueTypeSubsetTests::brokenFontIsRejectedWithoutCrash() {
    // 坏字体不许崩，也不许假装成功。
    const TrueTypeSubsetResult garbage = subsetTrueTypeFont(QByteArray("not a font at all"), QSet<int>{1});
    QVERIFY(!garbage.ok);
    QVERIFY(!garbage.error.isEmpty());

    const TrueTypeSubsetResult empty = subsetTrueTypeFont(QByteArray(), QSet<int>{1});
    QVERIFY(!empty.ok);
    QVERIFY(!empty.error.isEmpty());

    // 只有头、没有表目录的半截文件。
    QByteArray header;
    appendU32(header, 0x00010000);
    appendU16(header, 0);
    const TrueTypeSubsetResult stub = subsetTrueTypeFont(header, QSet<int>{1});
    QVERIFY(!stub.ok);
}

void TrueTypeSubsetTests::malformedLocaFormatIsRejectedHonestly() {
    // head 表损坏（indexToLocFormat 是非法值）：必须报错，不能猜格式硬读。
    const QByteArray broken = corruptLocaFormat(buildTestFont());
    const TrueTypeSubsetResult result = subsetTrueTypeFont(broken, QSet<int>{4});
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.bytes.isEmpty());
}

void TrueTypeSubsetTests::realSystemFontShrinksToAFraction() {
    // 真机上的中文字体：这份才是子集化要解决的实际问题（整份 10 MB）。
    const QStringList candidates = {
        QStringLiteral("C:/Windows/Fonts/simhei.ttf"),
        QStringLiteral("C:/Windows/Fonts/Deng.ttf"),
        QStringLiteral("C:/Windows/Fonts/simsunb.ttf"),
    };
    QByteArray fontBytes;
    for (const QString& path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        fontBytes = file.readAll();
        file.close();
        if (!fontBytes.isEmpty()) {
            break;
        }
    }
    if (fontBytes.isEmpty()) {
        QSKIP("本机没有可用的中文字体，跳过真实字体子集化");
    }
    QVERIFY(fontBytes.size() > 100000);

    // 用到 20 个字形（gid 1..20）：对真字体来说已经够画一句话了。
    QSet<int> used;
    for (int gid = 1; gid <= 20; ++gid) {
        used.insert(gid);
    }
    const TrueTypeSubsetResult result = subsetTrueTypeFont(fontBytes, used);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.bytes.size() < fontBytes.size() / 10);
    QVERIFY(result.keptGlyphCount >= 21);
    // 子集字体仍然是合法的 sfnt：表目录能读出来。
    const TableView subset = readTables(result.bytes);
    QVERIFY(subset.tables.contains(QStringLiteral("glyf")));
    QVERIFY(subset.tables.contains(QStringLiteral("head")));
    QVERIFY(subset.tables.contains(QStringLiteral("cmap")));
}

void TrueTypeSubsetTests::converterEmbedsSubsetAndTextSurvivesRoundTrip() {
    // 端到端：转换器嵌的是子集后的字体，PDFium 仍能把中文抽回来、也能画出来。
    const QByteArray font = buildTestFont();
    aipdf::doc::UniversalConverterOptions options;
    options.regularFont.bytes = font;
    options.regularFont.postScriptName = QStringLiteral("AipdfSubsetTest");
    options.boldFont = options.regularFont;

    const aipdf::doc::UniversalConversionResult result =
        aipdf::doc::convertToPdf(QByteArray("A 中 B"), QStringLiteral("t.txt"), options);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.usedEmbeddedFont);
    QVERIFY2(result.fontSubset, "嵌入的字体应当已经子集化");
    QVERIFY(result.originalFontBytes > result.embeddedFontBytes);
    // 合成字体本来就只有几百字节，PDF 的结构开销比它还大——这里不比 PDF 大小，
    // 只比「嵌进去的字体比原字体小」这件事实。

    const PdfOpenResult opened = openDocumentFromBytes(result.bytes, QStringLiteral("subset.pdf"),
                                                        QString());
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
    const std::optional<PdfPage> page = opened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(page->isValid());

    // 文字能抽回来（ToUnicode 仍然对得上）。
    const std::optional<TextPage> text = page->extractText();
    QVERIFY(text.has_value());
    QVERIFY2(text->plainText().contains(QStringLiteral("中")), qPrintable(text->plainText()));

    // 字也画得出来：渲染后页面里有墨（子集没把字形写坏）。
    const QImage rendered = page->render(2.0);
    QVERIFY(!rendered.isNull());
    bool hasInk = false;
    for (int y = 0; y < rendered.height() && !hasInk; ++y) {
        for (int x = 0; x < rendered.width(); ++x) {
            if (qGray(rendered.pixel(x, y)) < 200) {
                hasInk = true;
                break;
            }
        }
    }
    QVERIFY2(hasInk, "渲染出来的页面是空白的：子集字体写坏了");
}

void TrueTypeSubsetTests::converterFallsBackToFullEmbeddingWhenSubsetFails() {
    // 子集化做不了时（这里模拟字体损坏）：退回整份嵌入。
    //
    // 规矩是「不为了省体积把字弄丢」：文件大一点可以，中文画不出来不行。
    const QByteArray broken = corruptLocaFormat(buildTestFont());
    aipdf::doc::UniversalConverterOptions options;
    options.regularFont.bytes = broken;
    options.regularFont.postScriptName = QStringLiteral("AipdfBrokenFont");

    const aipdf::doc::UniversalConversionResult result =
        aipdf::doc::convertToPdf(QByteArray("A 中 B"), QStringLiteral("t.txt"), options);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.usedEmbeddedFont);
    QVERIFY(!result.fontSubset);
    QVERIFY(result.originalFontBytes > 0);
    QCOMPARE(result.embeddedFontBytes, result.originalFontBytes);

    // 字还在：ToUnicode 不受影响，PDFium 照样能把中文抽回来。
    const PdfOpenResult opened = openDocumentFromBytes(result.bytes, QStringLiteral("fallback.pdf"),
                                                        QString());
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    const std::optional<PdfPage> page = opened.document.page(0);
    QVERIFY(page.has_value());
    const std::optional<TextPage> text = page->extractText();
    QVERIFY(text.has_value());
    QVERIFY2(text->plainText().contains(QStringLiteral("中")), qPrintable(text->plainText()));
}

QTEST_MAIN(TrueTypeSubsetTests)
#include "TrueTypeSubsetTests.moc"
