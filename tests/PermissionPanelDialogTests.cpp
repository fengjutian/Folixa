// 权限管理面板的单元测试。
//
// 盯的是两件事：
//   1) 面板把**当前权限位**如实摆出来，勾选改动后 result() 跟着变；
//   2) 两条硬约束拦得住：所有者口令不能为空、打开口令不能与所有者口令相同
//      （R6 下两者相同 = 拿到打开口令的人就是所有者，权限限制形同虚设）。

#include "app/dialogs/PermissionPanelDialog.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QtTest>

using aipdf::app::PermissionPanelDialog;
using aipdf::pdf::PdfPermissions;

class PermissionPanelDialogTests final : public QObject {
    Q_OBJECT

private slots:
    void startsWithCurrentPermissions();
    void resultReflectsCheckboxState();
    void accessibilityBitIsAlwaysAllowedAndLocked();
    void ownerPasswordIsRequired();
    void userPasswordMustDifferFromOwnerPassword();
};

void PermissionPanelDialogTests::startsWithCurrentPermissions() {
    PdfPermissions current;
    current.allowPrinting = true;
    current.allowHighQualityPrinting = false;
    current.allowCopying = true;
    current.allowAnnotations = false;
    current.allowFormFilling = true;
    current.allowAssembly = false;
    current.allowModifying = true;

    PermissionPanelDialog dialog(current);

    // 权限位复选框按初始状态呈现（用 findChild 从**对话框**上找，不要写成测试类成员）。
    const auto result = dialog.result();
    QVERIFY(result.permissions.allowPrinting);
    QVERIFY(!result.permissions.allowHighQualityPrinting);
    QVERIFY(result.permissions.allowCopying);
    QVERIFY(!result.permissions.allowAnnotations);
    QVERIFY(result.permissions.allowFormFilling);
    QVERIFY(!result.permissions.allowAssembly);
    QVERIFY(result.permissions.allowModifying);

    // 两个口令框都在。
    QVERIFY(dialog.findChild<QLineEdit*>(QStringLiteral("permissionPanel.ownerPassword")) != nullptr);
    QVERIFY(dialog.findChild<QLineEdit*>(QStringLiteral("permissionPanel.userPassword")) != nullptr);
}

void PermissionPanelDialogTests::resultReflectsCheckboxState() {
    PdfPermissions current;
    current.allowPrinting = true;
    current.allowCopying = false;
    PermissionPanelDialog dialog(current);

    // 找到「允许复制文本与图片」那个复选框。
    //
    // 用 objectName 找，不按界面文案找：文案会随语言变，名字不会。
    auto* copying =
        dialog.findChild<QCheckBox*>(QStringLiteral("permissionPanel.copying"));
    QVERIFY(copying != nullptr);
    QVERIFY(!dialog.result().permissions.allowCopying);

    copying->setChecked(true);
    QVERIFY(dialog.result().permissions.allowCopying);

    // 口令也照实读出来。
    auto* owner = dialog.findChild<QLineEdit*>(QStringLiteral("permissionPanel.ownerPassword"));
    auto* user = dialog.findChild<QLineEdit*>(QStringLiteral("permissionPanel.userPassword"));
    QVERIFY(owner != nullptr);
    QVERIFY(user != nullptr);
    owner->setText(QStringLiteral("boss"));
    user->setText(QStringLiteral("reader"));
    QCOMPARE(dialog.result().ownerPassword, QStringLiteral("boss"));
    QCOMPARE(dialog.result().userPassword, QStringLiteral("reader"));
}

void PermissionPanelDialogTests::accessibilityBitIsAlwaysAllowedAndLocked() {
    PdfPermissions current;
    current.allowAccessibility = false;  // 就算文档里没有，面板也必须给上
    PermissionPanelDialog dialog(current);

    auto* accessibility =
        dialog.findChild<QCheckBox*>(QStringLiteral("permissionPanel.accessibility"));
    QVERIFY(accessibility != nullptr);
    // PDF 2.0 规定这一位始终允许：勾上、且不给用户关掉。
    QVERIFY(accessibility->isChecked());
    QVERIFY(!accessibility->isEnabled());
    QVERIFY(dialog.result().permissions.allowAccessibility);
}

void PermissionPanelDialogTests::ownerPasswordIsRequired() {
    const PdfPermissions empty;
    PermissionPanelDialog dialog(empty);
    auto* owner = dialog.findChild<QLineEdit*>(QStringLiteral("permissionPanel.ownerPassword"));
    QVERIFY(owner != nullptr);

    QString error;
    QVERIFY(!dialog.validate(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("所有者")), qPrintable(error));

    owner->setText(QStringLiteral("boss"));
    QVERIFY(dialog.validate(&error));
}

void PermissionPanelDialogTests::userPasswordMustDifferFromOwnerPassword() {
    const PdfPermissions empty;
    PermissionPanelDialog dialog(empty);
    auto* owner = dialog.findChild<QLineEdit*>(QStringLiteral("permissionPanel.ownerPassword"));
    auto* user = dialog.findChild<QLineEdit*>(QStringLiteral("permissionPanel.userPassword"));
    QVERIFY(owner != nullptr);
    QVERIFY(user != nullptr);

    owner->setText(QStringLiteral("boss"));
    user->setText(QStringLiteral("boss"));
    QString error;
    QVERIFY(!dialog.validate(&error));
    QVERIFY2(error.contains(QStringLiteral("打开口令")), qPrintable(error));

    // 打开口令留空是可以的（打开不需要口令，权限位仍然写入）。
    user->clear();
    QVERIFY(dialog.validate(&error));
}

QTEST_MAIN(PermissionPanelDialogTests)
#include "PermissionPanelDialogTests.moc"
