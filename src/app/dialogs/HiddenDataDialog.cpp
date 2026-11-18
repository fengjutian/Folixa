#include "app/dialogs/HiddenDataDialog.h"

#include "i18n/Translate.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace aipdf::app {

QString hiddenDataCategoryTitle(const QString& id)
{
    // 每条都写成字面量：翻译工具（lupdate）只认 tr() 的字面量参数，
    // 放进表里再按 id 取，工具抓不到，译文也就永远出不来。
    if (id == QLatin1String("metadata")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "文档属性（标题、作者、主题等）");
    }
    if (id == QLatin1String("metadata-streams")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "元数据流（XMP 等）");
    }
    if (id == QLatin1String("incremental-history")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "增量历史里的旧版本");
    }
    if (id == QLatin1String("scripts")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "文档脚本（JavaScript）");
    }
    if (id == QLatin1String("actions")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "自动动作（打开时 / 关闭时触发）");
    }
    if (id == QLatin1String("annotation-actions")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "批注上的自动动作（进入 / 点开时触发）");
    }
    if (id == QLatin1String("field-actions")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "表单域上的自动动作（校验 / 格式化脚本）");
    }
    if (id == QLatin1String("form-data")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "表单字段的已填值");
    }
    if (id == QLatin1String("thumbnails")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "页面缩略图");
    }
    if (id == QLatin1String("annotations")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "注释（含隐藏注释）");
    }
    if (id == QLatin1String("invisible-text")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "不可见文字层");
    }
    if (id == QLatin1String("layers")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "图层（OCG）");
    }
    if (id == QLatin1String("xfa")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "XFA 表单数据");
    }
    if (id == QLatin1String("signatures")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "数字签名");
    }
    if (id == QLatin1String("file-attachments")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "文件附件的引用点");
    }
    if (id == QLatin1String("embedded-files")) {
        return i18n::tr("aipdf::app::HiddenDataDialog", "嵌入文件本体");
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

QStringList HiddenDataDialog::defaultCleanupCategoryIds()
{
    // 保守白名单：清掉了不会有人心疼，而且本来就是「隐藏信息」的正主。
    //
    //   metadata          文档属性（标题、作者、主题……）
    //   metadata-streams  XMP 之类的元数据流
    //   incremental-history 旧版本（写出器重写文件时本来就会拍平，勾不勾都清）
    //   scripts           文档级 JavaScript
    //   actions           打开/关闭时触发的自动动作
    //   annotation-actions 批注上的自动动作（点开就跑的东西，与文档自动动作同类）
    //   thumbnails        预览图（派生缓存，删了无损）
    //
    // 不在名单里的（签名、表单已填值、OCR 文字层、图层、XFA、隐藏批注、
    // 嵌入文件本体、附件引用点、表单域上的自动动作）默认不勾 ——
    // 理由见头文件里的注释。表单域的动作默认不勾：校验与格式化脚本常常是
    // 表单特意写好的功能，删了用户会心疼；批注上的动作则不然——那是「点开就跑」。
    return {
        QStringLiteral("metadata"),
        QStringLiteral("metadata-streams"),
        QStringLiteral("incremental-history"),
        QStringLiteral("scripts"),
        QStringLiteral("actions"),
        QStringLiteral("annotation-actions"),
        QStringLiteral("thumbnails"),
    };
}

bool HiddenDataDialog::defaultChecked(const QString& id)
{
    return defaultCleanupCategoryIds().contains(id);
}

HiddenDataDialog::HiddenDataDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(i18n::tr("aipdf::app::HiddenDataDialog", "清理隐藏信息"));
    setObjectName(QStringLiteral("hiddenDataDialog"));

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        i18n::tr("aipdf::app::HiddenDataDialog",
                 "勾掉要清理的类别。默认只清文档属性、元数据流、旧版本、脚本、"
                 "自动动作、批注动作和缩略图；签名、表单已填值、不可见文字层、图层与嵌入文件"
                 "默认不勾，勾了才会删。"),
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
