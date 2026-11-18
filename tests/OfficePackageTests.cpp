// 真 Office 包（.pptx / .xlsx）的导出测试。
//
// 这一层只碰 QtCore：包就是 zip，zip 由 doc::zip::Writer 写、
// 由 doc::zip 的读接口回读，XML 用 QXmlStreamReader 解析。
//
// 为什么要单独回读一遍：**自己写、自己读不算验证** —— 写错了自己也读不出来。
// 所以这里除了本文件里的结构断言，还会把样例写到构建目录，
// 由 scripts/verify-office-packages.py 用**独立解析器**
// （Python 标准库 zipfile + 可选的 python-pptx / openpyxl）再验一次。

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTest>
#include <QXmlStreamReader>
#include <QVector>

#include "ai/content/PptGeneration.h"
#include "ai/content/PptxExport.h"
#include "doc/SpreadsheetExport.h"
#include "doc/ZipWriter.h"

namespace {

using aipdf::ai::PptDeck;
using aipdf::ai::Slide;

// 取一份 XML 里所有 <a:t>（pptx）或 <t>（xlsx）的文本。
//
// 走解析器而不是正则：正则分不清 <a:t> 与 <a:tab>，也看不出嵌套。
QStringList textNodes(const QByteArray& xml) {
    QStringList out;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() == QXmlStreamReader::StartElement &&
            (reader.name() == u"t" || reader.name() == u"tab")) {
            const QString text = reader.readElementText();
            if (!text.isEmpty()) {
                out.append(text);
            }
        }
    }
    return out;
}

PptDeck sampleDeck() {
    PptDeck deck;
    deck.title = QStringLiteral("季度总结");
    deck.theme = QStringLiteral("经营回顾");
    Slide first;
    first.index = 1;
    first.title = QStringLiteral("第一季度");
    first.subtitle = QStringLiteral("营收与成本");
    first.bullets = {QStringLiteral("营收 120 万"), QStringLiteral("成本 80 万"),
                     QStringLiteral("毛利率 33%")};
    first.notes = QStringLiteral("这里补充一句：第四季度口径有调整。");
    Slide second;
    second.index = 2;
    second.title = QStringLiteral("第二季度");
    second.bullets = {QStringLiteral("营收 150 万")};
    deck.slides = {first, second};
    return deck;
}

QVector<aipdf::doc::SheetRow> sampleRows() {
    QVector<aipdf::doc::SheetRow> rows;
    rows.append({QStringLiteral("姓名"), QStringLiteral("张三")});
    rows.append({QStringLiteral("单位"), QStringLiteral("示例 & 有限公司 <测试>")});
    rows.append({QStringLiteral("备注"), QString()});
    return rows;
}

}  // namespace

class OfficePackageTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void pptxHasExpectedPackageParts();
    void pptxSlideCarriesTitleAndBullets();
    void pptxKeepsSpeakerNotes();
    void pptxOfEmptyDeckIsEmpty();
    void pptxSurvivesSpecialCharacters();

    void xlsxHasExpectedPackageParts();
    void xlsxSheetCarriesFieldNamesAndValues();
    void xlsxDropsControlCharacters();
    void xlsxKeepsCarriageReturn();
    void xlsxSanitizesSheetName();

    void samplesAreWrittenForIndependentCheck();

private:
    QByteArray pptx_;
    QByteArray xlsx_;
};

void OfficePackageTests::initTestCase() {
    pptx_ = aipdf::ai::deckToPptx(sampleDeck());
    xlsx_ = aipdf::doc::rowsToXlsx(sampleRows(), QStringLiteral("表单数据"));
}

void OfficePackageTests::pptxHasExpectedPackageParts() {
    QVERIFY(!pptx_.isEmpty());
    const QStringList names = aipdf::doc::zip::listEntryNames(pptx_);
    // 少了任何一个部件，PowerPoint 都会判定文件损坏。
    const QStringList required = {
        QStringLiteral("[Content_Types].xml"),
        QStringLiteral("_rels/.rels"),
        QStringLiteral("ppt/presentation.xml"),
        QStringLiteral("ppt/_rels/presentation.xml.rels"),
        QStringLiteral("ppt/theme/theme1.xml"),
        QStringLiteral("ppt/slideMasters/slideMaster1.xml"),
        QStringLiteral("ppt/slideLayouts/slideLayout1.xml"),
        QStringLiteral("ppt/notesMasters/notesMaster1.xml"),
        QStringLiteral("ppt/slides/slide1.xml"),
        QStringLiteral("ppt/slides/slide2.xml"),
        QStringLiteral("ppt/slides/_rels/slide1.xml.rels"),
        QStringLiteral("ppt/notesSlides/notesSlide1.xml"),
        QStringLiteral("ppt/notesSlides/_rels/notesSlide1.xml.rels"),
    };
    for (const QString& name : required) {
        QVERIFY2(names.contains(name), qPrintable(QStringLiteral("缺部件：%1").arg(name)));
    }
    // 只有第一页有备注，就只该有一份 notesSlide。
    QCOMPARE(names.filter(QStringLiteral("ppt/notesSlides/notesSlide")).size(), 1);

    // 每个 XML 部件都必须是能解析的 XML。
    for (const QString& name : names) {
        if (!name.endsWith(QStringLiteral(".xml")) && !name.endsWith(QStringLiteral(".rels"))) {
            continue;
        }
        const QByteArray content = aipdf::doc::zip::readEntry(pptx_, name);
        QVERIFY(!content.isEmpty());
        QXmlStreamReader reader(content);
        while (!reader.atEnd()) {
            reader.readNext();
        }
        QVERIFY2(!reader.hasError(), qPrintable(QStringLiteral("%1 不是合法 XML：%2")
                                                     .arg(name, reader.errorString())));
    }
}

void OfficePackageTests::pptxSlideCarriesTitleAndBullets() {
    const QByteArray slide = aipdf::doc::zip::readEntry(pptx_, QStringLiteral("ppt/slides/slide1.xml"));
    QVERIFY(!slide.isEmpty());
    const QStringList texts = textNodes(slide);
    QVERIFY2(texts.contains(QStringLiteral("第一季度")), qPrintable(texts.join(QStringLiteral("|"))));
    QVERIFY(texts.contains(QStringLiteral("营收 120 万")));
    QVERIFY(texts.contains(QStringLiteral("成本 80 万")));
    QVERIFY(texts.contains(QStringLiteral("毛利率 33%")));
    // 副标题也在这页里。
    QVERIFY(texts.contains(QStringLiteral("营收与成本")));

    // 第二页只有一条要点。
    const QByteArray second = aipdf::doc::zip::readEntry(pptx_, QStringLiteral("ppt/slides/slide2.xml"));
    const QStringList secondTexts = textNodes(second);
    QVERIFY(secondTexts.contains(QStringLiteral("第二季度")));
    QVERIFY(secondTexts.contains(QStringLiteral("营收 150 万")));
    QVERIFY(!secondTexts.contains(QStringLiteral("营收 120 万")));

    // 幻灯片清单的条数与页数一致。
    const QByteArray presentation =
        aipdf::doc::zip::readEntry(pptx_, QStringLiteral("ppt/presentation.xml"));
    QVERIFY(QString::fromUtf8(presentation).count(QStringLiteral("<p:sldId ")) == 2);
}

void OfficePackageTests::pptxKeepsSpeakerNotes() {
    const QByteArray notes =
        aipdf::doc::zip::readEntry(pptx_, QStringLiteral("ppt/notesSlides/notesSlide1.xml"));
    QVERIFY(!notes.isEmpty());
    const QStringList texts = textNodes(notes);
    QVERIFY2(texts.contains(QStringLiteral("这里补充一句：第四季度口径有调整。")),
             qPrintable(texts.join(QStringLiteral("|"))));

    // notesSlide 必须指回它的幻灯片，否则备注挂不到对应的页上。
    const QByteArray rels = aipdf::doc::zip::readEntry(
        pptx_, QStringLiteral("ppt/notesSlides/_rels/notesSlide1.xml.rels"));
    QVERIFY(QString::fromUtf8(rels).contains(QStringLiteral("../slides/slide1.xml")));
}

void OfficePackageTests::pptxOfEmptyDeckIsEmpty() {
    // 空 deck 返回空字节：调用方要能区分「导出失败」与「没有内容」。
    QVERIFY(aipdf::ai::deckToPptx(PptDeck()).isEmpty());

    PptDeck broken;
    broken.error = QStringLiteral("模型没按格式回");
    broken.slides.clear();
    QVERIFY(aipdf::ai::deckToPptx(broken).isEmpty());
}

void OfficePackageTests::pptxSurvivesSpecialCharacters() {
    PptDeck deck;
    deck.title = QStringLiteral("A & B < C");
    Slide slide;
    slide.title = QStringLiteral("引号\"与撇号'");
    slide.bullets = {QStringLiteral("<tag> & 标签")};
    deck.slides = {slide};

    const QByteArray bytes = aipdf::ai::deckToPptx(deck);
    QVERIFY(!bytes.isEmpty());
    const QStringList texts = textNodes(
        aipdf::doc::zip::readEntry(bytes, QStringLiteral("ppt/slides/slide1.xml")));
    QVERIFY(texts.contains(QStringLiteral("引号\"与撇号'")));
    QVERIFY(texts.contains(QStringLiteral("<tag> & 标签")));
}

void OfficePackageTests::xlsxHasExpectedPackageParts() {
    QVERIFY(!xlsx_.isEmpty());
    const QStringList names = aipdf::doc::zip::listEntryNames(xlsx_);
    const QStringList required = {
        QStringLiteral("[Content_Types].xml"),
        QStringLiteral("_rels/.rels"),
        QStringLiteral("xl/workbook.xml"),
        QStringLiteral("xl/_rels/workbook.xml.rels"),
        QStringLiteral("xl/styles.xml"),
        QStringLiteral("xl/worksheets/sheet1.xml"),
    };
    for (const QString& name : required) {
        QVERIFY2(names.contains(name), qPrintable(QStringLiteral("缺部件：%1").arg(name)));
    }
    for (const QString& name : names) {
        const QByteArray content = aipdf::doc::zip::readEntry(xlsx_, name);
        QVERIFY(!content.isEmpty());
        QXmlStreamReader reader(content);
        while (!reader.atEnd()) {
            reader.readNext();
        }
        QVERIFY2(!reader.hasError(), qPrintable(QStringLiteral("%1 不是合法 XML：%2")
                                                     .arg(name, reader.errorString())));
    }
}

void OfficePackageTests::xlsxSheetCarriesFieldNamesAndValues() {
    const QByteArray sheet =
        aipdf::doc::zip::readEntry(xlsx_, QStringLiteral("xl/worksheets/sheet1.xml"));
    const QStringList texts = textNodes(sheet);
    // 表头与 CSV 一致。
    QVERIFY(texts.contains(QStringLiteral("name")));
    QVERIFY(texts.contains(QStringLiteral("value")));
    QVERIFY(texts.contains(QStringLiteral("姓名")));
    QVERIFY(texts.contains(QStringLiteral("张三")));
    // 值里的 & 与 < 要转义，但解析回来还是原样的字符。
    QVERIFY(texts.contains(QStringLiteral("示例 & 有限公司 <测试>")));
    // 空值也要有那一行（字段在，行就在）。
    QVERIFY(texts.contains(QStringLiteral("备注")));

    // 行数 = 表头 + 字段数。
    const QString sheetText = QString::fromUtf8(sheet);
    QCOMPARE(sheetText.count(QStringLiteral("<row ")), 4);
}

void OfficePackageTests::xlsxDropsControlCharacters() {
    QVector<aipdf::doc::SheetRow> rows;
    rows.append({QStringLiteral("带控制符"),
                  QStringLiteral("前") + QChar(0x01) + QStringLiteral("后")});

    const QByteArray bytes = aipdf::doc::rowsToXlsx(rows, QStringLiteral("S"));
    const QByteArray sheet =
        aipdf::doc::zip::readEntry(bytes, QStringLiteral("xl/worksheets/sheet1.xml"));
    // 控制字符必须被丢掉：留着它 Excel 会判定整个文件损坏。
    QVERIFY(!QString::fromUtf8(sheet).contains(QChar(0x01)));
    const QStringList texts = textNodes(sheet);
    QVERIFY(texts.contains(QStringLiteral("前后")));
}

void OfficePackageTests::xlsxKeepsCarriageReturn() {
    QVector<aipdf::doc::SheetRow> rows;
    rows.append({QStringLiteral("多行"), QStringLiteral("第一行\r第二行")});

    const QByteArray bytes = aipdf::doc::rowsToXlsx(rows, QStringLiteral("S"));
    const QByteArray sheet =
        aipdf::doc::zip::readEntry(bytes, QStringLiteral("xl/worksheets/sheet1.xml"));
    // 回车必须以字符引用写：字面回车会被 XML 解析器规范化成换行，值就变了。
    QVERIFY(QString::fromUtf8(sheet).contains(QStringLiteral("&#xD;")));
    const QStringList texts = textNodes(sheet);
    QVERIFY2(texts.contains(QStringLiteral("第一行\r第二行")),
             qPrintable(texts.join(QStringLiteral("|"))));
}

void OfficePackageTests::xlsxSanitizesSheetName() {
    // 非法的工作表名会让 Excel 判定文件损坏，导出前必须收拾干净。
    const QByteArray bytes = aipdf::doc::rowsToXlsx(
        sampleRows(), QStringLiteral("a/b:c*d?e[f]g超长名字超长名字超长名字超长"));
    const QByteArray workbook =
        aipdf::doc::zip::readEntry(bytes, QStringLiteral("xl/workbook.xml"));
    const QString text = QString::fromUtf8(workbook);

    // 只查工作表名本身：整份 XML 里的命名空间 URL 也有斜杠，不能拿全文判。
    const int start = text.indexOf(QStringLiteral("<sheet name=\""));
    QVERIFY(start >= 0);
    const int nameStart = start + QStringLiteral("<sheet name=\"").size();
    const int nameEnd = text.indexOf(QStringLiteral("\""), nameStart);
    QVERIFY(nameEnd > nameStart);
    const QString sheetName = text.mid(nameStart, nameEnd - nameStart);

    for (const QChar bad : {QLatin1Char('['), QLatin1Char(']'), QLatin1Char(':'),
                            QLatin1Char('*'), QLatin1Char('?'), QLatin1Char('/'),
                            QLatin1Char('\\')}) {
        QVERIFY2(!sheetName.contains(bad), qPrintable(sheetName));
    }
    // 名字长度上限 31。
    QVERIFY(sheetName.size() <= 31);
}

void OfficePackageTests::samplesAreWrittenForIndependentCheck() {
    // 写到构建目录，交给 scripts/verify-office-packages.py 用独立解析器再验一次。
    const QString dir = QStringLiteral(AIPDF_OFFICE_PACKAGE_DIR);
    QVERIFY2(QDir().mkpath(dir), qPrintable(dir));

    const QString pptxPath = QDir(dir).filePath(QStringLiteral("sample.pptx"));
    QFile pptxFile(pptxPath);
    QVERIFY2(pptxFile.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(pptxPath));
    pptxFile.write(pptx_);
    pptxFile.close();

    const QString xlsxPath = QDir(dir).filePath(QStringLiteral("sample.xlsx"));
    QFile xlsxFile(xlsxPath);
    QVERIFY2(xlsxFile.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(xlsxPath));
    xlsxFile.write(xlsx_);
    xlsxFile.close();

    QVERIFY(QFileInfo(pptxPath).size() > 0);
    QVERIFY(QFileInfo(xlsxPath).size() > 0);
}

QTEST_GUILESS_MAIN(OfficePackageTests)

#include "OfficePackageTests.moc"
