#include "app/SettingsDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {
QWidget* makePage(const QString& title, const QString& description, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    auto* heading = new QLabel(title, page);
    heading->setObjectName(QStringLiteral("settingsPageTitle"));
    layout->addWidget(heading);
    auto* hint = new QLabel(description, page);
    hint->setObjectName(QStringLiteral("secondaryText"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    return page;
}
}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("设置"));
    setModal(true);
    resize(620, 420);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 14);
    root->setSpacing(0);
    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    auto* navigation = new QListWidget(this);
    navigation->setObjectName(QStringLiteral("settingsNavigation"));
    navigation->setFixedWidth(150);
    navigation->addItems({tr("主题"), tr("字体"), tr("AI 设置")});
    body->addWidget(navigation);

    auto* pages = new QStackedWidget(this);
    body->addWidget(pages, 1);

    auto* themePage = makePage(tr("主题"), tr("选择应用的外观主题。"), pages);
    auto* themeLayout = qobject_cast<QVBoxLayout*>(themePage->layout());
    auto* themeCombo = new QComboBox(themePage);
    themeCombo->setObjectName(QStringLiteral("settingsTheme"));
    themeCombo->addItem(tr("浅色"), QStringLiteral("light"));
    themeLayout->addWidget(themeCombo);
    themeLayout->addStretch();
    pages->addWidget(themePage);

    auto* fontPage = makePage(tr("字体"), tr("设置界面使用的字体和字号。"), pages);
    auto* fontLayout = qobject_cast<QVBoxLayout*>(fontPage->layout());
    auto* form = new QFormLayout();
    fontFamily_ = new QFontComboBox(fontPage);
    fontFamily_->setObjectName(QStringLiteral("settingsFontFamily"));
    fontFamily_->setCurrentFont(qApp->font());
    fontSize_ = new QSpinBox(fontPage);
    fontSize_->setObjectName(QStringLiteral("settingsFontSize"));
    fontSize_->setRange(8, 24);
    fontSize_->setSuffix(tr(" 磅"));
    fontSize_->setValue(qApp->font().pointSize());
    form->addRow(tr("字体"), fontFamily_);
    form->addRow(tr("字号"), fontSize_);
    fontLayout->addLayout(form);
    fontLayout->addStretch();
    pages->addWidget(fontPage);

    auto* aiPage = makePage(tr("AI 设置"), tr("配置 AI 服务商、模型和 API 密钥。"), pages);
    auto* aiLayout = qobject_cast<QVBoxLayout*>(aiPage->layout());
    auto* aiButton = new QPushButton(tr("打开 AI 服务设置…"), aiPage);
    aiButton->setObjectName(QStringLiteral("openAiSettingsButton"));
    aiLayout->addWidget(aiButton, 0, Qt::AlignLeft);
    aiLayout->addStretch();
    pages->addWidget(aiPage);

    connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    connect(aiButton, &QPushButton::clicked, this, &SettingsDialog::aiSettingsRequested);
    navigation->setCurrentRow(0);
    root->addLayout(body, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons, 0, Qt::AlignRight);
}

QFont SettingsDialog::selectedFont() const {
    QFont font = fontFamily_->currentFont();
    font.setPointSize(fontSize_->value());
    return font;
}

}  // namespace aipdf::app
