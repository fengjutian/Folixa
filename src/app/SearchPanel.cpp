#include "app/SearchPanel.h"

#include "app/TextController.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {
constexpr int kDebounceMs = 250;
}

SearchPanel::SearchPanel(QWidget* parent) : QWidget(parent) {
    buildUi();
}

void SearchPanel::buildUi() {
    // objectName 会进 UI Automation 的 AutomationId，
    // 验收脚本靠它稳定地找到输入框、结果列表与状态标签。
    setObjectName(QStringLiteral("searchPanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    queryEdit_ = new QLineEdit(this);
    queryEdit_->setObjectName(QStringLiteral("searchQueryInput"));
    queryEdit_->setPlaceholderText(QStringLiteral("搜索文档…"));
    queryEdit_->setAccessibleName(QStringLiteral("搜索关键词"));
    queryEdit_->setClearButtonEnabled(true);
    layout->addWidget(queryEdit_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    previousButton_ = new QPushButton(QStringLiteral("上一处"), this);
    previousButton_->setObjectName(QStringLiteral("searchPreviousButton"));
    previousButton_->setAccessibleName(QStringLiteral("上一处结果"));
    nextButton_ = new QPushButton(QStringLiteral("下一处"), this);
    nextButton_->setObjectName(QStringLiteral("searchNextButton"));
    nextButton_->setAccessibleName(QStringLiteral("下一处结果"));
    clearButton_ = new QPushButton(QStringLiteral("清除"), this);
    clearButton_->setObjectName(QStringLiteral("searchClearButton"));
    clearButton_->setAccessibleName(QStringLiteral("清除搜索"));
    stopButton_ = new QPushButton(QStringLiteral("停止"), this);
    stopButton_->setObjectName(QStringLiteral("searchStopButton"));
    stopButton_->setAccessibleName(QStringLiteral("停止搜索"));
    stopButton_->setEnabled(false);
    buttons->addWidget(previousButton_);
    buttons->addWidget(nextButton_);
    buttons->addWidget(clearButton_);
    buttons->addWidget(stopButton_);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    statusLabel_ = new QLabel(QStringLiteral("输入关键词开始搜索"), this);
    // objectName 沿用 secondaryText：主题里用它定义次要文字的颜色。
    // 不设 accessibleName：设了之后 UI Automation 的 Name 就是它，
    // 真正的状态文本只能走 TextPattern 读，验收脚本取不到。
    statusLabel_->setObjectName(QStringLiteral("secondaryText"));
    layout->addWidget(statusLabel_);

    results_ = new QListWidget(this);
    results_->setObjectName(QStringLiteral("searchResults"));
    results_->setAccessibleName(QStringLiteral("搜索结果"));
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(results_, 1);

    debounceTimer_ = new QTimer(this);
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(kDebounceMs);

    connect(debounceTimer_, &QTimer::timeout, this, &SearchPanel::runSearch);
    connect(queryEdit_, &QLineEdit::returnPressed, this, [this]() {
        debounceTimer_->stop();
        runSearch();
    });
    connect(queryEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.trimmed().isEmpty()) {
            debounceTimer_->stop();
            clearResults();
            if (controller_ != nullptr) {
                controller_->cancelSearch();
            }
            stopButton_->setEnabled(false);
            statusLabel_->setText(QStringLiteral("输入关键词开始搜索"));
            return;
        }
        debounceTimer_->start();
    });
    connect(previousButton_, &QPushButton::clicked, this, [this]() { stepResult(-1); });
    connect(nextButton_, &QPushButton::clicked, this, [this]() { stepResult(1); });
    connect(clearButton_, &QPushButton::clicked, this, [this]() {
        queryEdit_->clear();
        queryEdit_->setFocus();
    });
    connect(stopButton_, &QPushButton::clicked, this, [this]() {
        if (controller_ != nullptr) {
            controller_->cancelSearch();
        }
    });
    connect(results_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && !suppressNavigation_) {
            currentResult_ = row;
            emit navigateToPage(pageOfResult(row));
        }
    });
}

void SearchPanel::setController(TextController* controller) {
    if (controller_ == controller) {
        return;
    }
    if (controller_ != nullptr) {
        disconnect(controller_, nullptr, this, nullptr);
    }
    controller_ = controller;
    clearResults();
    statusLabel_->setText(QStringLiteral("输入关键词开始搜索"));

    if (controller_ == nullptr) {
        return;
    }
    connect(controller_, &TextController::searchStarted, this, [this]() {
        stopButton_->setEnabled(true);
        statusLabel_->setText(QStringLiteral("正在搜索…"));
    });
    connect(controller_, &TextController::searchHitsFound, this, [this](int count) {
        Q_UNUSED(count);
        refreshResults();
    });
    connect(controller_, &TextController::searchFinished, this, [this](bool cancelled) {
        stopButton_->setEnabled(false);
        refreshResults();
        if (cancelled) {
            statusLabel_->setText(QStringLiteral("搜索已取消（共 %1 处结果）")
                                      .arg(results_->count()));
        }
    });}

void SearchPanel::focusSearch() {
    queryEdit_->setFocus();
    queryEdit_->selectAll();
}

int SearchPanel::resultCount() const {
    return results_->count();
}

void SearchPanel::runSearch() {
    if (controller_ == nullptr) {
        return;
    }
    const QString query = queryEdit_->text().trimmed();
    if (query.isEmpty()) {
        return;
    }
    currentResult_ = -1;
    results_->clear();
    controller_->startSearch(query);
}

void SearchPanel::refreshResults() {
    if (controller_ == nullptr) {
        return;
    }
    const QVector<SearchHit> hits = controller_->searchHits();
    const bool keepSelection = currentResult_ >= 0 && currentResult_ < hits.size();
    const int previous = currentResult_;

    // 重建列表会触发 currentRowChanged，后台搜索推进时不该把视图拽走。
    suppressNavigation_ = true;
    results_->clear();
    for (const SearchHit& hit : hits) {
        auto* item = new QListWidgetItem(
            QStringLiteral("第 %1 页 · %2").arg(hit.pageIndex + 1).arg(hit.snippet), results_);
        item->setData(Qt::UserRole, hit.pageIndex);
        item->setToolTip(hit.snippet);
    }
    if (keepSelection) {
        currentResult_ = previous;
        results_->setCurrentRow(previous);
    } else {
        currentResult_ = -1;
    }
    suppressNavigation_ = false;

    updateStatus();
}

void SearchPanel::updateStatus() {
    const int count = results_->count();
    // 搜索还在跑的时候，状态要一直显示「正在搜索」，
    // 否则长文档搜索刚开始就被「共 N 处」盖掉，用户以为已经搜完了。
    if (controller_ != nullptr && controller_->isSearching()) {
        statusLabel_->setText(QStringLiteral("正在搜索…（已找到 %1 处）").arg(count));
        return;
    }
    if (count == 0) {
        statusLabel_->setText(QStringLiteral("未找到匹配结果"));
    } else {
        statusLabel_->setText(QStringLiteral("共 %1 处结果").arg(count));
    }
}

int SearchPanel::pageOfResult(int index) const {
    if (index < 0 || index >= results_->count()) {
        return -1;
    }
    return results_->item(index)->data(Qt::UserRole).toInt();
}

void SearchPanel::goToResult(int index) {
    if (results_->count() <= 0) {
        return;
    }
    const int clamped = qBound(0, index, results_->count() - 1);
    if (results_->currentRow() == clamped) {
        currentResult_ = clamped;
        emit navigateToPage(pageOfResult(clamped));
        return;
    }
    currentResult_ = clamped;
    results_->setCurrentRow(clamped);  // 由 currentRowChanged 负责跳转
}

void SearchPanel::stepResult(int delta) {
    const int count = results_->count();
    if (count <= 0) {
        return;
    }
    int next = currentResult_ + delta;
    if (next < 0) {
        next = count - 1;
    } else if (next >= count) {
        next = 0;
    }
    goToResult(next);
}

void SearchPanel::clearResults() {
    results_->clear();
    currentResult_ = -1;
}

}  // namespace aipdf::app
