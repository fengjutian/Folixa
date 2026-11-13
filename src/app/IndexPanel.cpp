#include "app/IndexPanel.h"

#include "app/IndexController.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace aipdf::app {

IndexPanel::IndexPanel(QWidget* parent) : QWidget(parent) {
    buildUi();
}

void IndexPanel::buildUi() {
    // objectName 会进 UI Automation 的 AutomationId，
    // 验收脚本靠它稳定地找到状态标签、按钮、输入框与结果列表。
    setObjectName(QStringLiteral("indexPanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    statusLabel_ = new QLabel(QStringLiteral("尚未建立索引"), this);
    // objectName 沿用 secondaryText：主题里用它定义次要文字的颜色。
    // 不设 accessibleName：设了之后 UI Automation 的 Name 就是它，
    // 真正的状态文本只能走 TextPattern 读，验收脚本取不到。
    statusLabel_->setObjectName(QStringLiteral("secondaryText"));
    layout->addWidget(statusLabel_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    buildButton_ = new QPushButton(QStringLiteral("重建索引"), this);
    buildButton_->setObjectName(QStringLiteral("indexBuildButton"));
    buildButton_->setAccessibleName(QStringLiteral("重建索引"));
    cancelButton_ = new QPushButton(QStringLiteral("取消"), this);
    cancelButton_->setObjectName(QStringLiteral("indexCancelButton"));
    cancelButton_->setAccessibleName(QStringLiteral("取消索引"));
    cancelButton_->setEnabled(false);
    buttons->addWidget(buildButton_);
    buttons->addWidget(cancelButton_);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    queryEdit_ = new QLineEdit(this);
    queryEdit_->setObjectName(QStringLiteral("indexQueryInput"));
    queryEdit_->setPlaceholderText(QStringLiteral("检索索引…"));
    queryEdit_->setAccessibleName(QStringLiteral("检索关键词"));
    queryEdit_->setClearButtonEnabled(true);
    layout->addWidget(queryEdit_);

    results_ = new QListWidget(this);
    results_->setObjectName(QStringLiteral("indexResults"));
    results_->setAccessibleName(QStringLiteral("索引检索结果"));
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(results_, 1);

    connect(buildButton_, &QPushButton::clicked, this, [this]() { startIndexing(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this]() { cancelIndexing(); });
    connect(queryEdit_, &QLineEdit::textChanged, this, [this](const QString&) { runSearch(); });
    connect(results_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && !suppressNavigation_) {
            goToResult(row);
        }
    });
}

void IndexPanel::setController(IndexController* controller) {
    if (controller_ == controller) {
        return;
    }
    if (controller_ != nullptr) {
        disconnect(controller_, nullptr, this, nullptr);
    }
    controller_ = controller;
    clearResults();
    if (controller_ != nullptr) {
        // 控制器先没了（换文档、关标签页）：面板不能握着野指针。
        // QObject 析构时会发 destroyed，这里把指针清掉。
        connect(controller_, &QObject::destroyed, this, [this]() {
            controller_ = nullptr;
            clearResults();
            updateStatus();
        });
        connect(controller_, &IndexController::indexStarted, this, [this](int) {
            updateStatus();
            cancelButton_->setEnabled(true);
        });
        connect(controller_, &IndexController::indexProgress, this, [this](int, int) {
            updateStatus();
        });
        connect(controller_, &IndexController::indexFinished, this, [this](int) {
            updateStatus();
            cancelButton_->setEnabled(false);
            runSearch();
        });
        connect(controller_, &IndexController::indexCancelled, this, [this]() {
            updateStatus();
            cancelButton_->setEnabled(false);
        });
        connect(controller_, &IndexController::indexFailed, this, [this](const QString&) {
            updateStatus();
            cancelButton_->setEnabled(false);
        });
    }
    updateStatus();
}

void IndexPanel::startIndexing() {
    if (controller_ != nullptr) {
        controller_->startIndexing();
    }
}

void IndexPanel::cancelIndexing() {
    if (controller_ != nullptr) {
        controller_->cancelIndexing();
    }
}

int IndexPanel::resultCount() const {
    return results_->count();
}

QString IndexPanel::statusText() const {
    return statusLabel_->text();
}

void IndexPanel::runSearch() {
    hits_.clear();
    if (controller_ == nullptr) {
        refreshResults();
        return;
    }
    hits_ = controller_->search(queryEdit_->text());
    refreshResults();
}

void IndexPanel::refreshResults() {
    suppressNavigation_ = true;
    results_->clear();
    for (const doc::IndexHit& hit : hits_) {
        QString title = QStringLiteral("第 %1 页").arg(hit.pageIndex + 1);
        if (!hit.sectionPath.isEmpty()) {
            title = QStringLiteral("第 %1 页 · %2").arg(hit.pageIndex + 1).arg(hit.sectionPath);
        }
        results_->addItem(QStringLiteral("%1\n%2").arg(title, hit.snippet));
    }
    suppressNavigation_ = false;
}

void IndexPanel::updateStatus() {
    if (controller_ == nullptr) {
        statusLabel_->setText(QStringLiteral("尚未打开文档"));
        buildButton_->setEnabled(false);
        cancelButton_->setEnabled(false);
        return;
    }
    buildButton_->setEnabled(!controller_->isIndexing());
    cancelButton_->setEnabled(controller_->isIndexing());
    statusLabel_->setText(controller_->statusText());
}

void IndexPanel::goToResult(int row) {
    if (row < 0 || row >= hits_.size()) {
        return;
    }
    const doc::IndexHit& hit = hits_.at(row);
    emit chunkActivated(hit.pageIndex, hit.chunkId);
}

void IndexPanel::clearResults() {
    hits_.clear();
    suppressNavigation_ = true;
    results_->clear();
    suppressNavigation_ = false;
}

}  // namespace aipdf::app
