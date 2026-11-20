#pragma once

#include "i18n/Translate.h"
#include "pdf/security/PdfSecurity.h"

#include <QDialog>

class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;

namespace aipdf::app {

// 权限管理面板：**事后单独改权限位**，不用重新走一遍「加密与权限」。
//
// 为什么要有这个面板：以前权限位只能在保存时顺手写进去（EncryptionDialog），
// 已经加密的文档想改权限，只能解密 → 重新加密 → 重新设一遍所有选项。
// 这里直接把「当前权限位」摆出来让用户勾选，改完由调用方重新加密落盘。
//
// 语义（重要）：改权限 = **用所有者口令解密后重新加密**。
//   PDF 2.0 / R6（AES-256）下，/P、/Perms、/O、/OE、/U、/UE 全部由口令派生、
//   互相绑定；原地改 /P 会得到一份「/P 与 /Perms 打架」的加密字典
//   （实测：qpdf 会报「/Perms field in encryption dictionary doesn't match
//   expected value」，有效权限一点没变）。所以只能整体重新加密。
//
// 面板只负责收集参数，不碰文档：确认之后由 PdfView 去解密、重加密、落盘。
class PermissionPanelDialog final : public QDialog {
    Q_OBJECT

public:
    // 用户确认后的结果（只在 accepted 之后有意义）。
    struct Result {
        pdf::PdfPermissions permissions;
        QString ownerPassword;  // 所有者口令：既用来解密，也是新文档的所有者口令
        QString userPassword;   // 新的打开口令（可为空 = 无需口令即可打开）
    };

    explicit PermissionPanelDialog(const pdf::PdfPermissions& current, QWidget* parent = nullptr);

    Result result() const;

    // 校验当前输入能不能用。
    //
    // 为什么单独暴露：accept() 里要弹提示框（模态），单测没法直接驱动；
    // 把「判据」独立出来，界面只负责把失败原因显示出来。
    bool validate(QString* error) const;

protected:
    // 校验：所有者口令不能为空；打开口令不能与所有者口令相同。
    void accept() override;

private:
    void buildUi(const pdf::PdfPermissions& current);
    void retranslateUi();

    QGroupBox* permissionGroup_ = nullptr;
    QCheckBox* printing_ = nullptr;
    QCheckBox* highQualityPrinting_ = nullptr;
    QCheckBox* copying_ = nullptr;
    QCheckBox* accessibility_ = nullptr;
    QCheckBox* annotations_ = nullptr;
    QCheckBox* formFilling_ = nullptr;
    QCheckBox* assembly_ = nullptr;
    QCheckBox* modifying_ = nullptr;
    QLineEdit* ownerPassword_ = nullptr;
    QLineEdit* userPassword_ = nullptr;
    QLabel* hint_ = nullptr;
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
