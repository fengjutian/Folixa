#include "app/dialogs/SettingsDialog.h"
#include "app/dialogs/AiSettingsDialog.h"

#include "ai/settings/ProviderConfig.h"
#include "app/ui/Language.h"
#include "app/ui/Theme.h"
#include "i18n/Translate.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace aipdf::app {

namespace {
QVBoxLayout* makePage(const QString& title, const QString& description,
                      QStackedWidget* pages) {
    auto* page = new QWidget(pages);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);
    auto* heading = new QLabel(title, page);
    heading->setObjectName(QStringLiteral("settingsPageTitle"));
    layout->addWidget(heading);
    auto* hint = new QLabel(description, page);
    hint->setObjectName(QStringLiteral("secondaryText"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    pages->addWidget(page);
    return layout;
}

QFrame* makeCard(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("settingsCard"));
    return card;
}

}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("设置"));
    setModal(true);
    resize(760, 520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 14);
    root->setSpacing(0);
    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    auto* navigation = new QListWidget(this);
    navigation->setObjectName(QStringLiteral("settingsNavigation"));
    navigation->setFixedWidth(174);
    navigation->addItems({tr("常规"), tr("外观"), tr("AI 设置"), tr("隐私与数据"), tr("关于")});
    body->addWidget(navigation);

    auto* pages = new QStackedWidget(this);
    body->addWidget(pages, 1);
    QSettings settings;

    auto* general = makePage(tr("常规"), tr("管理界面语言和工作区的启动状态。"), pages);
    auto* generalCard = makeCard(this);
    auto* generalForm = new QFormLayout(generalCard);
    generalForm->setContentsMargins(18, 16, 18, 16);
    generalForm->setSpacing(14);
    language_ = new QComboBox(generalCard);
    language_->setObjectName(QStringLiteral("settingsLanguage"));
    rebuildLanguageList();
    // 语言包装不装得看用户手里有没有包：给一个「安装语言包…」的入口，
    // 缺包时用户才有事可做（否则界面只能报一句「没找到语言包」）。
    auto* languageRow = new QWidget(generalCard);
    auto* languageLayout = new QHBoxLayout(languageRow);
    languageLayout->setContentsMargins(0, 0, 0, 0);
    languageLayout->addWidget(language_);
    auto* installLanguageButton = new QPushButton(tr("安装语言包…"), languageRow);
    installLanguageButton->setObjectName(QStringLiteral("settingsInstallLanguagePack"));
    languageLayout->addWidget(installLanguageButton);
    generalForm->addRow(tr("界面语言"), languageRow);
    connect(installLanguageButton, &QPushButton::clicked, this,
            &SettingsDialog::installLanguagePack);
    navigationVisible_ = new QCheckBox(tr("启动时显示左侧导航栏"), generalCard);
    navigationVisible_->setChecked(
        settings.value(QStringLiteral("workspace/navigationVisible"), true).toBool());
    generalForm->addRow(QString(), navigationVisible_);
    aiVisible_ = new QCheckBox(tr("打开文档后显示 AI 助手"), generalCard);
    aiVisible_->setChecked(settings.value(QStringLiteral("workspace/aiVisible"), false).toBool());
    generalForm->addRow(QString(), aiVisible_);
    general->addWidget(generalCard);
    general->addStretch();

    auto* appearance = makePage(tr("外观"), tr("调整应用主题、界面字体和字号。"), pages);
    auto* appearanceCard = makeCard(this);
    auto* appearanceForm = new QFormLayout(appearanceCard);
    appearanceForm->setContentsMargins(18, 16, 18, 16);
    appearanceForm->setSpacing(14);
    // 主题：浅色 / 暗色 / 跟随系统。
    //
    // 之前这里只有「浅色」一项，而且下拉框选的值既没保存也没读回，
    // 主窗口那边还是写死一个 "light" —— 是个改不动的死控件。
    // 现在它存进 appearance/theme，保存后立刻生效，不用重启。
    theme_ = new QComboBox(appearanceCard);
    theme_->setObjectName(QStringLiteral("settingsTheme"));
    theme_->addItem(tr("浅色"), QStringLiteral("light"));
    theme_->addItem(tr("暗色"), QStringLiteral("dark"));
    theme_->addItem(tr("跟随系统"), QStringLiteral("system"));
    const QString currentTheme =
        aipdf::app::theme::modeKey(aipdf::app::theme::currentMode());
    const int themeIndex = theme_->findData(currentTheme);
    theme_->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
    appearanceForm->addRow(tr("主题"), theme_);
    fontFamily_ = new QFontComboBox(appearanceCard);
    fontFamily_->setObjectName(QStringLiteral("settingsFontFamily"));
    fontFamily_->setCurrentFont(qApp->font());
    appearanceForm->addRow(tr("字体"), fontFamily_);
    fontSize_ = new QSpinBox(appearanceCard);
    fontSize_->setObjectName(QStringLiteral("settingsFontSize"));
    fontSize_->setRange(8, 24);
    fontSize_->setSuffix(tr(" 磅"));
    fontSize_->setValue(std::max(8, qApp->font().pointSize()));
    appearanceForm->addRow(tr("字号"), fontSize_);
    fontPreview_ = new QLabel(tr("Folixa · 专注阅读，也为文档智能留出空间"), appearanceCard);
    fontPreview_->setObjectName(QStringLiteral("settingsFontPreview"));
    fontPreview_->setMinimumHeight(56);
    appearanceForm->addRow(tr("预览"), fontPreview_);
    auto updatePreview = [this] { fontPreview_->setFont(selectedFont()); };
    connect(fontFamily_, &QFontComboBox::currentFontChanged, this, updatePreview);
    connect(fontSize_, &QSpinBox::valueChanged, this, updatePreview);
    updatePreview();
    appearance->addWidget(appearanceCard);
    appearance->addStretch();

    auto* aiPage = makePage(tr("AI 设置"), tr("配置全局 AI 服务、模型、数据范围和安全凭据。"), pages);
    aiEditor_ = new AiSettingsDialog(pages, true);
    const ai::ProviderConfig aiConfig = ai::ProviderConfig::load();
    aiEditor_->applyConfig(aiConfig.providerId, aiConfig.baseUrl, aiConfig.model,
                           aiConfig.hasCredential(), aiConfig.inputPricePerMillion,
                           aiConfig.outputPricePerMillion, aiConfig.timeoutMs,
                           aiConfig.dataMode);
    aiPage->addWidget(aiEditor_, 1);

    auto* privacy = makePage(tr("隐私与数据"), tr("了解文档内容如何在本地与 AI 服务之间流转。"), pages);
    auto* privacyCard = makeCard(this);
    auto* privacyLayout = new QVBoxLayout(privacyCard);
    privacyLayout->setContentsMargins(18, 16, 18, 16);
    privacyLayout->setSpacing(10);
    auto* localTitle = new QLabel(tr("本地优先"), privacyCard);
    localTitle->setObjectName(QStringLiteral("settingsSectionTitle"));
    privacyLayout->addWidget(localTitle);
    auto* localText = new QLabel(
        tr("PDF 渲染、文本索引和搜索默认在本机完成。只有执行云端 AI 功能时，才会按所选数据模式发送内容。"),
        privacyCard);
    localText->setWordWrap(true);
    localText->setObjectName(QStringLiteral("secondaryText"));
    privacyLayout->addWidget(localText);
    auto* configPath = new QLabel(tr("AI 配置目录：%1").arg(ai::ProviderConfig::sharedDataDirectory()), privacyCard);
    configPath->setWordWrap(true);
    configPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    privacyLayout->addWidget(configPath);
    privacy->addWidget(privacyCard);
    privacy->addStretch();

    auto* about = makePage(tr("关于"), tr("Folixa · AI PDF Editor"), pages);
    auto* aboutCard = makeCard(this);
    auto* aboutLayout = new QVBoxLayout(aboutCard);
    aboutLayout->setContentsMargins(18, 18, 18, 18);
    aboutLayout->setSpacing(8);
    auto* product = new QLabel(QStringLiteral("Folixa"), aboutCard);
    product->setObjectName(QStringLiteral("settingsProductName"));
    aboutLayout->addWidget(product);
    aboutLayout->addWidget(new QLabel(tr("版本 %1").arg(QCoreApplication::applicationVersion()), aboutCard));
    auto* aboutText = new QLabel(tr("专注于 PDF 阅读、编辑、检索与受控 AI 协作。"), aboutCard);
    aboutText->setWordWrap(true);
    aboutText->setObjectName(QStringLiteral("secondaryText"));
    aboutLayout->addWidget(aboutText);
    about->addWidget(aboutCard);
    about->addStretch();

    connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    navigation->setCurrentRow(0);
    root->addLayout(body, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QString error;
        if (aiEditor_ != nullptr && !aiEditor_->saveConfig(&error)) {
            QMessageBox::warning(this, tr("AI 设置保存失败"), error);
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons, 0, Qt::AlignRight);
}

// 重建语言下拉框。
//
// 列出的是「现在能选」的语言：内置的那些 + 用户目录里装了语言包的那些。
// 装了却选不到，安装这一半就没有意义；所以这里跟 availableLanguages() 对齐，
// 装完立刻重建一次。
void SettingsDialog::rebuildLanguageList() {
    language_->clear();
    language_->addItem(tr("跟随系统"), QStringLiteral("system"));
    for (const i18n::Language& language : i18n::availableLanguages()) {
        language_->addItem(language.nativeName, language.code);
    }
    const QString preference = i18n::LanguageManager::instance().preference();
    const int index = language_->findData(preference);
    language_->setCurrentIndex(index >= 0 ? index : 0);
}

// 「安装语言包…」：选文件 → 校验 → 复制到用户目录 → 切语言。
//
// 装完之后要把下拉框跟着改过来：否则下拉框还停在旧的那一项，
// 用户会以为没装上（切换其实是生效的，只是下拉框没同步）。
void SettingsDialog::installLanguagePack() {
    if (!i18n::installLanguagePackInteractively(this)) {
        return;
    }
    rebuildLanguageList();
    const QString code = i18n::LanguageManager::instance().code();
    const int index = language_->findData(code);
    if (index >= 0) {
        language_->setCurrentIndex(index);
    }
}

QFont SettingsDialog::selectedFont() const {
    QFont font = fontFamily_->currentFont();
    font.setPointSize(fontSize_->value());
    return font;
}

QString SettingsDialog::languagePreference() const { return language_->currentData().toString(); }
QString SettingsDialog::selectedTheme() const { return theme_->currentData().toString(); }
bool SettingsDialog::navigationVisibleOnStartup() const { return navigationVisible_->isChecked(); }
bool SettingsDialog::aiVisibleOnStartup() const { return aiVisible_->isChecked(); }

}  // namespace aipdf::app
