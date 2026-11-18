#include "app/FormToolSettingsDialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace aipdf::app {

FormToolSettingsDialog::FormToolSettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("表单工具设置"));
    const pdf::FormToolSettings current = pdf::FormToolSettings::load();
    color_ = current.textColor;

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    font_ = new QComboBox(this);
    // 下拉里显示的是 BaseFont（Helvetica-Bold 这样用户认得），
    // 存的是资源名（HeBo）：/DA 里写的是资源名，两者要对得上。
    for (const QString& resourceName : pdf::FormToolSettings::fontChoices()) {
        const QString base = pdf::FormToolSettings::baseFontName(resourceName);
        font_->addItem(base.isEmpty() ? resourceName : base, resourceName);
    }
    const int fontIndex = font_->findData(current.fontName);
    font_->setCurrentIndex(fontIndex >= 0 ? fontIndex : 0);
    form->addRow(tr("默认字体"), font_);

    size_ = new QDoubleSpinBox(this);
    size_->setRange(4.0, 72.0);
    size_->setDecimals(1);
    size_->setValue(current.fontSize);
    form->addRow(tr("默认字号"), size_);

    colorButton_ = new QPushButton(this);
    colorButton_->setObjectName(QStringLiteral("formToolColor"));
    colorButton_->setText(current.textColor.name());
    connect(colorButton_, &QPushButton::clicked, this, &FormToolSettingsDialog::pickColor);
    form->addRow(tr("文字颜色"), colorButton_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
    layout->addLayout(form);
}

void FormToolSettingsDialog::pickColor() {
    const QColor picked = QColorDialog::getColor(color_, this, tr("选择文字颜色"));
    if (!picked.isValid()) return;
    color_ = picked;
    colorButton_->setText(picked.name());
}

pdf::FormToolSettings FormToolSettingsDialog::settings() const {
    pdf::FormToolSettings result;
    // 取资源名（combo 的 data），不是显示用的 BaseFont。
    result.fontName = font_->currentData().toString();
    if (result.fontName.isEmpty()) {
        result.fontName = QStringLiteral("Helv");
    }
    result.fontSize = size_->value();
    result.textColor = color_;
    return result;
}

}  // namespace aipdf::app
