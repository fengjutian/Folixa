#include "app/dialogs/RecordingCenterDialog.h"

#include "app/recording/RecordingLibrary.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <optional>

namespace aipdf::app {
namespace {

// 时长显示成「分:秒」。
QString formatDuration(int milliseconds) {
    if (milliseconds <= 0) return QStringLiteral("-");
    const int seconds = milliseconds / 1000;
    return QStringLiteral("%1:%2")
        .arg(seconds / 60)
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

// 体积显示成人类看得懂的单位。
QString formatSize(qint64 bytes) {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024);
    return QStringLiteral("%1 MB").arg(bytes / (1024 * 1024));
}

}  // namespace

RecordingCenterDialog::RecordingCenterDialog(RecordingLibrary* library, QWidget* parent)
    : QDialog(parent), library_(library) {
    setWindowTitle(tr("录制中心"));
    setMinimumSize(760, 560);
    setObjectName(QStringLiteral("recordingCenter"));
    buildUi();
    refreshList();
}

void RecordingCenterDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 22);
    root->setSpacing(16);

    auto* title = new QLabel(this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 7);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);
    texts_.bind([this, title] {
        title->setText(tr("录制与捕获"));
    });

    auto* intro = new QLabel(this);
    intro->setStyleSheet(QStringLiteral("color:#667085;"));
    root->addWidget(intro);
    texts_.bind([this, intro] {
        intro->setText(tr("录制 PDF 演示、应用窗口或声音，并把产物集中列在下面。"));
    });

    // ---- 四个动作 ----
    //
    // 每张卡片的文字都写成字面量：翻译工具（lupdate）只认 tr() 的字面量参数，
    // 拼出来的字符串它扫不到，英文界面就会漏译。
    auto* actions = new QGridLayout();
    actions->setHorizontalSpacing(12);
    actions->setVerticalSpacing(12);
    const QString cardStyle = QStringLiteral(
        "QPushButton{text-align:left;padding:14px 16px;border:1px solid #d8deea;"
        "border-radius:10px;background:white;color:#182033;font-weight:600;}"
        "QPushButton:hover{border-color:#4169C1;background:#EEF3FC;}");

    auto* screenButton = new QPushButton(this);
    screenButton->setMinimumHeight(82);
    screenButton->setStyleSheet(cardStyle);
    actions->addWidget(screenButton, 0, 0);
    texts_.bind([this, screenButton] {
        screenButton->setText(
            tr("屏幕录制")
            + QStringLiteral("\n")
            + tr("框选区域或录制整个窗口，产出 AVI 视频"));
    });

    auto* presentationButton = new QPushButton(this);
    presentationButton->setMinimumHeight(82);
    presentationButton->setStyleSheet(cardStyle);
    actions->addWidget(presentationButton, 0, 1);
    texts_.bind([this, presentationButton] {
        presentationButton->setText(
            tr("PDF 演示录制")
            + QStringLiteral("\n")
            + tr("放映当前 PDF 并录下放映过程"));
    });

    auto* audioButton = new QPushButton(this);
    audioButton->setMinimumHeight(82);
    audioButton->setStyleSheet(cardStyle);
    actions->addWidget(audioButton, 1, 0);
    texts_.bind([this, audioButton] {
        audioButton->setText(
            tr("声音录制")
            + QStringLiteral("\n")
            + tr("录制麦克风，产出 WAV 音频"));
    });

    auto* screenshotButton = new QPushButton(this);
    screenshotButton->setMinimumHeight(82);
    screenshotButton->setStyleSheet(cardStyle);
    actions->addWidget(screenshotButton, 1, 1);
    texts_.bind([this, screenshotButton] {
        screenshotButton->setText(
            tr("窗口截图")
            + QStringLiteral("\n")
            + tr("捕获当前窗口并保存为 PNG 图片"));
    });

    connect(screenButton, &QPushButton::clicked, this, &RecordingCenterDialog::screenRecordingRequested);
    connect(presentationButton, &QPushButton::clicked, this,
            &RecordingCenterDialog::presentationRecordingRequested);
    connect(audioButton, &QPushButton::clicked, this, &RecordingCenterDialog::audioRecordingRequested);
    connect(screenshotButton, &QPushButton::clicked, this, &RecordingCenterDialog::screenshotRequested);
    root->addLayout(actions);

    // ---- 录制列表 ----
    auto* listTitle = new QLabel(this);
    listTitle->setStyleSheet(QStringLiteral("font-weight:700;margin-top:6px;"));
    root->addWidget(listTitle);
    texts_.bind([this, listTitle] {
        listTitle->setText(tr("录制列表"));
    });

    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    root->addWidget(table_, 1);
    texts_.bind([this] {
        const QStringList headers = {
            tr("名称"),
            tr("类型"),
            tr("时长"),
            tr("大小"),
            tr("录制时间"),
        };
        table_->setHorizontalHeaderLabels(headers);
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            &RecordingCenterDialog::updateActionState);
    connect(table_, &QTableWidget::cellDoubleClicked, this, &RecordingCenterDialog::openSelected);

    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(QStringLiteral("color:#667085;"));
    root->addWidget(statusLabel_);

    // ---- 底部按钮 ----
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    openButton_ = buttons->addButton(tr("打开"),
                                     QDialogButtonBox::ActionRole);
    revealButton_ = buttons->addButton(tr("在文件夹中显示"),
                                        QDialogButtonBox::ActionRole);
    deleteButton_ = buttons->addButton(tr("删除"),
                                       QDialogButtonBox::ActionRole);
    auto* refreshButton = buttons->addButton(tr("刷新"),
                                              QDialogButtonBox::ActionRole);
    auto* folderButton = buttons->addButton(tr("打开录制文件夹"),
                                             QDialogButtonBox::ActionRole);

    connect(openButton_, &QPushButton::clicked, this, &RecordingCenterDialog::openSelected);
    connect(revealButton_, &QPushButton::clicked, this, &RecordingCenterDialog::revealSelected);
    connect(deleteButton_, &QPushButton::clicked, this, &RecordingCenterDialog::deleteSelected);
    connect(refreshButton, &QPushButton::clicked, this, &RecordingCenterDialog::refreshList);
    connect(folderButton, &QPushButton::clicked, this, [this] {
        if (library_ != nullptr) {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(RecordingLibrary::defaultDirectory()));
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    updateActionState();
}

std::optional<RecordingEntry> RecordingCenterDialog::selectedEntry() const {
    if (table_ == nullptr || library_ == nullptr) return std::nullopt;
    const int row = table_->currentRow();
    if (row < 0) return std::nullopt;
    const QVariant payload = table_->item(row, 0)->data(Qt::UserRole);
    if (!payload.isValid()) return std::nullopt;
    // entries() 按值返回：这里必须拷贝出来，不能把指针指回临时量。
    for (const RecordingEntry& entry : library_->entries()) {
        if (entry.id == payload.toString()) return entry;
    }
    return std::nullopt;
}

void RecordingCenterDialog::refreshList() {
    if (table_ == nullptr || library_ == nullptr) return;
    table_->setRowCount(0);

    QString syncNote;
    library_->syncWithDirectory(&syncNote);

    const QVector<RecordingEntry> entries = library_->entries();
    for (const RecordingEntry& entry : entries) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* nameItem = new QTableWidgetItem(entry.displayName());
        nameItem->setData(Qt::UserRole, entry.id);
        nameItem->setToolTip(QDir::toNativeSeparators(entry.path));
        table_->setItem(row, 0, nameItem);
        table_->setItem(row, 1, new QTableWidgetItem(RecordingLibrary::kindLabel(entry.kind)));
        table_->setItem(row, 2, new QTableWidgetItem(formatDuration(entry.durationMs)));
        table_->setItem(row, 3, new QTableWidgetItem(formatSize(entry.sizeBytes)));
        table_->setItem(row, 4, new QTableWidgetItem(
            entry.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    }

    if (statusLabel_ != nullptr) {
        if (!syncNote.isEmpty()) {
            statusLabel_->setText(syncNote);
        } else if (entries.isEmpty()) {
            statusLabel_->setText(
                tr("还没有录制产物。录一段就会出现在这里。"));
        } else {
            statusLabel_->setText(
                tr("共 %1 条录制产物。").arg(entries.size()));
        }
    }
    updateActionState();
}

void RecordingCenterDialog::updateActionState() {
    const bool hasSelection = table_ != nullptr && table_->currentRow() >= 0;
    if (openButton_ != nullptr) openButton_->setEnabled(hasSelection);
    if (revealButton_ != nullptr) revealButton_->setEnabled(hasSelection);
    if (deleteButton_ != nullptr) deleteButton_->setEnabled(hasSelection);
}

void RecordingCenterDialog::openSelected() {
    const std::optional<RecordingEntry> entry = selectedEntry();
    if (!entry.has_value()) return;
    if (!QFileInfo::exists(entry->path)) {
        QMessageBox::warning(this, tr("录制列表"),
                             tr("这个录制文件已经不在了：\n%1")
                                 .arg(QDir::toNativeSeparators(entry->path)));
        refreshList();
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(entry->path));
}

void RecordingCenterDialog::revealSelected() {
    const std::optional<RecordingEntry> entry = selectedEntry();
    if (!entry.has_value()) return;
    const QFileInfo info(entry->path);
    if (!info.exists()) {
        QMessageBox::warning(this, tr("录制列表"),
                             tr("这个录制文件已经不在了：\n%1")
                                 .arg(QDir::toNativeSeparators(entry->path)));
        refreshList();
        return;
    }
    // Windows 上用 Explorer 的 /select 参数定位到文件；其它平台退回到打开所在目录。
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,") + QDir::toNativeSeparators(info.absoluteFilePath())});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
}

void RecordingCenterDialog::deleteSelected() {
    const std::optional<RecordingEntry> entry = selectedEntry();
    if (!entry.has_value() || library_ == nullptr) return;
    const QString path = entry->path;
    const QString name = entry->displayName();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("删除录制"),
        tr("删除「%1」？文件会一起删掉，删了就找不回来了。")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    QString error;
    if (!library_->remove(entry->id, true, &error)) {
        QMessageBox::warning(this, tr("删除录制"),
                             error.isEmpty()
                                 ? tr("删除失败。")
                                 : error);
    }
    refreshList();
}

}  // namespace aipdf::app
