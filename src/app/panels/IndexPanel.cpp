#include "app/IndexPanel.h"

#include "app/IndexController.h"
#include "app/Language.h"

#include "i18n/Translate.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {
// 结果项标题：「第 X 页」或「第 X 页 · 章节路径」。
//
// 这个格式化函数不是 QObject 成员，拿不到类的 tr()，所以上下文显式
// 写成类名，和 .ts 里的 <context> 对齐。
QString hitLabel(const doc::IndexHit& hit) {
    if (hit.sectionPath.isEmpty()) {
        return i18n::tr("aipdf::app::IndexPanel", "第 %1 页").arg(hit.pageIndex + 1);
    }
    return i18n::tr("aipdf::app::IndexPanel", "第 %1 页 · %2")
        .arg(hit.pageIndex + 1)
        .arg(hit.sectionPath);
}
}  // namespace

IndexPanel::IndexPanel(QWidget* parent) : QWidget(parent) {
    buildUi();

    // 切换语言后登记过的常驻文案就地重译；动态文案由 retranslateUi() 重算。
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &IndexPanel::retranslateUi);
}

void IndexPanel::buildUi() {
    // objectName 会进 UI Automation 的 AutomationId，
    // 验收脚本靠它稳定地找到状态标签、按钮、输入框与结果列表。
    setObjectName(QStringLiteral("indexPanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    statusLabel_ = new QLabel(tr("尚未建立索引"), this);
    // objectName 沿用 secondaryText：主题里用它定义次要文字的颜色。
    // 不设 accessibleName：设了之后 UI Automation 的 Name 就是它，
    // 真正的状态文本只能走 TextPattern 读，验收脚本取不到。
    statusLabel_->setObjectName(QStringLiteral("secondaryText"));
    layout->addWidget(statusLabel_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    buildButton_ = new QPushButton(this);
    buildButton_->setObjectName(QStringLiteral("indexBuildButton"));
    cancelButton_ = new QPushButton(this);
    cancelButton_->setObjectName(QStringLiteral("indexCancelButton"));
    cancelButton_->setEnabled(false);
    texts_.bind([this] {
        buildButton_->setText(tr("重建索引"));
        buildButton_->setAccessibleName(tr("重建索引"));
        cancelButton_->setText(tr("取消"));
        cancelButton_->setAccessibleName(tr("取消索引"));
    });
    buttons->addWidget(buildButton_);
    buttons->addWidget(cancelButton_);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    queryEdit_ = new QLineEdit(this);
    queryEdit_->setObjectName(QStringLiteral("indexQueryInput"));
    texts_.bind([this] {
        queryEdit_->setPlaceholderText(tr("检索索引…"));
        queryEdit_->setAccessibleName(tr("检索关键词"));
    });
    queryEdit_->setClearButtonEnabled(true);
    layout->addWidget(queryEdit_);

    results_ = new QListWidget(this);
    results_->setObjectName(QStringLiteral("indexResults"));
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(results_, 1);
    texts_.bind([this] { results_->setAccessibleName(tr("索引检索结果")); });

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
    controllerAssigned_ = true;
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
        results_->addItem(QStringLiteral("%1\n%2").arg(hitLabel(hit), hit.snippet));
    }
    suppressNavigation_ = false;
}

void IndexPanel::updateStatus() {
    if (!controllerAssigned_) {
        // 还没绑定过控制器：保持面板的初始提示。
        statusLabel_->setText(tr("尚未建立索引"));
        buildButton_->setEnabled(true);
        cancelButton_->setEnabled(false);
        return;
    }
    if (controller_ == nullptr) {
        statusLabel_->setText(tr("尚未打开文档"));
        buildButton_->setEnabled(false);
        cancelButton_->setEnabled(false);
        return;
    }
    buildButton_->setEnabled(!controller_->isIndexing());
    cancelButton_->setEnabled(controller_->isIndexing());
    statusLabel_->setText(controller_->statusText());
}

void IndexPanel::retranslateUi() {
    texts_.retranslate();
    // 结果项前缀是动态文案：只改已有条目的文字，不重建列表，保住当前选中项。
    for (int i = 0; i < results_->count() && i < hits_.size(); ++i) {
        results_->item(i)->setText(
            QStringLiteral("%1\n%2").arg(hitLabel(hits_.at(i)), hits_.at(i).snippet));
    }
    updateStatus();
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
