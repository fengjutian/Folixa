#include "app/panels/AttachmentPanel.h"

#include "app/ui/Language.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {

// 名称列的下标固定下来：表格只有三列，验收脚本与测试按它取单元格。
enum Column {
    NameColumn = 0,
    DescriptionColumn = 1,
    SizeColumn = 2,
    ColumnCount = 3,
};

// 给用户看的大小：按本地化的 KB / MB 显示，而不是一串字节数。
QString formatSize(qint64 bytes) {
    if (bytes <= 0) {
        return QStringLiteral("—");
    }
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

}  // namespace

AttachmentPanel::AttachmentPanel(QWidget* parent) : QWidget(parent) {
    buildUi();
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &AttachmentPanel::retranslateUi);
}

void AttachmentPanel::buildUi() {
    setObjectName(QStringLiteral("attachmentPanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("attachmentStack"));

    // 空状态：没有附件时也不能只剩一个空白框。
    auto* empty = new QWidget(stack_);
    auto* emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(4, 16, 4, 16);
    emptyLayout->setSpacing(6);
    emptyTitle_ = new QLabel(empty);
    emptyTitle_->setObjectName(QStringLiteral("emptyStateTitle"));
    emptyTitle_->setAlignment(Qt::AlignHCenter);
    texts_.bind([this] { emptyTitle_->setText(tr("文档里还没有附件")); });
    emptyHint_ = new QLabel(empty);
    emptyHint_->setObjectName(QStringLiteral("secondaryText"));
    emptyHint_->setWordWrap(true);
    emptyHint_->setAlignment(Qt::AlignHCenter);
    texts_.bind([this] {
        emptyHint_->setText(tr("用「添加附件…」把文件嵌进 PDF，附件会随文档一起保存。"));
    });
    emptyLayout->addStretch(1);
    emptyLayout->addWidget(emptyTitle_);
    emptyLayout->addWidget(emptyHint_);
    emptyLayout->addStretch(1);
    stack_->addWidget(empty);

    table_ = new QTableWidget(stack_);
    table_->setObjectName(QStringLiteral("attachmentTable"));
    table_->setColumnCount(ColumnCount);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(DescriptionColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(SizeColumn, QHeaderView::ResizeToContents);
    table_->setColumnWidth(NameColumn, 90);
    texts_.bind([this] {
        table_->setHorizontalHeaderLabels(
            {tr("名称"), tr("说明"), tr("大小")});
        table_->setAccessibleName(tr("文档附件列表"));
    });
    stack_->addWidget(table_);

    layout->addWidget(stack_, 1);

    // 窄面板里四个按钮排一行会挤成省略号，改成两行两列。
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    auto* leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(6);
    auto* rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(6);
    addButton_ = new QPushButton(this);
    addButton_->setObjectName(QStringLiteral("attachmentAddButton"));
    openButton_ = new QPushButton(this);
    openButton_->setObjectName(QStringLiteral("attachmentOpenButton"));
    saveButton_ = new QPushButton(this);
    saveButton_->setObjectName(QStringLiteral("attachmentSaveButton"));
    removeButton_ = new QPushButton(this);
    removeButton_->setObjectName(QStringLiteral("attachmentRemoveButton"));
    texts_.bind([this] {
        addButton_->setText(tr("添加附件…"));
        addButton_->setAccessibleName(tr("添加附件"));
        openButton_->setText(tr("打开"));
        openButton_->setAccessibleName(tr("打开所选附件"));
        saveButton_->setText(tr("保存副本…"));
        saveButton_->setAccessibleName(tr("保存所选附件的副本"));
        removeButton_->setText(tr("删除"));
        removeButton_->setAccessibleName(tr("删除所选附件"));
    });
    leftColumn->addWidget(addButton_);
    leftColumn->addWidget(saveButton_);
    rightColumn->addWidget(openButton_);
    rightColumn->addWidget(removeButton_);
    buttons->addLayout(leftColumn, 1);
    buttons->addLayout(rightColumn, 1);
    layout->addLayout(buttons);

    connect(addButton_, &QPushButton::clicked, this, &AttachmentPanel::addRequested);
    connect(openButton_, &QPushButton::clicked, this, &AttachmentPanel::requestOpen);
    connect(saveButton_, &QPushButton::clicked, this, &AttachmentPanel::requestSave);
    connect(removeButton_, &QPushButton::clicked, this, &AttachmentPanel::requestRemove);
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            [this] { updateButtons(); });
    connect(table_, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem* item) { activateRow(item != nullptr ? item->row() : -1); });

    updateEmptyState();
    updateButtons();
}

void AttachmentPanel::setAttachments(const QVector<pdf::DocumentAttachment>& attachments) {
    entries_ = attachments;
    if (table_ != nullptr) {
        const QSignalBlocker blocker(table_);
        table_->setRowCount(entries_.size());
        for (int row = 0; row < entries_.size(); ++row) {
            const pdf::DocumentAttachment& entry = entries_.at(row);
            auto* name = new QTableWidgetItem(entry.name);
            name->setToolTip(entry.name);
            table_->setItem(row, NameColumn, name);
            auto* description = new QTableWidgetItem(
                entry.description.isEmpty() ? QString() : entry.description);
            description->setToolTip(entry.description);
            table_->setItem(row, DescriptionColumn, description);
            auto* size = new QTableWidgetItem(formatSize(entry.size));
            size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            size->setToolTip(tr("%1 字节").arg(entry.size));
            table_->setItem(row, SizeColumn, size);
            // 名称放在 UserRole 里：排序、重排之后按行号找不到人。
            name->setData(Qt::UserRole, entry.name);
        }
    }
    updateEmptyState();
    updateButtons();
}

int AttachmentPanel::count() const {
    return entries_.size();
}

QString AttachmentPanel::selectedName() const {
    if (table_ == nullptr) {
        return {};
    }
    const int row = table_->currentRow();
    if (row < 0 || row >= entries_.size()) {
        return {};
    }
    return entries_.at(row).name;
}

bool AttachmentPanel::selectAttachment(const QString& name) {
    if (table_ == nullptr) {
        return false;
    }
    for (int row = 0; row < entries_.size(); ++row) {
        if (entries_.at(row).name == name) {
            table_->selectRow(row);
            return true;
        }
    }
    return false;
}

void AttachmentPanel::retranslateUi() {
    texts_.retranslate();
    updateEmptyState();
    updateButtons();
}

void AttachmentPanel::updateEmptyState() {
    if (stack_ == nullptr) {
        return;
    }
    stack_->setCurrentIndex(entries_.isEmpty() ? 0 : 1);
}

void AttachmentPanel::updateButtons() {
    const bool hasSelection = !selectedName().isEmpty();
    if (openButton_ != nullptr) {
        openButton_->setEnabled(hasSelection);
    }
    if (saveButton_ != nullptr) {
        saveButton_->setEnabled(hasSelection);
    }
    if (removeButton_ != nullptr) {
        removeButton_->setEnabled(hasSelection);
    }
}

void AttachmentPanel::activateRow(int row) {
    if (row < 0 || row >= entries_.size()) {
        return;
    }
    emit openRequested(entries_.at(row).name);
}

void AttachmentPanel::requestRemove() {
    const QString name = selectedName();
    if (!name.isEmpty()) {
        emit removeRequested(name);
    }
}

void AttachmentPanel::requestSave() {
    const QString name = selectedName();
    if (!name.isEmpty()) {
        emit saveRequested(name);
    }
}

void AttachmentPanel::requestOpen() {
    const QString name = selectedName();
    if (!name.isEmpty()) {
        emit openRequested(name);
    }
}

}  // namespace aipdf::app
