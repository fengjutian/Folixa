#include "app/EncryptionDialog.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QtTest>

using aipdf::app::EncryptionDialog;
using aipdf::pdf::PdfEncryptionOptions;
using aipdf::pdf::PdfPermissions;

class EncryptionDialogTests final : public QObject {
    Q_OBJECT

private slots:
    void startsWithSavedValues();
    void uncheckingEncryptionDisablesInputs();
    void optionsReflectCheckboxState();
};

void EncryptionDialogTests::startsWithSavedValues() {
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader");
    options.ownerPassword = QStringLiteral("owner");
    options.permissions.allowCopying = false;
    options.permissions.allowPrinting = true;

    EncryptionDialog dialog(options, true);
    auto* enable = dialog.findChild<QCheckBox*>(QStringLiteral("encryptionEnabled"));
    auto* user = dialog.findChild<QLineEdit*>(QStringLiteral("encryptionUserPassword"));
    auto* confirm = dialog.findChild<QLineEdit*>(QStringLiteral("encryptionUserPasswordConfirm"));
    auto* owner = dialog.findChild<QLineEdit*>(QStringLiteral("encryptionOwnerPassword"));
    QVERIFY(enable != nullptr);
    QVERIFY(user != nullptr);
    QVERIFY(confirm != nullptr);
    QVERIFY(owner != nullptr);

    QVERIFY(enable->isChecked());
    QCOMPARE(user->text(), QStringLiteral("reader"));
    QCOMPARE(confirm->text(), QStringLiteral("reader"));
    QCOMPARE(owner->text(), QStringLiteral("owner"));
    QVERIFY(dialog.encryptionEnabled());
}

void EncryptionDialogTests::uncheckingEncryptionDisablesInputs() {
    EncryptionDialog dialog(PdfEncryptionOptions(), true);
    auto* enable = dialog.findChild<QCheckBox*>(QStringLiteral("encryptionEnabled"));
    auto* passwords = dialog.findChild<QGroupBox*>(QStringLiteral("encryptionPasswordGroup"));
    auto* permissions = dialog.findChild<QGroupBox*>(QStringLiteral("encryptionPermissionsGroup"));
    QVERIFY(enable != nullptr);
    QVERIFY(passwords != nullptr);
    QVERIFY(permissions != nullptr);
    QVERIFY(passwords->isEnabled());

    enable->setChecked(false);
    QVERIFY(!dialog.encryptionEnabled());
    QVERIFY(!passwords->isEnabled());
    QVERIFY(!permissions->isEnabled());

    // 再打开：输入区要恢复可用，不能留下灰掉的死界面。
    enable->setChecked(true);
    QVERIFY(passwords->isEnabled());
    QVERIFY(permissions->isEnabled());
}

void EncryptionDialogTests::optionsReflectCheckboxState() {
    PdfEncryptionOptions options;
    options.permissions.allowCopying = false;
    options.permissions.allowPrinting = true;
    options.permissions.allowAssembly = false;

    EncryptionDialog dialog(options, true);
    // 逐个按钮按文字找：对话框里的控件很多，按顺序取太脆。
    const QList<QCheckBox*> boxes = dialog.findChildren<QCheckBox*>();
    QCheckBox* copyBox = nullptr;
    QCheckBox* assemblyBox = nullptr;
    for (QCheckBox* box : boxes) {
        if (box->text().contains(QStringLiteral("复制"))) {
            copyBox = box;
        } else if (box->text().contains(QStringLiteral("旋转页面"))) {
            assemblyBox = box;
        }
    }
    QVERIFY(copyBox != nullptr);
    QVERIFY(assemblyBox != nullptr);
    QVERIFY(!copyBox->isChecked());
    QVERIFY(!assemblyBox->isChecked());

    copyBox->setChecked(true);
    assemblyBox->setChecked(true);
    const PdfPermissions permissions = dialog.options().permissions;
    QVERIFY(permissions.allowCopying);
    QVERIFY(permissions.allowAssembly);
    QVERIFY(permissions.allowPrinting);
}

QTEST_MAIN(EncryptionDialogTests)
#include "EncryptionDialogTests.moc"
