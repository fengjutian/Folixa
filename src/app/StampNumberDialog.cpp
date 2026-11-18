#include "app/StampNumberDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include "i18n/Translate.h"

namespace aipdf::app {

namespace {

// 编号形式的显示名（界面文案，翻译走 i18n）。
QString styleName(pdf::StampNumberStyle style) {
    switch (style) {
    case pdf::StampNumberStyle::UpperAlpha:
        return i18n::tr("aipdf::app::StampNumberDialog", "大写字母（A、B、C）");
    case pdf::StampNumberStyle::LowerAlpha:
        return i18n::tr("aipdf::app::StampNumberDialog", "小写字母（a、b、c）");
    case pdf::StampNumberStyle::Arabic:
        break;
    }
    return i18n::tr("aipdf::app::StampNumberDialog", "数字（1、2、3）");
}

pdf::StampNumberStyle styleFromIndex(int index) {
    switch (index) {
    case 1: return pdf::StampNumberStyle::UpperAlpha;
    case 2: return pdf::StampNumberStyle::LowerAlpha;
    default: break;
    }
    return pdf::StampNumberStyle::Arabic;
}

int indexFromStyle(pdf::StampNumberStyle style) {
    switch (style) {
    case pdf::StampNumberStyle::UpperAlpha: return 1;
    case pdf::StampNumberStyle::LowerAlpha: return 2;
    case pdf::StampNumberStyle::Arabic: break;
    }
    return 0;
}

}  // namespace

StampNumberDialog::StampNumberDialog(const pdf::StampNumberOptions& options, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("annotation.numberStampDialog"));
    setWindowTitle(i18n::tr("aipdf::app::StampNumberDialog", "编号图章"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    styleBox_ = new QComboBox(this);
    styleBox_->setObjectName(QStringLiteral("annotation.numberStamp.style"));
    styleBox_->addItem(styleName(pdf::StampNumberStyle::Arabic), 0);
    styleBox_->addItem(styleName(pdf::StampNumberStyle::UpperAlpha), 1);
    styleBox_->addItem(styleName(pdf::StampNumberStyle::LowerAlpha), 2);
    styleBox_->setCurrentIndex(indexFromStyle(options.style));
    form->addRow(i18n::tr("aipdf::app::StampNumberDialog", "编号形式:"), styleBox_);

    startSpin_ = new QSpinBox(this);
    startSpin_->setObjectName(QStringLiteral("annotation.numberStamp.start"));
    startSpin_->setRange(1, 999999);
    startSpin_->setValue(qMax(1, options.start));
    form->addRow(i18n::tr("aipdf::app::StampNumberDialog", "起始序号:"), startSpin_);

    digitsSpin_ = new QSpinBox(this);
    digitsSpin_->setObjectName(QStringLiteral("annotation.numberStamp.digits"));
    digitsSpin_->setRange(0, 6);
    digitsSpin_->setValue(qBound(0, options.digits, 6));
    form->addRow(i18n::tr("aipdf::app::StampNumberDialog", "补零位数:"), digitsSpin_);

    prefixEdit_ = new QLineEdit(this);
    prefixEdit_->setObjectName(QStringLiteral("annotation.numberStamp.prefix"));
    prefixEdit_->setMaxLength(8);
    prefixEdit_->setText(options.prefix);
    form->addRow(i18n::tr("aipdf::app::StampNumberDialog", "前缀:"), prefixEdit_);

    suffixEdit_ = new QLineEdit(this);
    suffixEdit_->setObjectName(QStringLiteral("annotation.numberStamp.suffix"));
    suffixEdit_->setMaxLength(8);
    suffixEdit_->setText(options.suffix);
    form->addRow(i18n::tr("aipdf::app::StampNumberDialog", "后缀:"), suffixEdit_);

    previewLabel_ = new QLabel(this);
    previewLabel_->setObjectName(QStringLiteral("annotation.numberStamp.preview"));
    layout->addWidget(previewLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(styleBox_, &QComboBox::currentIndexChanged, this, [this](int) { updatePreview(); });
    connect(startSpin_, &QSpinBox::valueChanged, this, [this](int) { updatePreview(); });
    connect(digitsSpin_, &QSpinBox::valueChanged, this, [this](int) { updatePreview(); });
    connect(prefixEdit_, &QLineEdit::textChanged, this, [this](const QString&) { updatePreview(); });
    connect(suffixEdit_, &QLineEdit::textChanged, this, [this](const QString&) { updatePreview(); });

    updatePreview();
}

pdf::StampNumberOptions StampNumberDialog::options() const {
    pdf::StampNumberOptions result;
    result.style = styleFromIndex(styleBox_->currentIndex());
    result.start = qMax(1, startSpin_->value());
    result.digits = qBound(0, digitsSpin_->value(), 6);
    result.prefix = prefixEdit_->text();
    result.suffix = suffixEdit_->text();
    return result;
}

void StampNumberDialog::updatePreview() {
    const QString sample = pdf::formatStampNumber(options(), 0);
    previewLabel_->setText(i18n::tr("aipdf::app::StampNumberDialog", "第一个图章: %1").arg(sample));
}

}  // namespace aipdf::app
