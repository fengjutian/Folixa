#include "app/ConversionToolsDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "i18n/Translate.h"

namespace aipdf::app {

namespace {

// 写文件：先写临时文件再替换，中途失败不会把原文件弄坏。
bool writeFile(const QString& path, const QByteArray& bytes, QString* error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::MainWindow", "写不了这个文件：%1").arg(path);
        }
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::MainWindow", "写入失败：%1").arg(path);
        }
        return false;
    }
    return true;
}

// 印前检查一组结论的树。
QTreeWidget* createFindingTree(QWidget* parent, const QString& objectName) {
    auto* tree = new QTreeWidget(parent);
    tree->setObjectName(objectName);
    tree->setColumnCount(4);
    tree->setHeaderLabels({i18n::tr("aipdf::app::MainWindow", "码"), i18n::tr("aipdf::app::MainWindow", "标题"), i18n::tr("aipdf::app::MainWindow", "说明"), i18n::tr("aipdf::app::MainWindow", "页码")});
    tree->header()->setStretchLastSection(true);
    tree->setRootIsDecorated(false);
    return tree;
}

void fillFindingTree(QTreeWidget* tree, const QVector<pdf::PreflightFinding>& findings) {
    if (tree == nullptr) {
        return;
    }
    tree->clear();
    for (const pdf::PreflightFinding& finding : findings) {
        auto* item = new QTreeWidgetItem(tree);
        item->setText(0, finding.code);
        item->setText(1, finding.title);
        item->setText(2, finding.detail);
        item->setText(3, finding.pageIndex >= 0 ? QString::number(finding.pageIndex + 1)
                                                : i18n::tr("aipdf::app::MainWindow", "整篇"));
    }
}

}  // namespace

// ------------------------------------------------------------ 印前检查 ----

PreflightDialog::PreflightDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("convert.preflightDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "印前检查"));
    resize(860, 560);

    auto* layout = new QVBoxLayout(this);

    conclusionLabel_ = new QLabel(this);
    conclusionLabel_->setObjectName(QStringLiteral("convert.preflight.conclusion"));
    conclusionLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "印前检查结论"));
    conclusionLabel_->setWordWrap(true);
    layout->addWidget(conclusionLabel_);

    errorTree_ = createFindingTree(this, QStringLiteral("convert.preflight.errors"));
    warningTree_ = createFindingTree(this, QStringLiteral("convert.preflight.warnings"));
    infoTree_ = createFindingTree(this, QStringLiteral("convert.preflight.infos"));

    const auto addGroup = [this, layout](const QString& title, const QString& objectName,
                                        QLabel*& status, QTreeWidget* tree) {
        auto* group = new QWidget(this);
        auto* groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        auto* titleLabel = new QLabel(title, group);
        titleLabel->setObjectName(objectName + QStringLiteral(".title"));
        titleLabel->setAccessibleName(title);
        groupLayout->addWidget(titleLabel);
        status = new QLabel(group);
        status->setObjectName(objectName + QStringLiteral(".status"));
        status->setAccessibleName(title + i18n::tr("aipdf::app::MainWindow", "状态"));
        groupLayout->addWidget(status);
        groupLayout->addWidget(tree);
        layout->addWidget(group, 1);
    };
    addGroup(i18n::tr("aipdf::app::MainWindow", "错误（必须处理）"), QStringLiteral("convert.preflight.errorGroup"),
             errorStatus_, errorTree_);
    addGroup(i18n::tr("aipdf::app::MainWindow", "警告（应当处理）"), QStringLiteral("convert.preflight.warningGroup"),
             warningStatus_, warningTree_);
    addGroup(i18n::tr("aipdf::app::MainWindow", "提示（知会）"), QStringLiteral("convert.preflight.infoGroup"),
             infoStatus_, infoTree_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PreflightDialog::setReport(const pdf::PreflightReport& report) {
    report_ = report;
    groups_ = groupPreflightFindings(report_);
    rebuild();
}

const pdf::PreflightReport& PreflightDialog::report() const {
    return report_;
}

int PreflightDialog::errorCount() const {
    return groups_.errors.size();
}

int PreflightDialog::warningCount() const {
    return groups_.warnings.size();
}

int PreflightDialog::infoCount() const {
    return groups_.infos.size();
}

int PreflightDialog::displayedTotal() const {
    return groups_.total();
}

int PreflightDialog::domainErrorCount() const {
    return report_.errorCount;
}

int PreflightDialog::domainWarningCount() const {
    return report_.warningCount;
}

QString PreflightDialog::conclusionText() const {
    return preflightConclusionText(report_);
}

void PreflightDialog::rebuild() {
    if (conclusionLabel_ != nullptr) {
        conclusionLabel_->setText(conclusionText());
    }
    fillFindingTree(errorTree_, groups_.errors);
    fillFindingTree(warningTree_, groups_.warnings);
    fillFindingTree(infoTree_, groups_.infos);
    // 空的列表控件在无障碍接口里查不到，所以每组都留一个状态标签。
    if (errorStatus_ != nullptr) {
        errorStatus_->setText(groups_.errors.isEmpty()
                                  ? i18n::tr("aipdf::app::MainWindow", "没有错误项")
                                  : i18n::tr("aipdf::app::MainWindow", "%1 条").arg(groups_.errors.size()));
    }
    if (warningStatus_ != nullptr) {
        warningStatus_->setText(groups_.warnings.isEmpty()
                                    ? i18n::tr("aipdf::app::MainWindow", "没有警告项")
                                    : i18n::tr("aipdf::app::MainWindow", "%1 条").arg(groups_.warnings.size()));
    }
    if (infoStatus_ != nullptr) {
        infoStatus_->setText(groups_.infos.isEmpty()
                                  ? i18n::tr("aipdf::app::MainWindow", "没有提示项")
                                  : i18n::tr("aipdf::app::MainWindow", "%1 条").arg(groups_.infos.size()));
    }
}

// -------------------------------------------------------- OCR 疑似错误 ----

OcrSuspicionDialog::OcrSuspicionDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("convert.ocrSuspicionDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "OCR 疑似错误"));
    resize(820, 460);

    auto* layout = new QVBoxLayout(this);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("convert.ocrSuspicion.status"));
    statusLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "疑似错误状态"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    table_ = new QTableWidget(0, 5, this);
    table_->setObjectName(QStringLiteral("convert.ocrSuspicion.table"));
    table_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "疑似错误列表"));
    table_->setHorizontalHeaderLabels({i18n::tr("aipdf::app::MainWindow", "页码"), i18n::tr("aipdf::app::MainWindow", "识别到的字"), i18n::tr("aipdf::app::MainWindow", "建议的字"),
                                       i18n::tr("aipdf::app::MainWindow", "理由"), i18n::tr("aipdf::app::MainWindow", "上下文")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    rebuild();
}

void OcrSuspicionDialog::setSuspicious(const QVector<pdf::SuspiciousChar>& items) {
    rows_ = suspicionRows(items);
    rebuild();
}

int OcrSuspicionDialog::rowCount() const {
    return rows_.size();
}

QVector<SuspicionRow> OcrSuspicionDialog::rows() const {
    return rows_;
}

QString OcrSuspicionDialog::statusText() const {
    return statusLabel_ != nullptr ? statusLabel_->text() : QString();
}

void OcrSuspicionDialog::rebuild() {
    if (table_ != nullptr) {
        table_->setRowCount(rows_.size());
        for (int index = 0; index < rows_.size(); ++index) {
            const SuspicionRow& row = rows_.at(index);
            table_->setItem(index, 0, new QTableWidgetItem(QString::number(row.pageNumber)));
            table_->setItem(index, 1, new QTableWidgetItem(row.recognized));
            table_->setItem(index, 2, new QTableWidgetItem(row.suggestion));
            table_->setItem(index, 3, new QTableWidgetItem(row.reason));
            table_->setItem(index, 4, new QTableWidgetItem(row.context));
        }
    }
    if (statusLabel_ != nullptr) {
        // 这里要说清楚：只是提示，不会自动改用户的文字。
        statusLabel_->setText(rows_.isEmpty()
                                  ? i18n::tr("aipdf::app::MainWindow",
                                             "没有发现疑似错误。这些检查只是提示，不会自动改文档里的文字。")
                                  : i18n::tr("aipdf::app::MainWindow", "发现 %1 处疑似错误。这些只是提示，"
                                         "文档里的文字不会被自动改写。")
                                        .arg(rows_.size()));
    }
}

// ------------------------------------------------------------- 活页夹 ----

PortfolioDialog::PortfolioDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("convert.portfolioDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "PDF 文件包"));
    resize(760, 520);

    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout;
    titleEdit_ = new QLineEdit(this);
    titleEdit_->setObjectName(QStringLiteral("convert.portfolio.title"));
    titleEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "包标题"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "标题:"), titleEdit_);
    authorEdit_ = new QLineEdit(this);
    authorEdit_->setObjectName(QStringLiteral("convert.portfolio.author"));
    authorEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "作者"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "作者:"), authorEdit_);
    subjectEdit_ = new QLineEdit(this);
    subjectEdit_->setObjectName(QStringLiteral("convert.portfolio.subject"));
    subjectEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "主题"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "主题:"), subjectEdit_);
    layout->addLayout(form);

    auto* sourceRow = new QHBoxLayout;
    sourceRow->addWidget(new QLabel(i18n::tr("aipdf::app::MainWindow", "要收进包里的文件:"), this));
    auto* addButton = new QPushButton(i18n::tr("aipdf::app::MainWindow", "添加文件"), this);
    addButton->setObjectName(QStringLiteral("convert.portfolio.add"));
    addButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "添加文件"));
    sourceRow->addWidget(addButton);
    layout->addLayout(sourceRow);

    sourceList_ = new QListWidget(this);
    sourceList_->setObjectName(QStringLiteral("convert.portfolio.sources"));
    sourceList_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "待打包文件"));
    layout->addWidget(sourceList_, 1);

    entryTable_ = new QTableWidget(0, 3, this);
    entryTable_->setObjectName(QStringLiteral("convert.portfolio.entries"));
    entryTable_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "包内条目"));
    entryTable_->setHorizontalHeaderLabels({i18n::tr("aipdf::app::MainWindow", "名字"), i18n::tr("aipdf::app::MainWindow", "说明"), i18n::tr("aipdf::app::MainWindow", "大小")});
    entryTable_->horizontalHeader()->setStretchLastSection(true);
    entryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(entryTable_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("convert.portfolio.status"));
    statusLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "文件包状态"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* createButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "创建文件包"), QDialogButtonBox::ActionRole);
    createButton->setObjectName(QStringLiteral("convert.portfolio.create"));
    createButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "创建文件包"));
    auto* openButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "打开文件包"), QDialogButtonBox::ActionRole);
    openButton->setObjectName(QStringLiteral("convert.portfolio.open"));
    openButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "打开文件包"));
    auto* extractButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "取出选中条目"), QDialogButtonBox::ActionRole);
    extractButton->setObjectName(QStringLiteral("convert.portfolio.extract"));
    extractButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "取出选中条目"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(addButton, &QPushButton::clicked, this, [this] {
        const QStringList paths = QFileDialog::getOpenFileNames(
            this, i18n::tr("aipdf::app::MainWindow", "选择要收进包里的文件"), QString(), i18n::tr("aipdf::app::MainWindow", "PDF 文件 (*.pdf)"));
        for (const QString& path : paths) {
            addSourceFile(path);
        }
    });
    connect(createButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, i18n::tr("aipdf::app::MainWindow", "保存文件包"), i18n::tr("aipdf::app::MainWindow", "文件包.pdf"), i18n::tr("aipdf::app::MainWindow", "PDF 文件 (*.pdf)"));
        if (path.isEmpty()) {
            return;
        }
        QString error;
        const QByteArray bytes = createPackage(&error);
        if (bytes.isEmpty()) {
            QMessageBox::warning(this, i18n::tr("aipdf::app::MainWindow", "PDF 文件包"), error);
            return;
        }
        if (!writeFile(path, bytes, &error)) {
            QMessageBox::warning(this, i18n::tr("aipdf::app::MainWindow", "PDF 文件包"), error);
            return;
        }
        if (statusLabel_ != nullptr) {
            statusLabel_->setText(i18n::tr("aipdf::app::MainWindow", "已创建文件包：%1").arg(path));
        }
    });
    connect(openButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, i18n::tr("aipdf::app::MainWindow", "打开文件包"), QString(), i18n::tr("aipdf::app::MainWindow", "PDF 文件 (*.pdf)"));
        if (path.isEmpty()) {
            return;
        }
        QString error;
        if (!openPackage(path, &error)) {
            QMessageBox::warning(this, i18n::tr("aipdf::app::MainWindow", "PDF 文件包"), error);
            return;
        }
    });
    connect(extractButton, &QPushButton::clicked, this, [this] {
        const int row = entryTable_->currentRow();
        if (row < 0) {
            QMessageBox::information(this, i18n::tr("aipdf::app::MainWindow", "PDF 文件包"), i18n::tr("aipdf::app::MainWindow", "先选中一个条目。"));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            this, i18n::tr("aipdf::app::MainWindow", "取出条目"), QString(), QString());
        if (path.isEmpty()) {
            return;
        }
        QString error;
        if (!extractEntry(row, path, &error)) {
            QMessageBox::warning(this, i18n::tr("aipdf::app::MainWindow", "PDF 文件包"), error);
            return;
        }
        if (statusLabel_ != nullptr) {
            statusLabel_->setText(i18n::tr("aipdf::app::MainWindow", "已取出条目：%1").arg(path));
        }
    });
}

void PortfolioDialog::addSourceFile(const QString& filePath) {
    if (sourceList_ == nullptr || filePath.isEmpty()) {
        return;
    }
    sourceList_->addItem(filePath);
    refreshEntries();
}

QVector<pdf::PortfolioEntry> PortfolioDialog::entries() const {
    QVector<pdf::PortfolioEntry> result;
    if (sourceList_ == nullptr) {
        return result;
    }
    for (int index = 0; index < sourceList_->count(); ++index) {
        const QString path = sourceList_->item(index)->text();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        pdf::PortfolioEntry entry;
        entry.name = QFileInfo(path).fileName();
        entry.bytes = file.readAll();
        entry.description = QFileInfo(path).absolutePath();
        result.append(entry);
    }
    return result;
}

QByteArray PortfolioDialog::createPackage(QString* error) const {
    pdf::PortfolioOptions options;
    options.title = titleEdit_ != nullptr ? titleEdit_->text() : QString();
    options.author = authorEdit_ != nullptr ? authorEdit_->text() : QString();
    options.subject = subjectEdit_ != nullptr ? subjectEdit_->text() : QString();
    // 重名条目会判失败，失败原因由领域层给，界面照原样显示。
    return pdf::writePortfolio(entries(), options, error);
}

bool PortfolioDialog::openPackage(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::MainWindow", "读不了这个文件：%1").arg(path);
        }
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    opened_ = pdf::readPortfolio(bytes);
    openedOk_ = opened_.ok;
    if (!opened_.ok && error != nullptr) {
        *error = opened_.error;
    }
    refreshEntries();
    return opened_.ok;
}

QVector<PortfolioEntryRow> PortfolioDialog::entryRows() const {
    if (!openedOk_) {
        return QVector<PortfolioEntryRow>();
    }
    return portfolioEntryRows(opened_);
}

bool PortfolioDialog::extractEntry(int index, const QString& targetPath, QString* error) {
    if (!openedOk_) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::MainWindow", "先打开一个文件包。");
        }
        return false;
    }
    if (index < 0 || index >= opened_.entries.size()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::MainWindow", "没有这个条目。");
        }
        return false;
    }
    return writeFile(targetPath, opened_.entries.at(index).bytes, error);
}

void PortfolioDialog::refreshEntries() {
    if (entryTable_ == nullptr) {
        return;
    }
    const QVector<PortfolioEntryRow> rows = entryRows();
    entryTable_->setRowCount(rows.size());
    for (int index = 0; index < rows.size(); ++index) {
        const PortfolioEntryRow& row = rows.at(index);
        entryTable_->setItem(index, 0, new QTableWidgetItem(row.name));
        entryTable_->setItem(index, 1, new QTableWidgetItem(row.description));
        entryTable_->setItem(index, 2, new QTableWidgetItem(row.sizeText()));
    }
    if (statusLabel_ != nullptr) {
        if (openedOk_) {
            statusLabel_->setText(i18n::tr("aipdf::app::MainWindow", "包里有 %1 个条目。").arg(rows.size()));
        } else if (sourceList_ != nullptr && sourceList_->count() > 0) {
            statusLabel_->setText(i18n::tr("aipdf::app::MainWindow", "待打包 %1 个文件。").arg(sourceList_->count()));
        } else {
            statusLabel_->setText(i18n::tr("aipdf::app::MainWindow", "还没有文件。"));
        }
    }
}

// ------------------------------------------------------ 转换与导出动作 ----

}  // namespace aipdf::app
