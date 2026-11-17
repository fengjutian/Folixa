#include "pdf/InvisibleTextLayer.h"

#include "pdf/PdfiumAdapter.h"

#include <QTest>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::InvisibleTextLayerOptions;
using aipdf::pdf::InvisibleTextLayerResult;
using aipdf::pdf::TextChar;
using aipdf::pdf::TextPage;

namespace {

// 造一份 PDF。
//
// xref 表必须覆盖到最大的对象号：表短了的话，后面几个对象就成了「找不到的引用」，
// qpdf 会去重建 xref，行为就不确定了。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects) {
    int maxId = 0;
    for (const auto& object : objects) maxId = qMax(maxId, object.first);
    QByteArray out("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = maxId + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        if (!offsets.contains(id)) {
            out += "0000000000 65535 f \n";
            continue;
        }
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id, 0), 10, 10, QLatin1Char('0'))
                   .toLatin1();
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// 一页空白 PDF（内容流里什么都没有）。
//
// 对象号必须连续：中间空一个号的话 xref 表里那个号没有偏移，
// qpdf 会报 "object has offset 0" 的警告，测试输出就不干净了。
QByteArray blankPdf(int pageCount) {
    const int fontId = 3 + 2 * pageCount;
    QStringList kids;
    for (int i = 0; i < pageCount; ++i) {
        kids.append(QStringLiteral("%1 0 R").arg(3 + 2 * i));
    }
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QStringLiteral("<< /Type /Pages /Kids [%1] /Count %2 >>")
                                   .arg(kids.join(QLatin1Char(' ')))
                                   .arg(pageCount)
                                   .toLatin1()));
    for (int i = 0; i < pageCount; ++i) {
        objects.append(qMakePair(3 + 2 * i,
                                 QStringLiteral("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                                "/Resources << /Font << /F1 %1 0 R >> >> "
                                                "/Contents %2 0 R >>")
                                     .arg(fontId)
                                     .arg(4 + 2 * i)
                                     .toLatin1()));
        objects.append(qMakePair(4 + 2 * i, QByteArray("<< /Length 0 >>\nstream\n\nendstream")));
    }
    objects.append(qMakePair(fontId,
                             QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    return buildPdf(objects);
}

// 一页带可见文字的 PDF。
QByteArray visibleTextPdf(const QByteArray& text) {
    const QByteArray content =
        "BT /F1 12 Tf 40 250 Td (" + text + ") Tj ET";
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R >>")));
    objects.append(qMakePair(4, QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
                                   .arg(content.size())
                                   .arg(QString::fromLatin1(content))
                                   .toLatin1()));
    objects.append(qMakePair(5, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    return buildPdf(objects);
}

// 一页文字：每个字符一个框。
//
// advance 是每个字符的框宽（点），size 是字号（也就是框高）。
TextPage makeTextPage(int pageIndex, const QString& text, double x, double baseline, double size,
                      double advance) {
    TextPage page;
    page.pageIndex = pageIndex;
    double cursor = x;
    for (const QChar& ch : text) {
        TextChar character;
        character.text = QString(ch);
        character.box = QRectF(cursor, baseline, advance, size);
        character.index = page.chars.size();
        page.chars.append(character);
        cursor += advance;
    }
    return page;
}

// 用 PDFium 重开一份 PDF，取出全部文字。
QString textOf(const QByteArray& bytes, int* pageCount = nullptr) {
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) return QString();
    if (pageCount) *pageCount = opened.document.pageCount();
    QString text;
    for (int i = 0; i < opened.document.pageCount(); ++i) {
        const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(i);
        if (!page.has_value()) continue;
        const std::optional<aipdf::pdf::TextPage> extracted = page->extractText();
        if (!extracted.has_value()) continue;
        text += extracted->plainText();
    }
    return text;
}

// 取出一页的内容流（用 qpdf 读）。
QByteArray pageContentOf(const QByteArray& bytes, int pageIndex) {
#ifdef AIPDF_WITH_QPDF
    QPDF pdf;
    pdf.processMemoryFile("check.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return QByteArray();
    QPDFObjectHandle page = pages.at(static_cast<size_t>(pageIndex));
    QByteArray content;
    for (const QPDFObjectHandle& item : page.getPageContents()) {
        QPDFObjectHandle stream = item;
        if (!stream.isStream()) continue;
        const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
        if (!data) continue;
        content += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                              static_cast<qsizetype>(data->getSize()));
    }
    return content;
#else
    Q_UNUSED(bytes)
    Q_UNUSED(pageIndex)
    return QByteArray();
#endif
}

}  // namespace

class InvisibleTextLayerTests : public QObject {
    Q_OBJECT

private slots:
    // ---- 负例 ----

    void emptyInputFails() {
        const InvisibleTextLayerResult result =
            writeInvisibleTextLayer(QByteArray(), QVector<TextPage>());
        QVERIFY(!result.ok());
        QVERIFY(result.error.contains(QStringLiteral("为空")));
    }

    void notAPdfFails() {
        const InvisibleTextLayerResult result =
            writeInvisibleTextLayer(QByteArray("this is not a pdf at all"), QVector<TextPage>());
        QVERIFY(!result.ok());
        QVERIFY2(!result.error.isEmpty(), "输入不是 PDF 时必须给出中文原因");
        QVERIFY(result.bytes.isEmpty());
    }

    void missingTextPagesReportsTheDifference() {
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(blankPdf(1), QVector<TextPage>());
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains(QStringLiteral("页数不一致")), qPrintable(result.error));
        QVERIFY2(result.error.contains(QStringLiteral("差 1 页")), qPrintable(result.error));
    }

    void tooManyTextPagesReportsTheDifference() {
        QVector<TextPage> pages;
        pages.append(makeTextPage(0, QStringLiteral("A"), 40, 250, 12, 7));
        pages.append(makeTextPage(1, QStringLiteral("B"), 40, 250, 12, 7));
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(blankPdf(1), pages);
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains(QStringLiteral("页数不一致")), qPrintable(result.error));
        QVERIFY2(result.error.contains(QStringLiteral("差 1 页")), qPrintable(result.error));
    }

    void inconsistentPageIndexFails() {
        QVector<TextPage> pages;
        // 标注的页序号与它在列表里的位置不一致。
        pages.append(makeTextPage(1, QStringLiteral("A"), 40, 250, 12, 7));
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(blankPdf(1), pages);
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains(QStringLiteral("页序号")), qPrintable(result.error));
    }

    // ---- 正常路径 ----

    void noRecognizedTextReturnsTheInputUnchanged() {
        const QByteArray input = blankPdf(1);
        QVector<TextPage> pages;
        pages.append(TextPage());
        pages.first().pageIndex = 0;
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(input, pages);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.bytes, input);
        QCOMPARE(result.charCount, 0);
    }

    void writesInvisibleTextThatCanBeFound() {
        QVector<TextPage> pages;
        pages.append(makeTextPage(0, QStringLiteral("Hello OCR"), 40, 250, 12, 7));
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(blankPdf(1), pages);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY(!result.bytes.isEmpty());

        int pageCount = 0;
        const QString extracted = textOf(result.bytes, &pageCount);
        QCOMPARE(pageCount, 1);
        QVERIFY2(extracted.contains(QStringLiteral("Hello OCR")),
                 qPrintable(QStringLiteral("取到的文字：%1").arg(extracted)));
        QCOMPARE(result.charCount, 9);
    }

    void pageCountDoesNotChange() {
        QVector<TextPage> pages;
        pages.append(makeTextPage(0, QStringLiteral("Page one"), 40, 250, 12, 7));
        pages.append(makeTextPage(1, QStringLiteral("Page two"), 40, 250, 12, 7));
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(blankPdf(2), pages);
        QVERIFY2(result.ok(), qPrintable(result.error));

        int pageCount = 0;
        const QString extracted = textOf(result.bytes, &pageCount);
        QCOMPARE(pageCount, 2);
        QVERIFY(extracted.contains(QStringLiteral("Page one")));
        QVERIFY(extracted.contains(QStringLiteral("Page two")));
    }

    void usesRenderModeThree() {
        QVector<TextPage> pages;
        pages.append(makeTextPage(0, QStringLiteral("Invisible"), 40, 250, 12, 7));
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(blankPdf(1), pages);
        QVERIFY2(result.ok(), qPrintable(result.error));
        const QByteArray content = pageContentOf(result.bytes, 0);
        QVERIFY2(content.contains("3 Tr"), "不可见文字层必须用渲染模式 3");
    }

    void keepsTheVisibleContentAndAppendsTheLayer() {
        const QByteArray input = visibleTextPdf("Visible");
        QVector<TextPage> pages;
        pages.append(makeTextPage(0, QStringLiteral("Hidden"), 40, 100, 12, 7));
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(input, pages);
        QVERIFY2(result.ok(), qPrintable(result.error));

        const QString extracted = textOf(result.bytes);
        QVERIFY2(extracted.contains(QStringLiteral("Visible")), "原来的可见文字必须还在");
        QVERIFY2(extracted.contains(QStringLiteral("Hidden")), "不可见文字必须能搜到");

        const QByteArray content = pageContentOf(result.bytes, 0);
        const int visibleAt = content.indexOf("Visible");
        const int invisibleAt = content.indexOf("3 Tr");
        QVERIFY(visibleAt >= 0);
        QVERIFY(invisibleAt >= 0);
        QVERIFY2(invisibleAt > visibleAt, "不可见文字层必须加在页面内容流的末尾");
    }

    void chineseTextKeepsItsUnicode() {
        QVector<TextPage> pages;
        pages.append(makeTextPage(0, QStringLiteral("你好世界"), 40, 250, 12, 12));
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(blankPdf(1), pages);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY2(result.unicodeFontUsed, "中文必须走带 /ToUnicode 的字体");

        const QString extracted = textOf(result.bytes);
        QVERIFY2(extracted.contains(QStringLiteral("你好世界")),
                 qPrintable(QStringLiteral("取到的文字：%1").arg(extracted)));
    }

    void countsTheTextRuns() {
        QVector<TextPage> pages;
        TextPage page = makeTextPage(0, QStringLiteral("First line"), 40, 250, 12, 7);
        TextPage second = makeTextPage(0, QStringLiteral("Second line"), 40, 200, 12, 7);
        page.chars += second.chars;
        pages.append(page);
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(blankPdf(1), pages);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.runCount, 2);
        // "First line" 10 个字 + "Second line" 11 个字。
        QCOMPARE(result.charCount, 21);
    }

    void usesTheInjectedFontResource() {
        InvisibleTextLayerOptions options;
        options.fontResourceName = QStringLiteral("F3");
        QVector<TextPage> pages;
        pages.append(makeTextPage(0, QStringLiteral("Injected"), 40, 250, 12, 7));
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(blankPdf(1), pages, options);
        QVERIFY2(result.ok(), qPrintable(result.error));
        const QByteArray content = pageContentOf(result.bytes, 0);
        QVERIFY2(content.contains("/F3"), qPrintable(content));
    }

    void moreThan255DifferentCharactersStillComeOutRight() {
        // 一个简单字体的 /Differences 最多 256 个码，所以 255 个不同的字符之后
        // 要换第二个字体资源；一段文字正好跨过这条界线时必须拆开写，
        // 否则后半段会用错字体，文字就乱了。
        QString text;
        for (int i = 0; i < 300; ++i) {
            text += QChar(static_cast<char16_t>(0x4E00 + i));
        }
        QVector<TextPage> pages;
        pages.append(makeTextPage(0, text, 40, 250, 12, 12));
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(blankPdf(1), pages);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY(result.unicodeFontUsed);
        QCOMPARE(result.charCount, 300);

        const QString extracted = textOf(result.bytes);
        // 读者会按位置在「换字体资源」的地方补一个空格（PDFium 的启发式，
        // 不是我们写进去的字），所以比对时先去掉空格：字符一个都不能少、也不能乱。
        QString compact = extracted;
        compact.remove(QLatin1Char(' '));
        QVERIFY2(compact == text,
                 qPrintable(QStringLiteral("取到的文字长度 %1，去掉空格后 %2")
                                .arg(extracted.size())
                                .arg(compact.size())));
    }

    void outputCanBeParsedAgain() {
        QVector<TextPage> pages;
        pages.append(makeTextPage(0, QStringLiteral("Round trip"), 40, 250, 12, 7));
        const InvisibleTextLayerResult result = writeInvisibleTextLayer(blankPdf(1), pages);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY(!result.bytes.isEmpty());
        // 再走一遍：输出还能被当作输入处理，说明结构没坏。
        QVector<TextPage> again;
        again.append(makeTextPage(0, QStringLiteral("Second pass"), 40, 250, 12, 7));
        const InvisibleTextLayerResult second = writeInvisibleTextLayer(result.bytes, again);
        QVERIFY2(second.ok(), qPrintable(second.error));
        QCOMPARE(textOf(second.bytes).contains(QStringLiteral("Second pass")), true);
    }

    // ---- 同一份 PDF 上写两次 ----

    void writingTwiceDoesNotMixUpFontSlots() {
#ifndef AIPDF_WITH_QPDF
        QSKIP("当前构建没有启用 qpdf，无法写不可见文字层");
#else
        // 同一份 PDF 上做两次 OCR 回写：两次写的字都必须原样读回来。
        //
        // 曾经的缺陷：给槽位分配字体资源名时，只避开了「页面原有的字体」，
        // 没避开「本次调用里已经分配出去的名字」。
        // 第二次写的时候，两个槽位会拿到同一个资源名，后一个把前一个覆盖掉 ——
        // 函数照样返回 ok=true，但前半段文字被按另一张字的映射解了出来。
        //
        // 触发条件很现实：同一份 PDF 上跑第二次 OCR 时，页面里已经有
        // /AipdfOcrF1..N 了。
        QString first;
        for (int i = 0; i < 300; ++i) first += QChar(0x4E00 + i);
        QString second;
        for (int i = 0; i < 300; ++i) second += QChar(0x5000 + i);

        const QByteArray base = blankPdf(1);
        const InvisibleTextLayerResult firstWrite = writeInvisibleTextLayer(
            base, QVector<TextPage>{makeTextPage(0, first, 10, 250, 12, 1.2)});
        QVERIFY2(firstWrite.ok(), qPrintable(firstWrite.error));

        const InvisibleTextLayerResult secondWrite = writeInvisibleTextLayer(
            firstWrite.bytes, QVector<TextPage>{makeTextPage(0, second, 10, 250, 12, 1.2)});
        QVERIFY2(secondWrite.ok(), qPrintable(secondWrite.error));

        const QString extracted = textOf(secondWrite.bytes);
        // 逐个抽查两批字里的代表字符。
        // 不能整段比对：阅读器会在字与字之间补空格（那是阅读器的启发式，不是缺陷）。
        for (int i = 0; i < 300; i += 37) {
            // 第二遍的字必须能取到 —— 曾经一个字都取不到（函数却返回 ok=true）。
            QVERIFY2(extracted.contains(second.at(i)),
                     qPrintable(QStringLiteral("第二次写的字取不到：U+%1")
                                    .arg(second.at(i).unicode(), 4, 16, QLatin1Char('0'))));
            // 第一遍的字应该已经被替换掉了：两层叠在同一坐标上时，
            // PDFium 只保留一层，留哪一层是不确定的。
            QVERIFY2(!extracted.contains(first.at(i)),
                     qPrintable(QStringLiteral("第一次写的字还在，旧层没被替换：U+%1")
                                    .arg(first.at(i).unicode(), 4, 16, QLatin1Char('0'))));
        }
#endif
    }
};

QTEST_GUILESS_MAIN(InvisibleTextLayerTests)
#include "InvisibleTextLayerTests.moc"
