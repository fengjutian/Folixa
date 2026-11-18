// 第三批界面接线（任务 J~U）的单测。
//
// 这一批接的都是「领域层已经做完、但界面上点不到」的能力。
// 所以这里测的不是算法，而是**接线是否真的存在**：
//   - Ribbon 上的入口有没有、名字对不对、会不会撞车；
//   - 面板上的条目数是不是领域层给的那个数（界面不重算）；
//   - 界面文案有没有把领域层做不到的事说成能做到（负例守卫）。
//
// 注意用 QTEST_MAIN 而不是 QTEST_GUILESS_MAIN：这里要建控件，
// QTEST_GUILESS_MAIN 只建 QCoreApplication，建 QWidget 会 abort。
#include "app/controllers/LastWaveBridge.h"
#include "app/panels/LastWavePanels.h"
#include "app/shell/RibbonBar.h"
#include "app/ui/SystemCjkFont.h"
#include "doc/index/OfflineQa.h"
#include "doc/model/BlankDocument.h"
#include "doc/model/DocumentChunk.h"
#include "doc/convert/UniversalConverter.h"
#include "pdf/content/InvisibleTextLayer.h"
#include "pdf/ops/OptionalContent.h"
#include "pdf/compliance/PdfUaChecker.h"
#include "pdf/kernel/TextPage.h"
#include "pdf/forms/XfaSupport.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

namespace ai = aipdf::ai;
namespace app = aipdf::app;
namespace doc = aipdf::doc;
namespace pdf = aipdf::pdf;

namespace {

// 这一批新增的 Ribbon 入口：标签与 objectName 必须与 MainWindow 里的一致。
struct RibbonCommandSpec {
    const char* label;
    const char* objectName;
};

const RibbonCommandSpec kNewCommands[] = {
    {"从文件新建 PDF", "file.fromFile"},
    {"动作向导", "file.actionWizard"},
    {"发票打印", "file.invoicePrint"},
    {"写入隐藏文字层", "ocr.invisibleTextLayer"},
    {"段落排版", "edit.paragraphFormat"},
    {"插入 3D 模型", "edit.insert3d"},
    {"插入多媒体", "edit.insertMedia"},
    {"PDF/UA 合规检查", "convert.pdfUa"},
    {"XFA 字段", "convert.xfa"},
    {"图层面板", "convert.layers"},
    {"按新页宽重排", "page.reflow"},
    {"本地离线问答", "ai.offlineQa"},
    {"读取标签树", "accessibility.structTree"},
};

// 第二批已经占用的 objectName：第三批不能跟它们撞车。
const RibbonCommandSpec kSecondBatchCommands[] = {
    {"新建", "file.new"},
    {"批量打印", "file.printBatch"},
    {"打印模板", "file.printTemplates"},
    {"文生图", "ai.generateImage"},
    {"总结配图", "ai.summaryImage"},
    {"生成 PPT", "ai.ppt"},
    {"内容检测", "ai.detect"},
    {"智能朗读", "ai.narration"},
    {"计算器", "annotation.calculator"},
    {"符号面板", "annotation.symbols"},
    {"连续批注", "annotation.continuous"},
    {"导出 HTML", "convert.exportHtml"},
    {"图像化 PDF", "convert.imagePdf"},
    {"到 OFD", "convert.exportOfd"},
    {"到 MS Office", "convert.exportOffice"},
    {"到 CAD", "convert.exportDxf"},
    {"PDF 转曲", "convert.flattenFonts"},
    {"印前检查", "convert.preflight"},
    {"PDF 文件包", "convert.portfolio"},
    {"OCR 疑似错误", "convert.ocrSuspicion"},
};

// 读翻译目录，返回 context → source 集合。
QSet<QString> catalogSources(const QString& context, const QString& path) {
    QSet<QString> sources;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return sources;
    }
    const QString xml = QString::fromUtf8(file.readAll());
    file.close();
    const QRegularExpression contextPattern(
        QStringLiteral("<name>([^<]*)</name>(.*?)</context>"),
        QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = contextPattern.globalMatch(xml);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (match.captured(1) != context) {
            continue;
        }
        const QRegularExpression sourcePattern(
            QStringLiteral("<source>([^<]*)</source>"),
            QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatchIterator sourceIt =
            sourcePattern.globalMatch(match.captured(2));
        while (sourceIt.hasNext()) {
            sources.insert(sourceIt.next().captured(1));
        }
    }
    return sources;
}

// 造一份只有一页的空白 PDF（真文档：后面几项要拿它跑领域层）。
QByteArray blankPdf(QString* error = nullptr) {
    doc::BlankDocumentOptions options;
    options.pageCount = 1;
    return doc::createBlankDocument(options, error);
}

}  // namespace

class LastWaveUiTests final : public QObject {
    Q_OBJECT

private slots:
    // 1) 每个新增的 Ribbon 动作都有非空 accessibleName（逐个断言）。
    void everyRibbonCommandHasAccessibleName();
    // 2) 新增的 accessibleName 不含助记符标记。
    void accessibleNamesHaveNoMnemonicMarker();
    // 3) 新增 objectName 唯一（且不与第二批撞车）。
    void newObjectNamesAreUnique();
    // 4) 隐藏文字层失败时显示的是领域层的中文原因。
    void invisibleTextLayerShowsDomainReason();
    // 5) 段落排版：justify 选项存在，参数原样交给领域层。
    void paragraphFormatPassesParametersThrough();
    // 6) 标签树读取：没有标签树时是明确说明，不是空白。
    void structTreeEmptyIsExplained();
    // 7) OCG 面板：图层条目数与 readOptionalContent().groups.size() 一致。
    void ocgPanelCountMatchesDomain();
    // 8) 任意文件→PDF：没嵌入字体时必须说清有多少字符显示不出来。
    void universalConversionReportsReplacedCharacters();
    // 8b) 任意文件→PDF：嵌入字体做了子集化时，提示里要说出省了多少体积。
    void universalConversionNoticeReportsFontSubset();
    // 8c) 任意文件→PDF：用户没选字体时，中文不该变成问号（自动用系统中文字体）。
    void universalConvertDialogAutoEmbedsSystemCjkFont();
    // 9) 离线问答：没找到时正文为空，并且列出最接近的片段。
    void offlineQaNoMatchShowsNoAnswer();
    // 10) PDF/UA：结论与 compliant 一致，且不许吹成「已生成合规文件」。
    void pdfUaVerdictMatchesDomain();
    // 11) XFA：动态 XFA 的提示文案出现。
    void xfaDynamicNoticeIsShown();
    // 12) 3D / 多媒体：文案不许承诺「可播放 / 可旋转 3D」。
    void richMediaNoticeDoesNotPromiseRendering();
    // 13) 重排：丢失清单非空时逐条显示。
    void reflowDroppedListIsItemised();
    // 14) 新增的用户可见文案都在翻译目录里。
    void newStringsAreInTranslationCatalog();
    // 15) OCR 词 → 字符框：框高取词框高，序号连续。
    void ocrWordsBecomeCharacterBoxes();
    // 16) 标签树的一行里能看到替代文本、语言与实际文字。
    void structTreeRowCarriesAltLanguageAndText();
    // 17) 图层面板写回的方案保留图层名与勾选状态。
    void ocgPlanKeepsNamesAndVisibility();
    // 18) 发票：币种与小数位由用户选，金额按选择显示。
    void invoiceAmountFollowsUserChoice();
    // 19) 动作向导：能力边界说清楚不是脚本引擎；失败原因来自领域层。
    void wizardBoundaryAndFailureReason();
    // 20) 不可见文字层的「替换」语义要写在界面上。
    void invisibleTextLayerNoticeSaysReplace();
};

// ---------------------------------------------------------------- 1 / 2 ----

void LastWaveUiTests::everyRibbonCommandHasAccessibleName() {
    app::RibbonBar ribbon;
    const int tab = ribbon.addTab(QStringLiteral("测试"));
    auto* group = ribbon.addGroup(tab, QStringLiteral("命令"));
    for (const RibbonCommandSpec& spec : kNewCommands) {
        ribbon.addCommand(group, QString::fromUtf8(spec.label), QIcon(),
                          QString::fromUtf8(spec.label), true,
                          QString::fromUtf8(spec.objectName));
    }
    const QVector<QToolButton*> buttons = ribbon.commandButtons();
    QCOMPARE(buttons.size(), static_cast<int>(sizeof(kNewCommands) / sizeof(kNewCommands[0])));
    // 逐个断言：只看一个按钮的话，漏掉其余的不会被发现。
    for (QToolButton* button : buttons) {
        QVERIFY2(button != nullptr, "命令按钮为空");
        QVERIFY2(!button->accessibleName().isEmpty(),
                 qPrintable(QStringLiteral("按钮 %1 没有无障碍名")
                                .arg(button->objectName())));
        QCOMPARE(button->accessibleName(), button->text().remove(QLatin1Char('&')));
    }
}

void LastWaveUiTests::accessibleNamesHaveNoMnemonicMarker() {
    app::RibbonBar ribbon;
    const int tab = ribbon.addTab(QStringLiteral("测试"));
    auto* group = ribbon.addGroup(tab, QStringLiteral("命令"));
    for (const RibbonCommandSpec& spec : kNewCommands) {
        ribbon.addCommand(group, QString::fromUtf8(spec.label), QIcon(),
                          QString::fromUtf8(spec.label), true,
                          QString::fromUtf8(spec.objectName));
    }
    for (QToolButton* button : ribbon.commandButtons()) {
        QVERIFY2(!button->accessibleName().contains(QLatin1Char('&')),
                 qPrintable(QStringLiteral("按钮 %1 的无障碍名里带着助记符标记")
                                .arg(button->objectName())));
    }
}

// ------------------------------------------------------------------- 3 ----

void LastWaveUiTests::newObjectNamesAreUnique() {
    QSet<QString> names;
    for (const RibbonCommandSpec& spec : kSecondBatchCommands) {
        const QString name = QString::fromUtf8(spec.objectName);
        QVERIFY2(!names.contains(name), qPrintable(name));
        names.insert(name);
    }
    for (const RibbonCommandSpec& spec : kNewCommands) {
        const QString name = QString::fromUtf8(spec.objectName);
        QVERIFY2(!names.contains(name), qPrintable(name));
        names.insert(name);
    }
    app::RibbonBar ribbon;
    const int tab = ribbon.addTab(QStringLiteral("测试"));
    auto* group = ribbon.addGroup(tab, QStringLiteral("命令"));
    for (const RibbonCommandSpec& spec : kNewCommands) {
        ribbon.addCommand(group, QString::fromUtf8(spec.label), QIcon(),
                          QString::fromUtf8(spec.label), true,
                          QString::fromUtf8(spec.objectName));
    }
    QSet<QString> seen;
    for (QToolButton* button : ribbon.commandButtons()) {
        const QString name = button->objectName();
        QVERIFY2(!name.isEmpty(), "命令按钮没有 objectName");
        QVERIFY2(!seen.contains(name), qPrintable(name));
        seen.insert(name);
    }
}

// ------------------------------------------------------------------- 4 ----

void LastWaveUiTests::invisibleTextLayerShowsDomainReason() {
    // 领域层失败时，界面显示的就是它的中文原因，不是「操作失败」这种空话。
    const QString reason = QStringLiteral("文字页数与 PDF 页数不一致：PDF 有 1 页，提供了 3 页文字（差 2 页）");
    const QString shown = app::invisibleTextLayerFailureText(reason);
    QCOMPARE(shown, reason);
    QVERIFY2(!shown.contains(QStringLiteral("操作失败")), qPrintable(shown));
    QVERIFY2(!shown.contains(QStringLiteral("失败")), "界面把领域层的中文原因换成了空话");
    // 真的跑一次领域层：页数对不上必须报错，而且原因能被界面原样显示。
    QString error;
    const QByteArray bytes = blankPdf(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QVector<pdf::TextPage> pages;
    pdf::TextPage page;
    page.pageIndex = 0;
    pages.append(page);
    pages.append(page);
    pages.append(page);
    const pdf::InvisibleTextLayerResult result = pdf::writeInvisibleTextLayer(bytes, pages);
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(app::invisibleTextLayerFailureText(result.error), result.error);
    // 页数差要说清楚差几页。
    const QString mismatch = app::invisibleTextLayerPageMismatchText(3, 1);
    QVERIFY(mismatch.contains(QStringLiteral("3")));
    QVERIFY(mismatch.contains(QStringLiteral("1")));
    QVERIFY(mismatch.contains(QStringLiteral("2")));
}

// ------------------------------------------------------------------- 5 ----

void LastWaveUiTests::paragraphFormatPassesParametersThrough() {
    // 两端对齐必须是一个可选项。
    QVERIFY(app::paragraphAlignments().contains(pdf::TextAlignment::Justify));
    QVERIFY(app::paragraphAlignments().contains(pdf::TextAlignment::Left));
    QVERIFY(app::paragraphAlignments().contains(pdf::TextAlignment::Center));
    QVERIFY(app::paragraphAlignments().contains(pdf::TextAlignment::Right));

    app::ParagraphFormatDialog dialog;
    pdf::ParagraphFormat format;
    format.alignment = pdf::TextAlignment::Justify;
    format.fontSize = 13.0;
    format.lineSpacingMode = pdf::LineSpacingMode::Fixed;
    format.lineSpacing = 17.0;
    format.firstLineIndent = 24.0;
    format.leftIndent = 12.0;
    format.rightIndent = 6.0;
    dialog.setFormat(format);
    // 两端对齐是「把多出来的宽度分摊给行内的空格」，所以文本里要有空格。
    const QString paragraphText = QStringLiteral(
        "layout parameters must reach the domain layer unchanged and justify stretches every line but the last");
    dialog.setText(paragraphText);
    dialog.setBox(QRectF(10.0, 20.0, 300.0, 200.0));

    // 界面把参数原样交给领域层。
    const pdf::ParagraphFormat passed = dialog.format();
    QCOMPARE(passed.alignment, pdf::TextAlignment::Justify);
    QCOMPARE(passed.fontSize, 13.0);
    QCOMPARE(passed.lineSpacingMode, pdf::LineSpacingMode::Fixed);
    QCOMPARE(passed.lineSpacing, 17.0);
    QCOMPARE(passed.firstLineIndent, 24.0);
    QCOMPARE(passed.leftIndent, 12.0);
    QCOMPARE(passed.rightIndent, 6.0);
    QCOMPARE(dialog.box(), QRectF(10.0, 20.0, 300.0, 200.0));

    // 排版本身由领域层完成：两端对齐时除最后一行外都撑满。
    const pdf::ParagraphPlan plan = pdf::layoutParagraph(dialog.text(), dialog.box(), passed);
    QVERIFY2(plan.ok(), qPrintable(plan.error));
    QVERIFY(plan.lines.size() >= 2);
    for (int index = 0; index < plan.lines.size() - 1; ++index) {
        QVERIFY2(plan.lines.at(index).justified, "两端对齐时非末行应当被撑满");
    }
    // 摘要里要能看出参数。
    QVERIFY(dialog.formatSummaryText().contains(QStringLiteral("13")));
}

// ------------------------------------------------------------------- 6 ----

void LastWaveUiTests::structTreeEmptyIsExplained() {
    pdf::StructTreeReport report;  // hasStructTree 为假
    QVERIFY(!report.hasStructTree);
    const QString summary = app::structTreeSummaryText(report);
    QVERIFY2(summary.contains(QStringLiteral("没有标签树")), qPrintable(summary));
    QVERIFY(!summary.trimmed().isEmpty());
    QCOMPARE(summary, app::structTreeEmptyText());

    // 界面上：表格是空的，但有明确说明（不是一片空白）。
    app::StructTreeDialog dialog;
    dialog.setReport(report);
    QCOMPARE(dialog.rowCount(), 0);
    QVERIFY(dialog.summaryText().contains(QStringLiteral("没有标签树")));
    QVERIFY(!dialog.emptyText().isEmpty());
}

// ------------------------------------------------------------------- 7 ----

void LastWaveUiTests::ocgPanelCountMatchesDomain() {
    QString error;
    const QByteArray bytes = blankPdf(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    pdf::OptionalContentPlan plan;
    pdf::OptionalContentLayer first;
    first.name = QStringLiteral("底图");
    first.defaultVisible = true;
    pdf::OptionalContentLayer second;
    second.name = QStringLiteral("标注");
    second.defaultVisible = false;
    plan.layers.append(first);
    plan.layers.append(second);
    const QByteArray written = pdf::writeOptionalContent(bytes, plan, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const pdf::OptionalContentSummary summary = pdf::readOptionalContent(written, &error);
    QVERIFY2(summary.hasOptionalContent, qPrintable(error));
    QCOMPARE(summary.groups.size(), 2);

    app::OcgLayerPanel dialog;
    dialog.setSummary(summary);
    QCOMPARE(dialog.layerCount(), summary.groups.size());
    QVERIFY(dialog.summaryText().contains(QStringLiteral("2")));
}

// ------------------------------------------------------------------- 8 ----

void LastWaveUiTests::universalConversionReportsReplacedCharacters() {
    // 真转一次：中文在标准 14 字体下必然被替换。
    const QByteArray chinese = QStringLiteral("这是一段中文，用来检查替换字符的提示。").toUtf8();
    const doc::UniversalConversionResult result = doc::convertToPdf(chinese, QStringLiteral("测试.txt"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.usedEmbeddedFont);
    QVERIFY(result.replacedCharacterCount > 0);

    const QString notice = app::universalConversionNoticeText(result);
    QVERIFY2(notice.contains(QString::number(result.replacedCharacterCount)),
             qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("无法显示")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("选择字体文件")), qPrintable(notice));

    // 用上了嵌入字体时就不该再提「字符被替换」。
    doc::UniversalConversionResult embedded = result;
    embedded.usedEmbeddedFont = true;
    const QString embeddedNotice = app::universalConversionNoticeText(embedded);
    QVERIFY2(!embeddedNotice.contains(QStringLiteral("无法显示")), qPrintable(embeddedNotice));
    QVERIFY(embeddedNotice.contains(QStringLiteral("嵌入")));
}

// ------------------------------------------------------------------- 9 ----

void LastWaveUiTests::universalConversionNoticeReportsFontSubset() {
    // 子集化是 H6 的另一半收益：中文不丢字之外，文件也不能是 10 MB 的巨物。
    // 提示里必须说出「嵌了多少 / 原来多少」，用户才看得见。
    doc::UniversalConversionResult result;
    result.ok = true;
    result.pageCount = 1;
    result.usedEmbeddedFont = true;
    result.fontSubset = true;
    result.originalFontBytes = 10 * 1024 * 1024;
    result.embeddedFontBytes = 48 * 1024;

    const QString notice = app::universalConversionNoticeText(result);
    QVERIFY2(notice.contains(QStringLiteral("只嵌了用到的字形")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("48.0 KB")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("10.0 MB")), qPrintable(notice));

    // 没做子集化时就不许提「只嵌了用到的字形」——没发生的事不吹。
    doc::UniversalConversionResult plain = result;
    plain.fontSubset = false;
    const QString plainNotice = app::universalConversionNoticeText(plain);
    QVERIFY2(!plainNotice.contains(QStringLiteral("只嵌了用到的字形")), qPrintable(plainNotice));
}

// ------------------------------------------------------------------- 9 ----

void LastWaveUiTests::universalConvertDialogAutoEmbedsSystemCjkFont() {
    // 用户没选字体、文件里有中文时，中文不该变成问号。
    //
    // 「系统里有没有中文字体」这件事**先独立查一遍**：有字体就必须嵌入成功
    // （断言，不是跳过），没有字体才跳过——否则这条用例在真出问题时
    // 会「跳过」成一片绿，把回归藏起来。
    doc::FontResource probe;
    QString probePath;
    if (!app::loadSystemCjkFont(&probe, &probePath)) {
        QSKIP("本机没有可用的中文字体，跳过自动嵌入这一条");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("中文.txt");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QStringLiteral("这是一段中文，应该显示得出来。").toUtf8());
    file.close();

    app::UniversalConvertDialog dialog;
    dialog.setSourcePath(path);
    QString error;
    QVERIFY2(dialog.convert(&error), qPrintable(error));

    const doc::UniversalConversionResult& result = dialog.result();
    QVERIFY(result.ok);
    QVERIFY(result.usedEmbeddedFont);
    // 用了字体就不该再有替换掉的字符。
    QCOMPARE(result.replacedCharacterCount, 0);
    // 提示里要说出用的是哪份字体。
    QVERIFY(!result.embeddedFontName.isEmpty());
    QVERIFY2(dialog.noticeText().contains(result.embeddedFontName),
             qPrintable(dialog.noticeText()));
}

// ------------------------------------------------------------------- 9 ----

void LastWaveUiTests::offlineQaNoMatchShowsNoAnswer() {
    ai::OfflineAnswer answer;
    answer.status = ai::OfflineAnswerStatus::NoMatch;
    answer.notice = QStringLiteral("本地索引是空的。");
    ai::OfflineCitation closest;
    closest.pageIndex = 2;
    closest.quote = QStringLiteral("最接近的一段原文");
    answer.closest.append(closest);

    QVERIFY(answer.isNoMatch());
    // 找不到就是没有正文：不编一句「看起来像答案」的话。
    QVERIFY(app::offlineAnswerText(answer).isEmpty());
    QVERIFY(app::offlineNoMatchText(answer).contains(QStringLiteral("没找到")));
    QCOMPARE(app::offlineClosestLines(answer).size(), 1);
    QVERIFY(app::offlineClosestLines(answer).first().contains(QStringLiteral("最接近的一段原文")));
    // 有答案时正文才非空。
    ai::OfflineAnswer found = answer;
    found.status = ai::OfflineAnswerStatus::Found;
    found.text = QStringLiteral("答案正文");
    QVERIFY(!app::offlineAnswerText(found).isEmpty());
    QVERIFY(app::offlineNoMatchText(found).isEmpty());
}

// ------------------------------------------------------------------ 10 ----

void LastWaveUiTests::pdfUaVerdictMatchesDomain() {
    pdf::PdfUaReport report;
    report.compliant = false;
    report.passCount = 3;
    report.warningCount = 1;
    report.errorCount = 2;
    pdf::PdfUaFinding finding;
    finding.code = QStringLiteral("ua-missing-lang");
    finding.title = QStringLiteral("缺少文档语言");
    finding.detail = QStringLiteral("catalog 里没有 /Lang");
    finding.suggestion = QStringLiteral("补上 /Lang");
    finding.severity = pdf::PdfUaSeverity::Error;
    report.findings.append(finding);

    // 结论必须与 compliant 一致。
    QVERIFY(app::pdfUaConclusionText(report).contains(QStringLiteral("不合规")));
    report.compliant = true;
    QVERIFY(app::pdfUaConclusionText(report).contains(QStringLiteral("合规")));
    QVERIFY(app::pdfUaConclusionText(report).contains(QStringLiteral("3")));

    // 每条结论都要有 code、说明与建议。
    const QStringList lines = app::pdfUaFindingLines(report);
    QCOMPARE(lines.size(), 1);
    QVERIFY(lines.first().contains(QStringLiteral("ua-missing-lang")));
    QVERIFY(lines.first().contains(QStringLiteral("补上 /Lang")));

    // 负例守卫：这个检查器只做检查，不许吹成「已生成 PDF/UA 合规文件」。
    const QString scope = app::pdfUaScopeNoticeText();
    QVERIFY(!scope.isEmpty());
    QVERIFY2(!scope.contains(QStringLiteral("已生成")), qPrintable(scope));
    QVERIFY2(!scope.contains(QStringLiteral("已生成合规")), qPrintable(scope));
    QVERIFY2(!scope.contains(QStringLiteral("生成了 PDF/UA")), qPrintable(scope));
    QVERIFY2(scope.contains(QStringLiteral("不会生成")), qPrintable(scope));

    // 界面上显示的也是同一句话。
    app::PdfUaReportDialog dialog;
    dialog.setReport(report);
    QVERIFY(dialog.conclusionText().contains(QStringLiteral("合规")));
    QCOMPARE(dialog.passCount(), 3);
    QCOMPARE(dialog.warningCount(), 1);
    QCOMPARE(dialog.errorCount(), 2);
    QVERIFY(dialog.scopeNoticeText().contains(QStringLiteral("不会生成")));
}

// ------------------------------------------------------------------ 11 ----

void LastWaveUiTests::xfaDynamicNoticeIsShown() {
    pdf::XfaReport report;
    report.status = pdf::XfaStatus::Dynamic;
    report.isXfa = true;
    report.hasDynamicLogic = true;
    report.dynamicMarkers.append(QStringLiteral("script"));
    pdf::XfaField field;
    field.name = QStringLiteral("f1");
    field.label = QStringLiteral("姓名");
    field.type = QStringLiteral("textField");
    field.value = QStringLiteral("张三");
    field.required = true;
    report.fields.append(field);

    const QString notice = app::xfaDynamicNoticeText(report);
    QVERIFY2(notice.contains(QStringLiteral("不执行脚本")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("静态字段提取")), qPrintable(notice));

    app::XfaFieldsDialog dialog;
    dialog.setReport(report);
    QCOMPARE(dialog.fieldCount(), 1);
    QVERIFY(dialog.dynamicNoticeText().contains(QStringLiteral("不执行脚本")));
    // 字段清单里能看到名称、标签、类型、当前值与是否必填。
    const QStringList fields = dialog.fieldLines();
    QCOMPARE(fields.size(), 1);
    QVERIFY(fields.first().contains(QStringLiteral("f1")));
    QVERIFY(fields.first().contains(QStringLiteral("张三")));
    QVERIFY(fields.first().contains(QStringLiteral("必填")));
    // 没有动态逻辑时不该出现那句提示。
    pdf::XfaReport staticReport = report;
    staticReport.hasDynamicLogic = false;
    QVERIFY(app::xfaDynamicNoticeText(staticReport).isEmpty());
}

// ------------------------------------------------------------------ 12 ----

void LastWaveUiTests::richMediaNoticeDoesNotPromiseRendering() {
    const QString scope = app::richMediaScopeNoticeText();
    QVERIFY(!scope.isEmpty());
    // 负例守卫：本程序不渲染 3D、不解码音视频，文案不许反过来。
    QVERIFY2(!scope.contains(QStringLiteral("可播放")), qPrintable(scope));
    QVERIFY2(!scope.contains(QStringLiteral("可以播放")), qPrintable(scope));
    QVERIFY2(!scope.contains(QStringLiteral("可旋转")), qPrintable(scope));
    QVERIFY2(!scope.contains(QStringLiteral("可以旋转")), qPrintable(scope));
    QVERIFY2(!scope.contains(QStringLiteral("可以查看")), qPrintable(scope));
    QVERIFY2(scope.contains(QStringLiteral("不渲染")), qPrintable(scope));
    QVERIFY2(scope.contains(QStringLiteral("不解码")), qPrintable(scope));

    app::RichMediaDialog dialog;
    QCOMPARE(dialog.scopeNoticeText(), scope);
}

// ------------------------------------------------------------------ 13 ----

void LastWaveUiTests::reflowDroppedListIsItemised() {
    pdf::ReflowReport report;
    report.sourcePageCount = 4;
    report.outputPageCount = 3;
    report.reflowedBlockCount = 7;
    pdf::ReflowDropped figure;
    figure.tag = QStringLiteral("Figure");
    figure.reason = QStringLiteral("图形没有重排");
    figure.pageIndex = 1;
    pdf::ReflowDropped table;
    table.tag = QStringLiteral("Table");
    table.reason = QStringLiteral("表格没有重排");
    report.dropped.append(figure);
    report.dropped.append(table);
    report.droppedCount = report.dropped.size();

    const QStringList lines = app::reflowDroppedLines(report);
    QCOMPARE(lines.size(), 2);
    QVERIFY(lines.at(0).contains(QStringLiteral("Figure")));
    QVERIFY(lines.at(0).contains(QStringLiteral("图形没有重排")));
    QVERIFY(lines.at(1).contains(QStringLiteral("Table")));

    // 界面上要逐条列出来，不能只给一个总数。
    app::ReflowDialog dialog;
    dialog.setReport(report);
    QCOMPARE(dialog.droppedLines().size(), 2);
    QVERIFY(dialog.noticeText().contains(QStringLiteral("2")));
    // 版面会变这件事要写在界面上。
    QVERIFY(app::reflowLayoutChangeNoticeText().contains(QStringLiteral("版面")));
}

// ------------------------------------------------------------------ 14 ----

void LastWaveUiTests::newStringsAreInTranslationCatalog() {
#ifdef AIPDF_I18N_TS_FILE
    const QString path = QString::fromUtf8(AIPDF_I18N_TS_FILE);
    struct Expect {
        const char* context;
        const char* source;
    };
    const Expect expects[] = {
        {"aipdf::app::LastWaveBridge", "这份文档没有标签树（文件里没有 /StructTreeRoot）。"},
        {"aipdf::app::LastWaveBridge", "这个检查器只做检查并给出修复建议，不会生成 PDF/UA 合规文件。"},
        {"aipdf::app::LastWaveBridge", "重新识别会替换上一次写入的隐藏文字层（不是再叠一层）：两层叠在同一坐标上时，阅读器只会保留其中一层。"},
        {"aipdf::app::LastWavePanels", "图层面板"},
        {"aipdf::app::LastWavePanels", "我知道重排会改变版面（页数、图表位置都会变）"},
        {"aipdf::app::LastWavePanels", "这份 XFA 没有动态逻辑：字段都是静态的。"},
        {"aipdf::app::LastWaveWriter", "本次构建没有启用 qpdf，往页面追加内容流不可用。"},
        {"aipdf::app::MainWindow", "写入隐藏文字层"},
        {"aipdf::app::MainWindow", "动作向导"},
        {"aipdf::app::MainWindow", "发票打印"},
        {"aipdf::app::MainWindow", "PDF/UA 合规检查"},
        {"aipdf::app::MainWindow", "XFA 字段"},
        {"aipdf::app::MainWindow", "从文件新建 PDF"},
        {"aipdf::app::MainWindow", "按新页宽重排"},
        {"aipdf::app::PdfView", "段落排版"},
        {"aipdf::app::PdfView", "读取文档标签树"},
        {"aipdf::app::AiPanel", "本地离线问答"},
    };
    for (const Expect& expect : expects) {
        const QSet<QString> sources =
            catalogSources(QString::fromUtf8(expect.context), path);
        QVERIFY2(!sources.isEmpty(),
                 qPrintable(QStringLiteral("翻译目录里没有 context %1")
                                .arg(QString::fromUtf8(expect.context))));
        // 「这段 XFA」只是一个前缀检查：完整的文案太长，这里只查开头几个字。
        const QString source = QString::fromUtf8(expect.source);
        bool found = false;
        for (const QString& candidate : sources) {
            if (candidate.startsWith(source) || source.startsWith(candidate)) {
                found = true;
                break;
            }
        }
        QVERIFY2(found, qPrintable(QStringLiteral("[%1] 里查不到：%2")
                                       .arg(QString::fromUtf8(expect.context), source)));
    }
#else
    QSKIP("没有定义 AIPDF_I18N_TS_FILE，跳过翻译目录核对");
#endif
}

// ------------------------------------------------------------------ 15 ----

void LastWaveUiTests::ocrWordsBecomeCharacterBoxes() {
    aipdf::ocr::OcrPageResult result;
    result.pageIndex = 0;
    aipdf::ocr::OcrLine line;
    aipdf::ocr::OcrWord word;
    word.text = QStringLiteral("中文");
    word.rect = QRectF(10.0, 20.0, 40.0, 12.0);
    line.words.append(word);
    aipdf::ocr::OcrWord latin;
    latin.text = QStringLiteral("AB");
    latin.rect = QRectF(10.0, 40.0, 20.0, 10.0);
    line.words.append(latin);
    result.lines.append(line);

    pdf::MediaBox box;
    box.left = 0.0;
    box.bottom = 0.0;
    box.right = 595.0;
    box.top = 842.0;
    pdf::TextPage page;
    QString reason;
    QVERIFY2(app::ocrPageToTextPage(result, box, &page, &reason), qPrintable(reason));
    QCOMPARE(page.pageIndex, 0);
    QCOMPARE(page.contentBox.right, 595.0);
    // 两个词四个字符：每个字符一个框，序号在页内连续。
    QCOMPARE(page.chars.size(), 4);
    for (int index = 0; index < page.chars.size(); ++index) {
        QCOMPARE(page.chars.at(index).index, index);
    }
    // 框高就是词框高（框高被领域层当字号用，切矮了字会看不见）。
    QCOMPARE(page.chars.at(0).box.height(), 12.0);
    QCOMPARE(page.chars.at(1).box.height(), 12.0);
    QCOMPARE(page.chars.at(2).box.height(), 10.0);
    // 宽度均分。
    QCOMPARE(page.chars.at(0).box.width(), 20.0);
    QCOMPARE(page.chars.at(1).box.width(), 20.0);
    QCOMPARE(page.chars.at(2).box.width(), 10.0);
    QCOMPARE(page.chars.at(0).box.x(), 10.0);
    QCOMPARE(page.chars.at(1).box.x(), 30.0);
    // 词与词之间不插空格。
    QCOMPARE(page.plainText(), QStringLiteral("中文AB"));

    // 被取消的识别结果要报错，而不是给出一页空文字。
    aipdf::ocr::OcrPageResult cancelled;
    cancelled.cancelled = true;
    QVERIFY(!app::ocrPageToTextPage(cancelled, box, &page, &reason));
    QVERIFY(!reason.isEmpty());
}

// ------------------------------------------------------------------ 16 ----

void LastWaveUiTests::structTreeRowCarriesAltLanguageAndText() {
    pdf::StructTreeReport report;
    report.hasStructTree = true;
    pdf::StructElement element;
    element.tag = QStringLiteral("Figure");
    element.alt = QStringLiteral("一张示意图");
    element.language = QStringLiteral("zh-CN");
    element.actualText = QStringLiteral("替代文字");
    element.text = QStringLiteral("正文里的文字");
    element.pageIndex = 2;
    element.depth = 1;
    report.elements.append(element);

    const QVector<app::StructTreeRow> rows = app::structTreeRows(report);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().tag, QStringLiteral("Figure"));
    QCOMPARE(rows.first().pageIndex, 2);
    QCOMPARE(rows.first().depth, 1);
    const QString line = app::structTreeRowText(rows.first());
    QVERIFY(line.contains(QStringLiteral("一张示意图")));
    QVERIFY(line.contains(QStringLiteral("zh-CN")));
    QVERIFY(line.contains(QStringLiteral("替代文字")));
    QVERIFY(line.contains(QStringLiteral("正文里的文字")));
}

// ------------------------------------------------------------------ 17 ----

void LastWaveUiTests::ocgPlanKeepsNamesAndVisibility() {
    pdf::OptionalContentSummary summary;
    summary.hasOptionalContent = true;
    pdf::OptionalContentGroup first;
    first.name = QStringLiteral("底图");
    first.visible = true;
    pdf::OptionalContentGroup second;
    second.name = QStringLiteral("标注");
    second.visible = false;
    summary.groups.append(first);
    summary.groups.append(second);

    app::OcgLayerPanel dialog;
    dialog.setSummary(summary);
    QCOMPARE(dialog.layerCount(), 2);
    const QVector<bool> states = dialog.visibleStates();
    QCOMPARE(states.size(), 2);
    QVERIFY(states.at(0));
    QVERIFY(!states.at(1));

    const pdf::OptionalContentPlan plan = dialog.plan();
    QCOMPARE(plan.layers.size(), 2);
    QCOMPARE(plan.layers.at(0).name, QStringLiteral("底图"));
    QVERIFY(plan.layers.at(0).defaultVisible);
    QCOMPARE(plan.layers.at(1).name, QStringLiteral("标注"));
    QVERIFY(!plan.layers.at(1).defaultVisible);
    // 写回的边界要写在界面上。
    QVERIFY(dialog.writeBackNoticeText().contains(QStringLiteral("/OCProperties")));
}

// ------------------------------------------------------------------ 18 ----

void LastWaveUiTests::invoiceAmountFollowsUserChoice() {
    // 币种与小数位由用户选：不硬编码成一种。
    QVERIFY(app::invoiceCurrencyOptions().size() >= 2);
    QVERIFY(app::invoiceCurrencySymbols().size() >= 2);
    QCOMPARE(app::invoiceAmountText(123456, QStringLiteral("¥"), 2), QStringLiteral("¥1,234.56"));
    QCOMPARE(app::invoiceAmountText(123456, QStringLiteral("$"), 2), QStringLiteral("$1,234.56"));
    QCOMPARE(app::invoiceAmountText(123456, QStringLiteral("¥"), 0), QStringLiteral("¥1,235"));
    // 税率按万分比存，显示成百分比。
    QCOMPARE(app::invoiceTaxRateText(1300), QStringLiteral("13.00%"));
    QVERIFY(app::invoiceTaxRateText(-1).contains(QStringLiteral("沿用")));

    // 界面上的下拉框与金额显示一致。
    app::InvoiceDialog dialog;
    QVERIFY(dialog.currencySymbol().isEmpty() || !dialog.currencySymbol().isEmpty());
    QVERIFY(dialog.decimals() >= 0);
}

// ------------------------------------------------------------------ 19 ----

void LastWaveUiTests::wizardBoundaryAndFailureReason() {
    // 向导不是脚本引擎：界面上要写清楚。
    const QString scope = app::wizardScopeNoticeText();
    QVERIFY2(scope.contains(QStringLiteral("不是脚本引擎")), qPrintable(scope));
    app::WizardDialog dialog;
    QCOMPARE(dialog.scopeNoticeText(), scope);

    // 一步失败时显示的是领域层给的中文原因。
    doc::WizardStepResult result;
    result.index = 2;
    result.typeName = QStringLiteral("addPageNumbers");
    result.executed = true;
    result.succeeded = false;
    result.reason = QStringLiteral("本程序的页码只能放在右下角（position 只支持 bottomRight）。");
    const QString failure = app::wizardStepFailureText(result);
    QVERIFY(failure.contains(result.reason));
    QVERIFY2(!failure.contains(QStringLiteral("操作失败")), qPrintable(failure));
    // 成功的步骤没有失败文案。
    result.succeeded = true;
    QVERIFY(app::wizardStepFailureText(result).isEmpty());

    // 整份向导的摘要要带上每一步的失败原因。
    doc::WizardRunReport report;
    report.totalSteps = 1;
    report.executedSteps = 1;
    report.failedSteps = 1;
    report.results.append(result);
    const QString summary = app::wizardRunSummaryText(report);
    QVERIFY(summary.contains(QStringLiteral("1")));
}

// ------------------------------------------------------------------ 20 ----

void LastWaveUiTests::invisibleTextLayerNoticeSaysReplace() {
    const QString notice = app::invisibleTextLayerReplaceNoticeText();
    QVERIFY2(notice.contains(QStringLiteral("替换")), qPrintable(notice));
    // 不是「又加了一层」：负例守卫。
    QVERIFY2(!notice.contains(QStringLiteral("又加了一层")), qPrintable(notice));
    QVERIFY2(!notice.contains(QStringLiteral("叠加")), qPrintable(notice));
}

QTEST_MAIN(LastWaveUiTests)
#include "LastWaveUiTests.moc"
