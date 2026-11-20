#include "app/dialogs/SemanticOrderSelectionDialog.h"

#include "app/ui/Language.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace aipdf::app {
namespace {

// 列表项里记页号（0 基）。
constexpr int kPageIndexRole = Qt::UserRole;

}  // namespace

SemanticOrderSelectionDialog::SemanticOrderSelectionDialog(const QByteArray& bytes, int currentPage,
                                                           int regionPage, const QRectF& region,
                                                           QWidget* parent)
    : QDialog(parent),
      bytes_(bytes),
      currentPage_(currentPage),
      regionPage_(regionPage),
      region_(region) {
    buildUi();
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged, this,
            &SemanticOrderSelectionDialog::retranslateUi);
}

void SemanticOrderSelectionDialog::buildUi() {
    setObjectName(QStringLiteral("semanticOrderSelectionDialog"));
    setModal(true);
    resize(720, 560);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);

    auto* hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("secondaryText"));
    hint->setWordWrap(true);
    texts_.bind([this, hint] {
        hint->setText(tr("这次只重排**选区里**的语义元素：按「从上到下、从左到右」的视觉顺序"
                         "换位。选区外的内容一个都不碰，也不会重建整篇标签树。"));
    });
    layout->addWidget(hint);

    // 文档里有语义元素的页：只有这些页能进选区。
    //
    // 为什么先读一次文档：对话框要知道自己有哪些页可选。
    const pdf::SemanticOrderScopeReport report = pdf::readSemanticOrderScope(bytes_, {});
    QString readError;
    if (!report.ok()) {
        readError = report.error;
    }

    auto* pageRow = new QHBoxLayout;
    auto* pageLabel = new QLabel(this);
    texts_.bind([this, pageLabel] { pageLabel->setText(tr("要重排的页：")); });
    pageRow->addWidget(pageLabel);

    pageList_ = new QListWidget(this);
    pageList_->setObjectName(QStringLiteral("semanticOrderSelection.pages"));
    pageList_->setSelectionMode(QAbstractItemView::NoSelection);
    // 每页有多少个语义元素：先数一遍，绑定文案时只带数字（不带着整份报告）。
    QHash<int, int> countOfPage;
    for (const pdf::SemanticOrderScopeElement& element : report.elements) {
        countOfPage[element.pageIndex] += 1;
    }
    for (int pageIndex : report.pages) {
        QListWidgetItem* item = new QListWidgetItem(pageList_);
        item->setData(kPageIndexRole, pageIndex);
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setCheckState(pageIndex == currentPage_ ? Qt::Checked : Qt::Unchecked);
        const int count = countOfPage.value(pageIndex);
        texts_.bind([this, item, pageIndex, count] {
            item->setText(tr("第 %1 页（%2 个语义元素）").arg(pageIndex + 1).arg(count));
        });
    }
    pageRow->addWidget(pageList_, 1);
    layout->addLayout(pageRow);

    regionOnly_ = new QCheckBox(this);
    regionOnly_->setObjectName(QStringLiteral("semanticOrderSelection.regionOnly"));
    const bool hasRegion = regionPage_ >= 0 && !region_.isEmpty();
    regionOnly_->setEnabled(hasRegion);
    regionOnly_->setChecked(hasRegion);
    // 成员不能直接被 lambda 捕获（捕获的是 this 之外的那一份拷贝），
    // 所以先把要用的值取成局部量再捕获。
    const int regionPage = regionPage_;
    QCheckBox* regionOnly = regionOnly_;
    texts_.bind([this, regionOnly, regionPage, hasRegion] {
        regionOnly->setText(hasRegion
                                ? tr("只作用于第 %1 页选中的文字区域").arg(regionPage + 1)
                                : tr("只作用于选中的文字区域（当前没有文字选区）"));
    });
    layout->addWidget(regionOnly_);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("secondaryText"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    elementList_ = new QListWidget(this);
    elementList_->setObjectName(QStringLiteral("semanticOrderSelection.elements"));
    layout->addWidget(elementList_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("semanticOrderSelection.accept"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("semanticOrderSelection.cancel"));
    texts_.bind([this, buttons] { buttons->button(QDialogButtonBox::Ok)->setText(tr("按视觉顺序重排")); });
    texts_.bind([this, buttons] { buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消")); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(pageList_, &QListWidget::itemChanged, this, [this] { refreshPreview(); });
    connect(regionOnly_, &QCheckBox::toggled, this, [this] { refreshPreview(); });

    texts_.bind([this] { setWindowTitle(tr("应用区域阅读顺序（选区）")); });
    retranslateUi();  // 里面会刷新一次预览

    if (!readError.isEmpty()) {
        summary_->setText(readError);
    }
}

void SemanticOrderSelectionDialog::retranslateUi() {
    texts_.retranslate();
    refreshPreview();
}

pdf::SemanticOrderScope SemanticOrderSelectionDialog::currentScope() const {
    pdf::SemanticOrderScope scope;
    if (regionOnly_ != nullptr && regionOnly_->isEnabled() && regionOnly_->isChecked()) {
        // 区域模式：选区就是那一块。
        scope.pageIndexes.append(regionPage_);
        scope.regions.append(region_);
        return scope;
    }
    if (pageList_ == nullptr) return scope;
    for (int row = 0; row < pageList_->count(); ++row) {
        QListWidgetItem* item = pageList_->item(row);
        if (item->checkState() != Qt::Checked) continue;
        scope.pageIndexes.append(item->data(kPageIndexRole).toInt());
        scope.regions.append(QRectF());
    }
    return scope;
}

void SemanticOrderSelectionDialog::refreshPreview() {
    if (elementList_ == nullptr) return;
    elementList_->clear();

    const pdf::SemanticOrderScope scope = currentScope();
    if (scope.isEmpty()) {
        summary_->setText(tr("还没有选区：勾选至少一页，或勾上「只作用于选中的文字区域」。"));
        return;
    }

    const pdf::SemanticOrderScopeReport report = pdf::readSemanticOrderScope(bytes_, scope);
    if (!report.ok()) {
        summary_->setText(report.error);
        return;
    }

    int outside = 0;
    for (const pdf::SemanticOrderScopeElement& element : report.elements) {
        if (element.inScope) continue;
        ++outside;
    }
    summary_->setText(tr("选区里有 %1 个语义元素；选区外还有 %2 个，不会被改动。")
                          .arg(report.scopedCount)
                          .arg(outside));

    for (const pdf::SemanticOrderScopeElement& element : report.elements) {
        if (!element.inScope) continue;
        QString text = element.text.trimmed();
        if (text.size() > 40) text = text.left(40) + QStringLiteral("…");
        QListWidgetItem* item = new QListWidgetItem(elementList_);
        const int rank = element.visualRank;
        // 这里**不**用 texts_.bind：预览是每次刷新重建的，
        // 登记的绑定会指着已经删掉的项目，切换语言时就会写到野指针上。
        // 切换语言走的是 retranslateUi() → refreshPreview()，整份重建。
        item->setText(rank >= 0
                          ? tr("第 %1 页｜%2：%3　（视觉顺序第 %4）")
                                .arg(element.pageIndex + 1)
                                .arg(element.role, text)
                                .arg(rank + 1)
                          : tr("第 %1 页｜%2：%3　（位置取不到，不参与重排）")
                                .arg(element.pageIndex + 1)
                                .arg(element.role, text));
    }
}

pdf::SemanticOrderScope SemanticOrderSelectionDialog::scope() const {
    return currentScope();
}

}  // namespace aipdf::app
