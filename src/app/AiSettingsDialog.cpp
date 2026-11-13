#include "app/AiSettingsDialog.h"

#include "ai/ProviderConfig.h"

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

namespace aipdf::app {

namespace {

constexpr auto kProviderDemo = "demo";
constexpr auto kProviderOpenAi = "openai";

}  // namespace

AiSettingsDialog::AiSettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("AI 服务设置"));
    setObjectName(QStringLiteral("aiSettingsDialog"));
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    auto* intro = new QLabel(
        tr("密钥保存在 Windows 凭据管理器里，不写进配置文件。\n"
           "留空密钥表示不修改已保存的值。"),
        this);
    intro->setObjectName(QStringLiteral("aiSettingsIntro"));
    intro->setWordWrap(true);

    providerCombo_ = new QComboBox(this);
    providerCombo_->setObjectName(QStringLiteral("aiSettingsProvider"));
    providerCombo_->addItem(tr("演示模式（本地生成，不联网）"),
                            QString::fromLatin1(kProviderDemo));
    providerCombo_->addItem(tr("OpenAI 兼容（云端模型）"),
                            QString::fromLatin1(kProviderOpenAi));

    // 数据策略（v0.12.0）：决定文档内容能不能离开本机、离开多少。
    dataModeCombo_ = new QComboBox(this);
    dataModeCombo_->setObjectName(QStringLiteral("aiSettingsDataMode"));
    dataModeCombo_->addItem(tr("本地（文档内容不出本机）"), QStringLiteral("local"));
    dataModeCombo_->addItem(tr("片段云端（只送当前页/选区或检索块）"), QStringLiteral("chunks"));
    dataModeCombo_->addItem(tr("选定页面（只送勾选的页）"), QStringLiteral("pages"));
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
    apiKeyEdit_->setPlaceholderText(tr("粘贴 API 密钥"));
    apiKeyEdit_->setAccessibleName(tr("API 密钥"));

    keyHint_ = new QLabel(this);
    keyHint_->setObjectName(QStringLiteral("aiSettingsKeyHint"));
    keyHint_->setWordWrap(true);

    clearKeyBox_ = new QCheckBox(tr("清除已保存的密钥"), this);
    clearKeyBox_->setObjectName(QStringLiteral("aiSettingsClearKey"));

    inputPrice_ = new QDoubleSpinBox(this);
    inputPrice_->setObjectName(QStringLiteral("aiSettingsInputPrice"));
    inputPrice_->setRange(0.0, 100000.0);
    inputPrice_->setDecimals(4);
    inputPrice_->setSingleStep(0.01);
    inputPrice_->setSuffix(tr(" / 百万 tokens"));

    outputPrice_ = new QDoubleSpinBox(this);
    outputPrice_->setObjectName(QStringLiteral("aiSettingsOutputPrice"));
    outputPrice_->setRange(0.0, 100000.0);
    outputPrice_->setDecimals(4);
    outputPrice_->setSingleStep(0.01);
    outputPrice_->setSuffix(tr(" / 百万 tokens"));

    timeout_ = new QSpinBox(this);
    timeout_->setObjectName(QStringLiteral("aiSettingsTimeout"));
    timeout_->setRange(1, 3600);
    timeout_->setSuffix(tr(" 秒"));

    auto* form = new QFormLayout;
    form->addRow(tr("服务商"), providerCombo_);
    form->addRow(tr("数据模式"), dataModeCombo_);
    form->addRow(QString(), dataModeHint_);
    form->addRow(tr("服务地址"), baseUrlEdit_);
    form->addRow(tr("模型"), modelEdit_);
    form->addRow(tr("API 密钥"), apiKeyEdit_);
    form->addRow(QString(), keyHint_);
    form->addRow(QString(), clearKeyBox_);
    form->addRow(tr("输入单价"), inputPrice_);
    form->addRow(tr("输出单价"), outputPrice_);
    form->addRow(tr("超时"), timeout_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));

    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
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
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(providerCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { updateEnabledState(); });
    connect(dataModeCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { updateDataModeHint(); });
    updateDataModeHint();
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
        dataModeHint_->setText(tr("文档内容一律不出本机；即使配了密钥也只用本地演示模型。"));
    } else if (mode == QLatin1String("pages")) {
        dataModeHint_->setText(tr("只发送你在预览里确认过的页；发送前会显示页码与字数。"));
    } else {
        dataModeHint_->setText(tr("只发送当前页、选区或检索到的几块；发送前会显示范围。"));
    }
}

void AiSettingsDialog::updateEnabledState() {
    const bool cloud = providerCombo_->currentData().toString() ==
                       QLatin1String(kProviderOpenAi);
    baseUrlEdit_->setEnabled(cloud);
    modelEdit_->setEnabled(cloud);
    apiKeyEdit_->setEnabled(cloud);
    clearKeyBox_->setEnabled(cloud && hasKey_);
    inputPrice_->setEnabled(cloud);
    outputPrice_->setEnabled(cloud);
    timeout_->setEnabled(cloud);

    if (!cloud) {
        keyHint_->setText(tr("演示模式不会联网，密钥与地址都不需要。"));
        return;
    }
    keyHint_->setText(hasKey_
        ? tr("凭据管理器里已经存了这个服务商的密钥；留空表示不修改。")
        : tr("还没有存过密钥。演示模式会在没有密钥时自动接管。"));
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

}  // namespace aipdf::app
