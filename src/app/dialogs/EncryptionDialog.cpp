#include "app/dialogs/EncryptionDialog.h"

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

EncryptionDialog::EncryptionDialog(const pdf::PdfEncryptionOptions& current,
                                   bool enabled,
                                   QWidget* parent)
    : QDialog(parent) {
    buildUi(current);
    enable_->setChecked(enabled);
    updateEnabledState();
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &EncryptionDialog::retranslateUi);
}

void EncryptionDialog::buildUi(const pdf::PdfEncryptionOptions& current) {
    setObjectName(QStringLiteral("encryptionDialog"));
    setModal(true);
    resize(480, 520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    enable_ = new QCheckBox(this);
    enable_->setObjectName(QStringLiteral("encryptionEnabled"));
    texts_.bind([this] { enable_->setText(tr("保存时用 AES-256 加密这份文档")); });
    connect(enable_, &QCheckBox::toggled, this, [this] { updateEnabledState(); });
    layout->addWidget(enable_);

    auto* passwordGroup = new QGroupBox(this);
    passwordGroup_ = passwordGroup;
    passwordGroup->setObjectName(QStringLiteral("encryptionPasswordGroup"));
    auto* passwordForm = new QFormLayout(passwordGroup);
    passwordForm->setSpacing(8);

    userPassword_ = new QLineEdit(passwordGroup);
    userPassword_->setObjectName(QStringLiteral("encryptionUserPassword"));
    userPassword_->setEchoMode(QLineEdit::Password);
    userPassword_->setText(current.userPassword);
    userPasswordConfirm_ = new QLineEdit(passwordGroup);
    userPasswordConfirm_->setObjectName(QStringLiteral("encryptionUserPasswordConfirm"));
    userPasswordConfirm_->setEchoMode(QLineEdit::Password);
    userPasswordConfirm_->setText(current.userPassword);
    ownerPassword_ = new QLineEdit(passwordGroup);
    ownerPassword_->setObjectName(QStringLiteral("encryptionOwnerPassword"));
    ownerPassword_->setEchoMode(QLineEdit::Password);
    ownerPassword_->setText(current.ownerPassword);

    auto* userLabel = new QLabel(passwordGroup);
    texts_.bind([this, userLabel] { userLabel->setText(tr("打开密码（留空表示不需要密码）")); });
    passwordForm->addRow(userLabel, userPassword_);
    auto* confirmLabel = new QLabel(passwordGroup);
    texts_.bind([this, confirmLabel] { confirmLabel->setText(tr("确认打开密码")); });
    passwordForm->addRow(confirmLabel, userPasswordConfirm_);
    auto* ownerLabel = new QLabel(passwordGroup);
    texts_.bind([this, ownerLabel] {
        ownerLabel->setText(tr("所有者密码（留空自动生成）"));
    });
    passwordForm->addRow(ownerLabel, ownerPassword_);

    openHint_ = new QLabel(passwordGroup);
    openHint_->setObjectName(QStringLiteral("secondaryText"));
    openHint_->setWordWrap(true);
    texts_.bind([this] {
        openHint_->setText(tr("打开密码为空：文件可以直接打开，权限位只对遵守规范的阅读器生效。"));
    });
    passwordForm->addRow(openHint_);

    ownerHint_ = new QLabel(passwordGroup);
    ownerHint_->setObjectName(QStringLiteral("secondaryText"));
    ownerHint_->setWordWrap(true);
    texts_.bind([this] {
        ownerHint_->setText(tr("所有者密码用于修改权限；留空时由程序随机生成，无法事后解除限制。"));
    });
    passwordForm->addRow(ownerHint_);
    layout->addWidget(passwordGroup);

    auto* permissionGroup = new QGroupBox(this);
    permissionGroup_ = permissionGroup;
    permissionGroup->setObjectName(QStringLiteral("encryptionPermissionsGroup"));
    auto* permissionLayout = new QVBoxLayout(permissionGroup);
    permissionLayout->setSpacing(6);

    const pdf::PdfPermissions& permissions = current.permissions;
    auto makeCheck = [permissionGroup, permissionLayout, this](const char* source,
                                                              bool checked) {
        auto* box = new QCheckBox(permissionGroup);
        box->setChecked(checked);
        texts_.bind([this, box, source] { box->setText(tr(source)); });
        permissionLayout->addWidget(box);
        return box;
    };
    printing_ = makeCheck(QT_TRANSLATE_NOOP("aipdf::app::EncryptionDialog", "允许打印"),
                          permissions.allowPrinting);
    highQualityPrinting_ = makeCheck(
        QT_TRANSLATE_NOOP("aipdf::app::EncryptionDialog", "允许高质量打印"), 
        permissions.allowHighQualityPrinting);
    copying_ = makeCheck(QT_TRANSLATE_NOOP("aipdf::app::EncryptionDialog", "允许复制文本与图片"),
                         permissions.allowCopying);
    annotations_ = makeCheck(QT_TRANSLATE_NOOP("aipdf::app::EncryptionDialog", "允许添加批注"),
                             permissions.allowAnnotations);
    formFilling_ = makeCheck(QT_TRANSLATE_NOOP("aipdf::app::EncryptionDialog", "允许填写表单"),
                             permissions.allowFormFilling);
    assembly_ = makeCheck(
        QT_TRANSLATE_NOOP("aipdf::app::EncryptionDialog", "允许插入、删除、旋转页面"),
        permissions.allowAssembly);
    modifying_ = makeCheck(QT_TRANSLATE_NOOP("aipdf::app::EncryptionDialog", "允许其它修改"),
                           permissions.allowModifying);

    accessibilityHint_ = new QLabel(permissionGroup);
    accessibilityHint_->setObjectName(QStringLiteral("secondaryText"));
    accessibilityHint_->setWordWrap(true);
    texts_.bind([this] {
        accessibilityHint_->setText(
            tr("无障碍提取在 PDF 2.0 中始终允许，无法关闭；这是规范要求，不是遗漏。"));
    });
    permissionLayout->addWidget(accessibilityHint_);
    layout->addWidget(permissionGroup);
    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("encryptionButtons"));
    texts_.bind([this, buttons] {
        buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
        buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &EncryptionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    texts_.bind([this] { setWindowTitle(tr("加密与权限")); });
    retranslateUi();
}

void EncryptionDialog::retranslateUi() {
    texts_.retranslate();
}

void EncryptionDialog::updateEnabledState() {
    const bool enabled = enable_ != nullptr && enable_->isChecked();
    if (passwordGroup_ != nullptr) {
        passwordGroup_->setEnabled(enabled);
    }
    if (permissionGroup_ != nullptr) {
        permissionGroup_->setEnabled(enabled);
    }
}

bool EncryptionDialog::encryptionEnabled() const {
    return enable_ != nullptr && enable_->isChecked();
}

pdf::PdfEncryptionOptions EncryptionDialog::options() const {
    pdf::PdfEncryptionOptions options;
    options.userPassword = userPassword_->text();
    options.ownerPassword = ownerPassword_->text();
    options.permissions.allowPrinting = printing_->isChecked();
    options.permissions.allowHighQualityPrinting = highQualityPrinting_->isChecked();
    options.permissions.allowCopying = copying_->isChecked();
    options.permissions.allowAnnotations = annotations_->isChecked();
    options.permissions.allowFormFilling = formFilling_->isChecked();
    options.permissions.allowAssembly = assembly_->isChecked();
    options.permissions.allowModifying = modifying_->isChecked();
    return options;
}

void EncryptionDialog::accept() {
    if (!encryptionEnabled()) {
        // 取消加密不需要校验密码：这几个控件此时是灰的。
        QDialog::accept();
        return;
    }
    if (userPassword_->text() != userPasswordConfirm_->text()) {
        QMessageBox::warning(this, tr("加密与权限"), tr("两次输入的打开密码不一致。"));
        userPasswordConfirm_->setFocus();
        userPasswordConfirm_->selectAll();
        return;
    }
    QDialog::accept();
}

}  // namespace aipdf::app
