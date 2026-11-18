#pragma once

#include "i18n/Translate.h"
#include "pdf/security/PdfSecurity.h"

#include <QDialog>

class QCheckBox;
class QGroupBox;
class QLineEdit;
class QLabel;

namespace aipdf::app {

// 加密与权限对话框：设置打开密码、所有者密码和权限位。
//
// 只负责收集参数，不碰文档：确认之后由 PdfView 记住这份设置，
// 下一次保存时由 DocumentWriter 统一加密落盘。
class EncryptionDialog final : public QDialog {
    Q_OBJECT

public:
    explicit EncryptionDialog(const pdf::PdfEncryptionOptions& current,
                              bool enabled,
                              QWidget* parent = nullptr);

    // 用户确认后的加密设置（只在 accepted 之后有意义）。
    pdf::PdfEncryptionOptions options() const;
    // 是否勾选了「保存时加密」。取消勾选表示移除加密。
    bool encryptionEnabled() const;

protected:
    void accept() override;

private:
    void buildUi(const pdf::PdfEncryptionOptions& current);
    void retranslateUi();
    void updateEnabledState();

    QCheckBox* enable_ = nullptr;
    QGroupBox* passwordGroup_ = nullptr;
    QGroupBox* permissionGroup_ = nullptr;
    QLineEdit* userPassword_ = nullptr;
    QLineEdit* userPasswordConfirm_ = nullptr;
    QLineEdit* ownerPassword_ = nullptr;
    QLabel* openHint_ = nullptr;
    QCheckBox* printing_ = nullptr;
    QCheckBox* highQualityPrinting_ = nullptr;
    QCheckBox* copying_ = nullptr;
    QCheckBox* annotations_ = nullptr;
    QCheckBox* formFilling_ = nullptr;
    QCheckBox* assembly_ = nullptr;
    QCheckBox* modifying_ = nullptr;
    QLabel* accessibilityHint_ = nullptr;
    QLabel* ownerHint_ = nullptr;
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
