#include "pdf/content/TextReflow.h"
#include "pdf/content/TrueTypeSubset.h"
#include "pdf/content/VerticalWriting.h"

#include <QtTest>

#include <QFile>

#include <cmath>

using namespace aipdf::pdf;

namespace {

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

// 表的校验和：按大端 32 位累加，末尾不足 4 字节补零。
quint32 tableChecksum(const QByteArray& data) {
    QByteArray padded = data;
    while (padded.size() % 4 != 0) {
        padded.append('\0');
    }
    quint32 sum = 0;
    for (int index = 0; index + 3 < padded.size(); index += 4) {
        const auto* bytes = reinterpret_cast<const uchar*>(padded.constData()) + index;
        sum += (static_cast<quint32>(bytes[0]) << 24) | (static_cast<quint32>(bytes[1]) << 16) |
               (static_cast<quint32>(bytes[2]) << 8) | static_cast<quint32>(bytes[3]);
    }
    return sum;
}

// 造一份最小的 TrueType 字体：head / maxp / hhea / hmtx（+ vhea / vmtx）。
//
// 为什么要自己造：系统字体（SimHei 之类）不是每台机器都有，
// 而且它们的竖排度量值各不相同，断言只能写死「这台机器上碰巧是多少」。
// 自己造的字体里度量是我们自己定的，断言才是确定的。
//
// 表结构照 TrueType 规范：vhea 与 hhea 同样是 36 字节定长表，
// vmtx 前 numberOfVMetrics 个字形各 4 字节，其余字形沿用最后一条的 advance。
QByteArray buildTestFont(bool withVerticalTables, int unitsPerEm = 1000, int glyphCount = 4,
                          int ascent = 880, int descent = -120, int numberOfVMetrics = 2,
                          const QVector<int>& advanceHeights = QVector<int>{1000, 900}) {
    QVector<QPair<QByteArray, QByteArray>> tables;

    QByteArray head;
    appendU32(head, 0x00010000);   // version
    appendU32(head, 0x00010000);   // fontRevision
    appendU32(head, 0);            // checkSumAdjustment
    appendU32(head, 0x5F0F3CF5);   // magicNumber
    appendU16(head, 0);            // flags
    appendU16(head, static_cast<quint16>(unitsPerEm));
    appendU32(head, 0); appendU32(head, 0);  // created
    appendU32(head, 0); appendU32(head, 0);  // modified
    appendS16(head, 0); appendS16(head, -200); appendS16(head, 1000); appendS16(head, 900);
    appendU16(head, 0);   // macStyle
    appendU16(head, 8);   // lowestRecPPEM
    appendS16(head, 2);   // fontDirectionHint
    appendS16(head, 0);   // indexToLocFormat
    appendS16(head, 0);   // glyphDataFormat
    tables.append(qMakePair(QByteArrayLiteral("head"), head));

    QByteArray maxp;
    appendU32(maxp, 0x00010000);
    appendU16(maxp, static_cast<quint16>(glyphCount));
    tables.append(qMakePair(QByteArrayLiteral("maxp"), maxp));

    QByteArray hhea;
    appendU16(hhea, 1);
    appendU16(hhea, 0);
    appendS16(hhea, static_cast<qint16>(ascent));
    appendS16(hhea, static_cast<qint16>(descent));
    appendS16(hhea, 0);
    appendU16(hhea, static_cast<quint16>(unitsPerEm));
    for (int index = 0; index < 9; ++index) appendS16(hhea, 0);
    appendS16(hhea, 0);  // metricDataFormat
    appendU16(hhea, static_cast<quint16>(glyphCount));
    tables.append(qMakePair(QByteArrayLiteral("hhea"), hhea));

    QByteArray hmtx;
    for (int gid = 0; gid < glyphCount; ++gid) {
        appendU16(hmtx, static_cast<quint16>(unitsPerEm));
        appendS16(hmtx, 0);
    }
    tables.append(qMakePair(QByteArrayLiteral("hmtx"), hmtx));

    if (withVerticalTables) {
        QByteArray vhea;
        appendU16(vhea, 1);
        appendU16(vhea, 0);
        appendS16(vhea, static_cast<qint16>(ascent));
        appendS16(vhea, static_cast<qint16>(descent));
        appendS16(vhea, 0);
        appendU16(vhea, static_cast<quint16>(unitsPerEm));
        for (int index = 0; index < 9; ++index) appendS16(vhea, 0);
        appendS16(vhea, 0);  // metricDataFormat
        appendU16(vhea, static_cast<quint16>(numberOfVMetrics));
        tables.append(qMakePair(QByteArrayLiteral("vhea"), vhea));

        QByteArray vmtx;
        for (int gid = 0; gid < numberOfVMetrics; ++gid) {
            appendU16(vmtx, static_cast<quint16>(advanceHeights.value(gid)));
            appendS16(vmtx, 0);
        }
        // 剩下的字形只留 top side bearing，advance 沿用最后一条。
        for (int gid = numberOfVMetrics; gid < glyphCount; ++gid) appendS16(vmtx, 0);
        tables.append(qMakePair(QByteArrayLiteral("vmtx"), vmtx));
    }

    // 表目录必须按 tag 排序（TrueType 规范要求）。
    std::sort(tables.begin(), tables.end(),
              [](const QPair<QByteArray, QByteArray>& left,
                 const QPair<QByteArray, QByteArray>& right) { return left.first < right.first; });

    const int tableCount = tables.size();
    int selector = 0;
    while ((1 << (selector + 1)) <= tableCount) ++selector;
    const int searchRange = 16 * (1 << selector);
    const int rangeShift = tableCount * 16 - searchRange;

    QByteArray out;
    appendU32(out, 0x00010000);
    appendU16(out, static_cast<quint16>(tableCount));
    appendU16(out, static_cast<quint16>(searchRange));
    appendU16(out, static_cast<quint16>(selector));
    appendU16(out, static_cast<quint16>(rangeShift));

    const int headerSize = 12 + tableCount * 16;
    int cursor = headerSize;
    QVector<int> offsets;
    for (const auto& table : tables) {
        offsets.append(cursor);
        cursor += ((table.second.size() + 3) / 4) * 4;
    }
    for (int index = 0; index < tableCount; ++index) {
        const auto& table = tables.at(index);
        out.append(table.first);
        appendU32(out, tableChecksum(table.second));
        appendU32(out, static_cast<quint32>(offsets.at(index)));
        appendU32(out, static_cast<quint32>(table.second.size()));
    }
    for (const auto& table : tables) {
        QByteArray payload = table.second;
        while (payload.size() % 4 != 0) payload.append('\0');
        out.append(payload);
    }
    return out;
}

// 内容流里用的字符码：Unicode 码点的四位十六进制（与 Identity-H 的写法一致）。
QByteArray codeOf(const QString& text) {
    return QByteArray::number(text.at(0).unicode(), 16).rightJustified(4, '0').toUpper();
}

VerticalMetrics sampleMetrics() {
    VerticalMetrics metrics;
    metrics.ascent = 880.0;
    metrics.descent = -120.0;
    metrics.defaultWidth = 1000.0;
    metrics.hasVerticalMetrics = true;
    return metrics;
}

}  // namespace

class VerticalWritingTests final : public QObject {
    Q_OBJECT
private slots:
    void readsVerticalMetricsFromFontTables();
    void reportsMissingVerticalMetricsHonestly();
    void keepsSubsettingVerticalTables();
    void laysOutColumnsRightToLeftAndTopToBottom();
    void stepsByVerticalMetricsNotHorizontalAdvance();
    void writesContentStreamInReadingOrder();
    void buildsWidthArrayByCid();
    void buildsFontEntriesWithWMode();
    void keepsWhitespaceSourceOffsetsInVerticalReflow();
    void usesVerticalAdvanceInVerticalReflow();
    void marksDegradedVerticalLayout();
    void checksSystemCjkFontVerticalMetrics();
};

// 有 vhea / vmtx 的字体：竖排度量读得出来。
void VerticalWritingTests::readsVerticalMetricsFromFontTables() {
    const QByteArray font = buildTestFont(true);
    const TrueTypeVerticalMetrics raw = readTrueTypeVerticalMetrics(font);
    QVERIFY2(raw.hasVerticalMetrics, qPrintable(raw.error));
    QCOMPARE(raw.unitsPerEm, 1000);
    QCOMPARE(raw.ascent, 880);
    QCOMPARE(raw.descent, -120);
    QCOMPARE(raw.numberOfVMetrics, 2);
    QCOMPARE(raw.glyphCount, 4);
    // 前两个字形各自有记录；后面的沿用最后一条（TrueType 规范如此）。
    QCOMPARE(raw.advanceHeightForGlyph(0), 1000);
    QCOMPARE(raw.advanceHeightForGlyph(1), 900);
    QCOMPARE(raw.advanceHeightForGlyph(2), 900);
    QCOMPARE(raw.advanceHeightForGlyph(3), 900);

    const VerticalMetrics metrics = readVerticalMetrics(font);
    QVERIFY(metrics.hasVerticalMetrics);
    QCOMPARE(metrics.ascent, 880.0);
    QCOMPARE(metrics.descent, -120.0);
    // 默认宽度取最后一条竖排度量（多数字形都用它）。
    QCOMPARE(metrics.defaultWidth, 900.0);
}

// 没有 vhea / vmtx：如实说没有，不编数。
void VerticalWritingTests::reportsMissingVerticalMetricsHonestly() {
    const TrueTypeVerticalMetrics raw = readTrueTypeVerticalMetrics(buildTestFont(false));
    QVERIFY(!raw.hasVerticalMetrics);
    QVERIFY(!raw.error.isEmpty());

    const VerticalMetrics metrics = readVerticalMetrics(buildTestFont(false));
    QVERIFY(!metrics.hasVerticalMetrics);
    QCOMPARE(metrics.ascent, 0.0);
    QCOMPARE(metrics.descent, 0.0);
    QCOMPARE(metrics.defaultWidth, 0.0);

    // 坏字体也不能编出数来。
    const VerticalMetrics broken = readVerticalMetrics(QByteArray("not a font at all"));
    QVERIFY(!broken.hasVerticalMetrics);
    QCOMPARE(broken.defaultWidth, 0.0);
}

// 子集化之后 vhea / vmtx 还在（字形号不变，竖排度量依然对得上）。
void VerticalWritingTests::keepsSubsettingVerticalTables() {
    const QByteArray font = buildTestFont(true, 1000, 4, 880, -120, 2, {1000, 900});
    const TrueTypeSubsetResult subset = subsetTrueTypeFont(font, {1, 2});
    QVERIFY2(subset.ok, qPrintable(subset.error));
    const TrueTypeVerticalMetrics raw = readTrueTypeVerticalMetrics(subset.bytes);
    QVERIFY2(raw.hasVerticalMetrics, qPrintable(raw.error));
    QCOMPARE(raw.ascent, 880);
    QCOMPARE(raw.advanceHeightForGlyph(1), 900);
    QCOMPARE(raw.advanceHeightForGlyph(2), 900);
}

// 列自右向左、列内自上而下，步进用竖排度量。
void VerticalWritingTests::laysOutColumnsRightToLeftAndTopToBottom() {
    VerticalLayoutOptions options;
    options.fontSize = 20.0;
    options.left = 0.0;
    options.right = 100.0;
    options.bottom = 0.0;
    options.top = 300.0;
    options.metrics = sampleMetrics();

    const QString text = QStringLiteral("甲乙丙丁戊己庚辛");
    const VerticalLayout layout = layoutVerticalText(text, options);
    QVERIFY2(layout.error.isEmpty(), qPrintable(layout.error));
    QCOMPARE(layout.glyphs.size(), 8);
    QCOMPARE(layout.columns.size(), 2);
    // 一列放 5 个字（300 / 20），剩下的到左边一列。
    QCOMPARE(layout.columns.at(0).glyphs.size(), 5);
    QCOMPARE(layout.columns.at(1).glyphs.size(), 3);
    // **列自右向左**：第 0 列在右边。
    QVERIFY(layout.columns.at(0).origin.x() > layout.columns.at(1).origin.x());
    QCOMPARE(layout.columns.at(0).origin.x(), 90.0);
    QCOMPARE(layout.columns.at(1).origin.x(), 70.0);
    // **列内自上而下**：PDF user space 里 y 递减。
    const QVector<VerticalGlyph>& first = layout.columns.at(0).glyphs;
    QCOMPARE(first.at(0).origin.y(), 300.0);
    QCOMPARE(first.at(1).origin.y(), 280.0);
    QCOMPARE(first.at(4).origin.y(), 220.0);
    QCOMPARE(first.at(0).columnIndex, 0);
    QCOMPARE(first.at(0).rowIndex, 0);
    QCOMPARE(first.at(4).rowIndex, 4);
    // 步进用的是竖排度量（1 em），不是横排宽度。
    QCOMPARE(first.at(1).advance, 20.0);
    QCOMPARE(layout.consumedLength, text.size());
    QVERIFY(!layout.overflow);
    QVERIFY(!layout.verticalMetricsDegraded);
}

// 步进不能退回横排 advance。
void VerticalWritingTests::stepsByVerticalMetricsNotHorizontalAdvance() {
    VerticalLayoutOptions options;
    options.fontSize = 20.0;
    options.left = 0.0;
    options.right = 400.0;
    options.bottom = 0.0;
    options.top = 400.0;
    options.metrics = sampleMetrics();
    options.metrics.defaultWidth = 2000.0;  // 2 em 的字身框

    const VerticalLayout layout = layoutVerticalText(QStringLiteral("甲乙丙"), options);
    QCOMPARE(layout.glyphs.size(), 3);
    QCOMPARE(layout.glyphs.at(1).origin.y() - layout.glyphs.at(0).origin.y(), -40.0);
    QCOMPARE(layout.glyphs.at(1).advance, 40.0);
}

// 内容流顺序 == 阅读顺序（列右→左、列内上→下）。
void VerticalWritingTests::writesContentStreamInReadingOrder() {
    VerticalLayoutOptions options;
    options.fontSize = 20.0;
    options.left = 0.0;
    options.right = 100.0;
    options.bottom = 0.0;
    options.top = 100.0;
    options.metrics = sampleMetrics();

    const QString text = QStringLiteral("甲乙丙丁戊己庚辛");
    VerticalLayout layout = layoutVerticalText(text, options);
    for (VerticalGlyph& glyph : layout.glyphs) {
        glyph.code = codeOf(glyph.text);
    }
    const QByteArray stream = buildVerticalContentStream(layout, QStringLiteral("F1"), 20.0);
    QVERIFY(!stream.isEmpty());
    QVERIFY(stream.contains("BT"));
    QVERIFY(stream.contains("/F1 20 Tf"));

    // 每个字在内容流里出现的先后，必须与阅读顺序一致。
    int previous = -1;
    for (const VerticalGlyph& glyph : layout.glyphs) {
        const int at = stream.indexOf("<" + glyph.code + ">");
        QVERIFY2(at > previous, qPrintable(QStringLiteral("字符 %1 的位置不对").arg(glyph.text)));
        previous = at;
    }

    // 每一列一个 BT/ET，列的顺序是右→左。
    const int firstTm = stream.indexOf("Tm");
    const int secondTm = stream.indexOf("Tm", firstTm + 1);
    QVERIFY(secondTm > firstTm);
    const QList<QByteArray> parts = stream.split('\n');
    double previousX = 0.0;
    int seen = 0;
    for (const QByteArray& line : parts) {
        if (!line.contains(" Tm")) continue;
        const QList<QByteArray> tokens = line.split(' ');
        if (tokens.size() < 6) continue;
        const double x = tokens.at(4).toDouble();
        if (seen > 0) QVERIFY(x < previousX);  // 后一列在左边
        previousX = x;
        ++seen;
    }
    QCOMPARE(seen, 2);
}

// /W2 数组：CID 从 0 起连续，每组三个数（w1y / w1x / vy）。
void VerticalWritingTests::buildsWidthArrayByCid() {
    QVector<VerticalCidMetrics> cids;
    cids.append({0, 880.0, 500.0, -1000.0});
    cids.append({1, 880.0, 500.0, -900.0});
    cids.append({2, 880.0, 500.0, -900.0});
    const QByteArray array = buildVerticalWidthArray(cids);
    // 连续且度量相同的 CID 并成一条。
    QCOMPARE(array, QByteArray("[0 [880 500 -1000] 1 [880 500 -900 880 500 -900]]"));

    // CID 是绝对编号：中间空出来的 CID 不影响后面的编号。
    QVector<VerticalCidMetrics> sparse;
    sparse.append({7, 880.0, 500.0, -1000.0});
    sparse.append({9, 880.0, 500.0, -1000.0});
    QCOMPARE(buildVerticalWidthArray(sparse),
             QByteArray("[7 [880 500 -1000] 9 [880 500 -1000]]"));
}

// 字体字典片段：/WMode 1 + /DW2（+ /W2）。
void VerticalWritingTests::buildsFontEntriesWithWMode() {
    QVector<VerticalCidMetrics> cids;
    cids.append({0, 880.0, 500.0, -1000.0});
    const QByteArray entries = buildVerticalFontEntries(sampleMetrics(), cids);
    QVERIFY(entries.startsWith("/WMode 1"));
    QVERIFY(entries.contains("/DW2 [880 -1000]"));
    QVERIFY(entries.contains("/W2 [0 [880 500 -1000]]"));

    // 没有竖排度量：只给 /WMode 1 与规范默认的 /DW2，**不给 /W2**。
    const QByteArray degraded = buildVerticalFontEntries(VerticalMetrics(), QVector<VerticalCidMetrics>());
    QVERIFY(degraded.contains("/WMode 1"));
    QVERIFY(degraded.contains("/DW2 [880 -1000]"));
    QVERIFY(!degraded.contains("/W2"));

    // 按字体字节生成：读得到竖排度量就带上 /W2，读不到就不给。
    const QByteArray withVertical = buildVerticalFontEntriesForFont(buildTestFont(true), {0, 1});
    QVERIFY(withVertical.contains("/WMode 1"));
    QVERIFY(withVertical.contains("/W2 [0 [880 500 -1000] 1 [880 500 -900]]"));
    const QByteArray withoutVertical = buildVerticalFontEntriesForFont(buildTestFont(false), {0, 1});
    QVERIFY(withoutVertical.contains("/WMode 1"));
    QVERIFY(!withoutVertical.contains("/W2"));
}

// TextReflow 回归：空白不能把源串偏移弄丢。
void VerticalWritingTests::keepsWhitespaceSourceOffsetsInVerticalReflow() {
    TextStyle style;
    style.fontSize = 20.0;
    style.vertical = true;
    const QString source = QStringLiteral("甲 乙丙");
    const ReflowResult result = TextReflow::layoutParagraph(source, QRectF(0, 0, 60, 200), style);
    QCOMPARE(result.lines.size(), 3);
    // 第二个字在源串里的位置要把空格算进去。
    QCOMPARE(result.lines.at(0).sourceStart, 0);
    QCOMPARE(result.lines.at(1).sourceStart, 2);
    QCOMPARE(result.lines.at(2).sourceStart, 3);
    for (const ReflowLine& line : result.lines) {
        QCOMPARE(line.text, source.mid(line.sourceStart, line.sourceLength));
    }
    // 整段都排进去了：consumedLength 不能少算空格。
    QCOMPARE(result.consumedLength, source.size());
    QVERIFY(result.remainingText.isEmpty());
}

// TextReflow 回归：竖排步进要用竖排度量，不能用横排 advance。
void VerticalWritingTests::usesVerticalAdvanceInVerticalReflow() {
    TextStyle style;
    style.fontSize = 20.0;
    style.vertical = true;
    style.hasVerticalMetrics = true;
    style.verticalAscent = 880.0;
    style.verticalDescent = -120.0;
    style.verticalDefaultWidth = 1000.0;

    const ReflowResult result =
        TextReflow::layoutParagraph(QStringLiteral("甲乙丙"), QRectF(0, 0, 80, 400), style);
    QCOMPARE(result.lines.size(), 3);
    // 步进 = 竖排度量（1 em = 20 点），不是横排 advance。
    QCOMPARE(result.lines.at(1).baseline.y() - result.lines.at(0).baseline.y(), 20.0);
    QCOMPARE(result.lines.at(2).baseline.y() - result.lines.at(1).baseline.y(), 20.0);
    QVERIFY(!result.verticalMetricsDegraded);

    // 竖排度量给成 2 em，步进就跟着变成 2 em。
    style.verticalDefaultWidth = 2000.0;
    const ReflowResult wide = TextReflow::layoutParagraph(
        QStringLiteral("甲乙丙"), QRectF(0, 0, 80, 400), style);
    QCOMPARE(wide.lines.size(), 3);
    QCOMPARE(wide.lines.at(1).baseline.y() - wide.lines.at(0).baseline.y(), 40.0);
}

// 没有竖排度量：按竖排默认宽度（1 em）走，并标出降级。
void VerticalWritingTests::marksDegradedVerticalLayout() {
    VerticalLayoutOptions options;
    options.fontSize = 20.0;
    options.left = 0.0;
    options.right = 100.0;
    options.bottom = 0.0;
    options.top = 300.0;
    // metrics.hasVerticalMetrics 保持为假。
    const VerticalLayout layout = layoutVerticalText(QStringLiteral("甲乙丙"), options);
    QVERIFY(layout.verticalMetricsDegraded);
    QCOMPARE(layout.glyphs.size(), 3);
    QCOMPARE(layout.glyphs.at(1).origin.y() - layout.glyphs.at(0).origin.y(), -20.0);

    TextStyle style;
    style.fontSize = 20.0;
    style.vertical = true;
    const ReflowResult reflow =
        TextReflow::layoutParagraph(QStringLiteral("甲乙丙"), QRectF(0, 0, 80, 400), style);
    QVERIFY(reflow.verticalMetricsDegraded);
}

// 系统里的中文字体：有竖排度量就应当读得出来（读不出也得是「没有」，不是编的数）。
void VerticalWritingTests::checksSystemCjkFontVerticalMetrics() {
    const QString path = QStringLiteral("C:/Windows/Fonts/simhei.ttf");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        // 这台机器上没有这份字体：那就确认解析器对空输入也如实说「没有」。
        const VerticalMetrics missing = readVerticalMetrics(QByteArray());
        QVERIFY(!missing.hasVerticalMetrics);
        return;
    }
    const VerticalMetrics metrics = readVerticalMetrics(file.readAll());
    QVERIFY2(metrics.hasVerticalMetrics, "SimHei 带 vhea / vmtx，应当读得出竖排度量");
    // SimHei 的竖排 advance 是 1 em（256/256），换算成 1/1000 em 就是 1000。
    QVERIFY2(std::abs(metrics.defaultWidth - 1000.0) < 1.0,
             qPrintable(QStringLiteral("默认竖排宽度不对：%1").arg(metrics.defaultWidth)));
    QVERIFY(metrics.ascent > 0.0);
    QVERIFY(metrics.descent < 0.0);
}

QTEST_MAIN(VerticalWritingTests)
#include "VerticalWritingTests.moc"
