#include "app/HiddenDataDialog.h"

#include "i18n/Translate.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {

// id -> 中文标题。
//
// 顺序按「先 innocuous 后要紧」排：属性、脚本、动作这些是用户主动勾的，
// 嵌入文件排在后面，因为它删了最难恢复。
struct TitleEntry {
    const char* id;
    const char* title;
};

const TitleEntry kTitles[] = {
    {"metadata", "文档属性（标题、作者、主题等）"},
    {"metadata-streams", "元数据流（XMP 等）"},
    {"incremental-history", "增量历史里的旧版本"},
    {"scripts", "文档脚本（JavaScript）"},
    {"actions", "自动动作（打开时 / 关闭时触发）"},
    {"form-data", "表单字段的已填值"},
    {"thumbnails", "页面缩略图"},
    {"annotations", "注释（含隐藏注释）"},
    {"invisible-text", "不可见文字层"},
    {"layers", "图层（OCG）"},
    {"xfa", "XFA 表单数据"},
    {"signatures", "数字签名"},
    {"file-attachments", "文件附件的引用点"},
    {"embedded-files", "嵌入文件本体"},
};

}  // namespace

QString hiddenDataCategoryTitle(const QString& id)
{
    for (const TitleEntry& entry : kTitles) {
        if (id == QLatin1String(entry.id)) {
            return i18n::tr("aipdf::app::HiddenDataDialog", entry.title);
        }
    }
    // 认不出来的 id 原样返回：编一句中文只会让人更看不懂。
    return id;
}

QString hiddenDataCategoryCountText(const pdf::HiddenDataCategory& category)
{
    if (category.itemCount <= 0) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "这份文档里没有");
    }
    return i18n::tr("aipdf::app::HiddenDataDialog", "%1 项").arg(category.itemCount);
}

bool HiddenDataDialog::defaultChecked(const QString& id)
{
    return id != QLatin1String("embedded-files") && id != QLatin1String("file-attachments");
}

HiddenDataDialog::HiddenDataDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(i18n::tr("aipdf::app::HiddenDataDialog", "清理隐藏信息"));
    setObjectName(QStringLiteral("hiddenDataDialog"));

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        i18n::tr("aipdf::app::HiddenDataDialog",
                 "勾掉要清理的类别。页面正文、可见批注和表单域结构都会保留；"
                 "嵌入文件与附件默认不勾，勾了才会删。"),
        this);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("hiddenDataDialog.hint"));
    layout->addWidget(hint);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("hiddenDataDialog.tree"));
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({i18n::tr("aipdf::app::HiddenDataDialog", "类别"),
                             i18n::tr("aipdf::app::HiddenDataDialog", "这份文档里有")});
    tree_->header()->setStretchLastSection(true);
    layout->addWidget(tree_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(i18n::tr("aipdf::app::HiddenDataDialog", "清理"));
    buttons->button(QDialogButtonBox::Cancel)->setText(i18n::tr("aipdf::app::HiddenDataDialog", "取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void HiddenDataDialog::setCategories(const QVector<pdf::HiddenDataCategory>& categories)
{
    categories_ = categories;
    rebuild();
}

void HiddenDataDialog::rebuild()
{
    if (tree_ == nullptr) {
        return;
    }
    tree_->clear();
    for (const pdf::HiddenDataCategory& category : categories_) {
        auto* item = new QTreeWidgetItem(tree_);
        item->setText(0, hiddenDataCategoryTitle(category.id));
        item->setText(1, hiddenDataCategoryCountText(category));
        item->setData(0, Qt::UserRole, category.id);
        // 这份文档里没有的东西没必要勾 —— 勾了也没得清。
        const bool hasItems = category.itemCount > 0;
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, (hasItems && defaultChecked(category.id)) ? Qt::Checked : Qt::Unchecked);
        if (!hasItems) {
            item->setDisabled(true);
        }
    }
}

QStringList HiddenDataDialog::selectedCategoryIds() const
{
    QStringList ids;
    if (tree_ == nullptr) {
        return ids;
    }
    for (int row = 0; row < tree_->topLevelItemCount(); ++row) {
        const QTreeWidgetItem* item = tree_->topLevelItem(row);
        if (item == nullptr || item->checkState(0) != Qt::Checked) {
            continue;
        }
        ids.append(item->data(0, Qt::UserRole).toString());
    }
    return ids;
}

QStringList HiddenDataDialog::categoryTitles() const
{
    QStringList titles;
    for (const pdf::HiddenDataCategory& category : categories_) {
        titles.append(hiddenDataCategoryTitle(category.id));
    }
    return titles;
}

}  // namespace aipdf::app
