#include "app/SearchPanel.h"

#include "app/Language.h"
#include "app/TextController.h"

#include "i18n/Translate.h"

#include <QCheckBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {
constexpr int kDebounceMs = 250;

// 搜索面板的翻页按钮只留图标。
//
// 侧栏展开也就 230px 左右，四个「图标 + 文字」按钮横着放不下：
// v0.11.0 截图上「上一处 / 下一处」被压成两团认不出的残字。
// chevron 是「上一条 / 下一条命中」的通用写法，文字挪进 tooltip 与
// accessibleName，既腾出宽度也不丢可读性。
//
// 提示文字由调用点用 QT_TRANSLATE_NOOP 带下来：这个辅助函数不是 QObject
// 成员，拿不到类的 tr()，所以这里显式写明上下文。
void makeIconButton(QPushButton* button, const QString& iconPath, const char* tooltipSource) {
    button->setIcon(QIcon(iconPath));
    button->setIconSize(QSize(18, 18));
    // 图标按钮不留文字内边距：Theme 里按 iconOnly 压成 32px 见方。
    button->setProperty("iconOnly", true);
    button->setToolTip(i18n::tr("aipdf::app::SearchPanel", tooltipSource));
}
}  // namespace

SearchPanel::SearchPanel(QWidget* parent) : QWidget(parent) {
    buildUi();

    // 切换语言后登记过的常驻文案就地重译；动态文案由 retranslateUi() 重算。
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &SearchPanel::retranslateUi);
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
    texts_.bind([this] {
        queryEdit_->setPlaceholderText(tr("搜索文档…"));
        queryEdit_->setAccessibleName(tr("搜索关键词"));
    });
    queryEdit_->setClearButtonEnabled(true);
    layout->addWidget(queryEdit_);

    // 检索选项：改了立刻重搜。勾选项本身就是查询的一部分，
    // 让用户改完还得再敲一次回车，等于把"选项"变成了"提示"。
    auto* optionsRow = new QHBoxLayout();
    optionsRow->setSpacing(12);
    caseSensitiveBox_ = new QCheckBox(this);
    caseSensitiveBox_->setObjectName(QStringLiteral("searchCaseSensitive"));
    wholeWordBox_ = new QCheckBox(this);
    wholeWordBox_->setObjectName(QStringLiteral("searchWholeWord"));
    regexBox_ = new QCheckBox(this);
    regexBox_->setObjectName(QStringLiteral("searchRegex"));
    texts_.bind([this] {
        caseSensitiveBox_->setText(tr("区分大小写"));
        wholeWordBox_->setText(tr("全字匹配"));
        regexBox_->setText(tr("正则"));
    });
    optionsRow->addWidget(caseSensitiveBox_);
    optionsRow->addWidget(wholeWordBox_);
    optionsRow->addWidget(regexBox_);

    // 页码范围：0 表示"不限"，用户改完立刻重搜。
    optionsRow->addSpacing(8);
    auto* fromPageLabel = new QLabel(QString(), this);
    optionsRow->addWidget(fromPageLabel);
    fromPageSpin_ = new QSpinBox(this);
    fromPageSpin_->setObjectName(QStringLiteral("searchFromPage"));
    fromPageSpin_->setRange(0, 99999);
    optionsRow->addWidget(fromPageSpin_);
    auto* toPageLabel = new QLabel(QString(), this);
    optionsRow->addWidget(toPageLabel);
    toPageSpin_ = new QSpinBox(this);
    toPageSpin_->setObjectName(QStringLiteral("searchToPage"));
    toPageSpin_->setRange(0, 99999);
    optionsRow->addWidget(toPageSpin_);
    auto* pageLabel = new QLabel(QString(), this);
    optionsRow->addWidget(pageLabel);
    texts_.bind([this, fromPageLabel, toPageLabel, pageLabel] {
        fromPageLabel->setText(tr("第"));
        toPageLabel->setText(tr("至"));
        pageLabel->setText(tr("页"));
        fromPageSpin_->setSpecialValueText(tr("不限"));
        toPageSpin_->setSpecialValueText(tr("不限"));
    });
    optionsRow->addStretch(1);
    layout->addLayout(optionsRow);

    auto restartSearch = [this]() {
        if (controller_ == nullptr || queryEdit_->text().trimmed().isEmpty()) {
            return;
        }
        debounceTimer_->stop();
        runSearch();
    };
    connect(caseSensitiveBox_, &QCheckBox::toggled, this, restartSearch);
    connect(wholeWordBox_, &QCheckBox::toggled, this, restartSearch);
    connect(regexBox_, &QCheckBox::toggled, this, restartSearch);
    connect(fromPageSpin_, &QSpinBox::valueChanged, this, restartSearch);
    connect(toPageSpin_, &QSpinBox::valueChanged, this, restartSearch);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    previousButton_ = new QPushButton(this);
    previousButton_->setObjectName(QStringLiteral("searchPreviousButton"));
    makeIconButton(previousButton_, QStringLiteral(":/icons/chevron-up.svg"),
                   QT_TRANSLATE_NOOP("aipdf::app::SearchPanel", "上一处"));
    nextButton_ = new QPushButton(this);
    nextButton_->setObjectName(QStringLiteral("searchNextButton"));
    makeIconButton(nextButton_, QStringLiteral(":/icons/chevron-down.svg"),
                   QT_TRANSLATE_NOOP("aipdf::app::SearchPanel", "下一处"));
    // 清除与停止是动词，图标猜不准：它们短，留着文字反而更好认。
    clearButton_ = new QPushButton(this);
    clearButton_->setObjectName(QStringLiteral("searchClearButton"));
    stopButton_ = new QPushButton(this);
    stopButton_->setObjectName(QStringLiteral("searchStopButton"));
    stopButton_->setEnabled(false);
    texts_.bind([this] {
        previousButton_->setToolTip(tr("上一处"));
        previousButton_->setAccessibleName(tr("上一处结果"));
        nextButton_->setToolTip(tr("下一处"));
        nextButton_->setAccessibleName(tr("下一处结果"));
        clearButton_->setText(tr("清除"));
        clearButton_->setAccessibleName(tr("清除搜索"));
        stopButton_->setText(tr("停止"));
        stopButton_->setAccessibleName(tr("停止搜索"));
    });
    buttons->addWidget(previousButton_);
    buttons->addWidget(nextButton_);
    buttons->addWidget(clearButton_);
    buttons->addWidget(stopButton_);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    // 状态行是随搜索状态变化的动态文案：这里先给初始提示，之后由 updateStatus() 重算。
    statusLabel_ = new QLabel(tr("输入关键词开始搜索"), this);
    // objectName 沿用 secondaryText：主题里用它定义次要文字的颜色。
    // 不设 accessibleName：设了之后 UI Automation 的 Name 就是它，
    // 真正的状态文本只能走 TextPattern 读，验收脚本取不到。
    statusLabel_->setObjectName(QStringLiteral("secondaryText"));
    layout->addWidget(statusLabel_);

    results_ = new QListWidget(this);
    results_->setObjectName(QStringLiteral("searchResults"));
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(results_, 1);
    texts_.bind([this] { results_->setAccessibleName(tr("搜索结果")); });

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
            statusMode_ = StatusMode::Prompt;
            updateStatus();
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
    statusMode_ = StatusMode::Prompt;
    updateStatus();

    if (controller_ == nullptr) {
        return;
    }
    // 控制器会被换掉：页面结构一变，文本控制器就重建了。
    // 它析构时要把指针清掉，不然这里拿到的就是野指针。
    connect(controller_, &QObject::destroyed, this, [this]() {
        controller_ = nullptr;
        clearResults();
    });
    connect(controller_, &TextController::searchStarted, this, [this]() {
        stopButton_->setEnabled(true);
        statusMode_ = StatusMode::Live;
        statusLabel_->setText(tr("正在搜索…"));
    });
    connect(controller_, &TextController::searchHitsFound, this, [this](int count) {
        Q_UNUSED(count);
        refreshResults();
    });
    connect(controller_, &TextController::searchFinished, this, [this](bool cancelled) {
        stopButton_->setEnabled(false);
        // 取消后的汇总文案也交给 updateStatus() 生成，切换语言时能原样重译。
        statusMode_ = cancelled ? StatusMode::Cancelled : StatusMode::Live;
        refreshResults();
    });
}

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
    SearchOptions options;
    options.caseSensitive = caseSensitiveBox_ != nullptr && caseSensitiveBox_->isChecked();
    options.wholeWord = wholeWordBox_ != nullptr && wholeWordBox_->isChecked();
    options.regex = regexBox_ != nullptr && regexBox_->isChecked();
    if (fromPageSpin_ != nullptr && fromPageSpin_->value() > 0) {
        options.firstPage = fromPageSpin_->value() - 1;
    }
    if (toPageSpin_ != nullptr && toPageSpin_->value() > 0) {
        options.lastPage = toPageSpin_->value() - 1;
    }
    controller_->startSearch(query, options);
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
    // 只补新增的条目：后台搜索是逐页推进的，每命中一页就重建整张列表，
    // 长文档下会退化成 O(n²)（500 页 = 12.5 万次条目创建），界面直接卡住。
    // 换关键词时 runSearch() 已经把两边都清空了，这里从空列表往后追加即可。
    while (results_->count() < hits.size()) {
        const SearchHit& hit = hits.at(results_->count());
        auto* item = new QListWidgetItem(
            tr("第 %1 页 · %2").arg(hit.pageIndex + 1).arg(hit.snippet), results_);
        item->setData(Qt::UserRole, hit.pageIndex);
        item->setToolTip(hit.snippet);
    }
    while (results_->count() > hits.size()) {
        delete results_->takeItem(results_->count() - 1);
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

void SearchPanel::refreshResultTexts() {
    if (controller_ == nullptr) {
        return;
    }
    const QVector<SearchHit> hits = controller_->searchHits();
    const int count = qMin(results_->count(), static_cast<int>(hits.size()));
    for (int i = 0; i < count; ++i) {
        const SearchHit& hit = hits.at(i);
        results_->item(i)->setText(
            tr("第 %1 页 · %2").arg(hit.pageIndex + 1).arg(hit.snippet));
    }
}

void SearchPanel::updateStatus() {
    if (statusMode_ == StatusMode::Prompt) {
        statusLabel_->setText(tr("输入关键词开始搜索"));
        return;
    }
    const int count = results_->count();
    if (statusMode_ == StatusMode::Cancelled) {
        statusLabel_->setText(tr("搜索已取消（共 %1 处结果）").arg(count));
        return;
    }
    // 搜索还在跑的时候，状态要一直显示「正在搜索」，
    // 否则长文档搜索刚开始就被「共 N 处」盖掉，用户以为已经搜完了。
    if (controller_ != nullptr && controller_->isSearching()) {
        statusLabel_->setText(tr("正在搜索…（已找到 %1 处）").arg(count));
        return;
    }
    if (count == 0) {
        statusLabel_->setText(tr("未找到匹配结果"));
    } else {
        statusLabel_->setText(tr("共 %1 处结果").arg(count));
    }
}

void SearchPanel::retranslateUi() {
    texts_.retranslate();
    // 结果项前缀与状态行是随搜索状态变化的文案：按当前语言重新生成。
    refreshResultTexts();
    updateStatus();
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
