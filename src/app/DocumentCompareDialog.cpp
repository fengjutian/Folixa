#include "app/DocumentCompareDialog.h"

#include "app/Language.h"

#include <QAbstractItemView>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {

// 差异种类 → 中文名。
QString kindName(pdf::DiffKind kind) {
    switch (kind) {
        case pdf::DiffKind::Inserted:
            return i18n::tr("aipdf::app::DocumentCompareDialog", "插入");
        case pdf::DiffKind::Deleted:
            return i18n::tr("aipdf::app::DocumentCompareDialog", "删除");
        case pdf::DiffKind::Modified:
            return i18n::tr("aipdf::app::DocumentCompareDialog", "修改");
        case pdf::DiffKind::PageAdded:
            return i18n::tr("aipdf::app::DocumentCompareDialog", "新增页");
        case pdf::DiffKind::PageRemoved:
            return i18n::tr("aipdf::app::DocumentCompareDialog", "删除页");
    }
    return i18n::tr("aipdf::app::DocumentCompareDialog", "差异");
}

}  // namespace

DocumentCompareDialog::DocumentCompareDialog(QWidget* parent) : QDialog(parent) {
    buildUi();
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &DocumentCompareDialog::retranslateUi);
}

void DocumentCompareDialog::buildUi() {
    setObjectName(QStringLiteral("documentCompareDialog"));
    setModal(true);
    resize(980, 620);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    // 原文一行。
    auto* oldRow = new QHBoxLayout;
    auto* oldLabel = new QLabel(this);
    oldLabel->setObjectName(QStringLiteral("compare.oldLabel"));
    texts_.bind([this, oldLabel] {
        oldLabel->setText(i18n::tr("aipdf::app::DocumentCompareDialog", "原文："));
    });
    oldFileEdit_ = new QLineEdit(this);
    oldFileEdit_->setObjectName(QStringLiteral("compare.oldFile"));
    texts_.bind([this] {
        oldFileEdit_->setPlaceholderText(
            i18n::tr("aipdf::app::DocumentCompareDialog", "选一份旧版本的 PDF"));
    });
    browseOldButton_ = new QPushButton(this);
    browseOldButton_->setObjectName(QStringLiteral("compare.browseOld"));
    texts_.bind([this] {
        browseOldButton_->setText(i18n::tr("aipdf::app::DocumentCompareDialog", "浏览…"));
    });
    connect(browseOldButton_, &QPushButton::clicked, this, &DocumentCompareDialog::browseOldFile);
    oldRow->addWidget(oldLabel);
    oldRow->addWidget(oldFileEdit_, 1);
    oldRow->addWidget(browseOldButton_);
    layout->addLayout(oldRow);

    // 新文一行。
    auto* newRow = new QHBoxLayout;
    auto* newLabel = new QLabel(this);
    newLabel->setObjectName(QStringLiteral("compare.newLabel"));
    texts_.bind([this, newLabel] {
        newLabel->setText(i18n::tr("aipdf::app::DocumentCompareDialog", "新文："));
    });
    newFileEdit_ = new QLineEdit(this);
    newFileEdit_->setObjectName(QStringLiteral("compare.newFile"));
    texts_.bind([this] {
        newFileEdit_->setPlaceholderText(
            i18n::tr("aipdf::app::DocumentCompareDialog", "选一份新版本的 PDF"));
    });
    browseNewButton_ = new QPushButton(this);
    browseNewButton_->setObjectName(QStringLiteral("compare.browseNew"));
    texts_.bind([this] {
        browseNewButton_->setText(i18n::tr("aipdf::app::DocumentCompareDialog", "浏览…"));
    });
    connect(browseNewButton_, &QPushButton::clicked, this, &DocumentCompareDialog::browseNewFile);
    newRow->addWidget(newLabel);
    newRow->addWidget(newFileEdit_, 1);
    newRow->addWidget(browseNewButton_);
    layout->addLayout(newRow);

    auto* actionRow = new QHBoxLayout;
    compareButton_ = new QPushButton(this);
    compareButton_->setObjectName(QStringLiteral("compare.run"));
    texts_.bind([this] {
        compareButton_->setText(i18n::tr("aipdf::app::DocumentCompareDialog", "开始比较"));
    });
    connect(compareButton_, &QPushButton::clicked, this, [this] { runComparison(); });
    auto* closeButton = new QPushButton(this);
    closeButton->setObjectName(QStringLiteral("compare.close"));
    texts_.bind([this, closeButton] {
        closeButton->setText(i18n::tr("aipdf::app::DocumentCompareDialog", "关闭"));
    });
    connect(closeButton, &QPushButton::clicked, this, &DocumentCompareDialog::reject);
    actionRow->addWidget(compareButton_);
    actionRow->addStretch(1);
    actionRow->addWidget(closeButton);
    layout->addLayout(actionRow);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("compare.summary"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("compare.table"));
    table_->setColumnCount(5);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    texts_.bind([this] {
        table_->setHorizontalHeaderLabels({
            i18n::tr("aipdf::app::DocumentCompareDialog", "类型"),
            i18n::tr("aipdf::app::DocumentCompareDialog", "页码"),
            i18n::tr("aipdf::app::DocumentCompareDialog", "原文"),
            i18n::tr("aipdf::app::DocumentCompareDialog", "新文"),
            i18n::tr("aipdf::app::DocumentCompareDialog", "相似度"),
        });
    });
    connect(table_, &QTableWidget::cellActivated, this, &DocumentCompareDialog::onTableActivated);
    layout->addWidget(table_, 1);

    texts_.bind([this] { setWindowTitle(i18n::tr("aipdf::app::DocumentCompareDialog", "比较文档")); });
    retranslateUi();
}

void DocumentCompareDialog::retranslateUi() {
    texts_.retranslate();
    updateSummary();
}

void DocumentCompareDialog::setFiles(const QString& oldPath, const QString& newPath) {
    if (oldFileEdit_ != nullptr) {
        oldFileEdit_->setText(oldPath);
    }
    if (newFileEdit_ != nullptr) {
        newFileEdit_->setText(newPath);
    }
}

QString DocumentCompareDialog::oldFilePath() const {
    return oldFileEdit_ != nullptr ? oldFileEdit_->text().trimmed() : QString();
}

QString DocumentCompareDialog::newFilePath() const {
    return newFileEdit_ != nullptr ? newFileEdit_->text().trimmed() : QString();
}

void DocumentCompareDialog::setOptions(const pdf::DocumentCompareOptions& options) {
    options_ = options;
}

pdf::DocumentCompareOptions DocumentCompareDialog::options() const {
    return options_;
}

bool DocumentCompareDialog::runComparison() {
    if (table_ != nullptr) {
        table_->setRowCount(0);
    }
    lastResult_ = pdf::DocumentCompareResult();
    lastError_.clear();

    const QString oldPath = oldFilePath();
    const QString newPath = newFilePath();
    if (oldPath.isEmpty() || newPath.isEmpty()) {
        lastError_ = i18n::tr("aipdf::app::DocumentCompareDialog", "请先选两份 PDF。");
        updateSummary();
        return false;
    }

    QString error;
    const bool succeeded =
        pdf::compareDocumentFiles(oldPath, newPath, options_, &lastResult_, &error);
    if (!succeeded) {
        lastError_ = lastResult_.error.isEmpty() ? error : lastResult_.error;
    }

    // 把差异摆进表格：每行都能直接看到页码、原文与新文，
    // 不打开原始文件也能核对。
    if (table_ != nullptr) {
        const QVector<pdf::CompareDifference>& differences = lastResult_.differences;
        table_->setRowCount(differences.size());
        for (int row = 0; row < differences.size(); ++row) {
            const pdf::CompareDifference& difference = differences.at(row);
            const int page = displayPageOf(difference);
            const QString pageText =
                i18n::tr("aipdf::app::DocumentCompareDialog", "第 %1 页").arg(page + 1);
            const QString similarity =
                QString::number(qRound(difference.similarity * 100.0)) + QStringLiteral("%");
            table_->setItem(row, 0, new QTableWidgetItem(kindName(difference.kind)));
            table_->setItem(row, 1, new QTableWidgetItem(pageText));
            table_->setItem(row, 2, new QTableWidgetItem(difference.oldText));
            table_->setItem(row, 3, new QTableWidgetItem(difference.newText));
            table_->setItem(row, 4, new QTableWidgetItem(similarity));
        }
    }

    updateSummary();
    return succeeded;
}

pdf::DocumentCompareResult DocumentCompareDialog::lastResult() const {
    return lastResult_;
}

QString DocumentCompareDialog::lastError() const {
    return lastError_;
}

const pdf::CompareDifference* DocumentCompareDialog::differenceAt(int row) const {
    if (row < 0 || row >= lastResult_.differences.size()) {
        return nullptr;
    }
    return &lastResult_.differences.at(row);
}

int DocumentCompareDialog::differenceRowCount() const {
    return static_cast<int>(lastResult_.differences.size());
}

QString DocumentCompareDialog::diffKindName(pdf::DiffKind kind) {
    return kindName(kind);
}

void DocumentCompareDialog::browseOldFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, i18n::tr("aipdf::app::DocumentCompareDialog", "选择原文 PDF"), oldFilePath(),
        i18n::tr("aipdf::app::DocumentCompareDialog", "PDF 文件 (*.pdf)"));
    if (!path.isEmpty()) {
        oldFileEdit_->setText(path);
    }
}

void DocumentCompareDialog::browseNewFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, i18n::tr("aipdf::app::DocumentCompareDialog", "选择新文 PDF"), newFilePath(),
        i18n::tr("aipdf::app::DocumentCompareDialog", "PDF 文件 (*.pdf)"));
    if (!path.isEmpty()) {
        newFileEdit_->setText(path);
    }
}

void DocumentCompareDialog::updateSummary() {
    if (summary_ == nullptr) {
        return;
    }
    if (!lastError_.isEmpty()) {
        summary_->setText(
            i18n::tr("aipdf::app::DocumentCompareDialog", "比较失败：%1").arg(lastError_));
        return;
    }
    if (!lastResult_.succeeded) {
        summary_->setText(i18n::tr("aipdf::app::DocumentCompareDialog",
                                   "选两份 PDF，然后点「开始比较」。"));
        return;
    }
    if (lastResult_.differences.isEmpty()) {
        summary_->setText(i18n::tr("aipdf::app::DocumentCompareDialog",
                                   "比较完成：两份文件的文字没有差异。"));
        return;
    }
    summary_->setText(i18n::tr("aipdf::app::DocumentCompareDialog",
                                "比较完成：共 %1 处差异（新增 %2 处、删除 %3 处、修改 %4 处、"
                                "新增页 %5 页、删除页 %6 页），整体相似度 %7%。")
                          .arg(lastResult_.differences.size())
                          .arg(lastResult_.insertedBlockCount)
                          .arg(lastResult_.deletedBlockCount)
                          .arg(lastResult_.modifiedBlockCount)
                          .arg(lastResult_.addedPageCount)
                          .arg(lastResult_.removedPageCount)
                          .arg(QString::number(qRound(lastResult_.overallSimilarity * 100.0))
                              + QStringLiteral("%")));
}

void DocumentCompareDialog::onTableActivated(int row, int column) {
    Q_UNUSED(column);
    activateDifference(row);
}

void DocumentCompareDialog::activateDifference(int row) {
    const pdf::CompareDifference* difference = differenceAt(row);
    if (difference == nullptr) {
        return;
    }
    const int page = displayPageOf(*difference);
    const QString snippet = difference->newText.isEmpty() ? difference->oldText : difference->newText;
    emit navigateToPage(page, snippet);
}

int DocumentCompareDialog::displayPageOf(const pdf::CompareDifference& difference) {
    return difference.newPageIndex >= 0 ? difference.newPageIndex : difference.oldPageIndex;
}

}  // namespace aipdf::app
