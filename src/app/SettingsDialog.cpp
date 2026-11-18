#include "app/SettingsDialog.h"

#include "ai/ProviderConfig.h"
#include "app/Language.h"
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

QString dataModeLabel(ai::DataMode mode) {
    switch (mode) {
        case ai::DataMode::LocalOnly:
            return i18n::tr("aipdf::app::SettingsDialog", "仅本地处理");
        case ai::DataMode::SelectedChunks:
            return i18n::tr("aipdf::app::SettingsDialog", "仅发送检索片段");
        case ai::DataMode::SelectedPages:
            return i18n::tr("aipdf::app::SettingsDialog", "发送选定页面");
    }
    return i18n::tr("aipdf::app::SettingsDialog", "未知");
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
    language_->addItem(tr("跟随系统"), QStringLiteral("system"));
    for (const i18n::Language& language : i18n::supportedLanguages()) {
        language_->addItem(language.nativeName, language.code);
    }
    const QString preference = i18n::LanguageManager::instance().preference();
    language_->setCurrentIndex(std::max(0, language_->findData(preference)));
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
    auto* theme = new QComboBox(appearanceCard);
    theme->setObjectName(QStringLiteral("settingsTheme"));
    theme->addItem(tr("浅色"), QStringLiteral("light"));
    appearanceForm->addRow(tr("主题"), theme);
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

    auto* aiPage = makePage(tr("AI 设置"), tr("查看当前 AI 服务状态并进入详细配置。"), pages);
    auto* aiCard = makeCard(this);
    auto* aiForm = new QFormLayout(aiCard);
    aiForm->setContentsMargins(18, 16, 18, 16);
    aiForm->setSpacing(12);
    aiProviderValue_ = new QLabel(aiCard);
    aiModelValue_ = new QLabel(aiCard);
    aiDataModeValue_ = new QLabel(aiCard);
    aiCredentialValue_ = new QLabel(aiCard);
    aiForm->addRow(tr("服务商"), aiProviderValue_);
    aiForm->addRow(tr("模型"), aiModelValue_);
    aiForm->addRow(tr("数据模式"), aiDataModeValue_);
    aiForm->addRow(tr("API 密钥"), aiCredentialValue_);
    aiPage->addWidget(aiCard);
    auto* aiButton = new QPushButton(tr("配置 AI 服务…"), this);
    aiButton->setObjectName(QStringLiteral("openAiSettingsButton"));
    aiPage->addWidget(aiButton, 0, Qt::AlignLeft);
    aiPage->addStretch();
    connect(aiButton, &QPushButton::clicked, this, [this] {
        emit aiSettingsRequested();
        refreshAiSummary();
    });
    refreshAiSummary();

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
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons, 0, Qt::AlignRight);
}

// 「安装语言包…」：选文件 → 校验 → 复制到用户目录 → 切语言。
//
// 装完之后要把下拉框跟着改过来：否则下拉框还停在旧的那一项，
// 用户会以为没装上（切换其实是生效的，只是下拉框没同步）。
void SettingsDialog::installLanguagePack() {
    if (!i18n::installLanguagePackInteractively(this)) {
        return;
    }
    const QString code = i18n::LanguageManager::instance().code();
    const int index = language_->findData(code);
    if (index >= 0) {
        language_->setCurrentIndex(index);
    }
}

void SettingsDialog::refreshAiSummary() {    const ai::ProviderConfig config = ai::ProviderConfig::load();
    aiProviderValue_->setText(config.providerId == QStringLiteral("demo") ? tr("演示模式")
                                                                          : config.providerId);
    aiModelValue_->setText(config.model.isEmpty() ? tr("未设置") : config.model);
    aiDataModeValue_->setText(dataModeLabel(config.effectiveDataMode()));
    aiCredentialValue_->setText(config.hasCredential() ? tr("已安全保存") : tr("未设置"));
}

QFont SettingsDialog::selectedFont() const {
    QFont font = fontFamily_->currentFont();
    font.setPointSize(fontSize_->value());
    return font;
}

QString SettingsDialog::languagePreference() const { return language_->currentData().toString(); }
bool SettingsDialog::navigationVisibleOnStartup() const { return navigationVisible_->isChecked(); }
bool SettingsDialog::aiVisibleOnStartup() const { return aiVisible_->isChecked(); }

}  // namespace aipdf::app
