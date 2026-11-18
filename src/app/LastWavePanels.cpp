#include "app/LastWavePanels.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "app/SystemCjkFont.h"
#include "i18n/Translate.h"
#include "pdf/ParagraphFormat.h"

namespace aipdf::app {

namespace {

// 建一棵四列的结论树：码 / 标题 / 说明 / 建议。
QTreeWidget* createFindingTree(QWidget* parent, const QString& objectName) {
    auto* tree = new QTreeWidget(parent);
    tree->setObjectName(objectName);
    tree->setColumnCount(4);
    tree->setHeaderLabels({i18n::tr("aipdf::app::LastWavePanels", "码"), i18n::tr("aipdf::app::LastWavePanels", "标题"), i18n::tr("aipdf::app::LastWavePanels", "说明"), i18n::tr("aipdf::app::LastWavePanels", "建议")});
    tree->header()->setStretchLastSection(true);
    tree->setRootIsDecorated(false);
    return tree;
}

void fillFindingTree(QTreeWidget* tree, const QVector<pdf::PdfUaFinding>& findings) {
    if (tree == nullptr) {
        return;
    }
    tree->clear();
    for (const pdf::PdfUaFinding& finding : findings) {
        auto* item = new QTreeWidgetItem(tree);
        item->setText(0, finding.code);
        item->setText(1, finding.title);
        item->setText(2, finding.detail);
        item->setText(3, finding.suggestion);
    }
}

// 向导界面用的参数表：名字、类型、默认值、是否必填。
//
// 这里只说「这一步要收哪些参数」；取值范围与合法��由领域层的
// validateActionWizard 判定，界面不另写一套。
struct WizardParamField {
    QString name;
    doc::WizardParamType type = doc::WizardParamType::String;
    QString defaultValue;
    bool required = true;
};

QVector<WizardParamField> wizardParamFields(doc::WizardActionType type) {
    using doc::WizardParamType;
    switch (type) {
        case doc::WizardActionType::OpenFile:
            return {{QStringLiteral("path"), WizardParamType::String, QString(), true}};
        case doc::WizardActionType::MergeDocuments:
            return {{QStringLiteral("inputPaths"), WizardParamType::StringList, QString(), true},
                    {QStringLiteral("outputPath"), WizardParamType::String, QString(), true}};
        case doc::WizardActionType::SplitDocument:
            return {{QStringLiteral("outputDirectory"), WizardParamType::String, QString(), true},
                    {QStringLiteral("mode"), WizardParamType::String, QStringLiteral("range"), false},
                    {QStringLiteral("ranges"), WizardParamType::StringList, QString(), false}};
        case doc::WizardActionType::AddBookmark:
            return {{QStringLiteral("title"), WizardParamType::String, QString(), true},
                    {QStringLiteral("pageIndex"), WizardParamType::Integer, QStringLiteral("1"), false}};
        case doc::WizardActionType::AddPageNumbers:
            return {{QStringLiteral("position"), WizardParamType::String,
                     QStringLiteral("bottomCenter"), true},
                    {QStringLiteral("startNumber"), WizardParamType::Integer,
                     QStringLiteral("1"), false}};
        case doc::WizardActionType::AddWatermark:
            return {{QStringLiteral("text"), WizardParamType::String, QString(), true},
                    {QStringLiteral("opacity"), WizardParamType::Number,
                     QStringLiteral("0.15"), false},
                    {QStringLiteral("position"), WizardParamType::String,
                     QStringLiteral("center"), false}};
        case doc::WizardActionType::Encrypt:
            return {{QStringLiteral("userPassword"), WizardParamType::String, QString(), true},
                    {QStringLiteral("ownerPassword"), WizardParamType::String, QString(), true},
                    {QStringLiteral("allowPrinting"), WizardParamType::Boolean,
                     QStringLiteral("true"), false}};
        case doc::WizardActionType::ConvertToFormat:
            return {{QStringLiteral("format"), WizardParamType::String, QStringLiteral("pdf"), true},
                    {QStringLiteral("outputPath"), WizardParamType::String, QString(), true}};
        case doc::WizardActionType::Print:
            return {{QStringLiteral("printerName"), WizardParamType::String, QString(), false},
                    {QStringLiteral("copies"), WizardParamType::Integer, QStringLiteral("1"), false},
                    {QStringLiteral("paperSize"), WizardParamType::String, QString(), false},
                    {QStringLiteral("pageRange"), WizardParamType::String, QString(), false}};
        case doc::WizardActionType::End:
        case doc::WizardActionType::Unknown:
        default:
            return {};
    }
}

// 参数类型的中文名（界面上给用户看）。
QString wizardParamTypeName(doc::WizardParamType type) {
    switch (type) {
        case doc::WizardParamType::StringList:
            return i18n::tr("aipdf::app::LastWavePanels", "字符串列表");
        case doc::WizardParamType::Integer:
            return i18n::tr("aipdf::app::LastWavePanels", "整数");
        case doc::WizardParamType::Number:
            return i18n::tr("aipdf::app::LastWavePanels", "小数");
        case doc::WizardParamType::Boolean:
            return i18n::tr("aipdf::app::LastWavePanels", "布尔");
        case doc::WizardParamType::String:
        default:
            return i18n::tr("aipdf::app::LastWavePanels", "字符串");
    }
}

// 界面上的字符串 → 领域层的参数。
doc::WizardParam makeParam(const WizardParamField& field, const QString& value) {
    switch (field.type) {
        case doc::WizardParamType::StringList: {
            QStringList items;
            const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                items.append(part.trimmed());
            }
            return doc::wizardStringListParam(field.name, items);
        }
        case doc::WizardParamType::Integer:
            return doc::wizardIntegerParam(field.name, value.toLongLong());
        case doc::WizardParamType::Number:
            return doc::wizardNumberParam(field.name, value.toDouble());
        case doc::WizardParamType::Boolean:
            return doc::wizardBooleanParam(field.name,
                                           value.compare(QStringLiteral("false"), Qt::CaseInsensitive) != 0 &&
                                               value.trimmed() != QStringLiteral("否") &&
                                               value.trimmed() != QStringLiteral("0"));
        case doc::WizardParamType::String:
        default:
            return doc::wizardStringParam(field.name, value);
    }
}

// 参数的当前值（编辑表格用）。
QString paramValue(const doc::WizardParam& param) {
    switch (param.type) {
        case doc::WizardParamType::StringList:
            return param.listValue.join(QLatin1Char(','));
        case doc::WizardParamType::Integer:
            return QString::number(param.integerValue);
        case doc::WizardParamType::Number:
            return QString::number(param.numberValue);
        case doc::WizardParamType::Boolean:
            return param.booleanValue ? QStringLiteral("true") : QStringLiteral("false");
        case doc::WizardParamType::String:
        default:
            return param.stringValue;
    }
}

// 把一行「名称 / 类型 / 值」变成领域层的参数。
doc::WizardParam paramFromRow(const QString& name, const QString& typeName, const QString& value) {
    doc::WizardParamType type = doc::WizardParamType::String;
    if (typeName == i18n::tr("aipdf::app::LastWavePanels", "字符串列表")) {
        type = doc::WizardParamType::StringList;
    } else if (typeName == i18n::tr("aipdf::app::LastWavePanels", "整数")) {
        type = doc::WizardParamType::Integer;
    } else if (typeName == i18n::tr("aipdf::app::LastWavePanels", "小数")) {
        type = doc::WizardParamType::Number;
    } else if (typeName == i18n::tr("aipdf::app::LastWavePanels", "布尔")) {
        type = doc::WizardParamType::Boolean;
    }
    WizardParamField field;
    field.name = name;
    field.type = type;
    return makeParam(field, value);
}

}  // namespace

// ================================================================ 任务 L ====

StructTreeDialog::StructTreeDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("lastWave.structTreeDialog"));
    setWindowTitle(i18n::tr("aipdf::app::LastWavePanels", "文档标签树"));
    resize(900, 520);

    auto* layout = new QVBoxLayout(this);
    summaryLabel_ = new QLabel(this);
    summaryLabel_->setObjectName(QStringLiteral("lastWave.structTree.summary"));
    summaryLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "标签树摘要"));
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("lastWave.structTree.table"));
    tree_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "标签树元素"));
    tree_->setColumnCount(7);
    tree_->setHeaderLabels({i18n::tr("aipdf::app::LastWavePanels", "标签"), i18n::tr("aipdf::app::LastWavePanels", "页码"), i18n::tr("aipdf::app::LastWavePanels", "层级"), i18n::tr("aipdf::app::LastWavePanels", "替代文本"), i18n::tr("aipdf::app::LastWavePanels", "语言"),
                             i18n::tr("aipdf::app::LastWavePanels", "实际文字"), i18n::tr("aipdf::app::LastWavePanels", "正文")});
    tree_->header()->setStretchLastSection(true);
    layout->addWidget(tree_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void StructTreeDialog::setReport(const pdf::StructTreeReport& report) {
    report_ = report;
    rebuild();
}

void StructTreeDialog::rebuild() {
    summaryLabel_->setText(structTreeSummaryText(report_));
    tree_->clear();
    const QVector<StructTreeRow> rows = structTreeRows(report_);
    for (const StructTreeRow& row : rows) {
        auto* item = new QTreeWidgetItem(tree_);
        item->setText(0, row.tag);
        item->setText(1, row.pageIndex >= 0 ? QString::number(row.pageIndex + 1) : QString());
        item->setText(2, QString::number(row.depth));
        item->setText(3, row.alt);
        item->setText(4, row.language);
        item->setText(5, row.actualText);
        item->setText(6, row.text);
    }
}

QString StructTreeDialog::summaryText() const {
    return structTreeSummaryText(report_);
}

int StructTreeDialog::rowCount() const {
    return tree_->topLevelItemCount();
}

QString StructTreeDialog::emptyText() const {
    return structTreeEmptyText();
}

// ================================================================ 任务 M ====

OcgLayerPanel::OcgLayerPanel(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("lastWave.ocgPanel"));
    setWindowTitle(i18n::tr("aipdf::app::LastWavePanels", "图层面板"));
    resize(560, 520);

    auto* layout = new QVBoxLayout(this);
    summaryLabel_ = new QLabel(this);
    summaryLabel_->setObjectName(QStringLiteral("lastWave.ocg.summary"));
    summaryLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "图层摘要"));
    layout->addWidget(summaryLabel_);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("lastWave.ocg.layers"));
    list_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "图层列表"));
    layout->addWidget(list_, 1);

    overrideLabel_ = new QLabel(this);
    overrideLabel_->setObjectName(QStringLiteral("lastWave.ocg.pageOverrides"));
    overrideLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "页面级覆盖"));
    overrideLabel_->setWordWrap(true);
    layout->addWidget(overrideLabel_);

    noticeLabel_ = new QLabel(this);
    noticeLabel_->setObjectName(QStringLiteral("lastWave.ocg.notice"));
    noticeLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "写回说明"));
    noticeLabel_->setWordWrap(true);
    layout->addWidget(noticeLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    applyButton_ = buttons->addButton(i18n::tr("aipdf::app::LastWavePanels", "写回文档"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(applyButton_, &QPushButton::clicked, this, [this] {
        emit applyRequested(plan());
        accept();
    });
}

void OcgLayerPanel::setSummary(const pdf::OptionalContentSummary& summary) {
    summary_ = summary;
    rebuild();
}

void OcgLayerPanel::rebuild() {
    summaryLabel_->setText(ocgSummaryText(summary_));
    list_->clear();
    const QVector<OcgLayerRow> rows = ocgLayerRows(summary_);
    for (const OcgLayerRow& row : rows) {
        auto* item = new QListWidgetItem(ocgLayerRowText(row), list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(row.visible ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, row.name);
    }
    const QStringList overrides = ocgPageOverrideLines(summary_);
    if (overrides.isEmpty()) {
        overrideLabel_->setText(i18n::tr("aipdf::app::LastWavePanels", "没有页面级覆盖：所有页共用同一套图层可见性。"));
    } else {
        overrideLabel_->setText(i18n::tr("aipdf::app::LastWavePanels", "页面级覆盖：\n%1")
                                .arg(overrides.join(QStringLiteral("\n"))));
    }
    noticeLabel_->setText(ocgWriteBackNoticeText());
}

int OcgLayerPanel::layerCount() const {
    return list_->count();
}

QString OcgLayerPanel::summaryText() const {
    return ocgSummaryText(summary_);
}

QStringList OcgLayerPanel::pageOverrideLines() const {
    return ocgPageOverrideLines(summary_);
}

QString OcgLayerPanel::writeBackNoticeText() const {
    return ocgWriteBackNoticeText();
}

QVector<bool> OcgLayerPanel::visibleStates() const {
    QVector<bool> states;
    states.reserve(list_->count());
    for (int index = 0; index < list_->count(); ++index) {
        states.append(list_->item(index)->checkState() == Qt::Checked);
    }
    return states;
}

pdf::OptionalContentPlan OcgLayerPanel::plan() const {
    pdf::OptionalContentPlan plan;
    const QVector<OcgLayerRow> rows = ocgLayerRows(summary_);
    const QVector<bool> states = visibleStates();
    for (int index = 0; index < rows.size(); ++index) {
        pdf::OptionalContentLayer layer;
        layer.name = rows.at(index).name;
        layer.defaultVisible = index < states.size() ? states.at(index) : true;
        layer.intent = rows.at(index).intent;
        plan.layers.append(layer);
    }
    return plan;
}

// ================================================================ 任务 N ====

UniversalConvertDialog::UniversalConvertDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("lastWave.universalConvertDialog"));
    setWindowTitle(i18n::tr("aipdf::app::LastWavePanels", "从文件新建 PDF"));
    resize(640, 380);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    sourceEdit_ = new QLineEdit(this);
    sourceEdit_->setObjectName(QStringLiteral("lastWave.universalConvert.source"));
    sourceEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "源文件"));
    sourceButton_ = new QPushButton(i18n::tr("aipdf::app::LastWavePanels", "选择文件…"), this);
    sourceButton_->setObjectName(QStringLiteral("lastWave.universalConvert.chooseSource"));
    auto* sourceRow = new QWidget(this);
    auto* sourceLayout = new QHBoxLayout(sourceRow);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->addWidget(sourceEdit_, 1);
    sourceLayout->addWidget(sourceButton_);
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "源文件（txt / md / csv / json / html）"), sourceRow);

    fontEdit_ = new QLineEdit(this);
    fontEdit_->setObjectName(QStringLiteral("lastWave.universalConvert.font"));
    fontEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "字体文件"));
    fontButton_ = new QPushButton(i18n::tr("aipdf::app::LastWavePanels", "选择字体文件…"), this);
    fontButton_->setObjectName(QStringLiteral("lastWave.universalConvert.chooseFont"));
    auto* fontRow = new QWidget(this);
    auto* fontLayout = new QHBoxLayout(fontRow);
    fontLayout->setContentsMargins(0, 0, 0, 0);
    fontLayout->addWidget(fontEdit_, 1);
    fontLayout->addWidget(fontButton_);
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "字体文件（可选，TTF）"), fontRow);
    layout->addLayout(form);

    noticeLabel_ = new QLabel(this);
    noticeLabel_->setObjectName(QStringLiteral("lastWave.universalConvert.notice"));
    noticeLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "转换结果说明"));
    noticeLabel_->setWordWrap(true);
    layout->addWidget(noticeLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    convertButton_ = buttons->addButton(i18n::tr("aipdf::app::LastWavePanels", "转换"), QDialogButtonBox::ActionRole);
    saveButton_ = buttons->addButton(i18n::tr("aipdf::app::LastWavePanels", "保存为 PDF…"), QDialogButtonBox::AcceptRole);
    saveButton_->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveButton_, &QPushButton::clicked, this, [this] {
        emit saveRequested(result_.suggestedFileName);
    });
    connect(sourceButton_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, i18n::tr("aipdf::app::LastWavePanels", "选择源文件"), sourceEdit_->text(),
            i18n::tr("aipdf::app::LastWavePanels", "文本文件 (*.txt *.md *.csv *.json *.html *.htm)"));
        if (!path.isEmpty()) {
            sourceEdit_->setText(path);
        }
    });
    connect(fontButton_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, i18n::tr("aipdf::app::LastWavePanels", "选择字体文件"), fontEdit_->text(), i18n::tr("aipdf::app::LastWavePanels", "字体文件 (*.ttf *.ttc)"));
        if (!path.isEmpty()) {
            fontEdit_->setText(path);
        }
    });
    connect(convertButton_, &QPushButton::clicked, this, [this] {
        QString error;
        if (!convert(&error)) {
            QMessageBox::warning(this, i18n::tr("aipdf::app::LastWavePanels", "从文件新建 PDF"), error);
        }
    });
    noticeLabel_->setText(i18n::tr("aipdf::app::LastWavePanels",
                                   "没选字体时：内容里有中文就自动用系统里的中文字体，"
                                   "纯西文用标准 14 字体。"));
}

void UniversalConvertDialog::setSourcePath(const QString& path) {
    sourceEdit_->setText(path);
}

QString UniversalConvertDialog::sourcePath() const {
    return sourceEdit_->text().trimmed();
}

void UniversalConvertDialog::setFontPath(const QString& path) {
    fontEdit_->setText(path);
}

QString UniversalConvertDialog::fontPath() const {
    return fontEdit_->text().trimmed();
}

bool UniversalConvertDialog::convert(QString* error) {
    const QString path = sourcePath();
    if (path.isEmpty()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::LastWavePanels", "先选一个源文件。");
        }
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::LastWavePanels", "读不了这个文件：%1").arg(path);
        }
        return false;
    }
    const QByteArray content = file.readAll();
    file.close();

    doc::UniversalConverterOptions options;
    const QString font = fontPath();
    if (!font.isEmpty()) {
        QFile fontFile(font);
        if (!fontFile.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::app::LastWavePanels", "读不了这个字体文件：%1").arg(font);
            }
            return false;
        }
        doc::FontResource resource;
        resource.bytes = fontFile.readAll();
        resource.postScriptName = QFileInfo(font).completeBaseName();
        fontFile.close();
        if (resource.bytes.isEmpty()) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::app::LastWavePanels", "这个字体文件是空的：%1").arg(font);
            }
            return false;
        }
        options.regularFont = resource;
        options.boldFont = resource;
    } else if (doc::contentNeedsCjkFont(content, doc::universalFileKindFor(QFileInfo(path).fileName()))) {
        // 用户没选字体，但内容里有中文：自动用系统里的中文字体。
        //
        // 为什么不能就这么用标准 14 字体：它没有中文字形，中文会变成问号——
        // 那是**静默丢字**：文件看着排好了，内容却没了。剪贴板与网页那两条路
        // 早就是这么做的（见 app/SystemCjkFont.h），这条路也得一样，
        // 否则同一个中文文件，从剪贴板转不丢字、从文件转却丢字。
        QString usedSystemFont;
        if (loadSystemCjkFont(&options.regularFont, &usedSystemFont)) {
            options.boldFont = options.regularFont;
        }
    }

    result_ = doc::convertToPdf(content, QFileInfo(path).fileName(), options);
    converted_ = true;
    saveButton_->setEnabled(result_.ok);
    if (!result_.ok) {
        if (error != nullptr) {
            *error = result_.error.isEmpty() ? i18n::tr("aipdf::app::LastWavePanels", "转换失败。") : result_.error;
        }
        refreshNotice();
        return false;
    }
    refreshNotice();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void UniversalConvertDialog::refreshNotice() {
    if (!converted_) {
        return;
    }
    noticeLabel_->setText(universalConversionNoticeText(result_));
}

const doc::UniversalConversionResult& UniversalConvertDialog::result() const {
    return result_;
}

QString UniversalConvertDialog::noticeText() const {
    if (!converted_) {
        return QString();
    }
    return universalConversionNoticeText(result_);
}

// ================================================================ 任务 P ====

PdfUaReportDialog::PdfUaReportDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("lastWave.pdfUaDialog"));
    setWindowTitle(i18n::tr("aipdf::app::LastWavePanels", "PDF/UA 合规检查"));
    resize(900, 600);

    auto* layout = new QVBoxLayout(this);
    conclusionLabel_ = new QLabel(this);
    conclusionLabel_->setObjectName(QStringLiteral("lastWave.pdfUa.conclusion"));
    conclusionLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "合规结论"));
    conclusionLabel_->setWordWrap(true);
    layout->addWidget(conclusionLabel_);

    scopeLabel_ = new QLabel(this);
    scopeLabel_->setObjectName(QStringLiteral("lastWave.pdfUa.scope"));
    scopeLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "检查器能力说明"));
    scopeLabel_->setWordWrap(true);
    layout->addWidget(scopeLabel_);

    passTree_ = createFindingTree(this, QStringLiteral("lastWave.pdfUa.passes"));
    warningTree_ = createFindingTree(this, QStringLiteral("lastWave.pdfUa.warnings"));
    errorTree_ = createFindingTree(this, QStringLiteral("lastWave.pdfUa.errors"));

    const auto addGroup = [this, layout](const QString& title, const QString& objectName,
                                         QTreeWidget* tree) {
        auto* group = new QWidget(this);
        auto* groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        auto* titleLabel = new QLabel(title, group);
        titleLabel->setObjectName(objectName + QStringLiteral(".title"));
        titleLabel->setAccessibleName(title);
        groupLayout->addWidget(titleLabel);
        groupLayout->addWidget(tree);
        layout->addWidget(group, 1);
    };
    addGroup(i18n::tr("aipdf::app::LastWavePanels", "通过"), QStringLiteral("lastWave.pdfUa.passGroup"), passTree_);
    addGroup(i18n::tr("aipdf::app::LastWavePanels", "警告"), QStringLiteral("lastWave.pdfUa.warningGroup"), warningTree_);
    addGroup(i18n::tr("aipdf::app::LastWavePanels", "不通过"), QStringLiteral("lastWave.pdfUa.errorGroup"), errorTree_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    scopeLabel_->setText(pdfUaScopeNoticeText());
}

void PdfUaReportDialog::setReport(const pdf::PdfUaReport& report) {
    report_ = report;
    rebuild();
}

void PdfUaReportDialog::rebuild() {
    conclusionLabel_->setText(pdfUaConclusionText(report_));
    scopeLabel_->setText(pdfUaScopeNoticeText());
    QVector<pdf::PdfUaFinding> passes;
    QVector<pdf::PdfUaFinding> warnings;
    QVector<pdf::PdfUaFinding> errors;
    for (const pdf::PdfUaFinding& finding : report_.findings) {
        if (finding.severity == pdf::PdfUaSeverity::Pass) {
            passes.append(finding);
        } else if (finding.severity == pdf::PdfUaSeverity::Warning) {
            warnings.append(finding);
        } else {
            errors.append(finding);
        }
    }
    fillFindingTree(passTree_, passes);
    fillFindingTree(warningTree_, warnings);
    fillFindingTree(errorTree_, errors);
}

QString PdfUaReportDialog::conclusionText() const {
    return pdfUaConclusionText(report_);
}

QString PdfUaReportDialog::scopeNoticeText() const {
    return pdfUaScopeNoticeText();
}

QStringList PdfUaReportDialog::findingLines() const {
    return pdfUaFindingLines(report_);
}

int PdfUaReportDialog::passCount() const {
    return report_.passCount;
}

int PdfUaReportDialog::warningCount() const {
    return report_.warningCount;
}

int PdfUaReportDialog::errorCount() const {
    return report_.errorCount;
}

// ================================================================ 任务 Q ====

XfaFieldsDialog::XfaFieldsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("lastWave.xfaDialog"));
    setWindowTitle(i18n::tr("aipdf::app::LastWavePanels", "XFA 字段"));
    resize(860, 520);

    auto* layout = new QVBoxLayout(this);
    summaryLabel_ = new QLabel(this);
    summaryLabel_->setObjectName(QStringLiteral("lastWave.xfa.summary"));
    summaryLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "XFA 摘要"));
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    dynamicLabel_ = new QLabel(this);
    dynamicLabel_->setObjectName(QStringLiteral("lastWave.xfa.dynamic"));
    dynamicLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "动态 XFA 提示"));
    dynamicLabel_->setWordWrap(true);
    layout->addWidget(dynamicLabel_);

    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("lastWave.xfa.table"));
    table_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "XFA 字段列表"));
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({i18n::tr("aipdf::app::LastWavePanels", "名称"), i18n::tr("aipdf::app::LastWavePanels", "标签"), i18n::tr("aipdf::app::LastWavePanels", "类型"), i18n::tr("aipdf::app::LastWavePanels", "当前值"), i18n::tr("aipdf::app::LastWavePanels", "必填")});
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    exportButton_ = buttons->addButton(i18n::tr("aipdf::app::LastWavePanels", "导出字段 CSV"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(exportButton_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, i18n::tr("aipdf::app::LastWavePanels", "导出 XFA 字段"), QStringLiteral("xfa-fields.csv"), i18n::tr("aipdf::app::LastWavePanels", "CSV 文件 (*.csv)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, i18n::tr("aipdf::app::LastWavePanels", "导出 XFA 字段"), i18n::tr("aipdf::app::LastWavePanels", "写不了这个文件：%1").arg(path));
            return;
        }
        file.write(exportCsv());
        file.close();
    });
}

void XfaFieldsDialog::setReport(const pdf::XfaReport& report) {
    report_ = report;
    rebuild();
}

void XfaFieldsDialog::rebuild() {
    summaryLabel_->setText(xfaSummaryText(report_));
    const QString dynamic = xfaDynamicNoticeText(report_);
    dynamicLabel_->setText(dynamic.isEmpty() ? i18n::tr("aipdf::app::LastWavePanels", "这份 XFA 没有动态逻辑：字段都是静态的。") : dynamic);
    const QVector<XfaFieldRow> rows = xfaFieldRows(report_);
    table_->setRowCount(rows.size());
    for (int index = 0; index < rows.size(); ++index) {
        const XfaFieldRow& row = rows.at(index);
        table_->setItem(index, 0, new QTableWidgetItem(row.name));
        table_->setItem(index, 1, new QTableWidgetItem(row.label));
        table_->setItem(index, 2, new QTableWidgetItem(row.type));
        table_->setItem(index, 3, new QTableWidgetItem(row.value));
        table_->setItem(index, 4, new QTableWidgetItem(row.required ? i18n::tr("aipdf::app::LastWavePanels", "是") : i18n::tr("aipdf::app::LastWavePanels", "否")));
    }
}

int XfaFieldsDialog::fieldCount() const {
    return table_->rowCount();
}

QString XfaFieldsDialog::summaryText() const {
    return xfaSummaryText(report_);
}

QString XfaFieldsDialog::dynamicNoticeText() const {
    return xfaDynamicNoticeText(report_);
}

QStringList XfaFieldsDialog::fieldLines() const {
    QStringList lines;
    const QVector<XfaFieldRow> rows = xfaFieldRows(report_);
    for (const XfaFieldRow& row : rows) {
        lines.append(xfaFieldRowText(row));
    }
    return lines;
}

QByteArray XfaFieldsDialog::exportCsv() const {
    return pdf::exportXfaFieldsToCsv(report_);
}

// ================================================================ 任务 R ====

RichMediaDialog::RichMediaDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("lastWave.richMediaDialog"));
    setWindowTitle(i18n::tr("aipdf::app::LastWavePanels", "插入 3D 模型 / 多媒体"));
    resize(640, 320);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    kindBox_ = new QComboBox(this);
    kindBox_->setObjectName(QStringLiteral("lastWave.richMedia.kind"));
    kindBox_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "注解类型"));
    kindBox_->addItem(i18n::tr("aipdf::app::LastWavePanels", "3D 模型（U3D）"));
    kindBox_->addItem(i18n::tr("aipdf::app::LastWavePanels", "多媒体（音频 / 视频 / 图片）"));
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "类型"), kindBox_);

    pathEdit_ = new QLineEdit(this);
    pathEdit_->setObjectName(QStringLiteral("lastWave.richMedia.path"));
    pathEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "文件路径"));
    pathButton_ = new QPushButton(i18n::tr("aipdf::app::LastWavePanels", "选择文件…"), this);
    pathButton_->setObjectName(QStringLiteral("lastWave.richMedia.choose"));
    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->addWidget(pathEdit_, 1);
    pathLayout->addWidget(pathButton_);
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "文件"), pathRow);
    layout->addLayout(form);

    scopeLabel_ = new QLabel(this);
    scopeLabel_->setObjectName(QStringLiteral("lastWave.richMedia.scope"));
    scopeLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "能力说明"));
    scopeLabel_->setWordWrap(true);
    layout->addWidget(scopeLabel_);

    resultLabel_ = new QLabel(this);
    resultLabel_->setObjectName(QStringLiteral("lastWave.richMedia.result"));
    resultLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "写入结果"));
    resultLabel_->setWordWrap(true);
    layout->addWidget(resultLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    insertButton_ = buttons->addButton(i18n::tr("aipdf::app::LastWavePanels", "插入"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(insertButton_, &QPushButton::clicked, this, [this] {
        if (sourcePath().isEmpty()) {
            QMessageBox::information(this, i18n::tr("aipdf::app::LastWavePanels", "插入 3D 模型 / 多媒体"), i18n::tr("aipdf::app::LastWavePanels", "先选一个文件。"));
            return;
        }
        emit insertRequested(sourcePath(), kind());
    });
    connect(pathButton_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, i18n::tr("aipdf::app::LastWavePanels", "选择文件"), pathEdit_->text());
        if (!path.isEmpty()) {
            pathEdit_->setText(path);
        }
    });
    connect(kindBox_, &QComboBox::currentIndexChanged, this, [this] { refreshKind(); });
    refreshKind();
}

void RichMediaDialog::refreshKind() {
    scopeLabel_->setText(richMediaScopeNoticeText());
}

void RichMediaDialog::setSourcePath(const QString& path) {
    pathEdit_->setText(path);
}

QString RichMediaDialog::sourcePath() const {
    return pathEdit_->text().trimmed();
}

void RichMediaDialog::setKind(Kind kind) {
    kindBox_->setCurrentIndex(kind == Kind::ThreeD ? 0 : 1);
}

RichMediaDialog::Kind RichMediaDialog::kind() const {
    return kindBox_->currentIndex() == 0 ? Kind::ThreeD : Kind::Media;
}

void RichMediaDialog::setReadResult(const pdf::RichMediaReadResult& result) {
    readResult_ = result;
    hasResult_ = true;
    resultLabel_->setText(richMediaResultText(result));
}

QString RichMediaDialog::resultText() const {
    if (!hasResult_) {
        return QString();
    }
    return richMediaResultText(readResult_);
}

QString RichMediaDialog::scopeNoticeText() const {
    return richMediaScopeNoticeText();
}

// ================================================================ 任务 S ====

ReflowDialog::ReflowDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("lastWave.reflowDialog"));
    setWindowTitle(i18n::tr("aipdf::app::LastWavePanels", "按新页宽重排"));
    resize(640, 520);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    widthSpin_ = new QDoubleSpinBox(this);
    widthSpin_->setObjectName(QStringLiteral("lastWave.reflow.width"));
    widthSpin_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "新页宽"));
    widthSpin_->setRange(72.0, 5000.0);
    widthSpin_->setDecimals(1);
    widthSpin_->setValue(420.0);
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "新页宽（点）"), widthSpin_);

    heightSpin_ = new QDoubleSpinBox(this);
    heightSpin_->setObjectName(QStringLiteral("lastWave.reflow.height"));
    heightSpin_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "新页高"));
    heightSpin_->setRange(0.0, 5000.0);
    heightSpin_->setDecimals(1);
    heightSpin_->setValue(0.0);
    heightSpin_->setSpecialValueText(i18n::tr("aipdf::app::LastWavePanels", "按页宽自动算"));
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "新页高（点，0 = 自动）"), heightSpin_);

    fontSizeSpin_ = new QDoubleSpinBox(this);
    fontSizeSpin_->setObjectName(QStringLiteral("lastWave.reflow.fontSize"));
    fontSizeSpin_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "字号"));
    fontSizeSpin_->setRange(4.0, 72.0);
    fontSizeSpin_->setValue(11.0);
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "字号（点）"), fontSizeSpin_);

    lineSpacingSpin_ = new QDoubleSpinBox(this);
    lineSpacingSpin_->setObjectName(QStringLiteral("lastWave.reflow.lineSpacing"));
    lineSpacingSpin_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "行距"));
    lineSpacingSpin_->setRange(0.5, 4.0);
    lineSpacingSpin_->setSingleStep(0.05);
    lineSpacingSpin_->setValue(1.35);
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "行距（倍）"), lineSpacingSpin_);

    alignBox_ = new QComboBox(this);
    alignBox_->setObjectName(QStringLiteral("lastWave.reflow.alignment"));
    alignBox_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "对齐"));
    for (pdf::TextAlignment alignment : paragraphAlignments()) {
        alignBox_->addItem(paragraphAlignmentLabel(alignment));
    }
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "对齐"), alignBox_);
    layout->addLayout(form);

    layoutNoticeLabel_ = new QLabel(this);
    layoutNoticeLabel_->setObjectName(QStringLiteral("lastWave.reflow.layoutNotice"));
    layoutNoticeLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "版面变化说明"));
    layoutNoticeLabel_->setWordWrap(true);
    layoutNoticeLabel_->setText(reflowLayoutChangeNoticeText());
    layout->addWidget(layoutNoticeLabel_);

    acknowledgeBox_ = new QCheckBox(i18n::tr("aipdf::app::LastWavePanels", "我知道重排会改变版面（页数、图表位置都会变）"), this);
    acknowledgeBox_->setObjectName(QStringLiteral("lastWave.reflow.acknowledge"));
    acknowledgeBox_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "确认版面会变"));
    layout->addWidget(acknowledgeBox_);

    noticeLabel_ = new QLabel(this);
    noticeLabel_->setObjectName(QStringLiteral("lastWave.reflow.notice"));
    noticeLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "重排结果"));
    noticeLabel_->setWordWrap(true);
    layout->addWidget(noticeLabel_);

    droppedList_ = new QListWidget(this);
    droppedList_->setObjectName(QStringLiteral("lastWave.reflow.dropped"));
    droppedList_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "没有搬过去的内容"));
    layout->addWidget(droppedList_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    runButton_ = buttons->addButton(i18n::tr("aipdf::app::LastWavePanels", "重排"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(runButton_, &QPushButton::clicked, this, [this] {
        if (!acknowledgeBox_->isChecked()) {
            QMessageBox::information(this, i18n::tr("aipdf::app::LastWavePanels", "按新页宽重排"),
                                     i18n::tr("aipdf::app::LastWavePanels", "重排会改变版面：请先勾选「我知道重排会改变版面」。"));
            return;
        }
        emit reflowRequested(options());
    });
}

void ReflowDialog::setOptions(const pdf::ReflowOptions& options) {
    widthSpin_->setValue(options.pageWidth > 0.0 ? options.pageWidth : 420.0);
    heightSpin_->setValue(options.pageHeight);
    fontSizeSpin_->setValue(options.paragraph.fontSize);
    lineSpacingSpin_->setValue(options.paragraph.lineSpacing);
    const QVector<pdf::TextAlignment> alignments = paragraphAlignments();
    const int index = alignments.indexOf(options.paragraph.alignment);
    alignBox_->setCurrentIndex(index < 0 ? 0 : index);
}

pdf::ReflowOptions ReflowDialog::options() const {
    pdf::ReflowOptions options;
    options.pageWidth = widthSpin_->value();
    options.pageHeight = heightSpin_->value();
    options.paragraph.fontSize = fontSizeSpin_->value();
    options.paragraph.lineSpacing = lineSpacingSpin_->value();
    const QVector<pdf::TextAlignment> alignments = paragraphAlignments();
    const int index = alignBox_->currentIndex();
    options.paragraph.alignment =
        (index >= 0 && index < alignments.size()) ? alignments.at(index) : pdf::TextAlignment::Left;
    return options;
}

void ReflowDialog::setReport(const pdf::ReflowReport& report) {
    report_ = report;
    hasReport_ = true;
    rebuild();
}

void ReflowDialog::rebuild() {
    noticeLabel_->setText(reflowNoticeText(report_));
    droppedList_->clear();
    const QStringList lines = reflowDroppedLines(report_);
    for (const QString& line : lines) {
        droppedList_->addItem(line);
    }
    if (lines.isEmpty()) {
        droppedList_->addItem(i18n::tr("aipdf::app::LastWavePanels", "（没有内容被丢掉）"));
    }
}

QString ReflowDialog::noticeText() const {
    if (!hasReport_) {
        return QString();
    }
    return reflowNoticeText(report_);
}

const pdf::ReflowReport& ReflowDialog::report() const {
    return report_;
}

QStringList ReflowDialog::droppedLines() const {
    if (!hasReport_) {
        return QStringList();
    }
    return reflowDroppedLines(report_);
}

bool ReflowDialog::layoutChangeAcknowledged() const {
    return acknowledgeBox_->isChecked();
}

QString ReflowDialog::layoutChangeNoticeText() const {
    return reflowLayoutChangeNoticeText();
}

// ================================================================ 任务 T ====

WizardDialog::WizardDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("lastWave.wizardDialog"));
    setWindowTitle(i18n::tr("aipdf::app::LastWavePanels", "动作向导"));
    resize(760, 560);

    auto* layout = new QVBoxLayout(this);
    auto* topRow = new QHBoxLayout();
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setObjectName(QStringLiteral("lastWave.wizard.name"));
    nameEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "向导名"));
    nameEdit_->setPlaceholderText(i18n::tr("aipdf::app::LastWavePanels", "给这份编排起个名字"));
    topRow->addWidget(nameEdit_, 1);
    layout->addLayout(topRow);

    auto* actionRow = new QHBoxLayout();
    typeBox_ = new QComboBox(this);
    typeBox_->setObjectName(QStringLiteral("lastWave.wizard.actionType"));
    typeBox_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "动作类型"));
    const doc::WizardActionType types[] = {
        doc::WizardActionType::OpenFile,      doc::WizardActionType::MergeDocuments,
        doc::WizardActionType::SplitDocument, doc::WizardActionType::AddBookmark,
        doc::WizardActionType::AddPageNumbers, doc::WizardActionType::AddWatermark,
        doc::WizardActionType::Encrypt,       doc::WizardActionType::ConvertToFormat,
        doc::WizardActionType::Print,         doc::WizardActionType::End,
    };
    for (doc::WizardActionType type : types) {
        typeBox_->addItem(doc::wizardActionTypeTitle(type),
                          static_cast<int>(type));
    }
    actionRow->addWidget(typeBox_, 1);
    addButton_ = new QPushButton(i18n::tr("aipdf::app::LastWavePanels", "添加这一步"), this);
    addButton_->setObjectName(QStringLiteral("lastWave.wizard.add"));
    actionRow->addWidget(addButton_);
    layout->addLayout(actionRow);

    stepList_ = new QListWidget(this);
    stepList_->setObjectName(QStringLiteral("lastWave.wizard.steps"));
    stepList_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "步骤列表"));
    layout->addWidget(stepList_, 1);

    auto* stepButtons = new QHBoxLayout();
    removeButton_ = new QPushButton(i18n::tr("aipdf::app::LastWavePanels", "删除这一步"), this);
    upButton_ = new QPushButton(i18n::tr("aipdf::app::LastWavePanels", "上移"), this);
    downButton_ = new QPushButton(i18n::tr("aipdf::app::LastWavePanels", "下移"), this);
    stepButtons->addWidget(removeButton_);
    stepButtons->addWidget(upButton_);
    stepButtons->addWidget(downButton_);
    stepButtons->addStretch(1);
    layout->addLayout(stepButtons);

    paramTable_ = new QTableWidget(this);
    paramTable_->setObjectName(QStringLiteral("lastWave.wizard.params"));
    paramTable_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "步骤参数"));
    paramTable_->setColumnCount(3);
    paramTable_->setHorizontalHeaderLabels({i18n::tr("aipdf::app::LastWavePanels", "参数名"), i18n::tr("aipdf::app::LastWavePanels", "类型"), i18n::tr("aipdf::app::LastWavePanels", "值")});
    paramTable_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(paramTable_);

    scopeLabel_ = new QLabel(this);
    scopeLabel_->setObjectName(QStringLiteral("lastWave.wizard.scope"));
    scopeLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "能力说明"));
    scopeLabel_->setWordWrap(true);
    scopeLabel_->setText(wizardScopeNoticeText());
    layout->addWidget(scopeLabel_);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setObjectName(QStringLiteral("lastWave.wizard.summary"));
    summaryLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "执行结果"));
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    runButton_ = buttons->addButton(i18n::tr("aipdf::app::LastWavePanels", "运行"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(addButton_, &QPushButton::clicked, this, &WizardDialog::addStep);
    connect(removeButton_, &QPushButton::clicked, this, &WizardDialog::removeCurrentStep);
    connect(upButton_, &QPushButton::clicked, this, [this] { moveCurrentStep(-1); });
    connect(downButton_, &QPushButton::clicked, this, [this] { moveCurrentStep(1); });
    connect(stepList_, &QListWidget::currentRowChanged, this, [this](int) { rebuildParams(); });
    connect(paramTable_, &QTableWidget::itemChanged, this, &WizardDialog::onParamChanged);
    connect(runButton_, &QPushButton::clicked, this, [this] { emit runRequested(wizard()); });
    connect(nameEdit_, &QLineEdit::textChanged, this, [this](const QString& value) {
        wizard_.name = value;
    });
    rebuildSteps();
}

void WizardDialog::rebuildSteps() {
    const int current = stepList_->currentRow();
    stepList_->blockSignals(true);
    stepList_->clear();
    for (int index = 0; index < wizard_.steps.size(); ++index) {
        const doc::WizardStep& step = wizard_.steps.at(index);
        stepList_->addItem(QStringLiteral("%1. %2").arg(index + 1).arg(
            step.title.isEmpty() ? doc::wizardActionTypeTitle(step.type) : step.title));
    }
    stepList_->blockSignals(false);
    if (current >= 0 && current < stepList_->count()) {
        stepList_->setCurrentRow(current);
    } else if (stepList_->count() > 0) {
        stepList_->setCurrentRow(0);
    }
    rebuildParams();
}

void WizardDialog::rebuildParams() {
    paramTable_->blockSignals(true);
    paramTable_->setRowCount(0);
    const int row = stepList_->currentRow();
    if (row >= 0 && row < wizard_.steps.size()) {
        const doc::WizardStep& step = wizard_.steps.at(row);
        paramTable_->setRowCount(step.params.size());
        for (int index = 0; index < step.params.size(); ++index) {
            const doc::WizardParam& param = step.params.at(index);
            auto* nameItem = new QTableWidgetItem(param.name);
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            auto* typeItem = new QTableWidgetItem(wizardParamTypeName(param.type));
            typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
            auto* valueItem = new QTableWidgetItem(paramValue(param));
            paramTable_->setItem(index, 0, nameItem);
            paramTable_->setItem(index, 1, typeItem);
            paramTable_->setItem(index, 2, valueItem);
        }
    }
    paramTable_->blockSignals(false);
}

void WizardDialog::onParamChanged() {
    const int row = stepList_->currentRow();
    if (row < 0 || row >= wizard_.steps.size()) {
        return;
    }
    doc::WizardStep step = wizard_.steps.at(row);
    QVector<doc::WizardParam> params;
    for (int index = 0; index < paramTable_->rowCount(); ++index) {
        const QTableWidgetItem* nameItem = paramTable_->item(index, 0);
        const QTableWidgetItem* typeItem = paramTable_->item(index, 1);
        const QTableWidgetItem* valueItem = paramTable_->item(index, 2);
        if (nameItem == nullptr || typeItem == nullptr) {
            continue;
        }
        params.append(paramFromRow(nameItem->text(), typeItem->text(),
                                   valueItem != nullptr ? valueItem->text() : QString()));
    }
    step.params = params;
    wizard_.steps[row] = step;
}

void WizardDialog::addStep() {
    const auto type = static_cast<doc::WizardActionType>(typeBox_->currentData().toInt());
    doc::WizardStep step = doc::makeWizardStep(type);
    QVector<doc::WizardParam> params;
    for (const WizardParamField& field : wizardParamFields(type)) {
        params.append(makeParam(field, field.defaultValue));
    }
    step.params = params;
    wizard_.steps.append(step);
    rebuildSteps();
    stepList_->setCurrentRow(stepList_->count() - 1);
}

void WizardDialog::removeCurrentStep() {
    const int row = stepList_->currentRow();
    if (row < 0 || row >= wizard_.steps.size()) {
        return;
    }
    wizard_.steps.remove(row);
    rebuildSteps();
}

void WizardDialog::moveCurrentStep(int delta) {
    const int row = stepList_->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= wizard_.steps.size()) {
        return;
    }
    wizard_.steps.swapItemsAt(row, target);
    rebuildSteps();
    stepList_->setCurrentRow(target);
}

doc::ActionWizard WizardDialog::wizard() const {
    doc::ActionWizard value = wizard_;
    value.name = nameEdit_->text();
    return value;
}

void WizardDialog::setWizard(const doc::ActionWizard& value) {
    wizard_ = value;
    nameEdit_->setText(wizard_.name);
    rebuildSteps();
}

void WizardDialog::setRunReport(const doc::WizardRunReport& report) {
    report_ = report;
    hasReport_ = true;
    summaryLabel_->setText(wizardRunSummaryText(report));
}

QString WizardDialog::runSummaryText() const {
    if (!hasReport_) {
        return QString();
    }
    return wizardRunSummaryText(report_);
}

QString WizardDialog::scopeNoticeText() const {
    return wizardScopeNoticeText();
}

// ================================================================ 任务 U ====

InvoiceDialog::InvoiceDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("lastWave.invoiceDialog"));
    setWindowTitle(i18n::tr("aipdf::app::LastWavePanels", "发票打印"));
    resize(860, 640);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    numberEdit_ = new QLineEdit(this);
    numberEdit_->setObjectName(QStringLiteral("lastWave.invoice.number"));
    numberEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "发票号码"));
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "发票号码"), numberEdit_);

    titleEdit_ = new QLineEdit(this);
    titleEdit_->setObjectName(QStringLiteral("lastWave.invoice.title"));
    titleEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "抬头"));
    titleEdit_->setText(i18n::tr("aipdf::app::LastWavePanels", "发 票"));
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "抬头"), titleEdit_);

    sellerNameEdit_ = new QLineEdit(this);
    sellerNameEdit_->setObjectName(QStringLiteral("lastWave.invoice.sellerName"));
    sellerNameEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "销售方名称"));
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "销售方名称"), sellerNameEdit_);

    sellerTaxEdit_ = new QLineEdit(this);
    sellerTaxEdit_->setObjectName(QStringLiteral("lastWave.invoice.sellerTax"));
    sellerTaxEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "销售方税号"));
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "销售方税号"), sellerTaxEdit_);

    buyerNameEdit_ = new QLineEdit(this);
    buyerNameEdit_->setObjectName(QStringLiteral("lastWave.invoice.buyerName"));
    buyerNameEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "购买方名称"));
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "购买方名称"), buyerNameEdit_);

    buyerTaxEdit_ = new QLineEdit(this);
    buyerTaxEdit_->setObjectName(QStringLiteral("lastWave.invoice.buyerTax"));
    buyerTaxEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "购买方税号"));
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "购买方税号"), buyerTaxEdit_);

    dateEdit_ = new QLineEdit(this);
    dateEdit_->setObjectName(QStringLiteral("lastWave.invoice.date"));
    dateEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "开票日期"));
    dateEdit_->setText(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "开票日期"), dateEdit_);

    currencyBox_ = new QComboBox(this);
    currencyBox_->setObjectName(QStringLiteral("lastWave.invoice.currency"));
    currencyBox_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "币种"));
    currencyBox_->addItems(invoiceCurrencyOptions());
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "币种"), currencyBox_);

    decimalsBox_ = new QComboBox(this);
    decimalsBox_->setObjectName(QStringLiteral("lastWave.invoice.decimals"));
    decimalsBox_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "小数位"));
    decimalsBox_->addItem(i18n::tr("aipdf::app::LastWavePanels", "0 位"), 0);
    decimalsBox_->addItem(i18n::tr("aipdf::app::LastWavePanels", "2 位"), 2);
    decimalsBox_->addItem(i18n::tr("aipdf::app::LastWavePanels", "3 位"), 3);
    decimalsBox_->setCurrentIndex(1);
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "小数位"), decimalsBox_);

    taxRateSpin_ = new QDoubleSpinBox(this);
    taxRateSpin_->setObjectName(QStringLiteral("lastWave.invoice.taxRate"));
    taxRateSpin_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "税率"));
    taxRateSpin_->setRange(0.0, 100.0);
    taxRateSpin_->setDecimals(2);
    taxRateSpin_->setValue(0.0);
    form->addRow(i18n::tr("aipdf::app::LastWavePanels", "税率（%）"), taxRateSpin_);
    layout->addLayout(form);

    itemTable_ = new QTableWidget(this);
    itemTable_->setObjectName(QStringLiteral("lastWave.invoice.items"));
    itemTable_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "行项目"));
    itemTable_->setColumnCount(5);
    itemTable_->setHorizontalHeaderLabels(
        {i18n::tr("aipdf::app::LastWavePanels", "名称"), i18n::tr("aipdf::app::LastWavePanels", "规格"), i18n::tr("aipdf::app::LastWavePanels", "单位"), i18n::tr("aipdf::app::LastWavePanels", "数量"), i18n::tr("aipdf::app::LastWavePanels", "单价")});
    itemTable_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(itemTable_, 1);

    auto* itemButtons = new QHBoxLayout();
    addItemButton_ = new QPushButton(i18n::tr("aipdf::app::LastWavePanels", "添加一行"), this);
    removeItemButton_ = new QPushButton(i18n::tr("aipdf::app::LastWavePanels", "删除一行"), this);
    itemButtons->addWidget(addItemButton_);
    itemButtons->addWidget(removeItemButton_);
    itemButtons->addStretch(1);
    layout->addLayout(itemButtons);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("lastWave.invoice.status"));
    statusLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "校验结果"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    preview_ = new QPlainTextEdit(this);
    preview_->setObjectName(QStringLiteral("lastWave.invoice.preview"));
    preview_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "版式预览"));
    preview_->setReadOnly(true);
    layout->addWidget(preview_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    printButton_ = buttons->addButton(i18n::tr("aipdf::app::LastWavePanels", "打印"), QDialogButtonBox::ActionRole);
    exportButton_ = buttons->addButton(i18n::tr("aipdf::app::LastWavePanels", "导出 PDF"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(printButton_, &QPushButton::clicked, this, [this] { emit printRequested(); });
    connect(exportButton_, &QPushButton::clicked, this, [this] { emit exportRequested(); });
    connect(addItemButton_, &QPushButton::clicked, this, [this] {
        itemTable_->insertRow(itemTable_->rowCount());
    });
    connect(removeItemButton_, &QPushButton::clicked, this, [this] {
        const int row = itemTable_->currentRow();
        if (row >= 0 && row < itemTable_->rowCount()) {
            itemTable_->removeRow(row);
        }
    });
    for (QLineEdit* edit : {numberEdit_, titleEdit_, sellerNameEdit_, sellerTaxEdit_, buyerNameEdit_,
                            buyerTaxEdit_, dateEdit_}) {
        connect(edit, &QLineEdit::textChanged, this, &InvoiceDialog::refreshPreview);
    }
    connect(currencyBox_, &QComboBox::currentIndexChanged, this, &InvoiceDialog::refreshPreview);
    connect(decimalsBox_, &QComboBox::currentIndexChanged, this, &InvoiceDialog::refreshPreview);
    connect(taxRateSpin_, &QDoubleSpinBox::valueChanged, this, &InvoiceDialog::refreshPreview);
    connect(itemTable_, &QTableWidget::itemChanged, this, &InvoiceDialog::refreshPreview);
    refreshPreview();
}

QString InvoiceDialog::currencySymbol() const {
    const QString option = currencyBox_->currentText();
    const int space = option.indexOf(QLatin1Char(' '));
    return space > 0 ? option.left(space) : option;
}

int InvoiceDialog::decimals() const {
    return decimalsBox_->currentData().toInt();
}

qint64 InvoiceDialog::taxRateBasisPoints() const {
    // 界面上按百分比填，领域层用万分比：13% → 1300。
    return static_cast<qint64>(qRound(taxRateSpin_->value() * 100.0));
}

doc::Invoice InvoiceDialog::invoice() const {
    doc::Invoice invoice;
    invoice.invoiceNumber = numberEdit_->text().trimmed();
    invoice.title = titleEdit_->text().trimmed();
    invoice.seller.name = sellerNameEdit_->text().trimmed();
    invoice.seller.taxId = sellerTaxEdit_->text().trimmed();
    invoice.buyer.name = buyerNameEdit_->text().trimmed();
    invoice.buyer.taxId = buyerTaxEdit_->text().trimmed();
    const QDate date = QDate::fromString(dateEdit_->text().trimmed(), QStringLiteral("yyyy-MM-dd"));
    if (date.isValid()) {
        invoice.issueDate = date;
    }
    invoice.taxRateBasisPoints = taxRateBasisPoints();

    qint64 total = 0;
    for (int row = 0; row < itemTable_->rowCount(); ++row) {
        doc::InvoiceLineItem item;
        const QTableWidgetItem* nameItem = itemTable_->item(row, 0);
        const QTableWidgetItem* specItem = itemTable_->item(row, 1);
        const QTableWidgetItem* unitItem = itemTable_->item(row, 2);
        const QTableWidgetItem* quantityItem = itemTable_->item(row, 3);
        const QTableWidgetItem* priceItem = itemTable_->item(row, 4);
        item.name = nameItem != nullptr ? nameItem->text().trimmed() : QString();
        if (item.name.isEmpty()) {
            continue;
        }
        item.specification = specItem != nullptr ? specItem->text().trimmed() : QString();
        item.unit = unitItem != nullptr ? unitItem->text().trimmed() : QString();
        const double quantity = quantityItem != nullptr ? quantityItem->text().toDouble() : 0.0;
        const double price = priceItem != nullptr ? priceItem->text().toDouble() : 0.0;
        item.quantityMilli = static_cast<qint64>(qRound(quantity * 1000.0));
        item.unitPriceCents = static_cast<qint64>(qRound(price * 100.0));
        bool ok = false;
        item.amountCents = doc::invoiceLineAmountCents(item.quantityMilli, item.unitPriceCents, &ok);
        if (!ok) {
            continue;
        }
        item.taxRateBasisPoints = -1;
        total += item.amountCents;
        invoice.items.append(item);
    }
    invoice.totalCents = total;
    if (invoice.taxRateBasisPoints > 0) {
        invoice.taxCents = (total * invoice.taxRateBasisPoints + 5000) / 10000;
    } else {
        invoice.taxCents = 0;
    }
    return invoice;
}

doc::InvoiceCheckResult InvoiceDialog::check() const {
    return doc::checkInvoice(invoice());
}

QString InvoiceDialog::previewText(QString* error) const {
    QString reason;
    const QString text = doc::renderInvoiceText(invoice(), doc::InvoiceLayoutOptions(), &reason);
    if (text.isEmpty() && error != nullptr) {
        *error = reason;
    }
    return text;
}

void InvoiceDialog::refreshPreview() {
    QString error;
    const QString text = previewText(&error);
    preview_->setPlainText(text);
    const doc::InvoiceCheckResult result = check();
    if (!result.ok) {
        statusLabel_->setText(i18n::tr("aipdf::app::LastWavePanels", "校验没通过：%1")
                                 .arg(result.issues.isEmpty()
                                          ? i18n::tr("aipdf::app::LastWavePanels", "发票要素不完整。")
                                          : result.issues.join(QStringLiteral("；"))));
        return;
    }
    const QString symbol = currencySymbol();
    const int decimals = this->decimals();
    statusLabel_->setText(
        i18n::tr("aipdf::app::LastWavePanels", "合计 %1；税额 %2；价税合计 %3")
            .arg(invoiceAmountText(result.subtotalCents, symbol, decimals),
                 invoiceAmountText(result.taxCents, symbol, decimals),
                 invoiceAmountText(result.grandTotalCents, symbol, decimals)));
}

// ================================================================ 任务 K ====

ParagraphFormatDialog::ParagraphFormatDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("lastWave.paragraphDialog"));
    setWindowTitle(i18n::tr("aipdf::app::LastWavePanels", "段落排版"));
    resize(680, 560);

    auto* rootLayout = new QVBoxLayout(this);
    textEdit_ = new QPlainTextEdit(this);
    textEdit_->setObjectName(QStringLiteral("lastWave.paragraph.text"));
    textEdit_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "要排的文字"));
    rootLayout->addWidget(textEdit_);

    auto* paragraphGroup = new QGroupBox(i18n::tr("aipdf::app::LastWavePanels", "段落"), this);
    paragraphGroup->setObjectName(QStringLiteral("lastWave.paragraph.group"));
    auto* groupForm = new QFormLayout(paragraphGroup);

    alignBox_ = new QComboBox(paragraphGroup);
    alignBox_->setObjectName(QStringLiteral("lastWave.paragraph.alignment"));
    alignBox_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "对齐"));
    for (pdf::TextAlignment alignment : paragraphAlignments()) {
        alignBox_->addItem(paragraphAlignmentLabel(alignment));
    }
    groupForm->addRow(i18n::tr("aipdf::app::LastWavePanels", "对齐"), alignBox_);

    spacingModeBox_ = new QComboBox(paragraphGroup);
    spacingModeBox_->setObjectName(QStringLiteral("lastWave.paragraph.spacingMode"));
    spacingModeBox_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "行距口径"));
    spacingModeBox_->addItem(i18n::tr("aipdf::app::LastWavePanels", "倍数"));
    spacingModeBox_->addItem(i18n::tr("aipdf::app::LastWavePanels", "固定磅值"));
    groupForm->addRow(i18n::tr("aipdf::app::LastWavePanels", "行距口径"), spacingModeBox_);

    fontSizeSpin_ = new QDoubleSpinBox(paragraphGroup);
    fontSizeSpin_->setObjectName(QStringLiteral("lastWave.paragraph.fontSize"));
    fontSizeSpin_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "字号"));
    fontSizeSpin_->setRange(1.0, 200.0);
    fontSizeSpin_->setValue(12.0);
    groupForm->addRow(i18n::tr("aipdf::app::LastWavePanels", "字号（点）"), fontSizeSpin_);

    spacingSpin_ = new QDoubleSpinBox(paragraphGroup);
    spacingSpin_->setObjectName(QStringLiteral("lastWave.paragraph.spacing"));
    spacingSpin_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "行距"));
    spacingSpin_->setRange(0.1, 200.0);
    spacingSpin_->setValue(1.2);
    groupForm->addRow(i18n::tr("aipdf::app::LastWavePanels", "行距"), spacingSpin_);

    firstLineIndentSpin_ = new QDoubleSpinBox(paragraphGroup);
    firstLineIndentSpin_->setObjectName(QStringLiteral("lastWave.paragraph.firstLineIndent"));
    firstLineIndentSpin_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "首行缩进"));
    firstLineIndentSpin_->setRange(0.0, 1000.0);
    groupForm->addRow(i18n::tr("aipdf::app::LastWavePanels", "首行缩进（点）"), firstLineIndentSpin_);

    leftIndentSpin_ = new QDoubleSpinBox(paragraphGroup);
    leftIndentSpin_->setObjectName(QStringLiteral("lastWave.paragraph.leftIndent"));
    leftIndentSpin_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "左缩进"));
    leftIndentSpin_->setRange(0.0, 1000.0);
    groupForm->addRow(i18n::tr("aipdf::app::LastWavePanels", "左缩进（点）"), leftIndentSpin_);

    rightIndentSpin_ = new QDoubleSpinBox(paragraphGroup);
    rightIndentSpin_->setObjectName(QStringLiteral("lastWave.paragraph.rightIndent"));
    rightIndentSpin_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "右缩进"));
    rightIndentSpin_->setRange(0.0, 1000.0);
    groupForm->addRow(i18n::tr("aipdf::app::LastWavePanels", "右缩进（点）"), rightIndentSpin_);
    rootLayout->addWidget(paragraphGroup);

    auto* boxGroup = new QGroupBox(i18n::tr("aipdf::app::LastWavePanels", "目标框（PDF 用户空间，左下原点）"), this);
    boxGroup->setObjectName(QStringLiteral("lastWave.paragraph.boxGroup"));
    auto* boxForm = new QFormLayout(boxGroup);
    xSpin_ = new QDoubleSpinBox(boxGroup);
    xSpin_->setObjectName(QStringLiteral("lastWave.paragraph.x"));
    xSpin_->setRange(-10000.0, 10000.0);
    boxForm->addRow(i18n::tr("aipdf::app::LastWavePanels", "X"), xSpin_);
    ySpin_ = new QDoubleSpinBox(boxGroup);
    ySpin_->setObjectName(QStringLiteral("lastWave.paragraph.y"));
    ySpin_->setRange(-10000.0, 10000.0);
    boxForm->addRow(i18n::tr("aipdf::app::LastWavePanels", "Y"), ySpin_);
    widthSpin_ = new QDoubleSpinBox(boxGroup);
    widthSpin_->setObjectName(QStringLiteral("lastWave.paragraph.width"));
    widthSpin_->setRange(1.0, 10000.0);
    widthSpin_->setValue(300.0);
    boxForm->addRow(i18n::tr("aipdf::app::LastWavePanels", "宽"), widthSpin_);
    heightSpin_ = new QDoubleSpinBox(boxGroup);
    heightSpin_->setObjectName(QStringLiteral("lastWave.paragraph.height"));
    heightSpin_->setObjectName(QStringLiteral("lastWave.paragraph.height"));
    heightSpin_->setRange(1.0, 10000.0);
    heightSpin_->setValue(200.0);
    boxForm->addRow(i18n::tr("aipdf::app::LastWavePanels", "高"), heightSpin_);
    rootLayout->addWidget(boxGroup);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setObjectName(QStringLiteral("lastWave.paragraph.summary"));
    summaryLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "段落参数"));
    summaryLabel_->setWordWrap(true);
    rootLayout->addWidget(summaryLabel_);

    planLabel_ = new QLabel(this);
    planLabel_->setObjectName(QStringLiteral("lastWave.paragraph.plan"));
    planLabel_->setAccessibleName(i18n::tr("aipdf::app::LastWavePanels", "排版结果"));
    planLabel_->setWordWrap(true);
    rootLayout->addWidget(planLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* layoutButton = buttons->addButton(i18n::tr("aipdf::app::LastWavePanels", "排版"), QDialogButtonBox::ActionRole);
    rootLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(layoutButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!layout(&error)) {
            QMessageBox::warning(this, i18n::tr("aipdf::app::LastWavePanels", "段落排版"), error);
        }
    });
    for (QDoubleSpinBox* spin : {fontSizeSpin_, spacingSpin_, firstLineIndentSpin_, leftIndentSpin_,
                                 rightIndentSpin_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, &ParagraphFormatDialog::refreshSummary);
    }
    connect(alignBox_, &QComboBox::currentIndexChanged, this, &ParagraphFormatDialog::refreshSummary);
    connect(spacingModeBox_, &QComboBox::currentIndexChanged, this,
            &ParagraphFormatDialog::refreshSummary);
    refreshSummary();
}

void ParagraphFormatDialog::setText(const QString& text) {
    textEdit_->setPlainText(text);
}

QString ParagraphFormatDialog::text() const {
    return textEdit_->toPlainText();
}

void ParagraphFormatDialog::setBox(const QRectF& box) {
    xSpin_->setValue(box.x());
    ySpin_->setValue(box.y());
    widthSpin_->setValue(qMax(1.0, box.width()));
    heightSpin_->setValue(qMax(1.0, box.height()));
}

QRectF ParagraphFormatDialog::box() const {
    return QRectF(xSpin_->value(), ySpin_->value(), widthSpin_->value(), heightSpin_->value());
}

pdf::ParagraphFormat ParagraphFormatDialog::format() const {
    pdf::ParagraphFormat format;
    format.fontSize = fontSizeSpin_->value();
    format.lineSpacingMode = spacingModeBox_->currentIndex() == 1 ? pdf::LineSpacingMode::Fixed
                                                                 : pdf::LineSpacingMode::Multiple;
    format.lineSpacing = spacingSpin_->value();
    format.firstLineIndent = firstLineIndentSpin_->value();
    format.leftIndent = leftIndentSpin_->value();
    format.rightIndent = rightIndentSpin_->value();
    const QVector<pdf::TextAlignment> alignments = paragraphAlignments();
    const int index = alignBox_->currentIndex();
    format.alignment =
        (index >= 0 && index < alignments.size()) ? alignments.at(index) : pdf::TextAlignment::Left;
    return format;
}

void ParagraphFormatDialog::setFormat(const pdf::ParagraphFormat& format) {
    fontSizeSpin_->setValue(format.fontSize);
    spacingModeBox_->setCurrentIndex(format.lineSpacingMode == pdf::LineSpacingMode::Fixed ? 1 : 0);
    spacingSpin_->setValue(format.lineSpacing);
    firstLineIndentSpin_->setValue(format.firstLineIndent);
    leftIndentSpin_->setValue(format.leftIndent);
    rightIndentSpin_->setValue(format.rightIndent);
    const QVector<pdf::TextAlignment> alignments = paragraphAlignments();
    const int index = alignments.indexOf(format.alignment);
    alignBox_->setCurrentIndex(index < 0 ? 0 : index);
    refreshSummary();
}

bool ParagraphFormatDialog::layout(QString* error) {
    // 位置与断行全部交给领域层：界面不自己算。
    plan_ = pdf::layoutParagraph(text(), box(), format());
    hasPlan_ = true;
    if (!plan_.ok()) {
        if (error != nullptr) {
            *error = plan_.error;
        }
        planLabel_->setText(i18n::tr("aipdf::app::LastWavePanels", "排版失败：%1").arg(plan_.error));
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    planLabel_->setText(i18n::tr("aipdf::app::LastWavePanels", "排成 %1 行%s")
                            .arg(plan_.lines.size())
                            .arg(plan_.overflow ? i18n::tr("aipdf::app::LastWavePanels", "，有文字没排进去") : QString()));
    return true;
}

pdf::ParagraphPlan ParagraphFormatDialog::plan() const {
    return plan_;
}

QString ParagraphFormatDialog::formatSummaryText() const {
    return paragraphFormatSummaryText(format());
}

void ParagraphFormatDialog::refreshSummary() {
    summaryLabel_->setText(paragraphFormatSummaryText(format()));
}

}  // namespace aipdf::app
