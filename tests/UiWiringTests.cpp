#include "app/AnnotationToolboxDialog.h"
#include "app/ConversionReports.h"
#include "app/ConversionToolsDialog.h"
#include "app/PrintToolsDialog.h"
#include "app/RibbonBar.h"
#include "pdf/AnnotationToolkit.h"
#include "pdf/OcrSuspicion.h"
#include "pdf/PortfolioWriter.h"
#include "pdf/PreflightChecker.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QRectF>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

using aipdf::app::CalculatorDialog;
using aipdf::app::OcrSuspicionDialog;
using aipdf::app::PreflightDialog;
using aipdf::app::RibbonBar;
using aipdf::app::SymbolPanelDialog;

namespace doc = aipdf::doc;
namespace pdf = aipdf::pdf;
namespace app = aipdf::app;

namespace {

// 这一批新接线的入口都在 Ribbon 上；这里按主窗口用的名字重建一遍，
// 用来逐个断言「每个动作按钮都有无障碍名」。
struct RibbonCommandSpec {
    const char* label;
    const char* objectName;
};

const RibbonCommandSpec kNewCommands[] = {
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
    {"数字图章", "annotation.numberstamp"},
    {"保持工具选择", "annotation.keepTool"},
    {"截图", "home.screenshot"},
    {"沉浸阅读", "view.immersiveReading"},
    {"手型", "home.panTool"},
    {"手型", "view.panTool"},
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
QSet<QString> catalogSources(const QString& context) {
    QSet<QString> sources;
    QFile file(context);
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
        if (match.captured(1) != QStringLiteral("aipdf::app::MainWindow")) {
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

}  // namespace

class UiWiringTests final : public QObject {
    Q_OBJECT

private slots:
    // 1) 每个动作按钮都有非空 accessibleName（逐个断言）。
    void everyRibbonCommandHasAccessibleName();
    // 2) accessibleName 里不含助记符标记。
    void accessibleNamesHaveNoMnemonicMarker();
    // 3) 助记符解析：单个 '&' 是标记，"&&" 是字面量。
    void mnemonicParsing();
    // 4) 新增 objectName 全仓唯一。
    void newObjectNamesAreUnique();
    // 5) 连续批注模式的状态与 ContinuousAnnotationMode 一致。
    void continuousAnnotationModeFollowsDomainState();
    // 6) 计算器失败路径：显示原因、批注文本为空。
    void calculatorFailurePath();
    // 7) 符号面板的条目数与领域层目录一致。
    void symbolPanelCountsMatchCatalog();
    // 8) 印前检查按 severity 分组，条目数与报告一致。
    void preflightGroupsMatchReport();
    // 9) 活页夹条目名与 readPortfolio 的结果一一对应。
    void portfolioRowsMatchReadResult();
    // 10) 疑似错误只是提示：原文不会被改写。
    void suspicionDoesNotRewriteSource();
    // 11) 转曲文案不许宣称已经变成矢量或轮廓。
    void flattenNoticeDoesNotClaimVectorOutput();
    // 12) 新增的用户可见文案都在翻译目录里。
    void newStringsAreInTranslationCatalog();
    // 13) Ribbon 上的助记键唯一（Alt+同一键不能指向两个按钮）。
    void ribbonMnemonicsAreUnique();
    // 14) 自定义纸张时方向控件置灰。
    void customPaperDisablesOrientation();
    // 15) 打印模板的校验失败原因要显示出来。
    void printTemplateValidationReportsReason();
    // 16) 批量打印的总页数恒为未知，界面上不能显示假数字。
    void printBatchTotalPagesStaysUnknown();
    // 17) 导出到 Office 的说明：要说清导了什么、不保什么。
    void officeExportNoticeStatesWhatIsKept();
};

// ---------------------------------------------------------------- 1 / 2 ----

void UiWiringTests::everyRibbonCommandHasAccessibleName() {
    RibbonBar ribbon;
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

void UiWiringTests::accessibleNamesHaveNoMnemonicMarker() {
    RibbonBar ribbon;
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

void UiWiringTests::mnemonicParsing() {
    // 单个 '&' 是助记符标记：它后面的那个字符就是助记键。
    const QChar chineseKey = QStringLiteral("打").at(0);
    QCOMPARE(RibbonBar::mnemonicKey(QStringLiteral("&打开")), chineseKey);
    QCOMPARE(RibbonBar::mnemonicKey(QStringLiteral("保存(&S)")), QChar('S'));
    // "&&" 表示字面量 '&'，不算助记符。
    QVERIFY(RibbonBar::mnemonicKey(QStringLiteral("A && B")).isNull());
    QCOMPARE(RibbonBar::stripMnemonics(QStringLiteral("&打开")), QStringLiteral("打开"));
    QCOMPARE(RibbonBar::stripMnemonics(QStringLiteral("保存(&S)")), QStringLiteral("保存(S)"));
    QCOMPARE(RibbonBar::stripMnemonics(QStringLiteral("A && B")), QStringLiteral("A & B"));
    QCOMPARE(RibbonBar::insertMnemonic(QStringLiteral("打开"), chineseKey),
             QStringLiteral("&打开"));
}

// ------------------------------------------------------------------- 4 ----

void UiWiringTests::newObjectNamesAreUnique() {
    QSet<QString> names;
    for (const RibbonCommandSpec& spec : kNewCommands) {
        const QString name = QString::fromUtf8(spec.objectName);
        QVERIFY2(!names.contains(name), qPrintable(name));
        names.insert(name);
    }
    RibbonBar ribbon;
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

// ------------------------------------------------------------------- 5 ----

void UiWiringTests::continuousAnnotationModeFollowsDomainState() {
    // 开关打开 → 领域层记着"有工具挂着"；关掉 → 不再挂着。
    pdf::ContinuousAnnotationMode mode;
    QVERIFY(!mode.isActive());
    mode.activate(QStringLiteral("note"), true);
    QVERIFY(mode.isActive());
    QCOMPARE(mode.activeTool(), QStringLiteral("note"));
    QVERIFY(mode.repeatsLastPlacement());
    // 放完一个批注：连续模式下工具保持激活。
    QVERIFY(mode.afterPlacement());
    QVERIFY(mode.isActive());
    mode.deactivate();
    QVERIFY(!mode.isActive());
    QVERIFY(mode.activeTool().isEmpty());

    // 「放完就退出」的工具：放一次就退出连续模式。
    mode.activate(QStringLiteral("ink"), false);
    QVERIFY(mode.isActive());
    QVERIFY(!mode.repeatsLastPlacement());
    QVERIFY(!mode.afterPlacement());
    QVERIFY(!mode.isActive());

    // Ribbon 上确实有这个开关。
    RibbonBar ribbon;
    const int tab = ribbon.addTab(QStringLiteral("注释"));
    auto* group = ribbon.addGroup(tab, QStringLiteral("工具箱"));
    QToolButton* button = ribbon.addCommand(group, QStringLiteral("连续批注"), QIcon(),
                                            QStringLiteral("连续批注"), true,
                                            QStringLiteral("annotation.continuous"));
    QVERIFY(button != nullptr);
    QCOMPARE(button->objectName(), QStringLiteral("annotation.continuous"));
    QVERIFY(!button->accessibleName().isEmpty());
}

// ------------------------------------------------------------------- 6 ----

void UiWiringTests::calculatorFailurePath() {
    CalculatorDialog dialog;
    dialog.setExpression(QStringLiteral("1+"));
    // 失败原因要给用户看。
    QVERIFY(!dialog.errorText().isEmpty());
    // 失败时不能有能贴进批注的文本（错误不进文档）。
    QVERIFY(dialog.annotationText().isEmpty());
    QVERIFY(dialog.normalizedExpression().isEmpty());
    // 成功路径：算式与结果都在。
    dialog.setExpression(QStringLiteral("100+10%"));
    QVERIFY(dialog.errorText().isEmpty());
    QCOMPARE(dialog.normalizedExpression(), QStringLiteral("100+10%"));
    QCOMPARE(dialog.annotationText(), QStringLiteral("100+10% = 110"));
}

// ------------------------------------------------------------------- 7 ----

void UiWiringTests::symbolPanelCountsMatchCatalog() {
    SymbolPanelDialog dialog;
    const QVector<pdf::SymbolCategory> categories = dialog.categories();
    QVERIFY(!categories.isEmpty());
    for (pdf::SymbolCategory category : categories) {
        dialog.setCategory(category);
        const QVector<pdf::SymbolEntry> entries = pdf::symbolCatalog(category);
        QCOMPARE(dialog.symbolButtonCount(), entries.size());
        QCOMPARE(dialog.currentEntries().size(), entries.size());
    }
    // 插符号：按领域层的规则插到光标处，插完面板还开着。
    dialog.setCategory(pdf::SymbolCategory::Currency);
    const QVector<pdf::SymbolEntry> entries = pdf::symbolCatalog(pdf::SymbolCategory::Currency);
    QVERIFY(!entries.isEmpty());
    dialog.setTargetText(QStringLiteral("金额"));
    dialog.setTargetCursorPosition(2);
    const QString updated = dialog.insertSymbolAt(0);
    QCOMPARE(updated.size(), dialog.targetText().size());
    QVERIFY(dialog.targetText().startsWith(QStringLiteral("金额")));
    QVERIFY(dialog.targetText().contains(entries.first().symbol));
}

// ------------------------------------------------------------------- 8 ----

void UiWiringTests::preflightGroupsMatchReport() {
    pdf::PreflightReport report;
    pdf::PreflightFinding error;
    error.code = QStringLiteral("font-not-embedded");
    error.title = QStringLiteral("字体内嵌");
    error.detail = QStringLiteral("正文用了一款没内嵌的字体");
    error.severity = pdf::PreflightSeverity::Error;
    error.pageIndex = 2;
    pdf::PreflightFinding warning;
    warning.code = QStringLiteral("low-resolution");
    warning.title = QStringLiteral("分辨率偏低");
    warning.severity = pdf::PreflightSeverity::Warning;
    warning.pageIndex = 0;
    pdf::PreflightFinding info;
    info.code = QStringLiteral("transparent-group");
    info.title = QStringLiteral("存在透明组");
    info.severity = pdf::PreflightSeverity::Info;
    info.pageIndex = -1;
    report.findings = {error, warning, info, warning};
    report.errorCount = 1;
    report.warningCount = 2;
    report.ok = false;

    PreflightDialog dialog;
    dialog.setReport(report);
    QCOMPARE(dialog.errorCount(), 1);
    QCOMPARE(dialog.warningCount(), 2);
    QCOMPARE(dialog.infoCount(), 1);
    QCOMPARE(dialog.displayedTotal(), report.findings.size());
    // 数量以领域层给的为准，界面不重算。
    QCOMPARE(dialog.domainErrorCount(), report.errorCount);
    QCOMPARE(dialog.domainWarningCount(), report.warningCount);
    // 结论里要说清楚能不能送印。
    QVERIFY(dialog.conclusionText().contains(QStringLiteral("不可送印")));
}

// ------------------------------------------------------------------- 9 ----

void UiWiringTests::portfolioRowsMatchReadResult() {
    QVector<pdf::PortfolioEntry> entries;
    pdf::PortfolioEntry first;
    first.name = QStringLiteral("合同.pdf");
    first.bytes = QByteArray("%PDF-1.7\nfirst");
    first.description = QStringLiteral("第一份");
    pdf::PortfolioEntry second;
    second.name = QStringLiteral("发票.pdf");
    second.bytes = QByteArray("%PDF-1.7\nsecond");
    second.description = QStringLiteral("第二份");
    entries << first << second;

    // 界面层的映射先用一份构造出来的读回结果核对：
    // 这一段不依赖 qpdf，任何构建里都必须过。
    pdf::PortfolioReadResult synthetic;
    synthetic.ok = true;
    synthetic.entries = entries;
    const QVector<app::PortfolioEntryRow> syntheticRows =
        app::portfolioEntryRows(synthetic);
    QCOMPARE(syntheticRows.size(), synthetic.entries.size());
    for (int index = 0; index < syntheticRows.size(); ++index) {
        QCOMPARE(syntheticRows.at(index).name, synthetic.entries.at(index).name);
        QCOMPARE(syntheticRows.at(index).description, synthetic.entries.at(index).description);
        QCOMPARE(syntheticRows.at(index).sizeBytes, synthetic.entries.at(index).bytes.size());
        QVERIFY(!syntheticRows.at(index).sizeText().isEmpty());
    }

#ifdef AIPDF_WITH_QPDF
    // 真正写一个包再读回来：写入与读取都要 qpdf。
    pdf::PortfolioOptions options;
    options.title = QStringLiteral("一批材料");
    options.author = QStringLiteral("验收");
    QString error;
    const QByteArray bytes = pdf::writePortfolio(entries, options, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    const pdf::PortfolioReadResult result = pdf::readPortfolio(bytes);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.entries.size(), entries.size());

    const QVector<app::PortfolioEntryRow> rows =
        app::portfolioEntryRows(result);
    QCOMPARE(rows.size(), result.entries.size());
    for (int index = 0; index < rows.size(); ++index) {
        QCOMPARE(rows.at(index).name, result.entries.at(index).name);
        QCOMPARE(rows.at(index).description, result.entries.at(index).description);
        QCOMPARE(rows.at(index).sizeBytes, result.entries.at(index).bytes.size());
        QVERIFY(!rows.at(index).sizeText().isEmpty());
    }
#endif
}

// ------------------------------------------------------------------ 10 ----

void UiWiringTests::suspicionDoesNotRewriteSource() {
    // 造一页带文本层的页面：数字串里混进一个字母。
    pdf::TextPage page;
    page.pageIndex = 0;
    pdf::TextChar ch;
    ch.text = QStringLiteral("1");
    ch.box = QRectF(0, 0, 5, 10);
    page.chars << ch;
    ch.text = QStringLiteral("O");
    ch.box = QRectF(5, 0, 10, 10);
    page.chars << ch;
    ch.text = QStringLiteral("2");
    ch.box = QRectF(10, 0, 15, 10);
    page.chars << ch;

    const QString before = page.plainText();
    const QVector<pdf::SuspiciousChar> items =
        pdf::findSuspiciousCharacters({page}, QString());
    // 原文一个字都不能被改：这是提示，不是自动替换。
    QCOMPARE(page.plainText(), before);
    QCOMPARE(page.chars.size(), 3);

    OcrSuspicionDialog dialog;
    dialog.setSuspicious(items);
    QCOMPARE(dialog.rowCount(), items.size());
    // 状态行要说清楚「不会自动改」。
    QVERIFY(dialog.statusText().contains(QStringLiteral("不会")));
}

// ------------------------------------------------------------------ 11 ----

void UiWiringTests::flattenNoticeDoesNotClaimVectorOutput() {
    const QString notice = app::flattenFontsNoticeText();
    QVERIFY(!notice.isEmpty());
    // 负例守卫：转曲是视觉等价的栅格化版本，文案不许吹成矢量或轮廓。
    QVERIFY2(!notice.contains(QStringLiteral("矢量")),
             qPrintable(QStringLiteral("转曲文案里出现了「矢量」：%1").arg(notice)));
    QVERIFY2(!notice.contains(QStringLiteral("轮廓")),
             qPrintable(QStringLiteral("转曲文案里出现了「轮廓」：%1").arg(notice)));
    // 要说清楚代价：文字不能再选、文件会变大。
    QVERIFY(notice.contains(QStringLiteral("栅格")));
}

// ------------------------------------------------------------------ 12 ----

void UiWiringTests::newStringsAreInTranslationCatalog() {
#ifdef AIPDF_I18N_TS_FILE
    const QSet<QString> sources = catalogSources(QStringLiteral(AIPDF_I18N_TS_FILE));
    QVERIFY2(!sources.isEmpty(), "翻译目录里没有 aipdf::app::MainWindow 这个 context");
    // 每个新面板都要有一条能读到的文案。
    const QStringList samples = {
        QStringLiteral("新建文档"),
        QStringLiteral("批量打印"),
        QStringLiteral("打印模板"),
        QStringLiteral("会计计算器"),
        QStringLiteral("符号面板"),
        QStringLiteral("印前检查"),
        QStringLiteral("PDF 文件包"),
        QStringLiteral("OCR 疑似错误"),
        QStringLiteral("导出 HTML"),
        QStringLiteral("图像化 PDF"),
        QStringLiteral("到 OFD"),
        QStringLiteral("到 MS Office"),
        QStringLiteral("到 CAD"),
        QStringLiteral("PDF 转曲"),
        QStringLiteral("文生图"),
        QStringLiteral("总结配图"),
        QStringLiteral("生成 PPT"),
        QStringLiteral("内容检测"),
        QStringLiteral("智能朗读"),
        QStringLiteral("连续批注"),
        QStringLiteral("手型"),
    };
    for (const QString& sample : samples) {
        QVERIFY2(sources.contains(sample), qPrintable(sample));
    }
#else
    QSKIP("没有定义 AIPDF_I18N_TS_FILE，跳过翻译目录核对");
#endif
}

// ------------------------------------------------------------------ 13 ----

void UiWiringTests::ribbonMnemonicsAreUnique() {
    RibbonBar ribbon;
    const int tab = ribbon.addTab(QStringLiteral("测试"));
    auto* group = ribbon.addGroup(tab, QStringLiteral("命令"));
    for (const RibbonCommandSpec& spec : kNewCommands) {
        ribbon.addCommand(group, QString::fromUtf8(spec.label), QIcon(),
                          QString::fromUtf8(spec.label), true,
                          QString::fromUtf8(spec.objectName));
    }
    QSet<QString> keys;
    for (QToolButton* button : ribbon.commandButtons()) {
        const QChar key = RibbonBar::mnemonicKey(button->text());
        QVERIFY2(!key.isNull(),
                 qPrintable(QStringLiteral("按钮 %1 没有助记符").arg(button->objectName())));
        const QString normalized = QString(key).toLower();
        QVERIFY2(!keys.contains(normalized),
                 qPrintable(QStringLiteral("助记键 %1 重复了").arg(normalized)));
        keys.insert(normalized);
    }
}

// ------------------------------------------------------------------ 14 ----

void UiWiringTests::customPaperDisablesOrientation() {
    aipdf::app::NewDocumentDialog dialog;
    // 默认 A4：方向可选。
    QVERIFY(dialog.orientationEnabled());
    // 自定义纸张：方向不生效，控件必须置灰。
    dialog.setPaper(doc::PageSize::Custom);
    QVERIFY(!dialog.orientationEnabled());
    QCOMPARE(dialog.selectedPaper(), doc::PageSize::Custom);
    dialog.setPaper(doc::PageSize::A4);
    QVERIFY(dialog.orientationEnabled());
    // 生成的文档必须是能用的 PDF。
    QString error;
    const QByteArray bytes = dialog.createDocument(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QVERIFY(bytes.startsWith("%PDF"));
}

// ------------------------------------------------------------------ 15 ----

void UiWiringTests::printTemplateValidationReportsReason() {
    doc::PrintTemplate invalid;
    invalid.name = QString();
    QString error;
    QVERIFY(!doc::validatePrintTemplate(invalid, &error));
    QVERIFY(!error.isEmpty());

    doc::PrintTemplate valid;
    valid.name = QStringLiteral("双面 A4");
    valid.copies = 2;
    valid.paperSize = QStringLiteral("A4");
    valid.scaling = QStringLiteral("FitToPage");
    valid.scalePercent = 100;
    QVERIFY(doc::validatePrintTemplate(valid, &error));

    // 模板存在固定的设置分组里，列得出、读得回、删得掉。
    const QString key = QStringLiteral("printTemplates");
    const QString id = QStringLiteral("ui-wiring-test-template");
    doc::PrintTemplate stored = valid;
    stored.id = id;
    QVERIFY2(doc::savePrintTemplate(stored, key, &error), qPrintable(error));
    const QVector<doc::PrintTemplate> list = doc::listPrintTemplates(key);
    bool found = false;
    for (const doc::PrintTemplate& value : list) {
        if (value.id == id) {
            found = true;
            QCOMPARE(value.name, valid.name);
        }
    }
    QVERIFY(found);
    QVERIFY(doc::deletePrintTemplate(id, key));
}

// ------------------------------------------------------------------ 16 ----

void UiWiringTests::printBatchTotalPagesStaysUnknown() {
    aipdf::app::PrintBatchDialog dialog;
    dialog.addFile(QStringLiteral("C:/不存在的文件.pdf"));
    // 领域层这一层不打开 PDF，读不到真实页数：恒为 -1。
    QCOMPARE(dialog.totalPages(), -1);
    QVERIFY(dialog.totalPagesText().contains(QStringLiteral("未知")));
    // 界面上不能显示一个假数字。
    QVERIFY(!dialog.totalPagesText().contains(QStringLiteral("-1")));
}

// ------------------------------------------------------------------ 17 ----

void UiWiringTests::officeExportNoticeStatesWhatIsKept() {
    const QString notice = aipdf::app::officeExportNoticeText(
        QStringLiteral("Excel 工作簿"), QStringLiteral("共 12 页、12 张工作表（认出 2 个表格）"));
    // 说了导的是什么、导了多少。
    QVERIFY2(notice.contains(QStringLiteral("Excel 工作簿")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("12")), qPrintable(notice));
    // 负例守卫：只含文字层——不许吹成「图片也带过去了」。
    QVERIFY2(!notice.contains(QStringLiteral("图片已")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("文字层")), qPrintable(notice));
}

QTEST_MAIN(UiWiringTests)
#include "UiWiringTests.moc"
