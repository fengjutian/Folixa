#include "app/dialogs/AiSettingsDialog.h"

#include "app/ui/Language.h"
#include "ai/settings/ProviderConfig.h"
#include "ai/settings/ProviderPreset.h"
#include "ai/settings/CredentialStore.h"
#include "i18n/Translate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <optional>

namespace aipdf::app {

namespace {

constexpr auto kProviderDemo = "demo";

}  // namespace

AiSettingsDialog::AiSettingsDialog(QWidget* parent, bool embedded) : QDialog(parent) {
    if (embedded) {
        setWindowFlags(Qt::Widget);
    }
    texts_.bind([this] { setWindowTitle(tr("AI 服务设置")); });
    setObjectName(QStringLiteral("aiSettingsDialog"));
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    auto* intro = new QLabel(this);
    intro->setObjectName(QStringLiteral("aiSettingsIntro"));
    intro->setWordWrap(true);
    texts_.bind([this, intro] {
        intro->setText(tr("密钥保存在 Windows 凭据管理器里，不写进配置文件。") +
                       QStringLiteral("\n") +
                       tr("留空密钥表示不修改已保存的值。"));
    });

    providerCombo_ = new QComboBox(this);
    providerCombo_->setObjectName(QStringLiteral("aiSettingsProvider"));
    // 下拉框的内容由预设表生成：界面里不再另写一份服务商清单，
    // 否则加一家服务商就得改两个地方，迟早对不上。
    for (const ai::ProviderPreset& preset : ai::providerPresets()) {
        providerCombo_->addItem(QString(), preset.id);
    }
    texts_.bind([this] {
        for (int index = 0; index < providerCombo_->count(); ++index) {
            providerCombo_->setItemText(
                index,
                ai::providerDisplayName(providerCombo_->itemData(index).toString()));
        }
    });

    providerHint_ = new QLabel(this);
    providerHint_->setObjectName(QStringLiteral("aiSettingsProviderHint"));
    providerHint_->setWordWrap(true);

    // 数据策略（v0.12.0）：决定文档内容能不能离开本机、离开多少。
    dataModeCombo_ = new QComboBox(this);
    dataModeCombo_->setObjectName(QStringLiteral("aiSettingsDataMode"));
    dataModeCombo_->addItem(QString(), QStringLiteral("local"));
    dataModeCombo_->addItem(QString(), QStringLiteral("chunks"));
    dataModeCombo_->addItem(QString(), QStringLiteral("pages"));
    texts_.bind([this] {
        dataModeCombo_->setItemText(0, tr("本地（文档内容不出本机）"));
        dataModeCombo_->setItemText(1, tr("片段云端（只送当前页/选区或检索块）"));
        dataModeCombo_->setItemText(2, tr("选定页面（只送勾选的页）"));
    });
    dataModeHint_ = new QLabel(this);
    dataModeHint_->setObjectName(QStringLiteral("aiSettingsDataModeHint"));
    dataModeHint_->setWordWrap(true);

    baseUrlEdit_ = new QLineEdit(this);
    baseUrlEdit_->setObjectName(QStringLiteral("aiSettingsBaseUrl"));
    baseUrlEdit_->setPlaceholderText(QStringLiteral("https://api.openai.com/v1"));

    modelEdit_ = new QLineEdit(this);
    modelEdit_->setObjectName(QStringLiteral("aiSettingsModel"));

    apiKeyEdit_ = new QLineEdit(this);
    apiKeyEdit_->setObjectName(QStringLiteral("aiSettingsApiKey"));
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    texts_.bind([this] { apiKeyEdit_->setPlaceholderText(tr("粘贴 API 密钥")); });
    texts_.bind([this] { apiKeyEdit_->setAccessibleName(tr("API 密钥")); });

    keyHint_ = new QLabel(this);
    keyHint_->setObjectName(QStringLiteral("aiSettingsKeyHint"));
    keyHint_->setWordWrap(true);

    clearKeyBox_ = new QCheckBox(this);
    clearKeyBox_->setObjectName(QStringLiteral("aiSettingsClearKey"));
    texts_.bind([this] { clearKeyBox_->setText(tr("清除已保存的密钥")); });

    inputPrice_ = new QDoubleSpinBox(this);
    inputPrice_->setObjectName(QStringLiteral("aiSettingsInputPrice"));
    inputPrice_->setRange(0.0, 100000.0);
    inputPrice_->setDecimals(4);
    inputPrice_->setSingleStep(0.01);
    texts_.bind([this] { inputPrice_->setSuffix(tr(" / 百万 tokens")); });

    outputPrice_ = new QDoubleSpinBox(this);
    outputPrice_->setObjectName(QStringLiteral("aiSettingsOutputPrice"));
    outputPrice_->setRange(0.0, 100000.0);
    outputPrice_->setDecimals(4);
    outputPrice_->setSingleStep(0.01);
    texts_.bind([this] { outputPrice_->setSuffix(tr(" / 百万 tokens")); });

    timeout_ = new QSpinBox(this);
    timeout_->setObjectName(QStringLiteral("aiSettingsTimeout"));
    timeout_->setRange(1, 3600);
    texts_.bind([this] { timeout_->setSuffix(tr(" 秒")); });

    auto* form = new QFormLayout;
    // 表单行标签也是常驻文案：源文案作为参数带进来，收的一侧用 tr(source) 现翻。
    auto addFormLabel = [&](const char* source, QWidget* field) {
        auto* label = new QLabel(this);
        form->addRow(label, field);
        texts_.bind([this, label, source] { label->setText(tr(source)); });
    };
    addFormLabel(QT_TRANSLATE_NOOP("aipdf::app::AiSettingsDialog", "服务商"),
                 providerCombo_);
    form->addRow(QString(), providerHint_);
    addFormLabel(QT_TRANSLATE_NOOP("aipdf::app::AiSettingsDialog", "数据模式"),
                 dataModeCombo_);
    form->addRow(QString(), dataModeHint_);
    addFormLabel(QT_TRANSLATE_NOOP("aipdf::app::AiSettingsDialog", "服务地址"),
                 baseUrlEdit_);
    addFormLabel(QT_TRANSLATE_NOOP("aipdf::app::AiSettingsDialog", "模型"),
                 modelEdit_);
    addFormLabel(QT_TRANSLATE_NOOP("aipdf::app::AiSettingsDialog", "API 密钥"),
                 apiKeyEdit_);
    form->addRow(QString(), keyHint_);
    form->addRow(QString(), clearKeyBox_);
    addFormLabel(QT_TRANSLATE_NOOP("aipdf::app::AiSettingsDialog", "输入单价"),
                 inputPrice_);
    addFormLabel(QT_TRANSLATE_NOOP("aipdf::app::AiSettingsDialog", "输出单价"),
                 outputPrice_);
    addFormLabel(QT_TRANSLATE_NOOP("aipdf::app::AiSettingsDialog", "超时"), timeout_);

    QDialogButtonBox* buttons = nullptr;
    if (!embedded) {
        buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        texts_.bind([this, buttons] {
            buttons->button(QDialogButtonBox::Ok)->setText(tr("保存"));
            buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
        });
    }

    layout->addWidget(intro);
    layout->addLayout(form);
    if (buttons != nullptr) {
        layout->addWidget(buttons);
    }

    if (buttons != nullptr) connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        // 保存前先验一次：地址写错要当场说清楚，别等到发请求才失败。
        ai::ProviderConfig probe;
        probe.providerId = providerId();
        probe.baseUrl = baseUrl();
        probe.model = model();
        probe.timeoutMs = timeoutMs();
        probe.inputPricePerMillion = inputPricePerMillion();
        probe.outputPricePerMillion = outputPricePerMillion();
        const QString error = ai::ProviderConfig::validationError(probe);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, tr("设置有误"), error);
            return;
        }
        accept();
    });
    if (buttons != nullptr) {
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
    connect(providerCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        applyProviderPreset();
        updateEnabledState();
        updateProviderHint();
    });
    connect(dataModeCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { updateDataModeHint(); });
    // 地址改成指向本机时，「要不要密钥」要跟着变：
    // 否则用户填了一个本机地址，密钥框还亮着，看着像必须填。
    connect(baseUrlEdit_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateEnabledState(); });
    updateDataModeHint();
    updateProviderHint();
    // 切换语言后常驻文案就地重译，随模式变化的说明重新算一遍。
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &AiSettingsDialog::retranslateUi);
}

void AiSettingsDialog::retranslateUi() {
    texts_.retranslate();
    updateDataModeHint();
    updateProviderHint();
    updateEnabledState();
}

void AiSettingsDialog::applyConfig(const QString& providerId,
                                   const QString& baseUrl,
                                   const QString& model,
                                   bool hasKey,
                                   double inputPricePerMillion,
                                   double outputPricePerMillion,
                                   int timeoutMs,
                                   const QString& dataMode) {
    hasKey_ = hasKey;
    const int index = providerCombo_->findData(providerId);
    providerCombo_->setCurrentIndex(index >= 0 ? index : 0);
    const int modeIndex = dataModeCombo_->findData(dataMode);
    dataModeCombo_->setCurrentIndex(modeIndex >= 0 ? modeIndex : 1);
    baseUrlEdit_->setText(baseUrl);
    modelEdit_->setText(model);
    inputPrice_->setValue(inputPricePerMillion);
    outputPrice_->setValue(outputPricePerMillion);
    timeout_->setValue(timeoutMs > 0 ? timeoutMs / 1000 : 60);

    apiKeyEdit_->clear();
    clearKeyBox_->setChecked(false);
    updateEnabledState();
    updateDataModeHint();
}

void AiSettingsDialog::updateDataModeHint() {
    if (dataModeHint_ == nullptr) {
        return;
    }
    const QString mode = dataModeCombo_->currentData().toString();
    if (mode == QLatin1String("local")) {
        dataModeHint_->setText(
            tr("文档内容一律不出本机：云端服务商会退回演示模型，本机模型（如 Ollama）照常可用。"));
    } else if (mode == QLatin1String("pages")) {
        dataModeHint_->setText(tr("只发送你在预览里确认过的页；发送前会显示页码与字数。"));
    } else {
        dataModeHint_->setText(tr("只发送当前页、选区或检索到的几块；发送前会显示范围。"));
    }
}

void AiSettingsDialog::updateEnabledState() {
    const QString provider = providerCombo_->currentData().toString();
    const bool cloud = provider != QLatin1String(kProviderDemo);
    // 本机地址上的模型不需要密钥：把密钥框灰掉，
    // 别让用户以为必须填一个其实用不上的密钥。
    const bool needsKey = cloud && ai::providerRequiresApiKey(provider, baseUrl());
    baseUrlEdit_->setEnabled(cloud);
    modelEdit_->setEnabled(cloud);
    apiKeyEdit_->setEnabled(needsKey);
    clearKeyBox_->setEnabled(needsKey && hasKey_);
    inputPrice_->setEnabled(cloud);
    outputPrice_->setEnabled(cloud);
    timeout_->setEnabled(cloud);

    if (!cloud) {
        keyHint_->setText(tr("演示模式不会联网，密钥与地址都不需要。"));
        return;
    }
    if (!needsKey) {
        keyHint_->setText(tr("本机模型不需要密钥，文档内容不会离开这台机器。"));
        return;
    }
    keyHint_->setText(hasKey_
        ? tr("凭据管理器里已经存了这个服务商的密钥；留空表示不修改。")
        : tr("还没有存过密钥。演示模式会在没有密钥时自动接管。"));
}

void AiSettingsDialog::updateProviderHint() {
    if (providerHint_ == nullptr) {
        return;
    }
    const QString provider = providerCombo_->currentData().toString();
    if (provider == QLatin1String("demo")) {
        providerHint_->setText(tr("演示模式只在本地生成示例回答，不联网、不发送文档。"));
    } else if (provider == QLatin1String("minimax")) {
        providerHint_->setText(
            tr("国内账号用这个地址；海外账号把地址换成 https://api.minimax.io/v1。"));
    } else if (provider == QLatin1String("deepseek")) {
        providerHint_->setText(
            tr("在 DeepSeek 开放平台创建密钥；模型可用 deepseek-chat 与 deepseek-reasoner。"));
    } else if (provider == QLatin1String("qwen")) {
        providerHint_->setText(tr("阿里云百炼的兼容模式，密钥在百炼控制台创建。"));
    } else if (provider == QLatin1String("kimi")) {
        providerHint_->setText(
            tr("月之暗面开放平台创建密钥；模型可用 moonshot-v1-8k、32k、128k 与 kimi-k2。"));
    } else if (provider == QLatin1String("ollama")) {
        providerHint_->setText(
            tr("本机模型，不需要密钥，文档内容不出本机；模型名要和 ollama list 里一致。"));
    } else {
        providerHint_->setText(tr("任何 OpenAI 兼容服务：地址和模型都由你填。"));
    }
}

void AiSettingsDialog::applyProviderPreset() {
    const std::optional<ai::ProviderPreset> preset =
        ai::findProviderPreset(providerCombo_->currentData().toString());
    if (!preset.has_value()) {
        return;
    }
    if (!preset->baseUrl.isEmpty()) {
        baseUrlEdit_->setText(preset->baseUrl);
    }
    if (!preset->model.isEmpty()) {
        modelEdit_->setText(preset->model);
    } else if (preset->runsLocally) {
        // 本机模型不预置模型名：装了什么模型只有用户自己知道。
        // 留着上一个服务商的模型名只会让请求打到一个不存在的模型上。
        modelEdit_->clear();
    }
}

QString AiSettingsDialog::providerId() const {
    return providerCombo_->currentData().toString();
}

QString AiSettingsDialog::dataMode() const {
    return dataModeCombo_->currentData().toString();
}

QString AiSettingsDialog::baseUrl() const {
    return baseUrlEdit_->text().trimmed();
}

QString AiSettingsDialog::model() const {
    return modelEdit_->text().trimmed();
}

QString AiSettingsDialog::apiKey() const {
    return apiKeyEdit_->text().trimmed();
}

bool AiSettingsDialog::clearCredential() const {
    return clearKeyBox_->isChecked();
}

double AiSettingsDialog::inputPricePerMillion() const {
    return inputPrice_->value();
}

double AiSettingsDialog::outputPricePerMillion() const {
    return outputPrice_->value();
}

int AiSettingsDialog::timeoutMs() const {
    return timeout_->value() * 1000;
}

bool AiSettingsDialog::saveConfig(QString* error) const {
    ai::ProviderConfig updated = ai::ProviderConfig::load();
    updated.providerId = providerId();
    updated.baseUrl = baseUrl();
    updated.model = model();
    updated.inputPricePerMillion = inputPricePerMillion();
    updated.outputPricePerMillion = outputPricePerMillion();
    updated.timeoutMs = timeoutMs();
    updated.dataMode = ai::dataModeId(ai::dataModeFromId(dataMode()));
    const QString validation = ai::ProviderConfig::validationError(updated);
    if (!validation.isEmpty()) {
        if (error) *error = validation;
        return false;
    }
    const QString target = ai::CredentialStore::targetNameFor(updated.providerId);
    QString credentialError;
    if (clearCredential() && !ai::CredentialStore::remove(target, &credentialError)) {
        if (error) *error = credentialError;
        return false;
    }
    const QString key = apiKey();
    if (!key.isEmpty() && !ai::CredentialStore::store(target, key, &credentialError)) {
        if (error) *error = credentialError;
        return false;
    }
    if (!updated.save()) {
        if (error) *error = tr("配置文件写不进去，检查 %1 是否可写。")
                                .arg(ai::ProviderConfig::configFilePath());
        return false;
    }
    return true;
}

}  // namespace aipdf::app
