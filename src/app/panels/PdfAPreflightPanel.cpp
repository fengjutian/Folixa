#include "app/panels/PdfAPreflightPanel.h"

#include "i18n/Translate.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {

// 结论分组的界面顺序：先看问题，再看警告，最后才是通过的。
//
// 为什么通过的排在最后：预检的价值在于「离合规还差什么」，
// 几十条「通过」挡在前面会把真正的问题埋掉。
QString severityGroupTitle(pdf::PdfAPreflightSeverity severity) {
    switch (severity) {
        case pdf::PdfAPreflightSeverity::Error:
            return i18n::tr("aipdf::app::PdfAPreflightPanel", "不通过");
        case pdf::PdfAPreflightSeverity::Warning:
            return i18n::tr("aipdf::app::PdfAPreflightPanel", "警告");
        case pdf::PdfAPreflightSeverity::Pass:
            break;
    }
    return i18n::tr("aipdf::app::PdfAPreflightPanel", "通过");
}

}  // namespace

QString pdfAPreflightCodeTitle(const QString& code) {
    // 这张表只做「码 → 中文标题」一件事。
    //
    // 为什么不用领域层给的 ASCII title 直接显示：界面是中文优先的，
    // 用户看不懂 "missing output intent" 这种说法；
    // 但领域层不能放中文（仓库硬约束），所以映射放在这里。
    if (code == QLatin1String("a-empty-document")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "文档是空的");
    }
    if (code == QLatin1String("a-not-a-pdf")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "不是 PDF 文件");
    }
    if (code == QLatin1String("a-qpdf-unavailable")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "本次构建没有启用 qpdf");
    }
    if (code == QLatin1String("a-unreadable-document")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "文档读不出来");
    }
    if (code == QLatin1String("a-pdf-version-ok")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "文件头版本符合要求");
    }
    if (code == QLatin1String("a-pdf-version-unsupported")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "文件头版本不是 %PDF-1.7");
    }
    if (code == QLatin1String("a-file-id-ok")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "trailer 里有 /ID");
    }
    if (code == QLatin1String("a-missing-file-id")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "trailer 里缺 /ID");
    }
    if (code == QLatin1String("a-document-not-encrypted")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "文档没有加密");
    }
    if (code == QLatin1String("a-document-encrypted")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "文档被加密了");
    }
    if (code == QLatin1String("a-missing-xmp-metadata")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "缺少 XMP 元数据");
    }
    if (code == QLatin1String("a-xmp-metadata-invalid")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "XMP 元数据的类型不对");
    }
    if (code == QLatin1String("a-xmp-missing-pdfaid")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "XMP 里没有声明 PDF/A");
    }
    if (code == QLatin1String("a-xmp-wrong-part")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "XMP 声明的 PDF/A 部分不对");
    }
    if (code == QLatin1String("a-xmp-wrong-conformance")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "XMP 声明的一致级别不对");
    }
    if (code == QLatin1String("a-xmp-pdfaid-ok")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "XMP 声明正确");
    }
    if (code == QLatin1String("a-missing-output-intent")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "缺少输出意图（OutputIntent）");
    }
    if (code == QLatin1String("a-output-intent-wrong-subtype")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "输出意图的类型不对");
    }
    if (code == QLatin1String("a-output-intent-icc-missing")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "输出意图里没有嵌入 ICC 配置文件");
    }
    if (code == QLatin1String("a-output-intent-icc-invalid")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "嵌入的 ICC 配置文件不合要求");
    }
    if (code == QLatin1String("a-output-intent-ok")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "输出意图符合要求");
    }
    if (code == QLatin1String("a-fonts-embedded")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "字体全部内嵌");
    }
    if (code == QLatin1String("a-font-not-embedded")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "有字体没有内嵌");
    }
    if (code == QLatin1String("a-no-lzw-compression")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "没有用 LZW 压缩");
    }
    if (code == QLatin1String("a-lzw-compression")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "用了 LZW 压缩");
    }
    if (code == QLatin1String("a-no-external-file-reference")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "没有外部文件引用");
    }
    if (code == QLatin1String("a-external-file-reference")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "有外部文件引用");
    }
    if (code == QLatin1String("a-no-javascript")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "没有 JavaScript 动作");
    }
    if (code == QLatin1String("a-javascript-action")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "有 JavaScript 动作");
    }
    if (code == QLatin1String("a-colorspaces-ok")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "色彩空间符合要求");
    }
    if (code == QLatin1String("a-colorspace-not-allowed")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "用了不能用的色彩空间");
    }
    if (code == QLatin1String("a-no-transparency")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "没有透明对象");
    }
    if (code == QLatin1String("a-transparency-allowed")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "透明对象符合要求");
    }
    if (code == QLatin1String("a-transparency-without-output-intent")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "有透明对象但没有输出意图");
    }
    if (code == QLatin1String("a-transparency-without-page-group")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "有透明对象但页面缺透明组");
    }
    if (code == QLatin1String("a-softmask-tr-not-identity")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "软掩码的 /TR 不是 /Identity");
    }
    if (code == QLatin1String("a-annotations-ok")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "注解符合要求");
    }
    if (code == QLatin1String("a-forbidden-annotation")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "用了 PDF/A 禁止的注解");
    }
    if (code == QLatin1String("a-annotation-missing-appearance")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "注解缺外观流");
    }
    if (code == QLatin1String("a-annotation-flags")) {
        return i18n::tr("aipdf::app::PdfAPreflightPanel", "注解的标志位不对");
    }
    // 认不出来的码原样显示，不编一句。
    return code;
}

QString pdfAPreflightFindingLine(const pdf::PdfAPreflightFinding& finding) {
    QString line = QStringLiteral("[%1] %2：%3")
                       .arg(finding.clause.isEmpty() ? QStringLiteral("-") : finding.clause,
                            pdfAPreflightCodeTitle(finding.code), finding.detail);
    if (!finding.suggestion.isEmpty()) {
        line += i18n::tr("aipdf::app::PdfAPreflightPanel", " 建议：%1").arg(finding.suggestion);
    }
    return line;
}

QString pdfAPreflightConclusionText(const pdf::PdfAPreflightReport& report) {
    // 措辞是刻意的：**只说「预检发现 N 个问题」**。
    //
    // 为什么不能说「已通过 PDF/A 校验」：预检只看得到文件结构里能读出来的那部分要求，
    // 认证结论要 veraPDF 给。这两件事混淆过一次就会让用户拿「预检通过」当合规证明。
    QString text = i18n::tr("aipdf::app::PdfAPreflightPanel",
                            "PDF/A-2b 预检发现 %1 个问题（通过 %2 项、警告 %3 项、不通过 %4 项）。")
                        .arg(report.issueCount())
                        .arg(report.passCount)
                        .arg(report.warningCount)
                        .arg(report.errorCount);
    if (!report.declaredPart.isEmpty() || !report.declaredConformance.isEmpty()) {
        text += QLatin1Char('\n') +
                i18n::tr("aipdf::app::PdfAPreflightPanel", "文件自己声明的是 PDF/A-%1%2。")
                    .arg(report.declaredPart.isEmpty() ? QStringLiteral("?") : report.declaredPart,
                         report.declaredConformance.isEmpty() ? QString()
                                                             : QStringLiteral("-") + report.declaredConformance);
    }
    return text;
}

QString pdfAPreflightScopeNoticeText() {
    // 领域层给的那句 ASCII 说明是技术原文，这里当技术细节附在中文说明后面。
    return i18n::tr("aipdf::app::PdfAPreflightPanel",
                    "这是预检，不是合规认证：它只检查文件结构里能读出来的那些要求。")
           + QStringLiteral(" (") + pdf::pdfAPreflightDisclaimer() + QStringLiteral(")");
}

PdfAPreflightDialog::PdfAPreflightDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("pdfA.preflightDialog"));
    setWindowTitle(i18n::tr("aipdf::app::PdfAPreflightPanel", "PDF/A-2b 预检"));
    resize(960, 620);

    auto* layout = new QVBoxLayout(this);

    conclusionLabel_ = new QLabel(this);
    conclusionLabel_->setObjectName(QStringLiteral("pdfA.preflight.conclusion"));
    conclusionLabel_->setAccessibleName(i18n::tr("aipdf::app::PdfAPreflightPanel", "预检结论"));
    conclusionLabel_->setWordWrap(true);
    layout->addWidget(conclusionLabel_);

    scopeLabel_ = new QLabel(this);
    scopeLabel_->setObjectName(QStringLiteral("pdfA.preflight.scope"));
    scopeLabel_->setAccessibleName(i18n::tr("aipdf::app::PdfAPreflightPanel", "预检能力说明"));
    scopeLabel_->setWordWrap(true);
    layout->addWidget(scopeLabel_);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("pdfA.preflight.tree"));
    tree_->setAccessibleName(i18n::tr("aipdf::app::PdfAPreflightPanel", "预检结论明细"));
    tree_->setColumnCount(5);
    tree_->setHeaderLabels({i18n::tr("aipdf::app::PdfAPreflightPanel", "码"),
                             i18n::tr("aipdf::app::PdfAPreflightPanel", "条款"),
                             i18n::tr("aipdf::app::PdfAPreflightPanel", "标题"),
                             i18n::tr("aipdf::app::PdfAPreflightPanel", "说明"),
                             i18n::tr("aipdf::app::PdfAPreflightPanel", "建议")});
    tree_->header()->setStretchLastSection(true);
    layout->addWidget(tree_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    scopeLabel_->setText(pdfAPreflightScopeNoticeText());
}

void PdfAPreflightDialog::setReport(const pdf::PdfAPreflightReport& report) {
    report_ = report;
    rebuild();
}

void PdfAPreflightDialog::rebuild() {
    conclusionLabel_->setText(pdfAPreflightConclusionText(report_));
    scopeLabel_->setText(pdfAPreflightScopeNoticeText());
    tree_->clear();

    // 三组：不通过 / 警告 / 通过。
    //
    // 为什么用分组而不是平铺几十行：预检一次能出四十来条结论，
    // 平铺的话用户要自己扫，分组之后「不通过」那组一眼就能看完。
    QTreeWidgetItem* errorGroup = new QTreeWidgetItem(tree_);
    errorGroup->setText(0, severityGroupTitle(pdf::PdfAPreflightSeverity::Error));
    errorGroup->setText(2, QStringLiteral("%1").arg(report_.errorCount));
    QTreeWidgetItem* warningGroup = new QTreeWidgetItem(tree_);
    warningGroup->setText(0, severityGroupTitle(pdf::PdfAPreflightSeverity::Warning));
    warningGroup->setText(2, QStringLiteral("%1").arg(report_.warningCount));
    QTreeWidgetItem* passGroup = new QTreeWidgetItem(tree_);
    passGroup->setText(0, severityGroupTitle(pdf::PdfAPreflightSeverity::Pass));
    passGroup->setText(2, QStringLiteral("%1").arg(report_.passCount));

    for (const pdf::PdfAPreflightFinding& finding : report_.findings) {
        QTreeWidgetItem* group = nullptr;
        switch (finding.severity) {
            case pdf::PdfAPreflightSeverity::Error:
                group = errorGroup;
                break;
            case pdf::PdfAPreflightSeverity::Warning:
                group = warningGroup;
                break;
            case pdf::PdfAPreflightSeverity::Pass:
                group = passGroup;
                break;
        }
        auto* item = new QTreeWidgetItem(group);
        item->setText(0, finding.code);
        item->setText(1, finding.clause);
        item->setText(2, pdfAPreflightCodeTitle(finding.code));
        item->setText(3, finding.detail);
        item->setText(4, finding.suggestion);
    }
    tree_->expandAll();
}

QString PdfAPreflightDialog::conclusionText() const {
    return pdfAPreflightConclusionText(report_);
}

QString PdfAPreflightDialog::scopeNoticeText() const {
    return pdfAPreflightScopeNoticeText();
}

QStringList PdfAPreflightDialog::findingLines() const {
    QStringList lines;
    for (const pdf::PdfAPreflightFinding& finding : report_.findings) {
        lines.append(pdfAPreflightFindingLine(finding));
    }
    return lines;
}

int PdfAPreflightDialog::passCount() const {
    return report_.passCount;
}

int PdfAPreflightDialog::warningCount() const {
    return report_.warningCount;
}

int PdfAPreflightDialog::errorCount() const {
    return report_.errorCount;
}

}  // namespace aipdf::app
