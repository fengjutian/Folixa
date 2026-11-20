#include "app/dialogs/PermissionPanelDialog.h"

#include "app/ui/Language.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace aipdf::app {

PermissionPanelDialog::PermissionPanelDialog(const pdf::PdfPermissions& current, QWidget* parent)
    : QDialog(parent) {
    buildUi(current);
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &PermissionPanelDialog::retranslateUi);
}

void PermissionPanelDialog::buildUi(const pdf::PdfPermissions& current) {
    setObjectName(QStringLiteral("permissionPanelDialog"));
    setModal(true);
    resize(480, 520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("secondaryText"));
    hint_->setWordWrap(true);
    texts_.bind([this] {
        hint_->setText(tr(
            "改权限会用所有者口令把文档解开、再按新的权限位重新加密（PDF 2.0 / AES-256）。"
            "文档内容不会变；所有者口令保持不变。"));
    });
    layout->addWidget(hint_);

    auto* passwordGroup = new QGroupBox(this);
    passwordGroup->setObjectName(QStringLiteral("permissionPanelPasswordGroup"));
    auto* passwordForm = new QFormLayout(passwordGroup);
    passwordForm->setSpacing(8);

    ownerPassword_ = new QLineEdit(passwordGroup);
    ownerPassword_->setObjectName(QStringLiteral("permissionPanel.ownerPassword"));
    ownerPassword_->setEchoMode(QLineEdit::Password);
    auto* ownerLabel = new QLabel(passwordGroup);
    texts_.bind([this, ownerLabel] {
        ownerLabel->setText(tr("所有者口令（用来解开文档，也是改完后的所有者口令）"));
    });
    passwordForm->addRow(ownerLabel, ownerPassword_);

    userPassword_ = new QLineEdit(passwordGroup);
    userPassword_->setObjectName(QStringLiteral("permissionPanel.userPassword"));
    userPassword_->setEchoMode(QLineEdit::Password);
    auto* userLabel = new QLabel(passwordGroup);
    texts_.bind([this, userLabel] {
        userLabel->setText(tr("新的打开口令（留空表示打开不需要口令）"));
    });
    passwordForm->addRow(userLabel, userPassword_);

    auto* userHint = new QLabel(passwordGroup);
    userHint->setObjectName(QStringLiteral("secondaryText"));
    userHint->setWordWrap(true);
    texts_.bind([this, userHint] {
        userHint->setText(tr(
            "打开口令不能与所有者口令相同：两者相同时，拿到打开口令的人就是所有者，权限限制形同虚设。"));
    });
    passwordForm->addRow(userHint);
    layout->addWidget(passwordGroup);

    auto* permissionGroup = new QGroupBox(this);
    permissionGroup_ = permissionGroup;
    permissionGroup->setObjectName(QStringLiteral("permissionPanelPermissionsGroup"));
    auto* permissionLayout = new QVBoxLayout(permissionGroup);
    permissionLayout->setSpacing(6);

    auto makeCheck = [permissionGroup, permissionLayout, this](const char* source,
                                                              const char* objectName,
                                                              bool checked) {
        auto* box = new QCheckBox(permissionGroup);
        // objectName 是给测试与自动化用的稳定抓手：界面文案会随语言变，
        // 名字不会。用文案找控件的测试换个语言就废了。
        box->setObjectName(QString::fromLatin1(objectName));
        box->setChecked(checked);
        texts_.bind([this, box, source] { box->setText(tr(source)); });
        permissionLayout->addWidget(box);
        return box;
    };
    printing_ = makeCheck(QT_TRANSLATE_NOOP("aipdf::app::PermissionPanelDialog", "允许打印"),
                          "permissionPanel.printing", current.allowPrinting);
    highQualityPrinting_ = makeCheck(
        QT_TRANSLATE_NOOP("aipdf::app::PermissionPanelDialog", "允许高质量打印"),
        "permissionPanel.highQualityPrinting", current.allowHighQualityPrinting);
    copying_ = makeCheck(
        QT_TRANSLATE_NOOP("aipdf::app::PermissionPanelDialog", "允许复制文本与图片"),
        "permissionPanel.copying", current.allowCopying);
    annotations_ = makeCheck(
        QT_TRANSLATE_NOOP("aipdf::app::PermissionPanelDialog", "允许添加批注"),
        "permissionPanel.annotations", current.allowAnnotations);
    formFilling_ = makeCheck(
        QT_TRANSLATE_NOOP("aipdf::app::PermissionPanelDialog", "允许填写表单"),
        "permissionPanel.formFilling", current.allowFormFilling);
    assembly_ = makeCheck(
        QT_TRANSLATE_NOOP("aipdf::app::PermissionPanelDialog", "允许插入、删除、旋转页面"),
        "permissionPanel.assembly", current.allowAssembly);
    modifying_ = makeCheck(QT_TRANSLATE_NOOP("aipdf::app::PermissionPanelDialog", "允许其它修改"),
                           "permissionPanel.modifying", current.allowModifying);

    // 无障碍提取位不能关：PDF 2.0 规定这一位始终允许（实测 qpdf 12.4.2：
    // 即使传 false，写出来的 /P 里这一位也是 1）。
    // 这里显示成勾上且禁用的复选框，并把原因写清楚，不给用户一个按不动的开关。
    accessibility_ = new QCheckBox(permissionGroup);
    accessibility_->setObjectName(QStringLiteral("permissionPanel.accessibility"));
    accessibility_->setChecked(true);
    accessibility_->setEnabled(false);
    texts_.bind([this] {
        accessibility_->setText(
            tr("允许无障碍工具提取内容（PDF 2.0 规定始终允许，不能关闭）"));
    });
    permissionLayout->addWidget(accessibility_);

    layout->addWidget(permissionGroup);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("permissionPanelButtons"));
    texts_.bind([this, buttons] {
        buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
        buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &PermissionPanelDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PermissionPanelDialog::reject);
    layout->addWidget(buttons);

    texts_.bind([this] { setWindowTitle(tr("权限管理")); });
    retranslateUi();
}

void PermissionPanelDialog::retranslateUi() {
    texts_.retranslate();
}

PermissionPanelDialog::Result PermissionPanelDialog::result() const {
    Result outcome;
    outcome.permissions.allowPrinting = printing_->isChecked();
    outcome.permissions.allowHighQualityPrinting = highQualityPrinting_->isChecked();
    outcome.permissions.allowCopying = copying_->isChecked();
    outcome.permissions.allowAccessibility = true;  // 协议规定始终允许
    outcome.permissions.allowAnnotations = annotations_->isChecked();
    outcome.permissions.allowFormFilling = formFilling_->isChecked();
    outcome.permissions.allowAssembly = assembly_->isChecked();
    outcome.permissions.allowModifying = modifying_->isChecked();
    outcome.ownerPassword = ownerPassword_->text();
    outcome.userPassword = userPassword_->text();
    return outcome;
}

bool PermissionPanelDialog::validate(QString* error) const {
    if (ownerPassword_->text().isEmpty()) {
        if (error != nullptr) {
            *error = tr("所有者口令不能为空：它用来解开这份文档。");
        }
        return false;
    }
    if (!userPassword_->text().isEmpty() && userPassword_->text() == ownerPassword_->text()) {
        if (error != nullptr) {
            *error = tr("打开口令不能与所有者口令相同：那样任何拿到打开口令的人都能绕过权限限制。");
        }
        return false;
    }
    return true;
}

void PermissionPanelDialog::accept() {
    QString error;
    if (!validate(&error)) {
        QMessageBox::warning(this, tr("权限管理"), error);
        if (ownerPassword_->text().isEmpty()) {
            ownerPassword_->setFocus();
        } else {
            userPassword_->setFocus();
        }
        return;
    }
    QDialog::accept();
}

}  // namespace aipdf::app
