#include "app/PrintToolsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "i18n/Translate.h"

namespace aipdf::app {

namespace {

// 纸张下拉框里的顺序与领域层的枚举一一对应。
struct PaperItem {
    const char* label;
    doc::PageSize size;
};

const PaperItem kPapers[] = {
    {"A4", doc::PageSize::A4},
    {"A3", doc::PageSize::A3},
    {"A5", doc::PageSize::A5},
    {"Letter", doc::PageSize::Letter},
    {"Legal", doc::PageSize::Legal},
    {"自定义", doc::PageSize::Custom},
};

}  // namespace

// ------------------------------------------------------------ 新建文档 ----

NewDocumentDialog::NewDocumentDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("file.newDocumentDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "新建文档"));
    resize(420, 260);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    paperBox_ = new QComboBox(this);
    paperBox_->setObjectName(QStringLiteral("file.newDocument.paper"));
    paperBox_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "纸张"));
    for (const PaperItem& item : kPapers) {
        paperBox_->addItem(i18n::tr("aipdf::app::MainWindow", item.label), static_cast<int>(item.size));
    }
    form->addRow(i18n::tr("aipdf::app::MainWindow", "纸张:"), paperBox_);

    orientationBox_ = new QComboBox(this);
    orientationBox_->setObjectName(QStringLiteral("file.newDocument.orientation"));
    orientationBox_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "方向"));
    orientationBox_->addItem(i18n::tr("aipdf::app::MainWindow", "纵向"), static_cast<int>(doc::PageOrientation::Portrait));
    orientationBox_->addItem(i18n::tr("aipdf::app::MainWindow", "横向"), static_cast<int>(doc::PageOrientation::Landscape));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "方向:"), orientationBox_);

    pageCountSpin_ = new QSpinBox(this);
    pageCountSpin_->setObjectName(QStringLiteral("file.newDocument.pageCount"));
    pageCountSpin_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "页数"));
    pageCountSpin_->setRange(1, 1000);
    pageCountSpin_->setValue(1);
    form->addRow(i18n::tr("aipdf::app::MainWindow", "页数:"), pageCountSpin_);

    widthSpin_ = new QDoubleSpinBox(this);
    widthSpin_->setObjectName(QStringLiteral("file.newDocument.width"));
    widthSpin_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "自定义宽度"));
    widthSpin_->setRange(1.0, 5000.0);
    widthSpin_->setDecimals(1);
    widthSpin_->setValue(595.0);
    form->addRow(i18n::tr("aipdf::app::MainWindow", "自定义宽度（点）:"), widthSpin_);

    heightSpin_ = new QDoubleSpinBox(this);
    heightSpin_->setObjectName(QStringLiteral("file.newDocument.height"));
    heightSpin_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "自定义高度"));
    heightSpin_->setRange(1.0, 5000.0);
    heightSpin_->setDecimals(1);
    heightSpin_->setValue(842.0);
    form->addRow(i18n::tr("aipdf::app::MainWindow", "自定义高度（点）:"), heightSpin_);

    layout->addLayout(form);

    orientationHint_ = new QLabel(this);
    orientationHint_->setObjectName(QStringLiteral("file.newDocument.hint"));
    orientationHint_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "提示"));
    orientationHint_->setWordWrap(true);
    layout->addWidget(orientationHint_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(i18n::tr("aipdf::app::MainWindow", "创建"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(paperBox_, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshEnabledState(); });
    refreshEnabledState();
}

doc::PageSize NewDocumentDialog::selectedPaper() const {
    if (paperBox_ == nullptr || paperBox_->currentIndex() < 0) {
        return doc::PageSize::A4;
    }
    return static_cast<doc::PageSize>(paperBox_->currentData().toInt());
}

void NewDocumentDialog::setPaper(doc::PageSize size) {
    if (paperBox_ == nullptr) {
        return;
    }
    const int index = paperBox_->findData(static_cast<int>(size));
    if (index >= 0 && index != paperBox_->currentIndex()) {
        paperBox_->setCurrentIndex(index);
        return;
    }
    refreshEnabledState();
}

doc::PageOrientation NewDocumentDialog::selectedOrientation() const {
    if (orientationBox_ == nullptr || orientationBox_->currentIndex() < 0) {
        return doc::PageOrientation::Portrait;
    }
    return static_cast<doc::PageOrientation>(orientationBox_->currentData().toInt());
}

bool NewDocumentDialog::orientationEnabled() const {
    return selectedPaper() != doc::PageSize::Custom;
}

int NewDocumentDialog::pageCount() const {
    return pageCountSpin_ != nullptr ? pageCountSpin_->value() : 1;
}

doc::BlankDocumentOptions NewDocumentDialog::options() const {
    doc::BlankDocumentOptions options;
    options.size = selectedPaper();
    options.orientation = selectedOrientation();
    options.pageCount = pageCount();
    options.widthPt = widthSpin_ != nullptr ? widthSpin_->value() : 0.0;
    options.heightPt = heightSpin_ != nullptr ? heightSpin_->value() : 0.0;
    return options;
}

QByteArray NewDocumentDialog::createDocument(QString* error) const {
    return doc::createBlankDocument(options(), error);
}

void NewDocumentDialog::refreshEnabledState() {
    const bool custom = selectedPaper() == doc::PageSize::Custom;
    // 自定义纸张时方向不生效：控件置灰，并把原因写出来。
    if (orientationBox_ != nullptr) {
        orientationBox_->setEnabled(!custom);
    }
    if (widthSpin_ != nullptr) {
        widthSpin_->setEnabled(custom);
    }
    if (heightSpin_ != nullptr) {
        heightSpin_->setEnabled(custom);
    }
    if (orientationHint_ != nullptr) {
        // 非自定义纸张时没有提示语：空串不需要进翻译目录。
        orientationHint_->setText(custom ? i18n::tr("aipdf::app::MainWindow", "自定义纸张按下面的宽高建页，方向不生效。")
                                        : QString());
    }
}

// ------------------------------------------------------------ 批量打印 ----

PrintBatchDialog::PrintBatchDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("file.printBatchDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "批量打印"));
    resize(680, 460);

    auto* layout = new QVBoxLayout(this);

    table_ = new QTableWidget(0, 3, this);
    table_->setObjectName(QStringLiteral("file.printBatch.table"));
    table_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "待打印文件"));
    table_->setHorizontalHeaderLabels({i18n::tr("aipdf::app::MainWindow", "文件"), i18n::tr("aipdf::app::MainWindow", "份数"), i18n::tr("aipdf::app::MainWindow", "页码范围")});
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_);

    auto* addRow = new QHBoxLayout;
    auto* addButton = new QPushButton(i18n::tr("aipdf::app::MainWindow", "添加文件"), this);
    addButton->setObjectName(QStringLiteral("file.printBatch.add"));
    addButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "添加文件"));
    auto* removeButton = new QPushButton(i18n::tr("aipdf::app::MainWindow", "移除选中"), this);
    removeButton->setObjectName(QStringLiteral("file.printBatch.remove"));
    removeButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "移除选中"));
    addRow->addWidget(addButton);
    addRow->addWidget(removeButton);
    addRow->addStretch(1);
    layout->addLayout(addRow);

    auto* form = new QFormLayout;
    copiesSpin_ = new QSpinBox(this);
    copiesSpin_->setObjectName(QStringLiteral("file.printBatch.copies"));
    copiesSpin_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "份数"));
    copiesSpin_->setRange(1, 99);
    copiesSpin_->setValue(1);
    form->addRow(i18n::tr("aipdf::app::MainWindow", "份数:"), copiesSpin_);

    rangeEdit_ = new QLineEdit(this);
    rangeEdit_->setObjectName(QStringLiteral("file.printBatch.range"));
    rangeEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "页码范围"));
    rangeEdit_->setPlaceholderText(i18n::tr("aipdf::app::MainWindow", "留空表示全部页，例如 1-3,5"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "页码范围:"), rangeEdit_);

    jobNameEdit_ = new QLineEdit(this);
    jobNameEdit_->setObjectName(QStringLiteral("file.printBatch.jobName"));
    jobNameEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "任务名"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "任务名:"), jobNameEdit_);

    combineBox_ = new QCheckBox(i18n::tr("aipdf::app::MainWindow", "合并成一份打印"), this);
    combineBox_->setObjectName(QStringLiteral("file.printBatch.combine"));
    combineBox_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "合并成一份打印"));
    form->addRow(QString(), combineBox_);

    outputEdit_ = new QLineEdit(this);
    outputEdit_->setObjectName(QStringLiteral("file.printBatch.output"));
    outputEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "合并输出路径"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "合并输出路径:"), outputEdit_);
    layout->addLayout(form);

    planLabel_ = new QLabel(this);
    planLabel_->setObjectName(QStringLiteral("file.printBatch.plan"));
    planLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "打印计划"));
    layout->addWidget(planLabel_);

    warningsLabel_ = new QLabel(this);
    warningsLabel_->setObjectName(QStringLiteral("file.printBatch.warnings"));
    warningsLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "打印提醒"));
    warningsLabel_->setWordWrap(true);
    layout->addWidget(warningsLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(addButton, &QPushButton::clicked, this, [this] {
        const QStringList paths = QFileDialog::getOpenFileNames(
            this, i18n::tr("aipdf::app::MainWindow", "选择要打印的文件"), QString(), i18n::tr("aipdf::app::MainWindow", "PDF 文件 (*.pdf)"));
        for (const QString& path : paths) {
            addFile(path);
        }
    });
    connect(removeButton, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row >= 0) {
            table_->removeRow(row);
            refreshPlan();
        }
    });
    connect(copiesSpin_, &QSpinBox::valueChanged, this, [this](int) { refreshPlan(); });
    connect(rangeEdit_, &QLineEdit::textChanged, this, [this](const QString&) { refreshPlan(); });
    connect(combineBox_, &QCheckBox::toggled, this, [this](bool) { refreshPlan(); });

    refreshPlan();
}

void PrintBatchDialog::addFile(const QString& filePath) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, 0, new QTableWidgetItem(filePath));
    table_->setItem(row, 1, new QTableWidgetItem(QString::number(copiesSpin_ != nullptr
                                                                        ? copiesSpin_->value()
                                                                        : 1)));
    table_->setItem(row, 2, new QTableWidgetItem(rangeEdit_ != nullptr ? rangeEdit_->text()
                                                                        : QString()));
    refreshPlan();
}

QVector<doc::PrintBatchItem> PrintBatchDialog::items() const {
    QVector<doc::PrintBatchItem> items;
    for (int row = 0; row < table_->rowCount(); ++row) {
        doc::PrintBatchItem item;
        const QTableWidgetItem* path = table_->item(row, 0);
        const QTableWidgetItem* copies = table_->item(row, 1);
        const QTableWidgetItem* range = table_->item(row, 2);
        item.filePath = path != nullptr ? path->text() : QString();
        item.copies = copies != nullptr ? copies->text().toInt() : 1;
        item.pageRange = range != nullptr ? range->text() : QString();
        items.append(item);
    }
    return items;
}

doc::PrintBatchOptions PrintBatchDialog::options() const {
    doc::PrintBatchOptions options;
    options.jobName = jobNameEdit_ != nullptr ? jobNameEdit_->text() : QString();
    options.combineIntoOne = combineBox_ != nullptr && combineBox_->isChecked();
    options.outputPath = outputEdit_ != nullptr ? outputEdit_->text() : QString();
    return options;
}

doc::PrintBatchPlan PrintBatchDialog::plan() const {
    QString error;
    return doc::planPrintBatch(items(), options(), &error);
}

QStringList PrintBatchDialog::warnings() const {
    return plan().warnings;
}

int PrintBatchDialog::plannedJobCount() const {
    return plan().totalJobs;
}

int PrintBatchDialog::totalPages() const {
    return plan().totalPages;
}

QString PrintBatchDialog::totalPagesText() const {
    const int pages = totalPages();
    // 领域层这一层不打开 PDF，读不到真实页数（恒为 -1）：
    // 显示「未知」，不要显示一个假的数字。
    if (pages < 0) {
        return i18n::tr("aipdf::app::MainWindow", "总页数：未知");
    }
    return i18n::tr("aipdf::app::MainWindow", "总页数：%1").arg(pages);
}

void PrintBatchDialog::refreshPlan() {
    const doc::PrintBatchPlan current = plan();
    plan_ = current;
    if (planLabel_ != nullptr) {
        if (!current.ok) {
            planLabel_->setText(current.error);
        } else {
            planLabel_->setText(i18n::tr("aipdf::app::MainWindow", "共 %1 个打印任务 · %2").arg(current.totalJobs)
                                    .arg(totalPagesText()));
        }
    }
    if (warningsLabel_ != nullptr) {
        // 提醒要显示出来：跳过的项、被夹住的参数都是用户该知道的。
        warningsLabel_->setText(current.warnings.isEmpty()
                                    ? QString()
                                    : i18n::tr("aipdf::app::MainWindow", "提醒：%1").arg(current.warnings.join(QStringLiteral("；"))));
    }
}

// ------------------------------------------------------------ 打印模板 ----

PrintTemplatesDialog::PrintTemplatesDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("file.printTemplatesDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "打印模板"));
    resize(640, 480);

    auto* layout = new QHBoxLayout(this);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("file.printTemplates.list"));
    list_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "打印模板列表"));
    list_->setMinimumWidth(180);
    layout->addWidget(list_);

    auto* right = new QVBoxLayout;
    auto* form = new QFormLayout;

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setObjectName(QStringLiteral("file.printTemplates.name"));
    nameEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "模板名"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "模板名:"), nameEdit_);

    copiesSpin_ = new QSpinBox(this);
    copiesSpin_->setObjectName(QStringLiteral("file.printTemplates.copies"));
    copiesSpin_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "份数"));
    copiesSpin_->setRange(1, 99);
    copiesSpin_->setValue(1);
    form->addRow(i18n::tr("aipdf::app::MainWindow", "份数:"), copiesSpin_);

    paperBox_ = new QComboBox(this);
    paperBox_->setObjectName(QStringLiteral("file.printTemplates.paper"));
    paperBox_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "纸张"));
    for (const PaperItem& item : kPapers) {
        if (item.size == doc::PageSize::Custom) {
            continue;
        }
        paperBox_->addItem(i18n::tr("aipdf::app::MainWindow", item.label), static_cast<int>(item.size));
    }
    form->addRow(i18n::tr("aipdf::app::MainWindow", "纸张:"), paperBox_);

    orientationBox_ = new QComboBox(this);
    orientationBox_->setObjectName(QStringLiteral("file.printTemplates.orientation"));
    orientationBox_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "方向"));
    orientationBox_->addItem(i18n::tr("aipdf::app::MainWindow", "纵向"), static_cast<int>(doc::PageOrientation::Portrait));
    orientationBox_->addItem(i18n::tr("aipdf::app::MainWindow", "横向"), static_cast<int>(doc::PageOrientation::Landscape));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "方向:"), orientationBox_);

    scalingBox_ = new QComboBox(this);
    scalingBox_->setObjectName(QStringLiteral("file.printTemplates.scaling"));
    scalingBox_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "缩放方式"));
    scalingBox_->addItem(i18n::tr("aipdf::app::MainWindow", "不缩放"), QStringLiteral("None"));
    scalingBox_->addItem(i18n::tr("aipdf::app::MainWindow", "适应页面"), QStringLiteral("FitToPage"));
    scalingBox_->addItem(i18n::tr("aipdf::app::MainWindow", "缩小到可打印"), QStringLiteral("ShrinkToFit"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "缩放方式:"), scalingBox_);

    scaleSpin_ = new QSpinBox(this);
    scaleSpin_->setObjectName(QStringLiteral("file.printTemplates.scale"));
    scaleSpin_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "缩放比例"));
    scaleSpin_->setRange(10, 399);
    scaleSpin_->setValue(100);
    form->addRow(i18n::tr("aipdf::app::MainWindow", "缩放比例（%）:"), scaleSpin_);

    duplexBox_ = new QCheckBox(i18n::tr("aipdf::app::MainWindow", "双面打印"), this);
    duplexBox_->setObjectName(QStringLiteral("file.printTemplates.duplex"));
    duplexBox_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "双面打印"));
    form->addRow(QString(), duplexBox_);

    colorBox_ = new QCheckBox(i18n::tr("aipdf::app::MainWindow", "彩色打印"), this);
    colorBox_->setObjectName(QStringLiteral("file.printTemplates.color"));
    colorBox_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "彩色打印"));
    colorBox_->setChecked(true);
    form->addRow(QString(), colorBox_);

    rangeEdit_ = new QLineEdit(this);
    rangeEdit_->setObjectName(QStringLiteral("file.printTemplates.range"));
    rangeEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "页码范围"));
    rangeEdit_->setPlaceholderText(i18n::tr("aipdf::app::MainWindow", "留空表示全部页"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "页码范围:"), rangeEdit_);

    right->addLayout(form);
    right->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* saveButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "保存模板"), QDialogButtonBox::ActionRole);
    saveButton->setObjectName(QStringLiteral("file.printTemplates.save"));
    saveButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "保存模板"));
    auto* applyButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "套用"), QDialogButtonBox::ActionRole);
    applyButton->setObjectName(QStringLiteral("file.printTemplates.apply"));
    applyButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "套用模板"));
    auto* deleteButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "删除"), QDialogButtonBox::ActionRole);
    deleteButton->setObjectName(QStringLiteral("file.printTemplates.delete"));
    deleteButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "删除模板"));
    right->addWidget(buttons);
    layout->addLayout(right);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!saveTemplate(&error)) {
            QMessageBox::warning(this, i18n::tr("aipdf::app::MainWindow", "打印模板"), error);
        }
    });
    connect(applyButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!validateCurrent(&error)) {
            QMessageBox::warning(this, i18n::tr("aipdf::app::MainWindow", "打印模板"), error);
            return;
        }
        emit applyRequested(currentTemplate());
    });
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!deleteSelected(&error)) {
            QMessageBox::warning(this, i18n::tr("aipdf::app::MainWindow", "打印模板"), error);
        }
    });
    connect(list_, &QListWidget::currentTextChanged, this, [this](const QString&) {
        refreshFromSelection();
    });

    reloadList();
}

doc::PrintTemplate PrintTemplatesDialog::currentTemplate() const {
    doc::PrintTemplate value;
    value.id = currentId_;
    value.name = nameEdit_ != nullptr ? nameEdit_->text() : QString();
    value.copies = copiesSpin_ != nullptr ? copiesSpin_->value() : 1;
    value.paperSize = paperBox_ != nullptr && paperBox_->currentIndex() >= 0
                          ? paperBox_->currentText()
                          : QStringLiteral("A4");
    value.orientation = orientationBox_ != nullptr && orientationBox_->currentIndex() >= 0
                            ? static_cast<doc::PageOrientation>(orientationBox_->currentData().toInt())
                            : doc::PageOrientation::Portrait;
    value.scaling = scalingBox_ != nullptr && scalingBox_->currentIndex() >= 0
                        ? scalingBox_->currentData().toString()
                        : QStringLiteral("FitToPage");
    value.scalePercent = scaleSpin_ != nullptr ? scaleSpin_->value() : 100;
    value.duplex = duplexBox_ != nullptr && duplexBox_->isChecked();
    value.color = colorBox_ != nullptr && colorBox_->isChecked();
    value.pageRange = rangeEdit_ != nullptr ? rangeEdit_->text() : QString();
    return value;
}

void PrintTemplatesDialog::setCurrentTemplate(const doc::PrintTemplate& value) {
    currentId_ = value.id;
    if (nameEdit_ != nullptr) {
        nameEdit_->setText(value.name);
    }
    if (copiesSpin_ != nullptr) {
        copiesSpin_->setValue(value.copies);
    }
    if (paperBox_ != nullptr) {
        const int index = paperBox_->findText(value.paperSize);
        if (index >= 0) {
            paperBox_->setCurrentIndex(index);
        }
    }
    if (orientationBox_ != nullptr) {
        const int index = orientationBox_->findData(static_cast<int>(value.orientation));
        if (index >= 0) {
            orientationBox_->setCurrentIndex(index);
        }
    }
    if (scalingBox_ != nullptr) {
        const int index = scalingBox_->findData(value.scaling);
        if (index >= 0) {
            scalingBox_->setCurrentIndex(index);
        }
    }
    if (scaleSpin_ != nullptr) {
        scaleSpin_->setValue(value.scalePercent);
    }
    if (duplexBox_ != nullptr) {
        duplexBox_->setChecked(value.duplex);
    }
    if (colorBox_ != nullptr) {
        colorBox_->setChecked(value.color);
    }
    if (rangeEdit_ != nullptr) {
        rangeEdit_->setText(value.pageRange);
    }
}

bool PrintTemplatesDialog::validateCurrent(QString* error) const {
    return doc::validatePrintTemplate(currentTemplate(), error);
}

bool PrintTemplatesDialog::saveTemplate(QString* error) {
    doc::PrintTemplate value = currentTemplate();
    if (!doc::savePrintTemplate(value, QString::fromLatin1(kPrintTemplatesSettingsKey), error)) {
        return false;
    }
    currentId_ = value.id;
    reloadList();
    return true;
}

QVector<doc::PrintTemplate> PrintTemplatesDialog::templates() const {
    return doc::listPrintTemplates(QString::fromLatin1(kPrintTemplatesSettingsKey));
}

bool PrintTemplatesDialog::deleteSelected(QString* error) {
    if (currentId_.isEmpty()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::MainWindow", "先选中一个模板。");
        }
        return false;
    }
    const bool ok = doc::deletePrintTemplate(currentId_,
                                             QString::fromLatin1(kPrintTemplatesSettingsKey));
    if (!ok && error != nullptr) {
        *error = i18n::tr("aipdf::app::MainWindow", "删除失败：模板可能已经不在了。");
    }
    if (ok) {
        currentId_.clear();
        reloadList();
    }
    return ok;
}

void PrintTemplatesDialog::reloadList() {
    if (list_ == nullptr) {
        return;
    }
    const QString previous = currentId_;
    list_->clear();
    for (const doc::PrintTemplate& value : templates()) {
        auto* item = new QListWidgetItem(value.name, list_);
        item->setData(Qt::UserRole, value.id);
    }
    for (int index = 0; index < list_->count(); ++index) {
        if (list_->item(index)->data(Qt::UserRole).toString() == previous) {
            list_->setCurrentRow(index);
            break;
        }
    }
}

void PrintTemplatesDialog::refreshFromSelection() {
    if (list_ == nullptr || list_->currentItem() == nullptr) {
        return;
    }
    const QString id = list_->currentItem()->data(Qt::UserRole).toString();
    doc::PrintTemplate value;
    QString error;
    if (!doc::loadPrintTemplate(id, QString::fromLatin1(kPrintTemplatesSettingsKey), &value, &error)) {
        return;
    }
    setCurrentTemplate(value);
}

}  // namespace aipdf::app
