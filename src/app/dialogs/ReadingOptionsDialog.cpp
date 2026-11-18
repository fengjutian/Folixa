#include "app/ReadingOptionsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

#include "i18n/Translate.h"

namespace aipdf::app {

namespace {

// 音色下拉里"自动"那一项的固定标识。
constexpr int kAutoVoiceIndex = 0;

}  // namespace

ReadingOptionsDialog::ReadingOptionsDialog(const QString& readingOrder,
                                           const speech::SpeechSettings& settings,
                                           const QVector<speech::SpeechVoice>& voices,
                                           QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("accessibility.readingOptionsDialog"));
    setWindowTitle(i18n::tr("aipdf::app::ReadingOptionsDialog", "阅读选项"));
    resize(640, 520);

    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("accessibility.readingOptions.tabs"));
    layout->addWidget(tabs);

    // ---- 阅读顺序 ----
    auto* orderPage = new QWidget(tabs);
    auto* orderLayout = new QVBoxLayout(orderPage);
    auto* hint = new QLabel(
        i18n::tr("aipdf::app::ReadingOptionsDialog",
                 "以下内容按页面上的视觉顺序排列（多栏与表格已按位置重排），"
                 "可用于人工核对屏幕阅读器的阅读次序。"),
        orderPage);
    hint->setWordWrap(true);
    orderLayout->addWidget(hint);
    auto* orderText = new QPlainTextEdit(orderPage);
    orderText->setObjectName(QStringLiteral("accessibility.readingOptions.order"));
    orderText->setReadOnly(true);
    orderText->setPlainText(readingOrder);
    orderText->setAccessibleName(i18n::tr("aipdf::app::ReadingOptionsDialog", "当前页阅读顺序"));
    orderLayout->addWidget(orderText, 1);
    tabs->addTab(orderPage, i18n::tr("aipdf::app::ReadingOptionsDialog", "阅读顺序"));

    // ---- 朗读设置 ----
    auto* speechPage = new QWidget(tabs);
    auto* speechLayout = new QVBoxLayout(speechPage);

    auto* voiceRow = new QLabel(i18n::tr("aipdf::app::ReadingOptionsDialog", "音色:"), speechPage);
    speechLayout->addWidget(voiceRow);
    voiceBox_ = new QComboBox(speechPage);
    voiceBox_->setObjectName(QStringLiteral("accessibility.readingOptions.voice"));
    voiceBox_->addItem(i18n::tr("aipdf::app::ReadingOptionsDialog", "自动（按文本语言选择）"),
                       QString());
    for (const speech::SpeechVoice& voice : voices) {
        const QString label = voice.culture.isEmpty()
            ? voice.name
            : i18n::tr("aipdf::app::ReadingOptionsDialog", "%1（%2）").arg(voice.name, voice.culture);
        voiceBox_->addItem(label, voice.name);
    }
    const int voiceIndex = voiceBox_->findData(settings.voice);
    voiceBox_->setCurrentIndex(voiceIndex >= 0 ? voiceIndex : kAutoVoiceIndex);
    speechLayout->addWidget(voiceBox_);

    auto* rateRow = new QLabel(i18n::tr("aipdf::app::ReadingOptionsDialog", "语速:"), speechPage);
    speechLayout->addWidget(rateRow);
    rateSlider_ = new QSlider(Qt::Horizontal, speechPage);
    rateSlider_->setObjectName(QStringLiteral("accessibility.readingOptions.rate"));
    rateSlider_->setRange(speech::kSpeechRateMin, speech::kSpeechRateMax);
    rateSlider_->setValue(qBound(speech::kSpeechRateMin, settings.rate, speech::kSpeechRateMax));
    speechLayout->addWidget(rateSlider_);
    rateLabel_ = new QLabel(speechPage);
    rateLabel_->setObjectName(QStringLiteral("accessibility.readingOptions.rateValue"));
    speechLayout->addWidget(rateLabel_);

    auto* volumeRow = new QLabel(i18n::tr("aipdf::app::ReadingOptionsDialog", "音量:"), speechPage);
    speechLayout->addWidget(volumeRow);
    volumeSlider_ = new QSlider(Qt::Horizontal, speechPage);
    volumeSlider_->setObjectName(QStringLiteral("accessibility.readingOptions.volume"));
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(qBound(0, settings.volume, 100));
    speechLayout->addWidget(volumeSlider_);
    volumeLabel_ = new QLabel(speechPage);
    volumeLabel_->setObjectName(QStringLiteral("accessibility.readingOptions.volumeValue"));
    speechLayout->addWidget(volumeLabel_);

    speechLayout->addStretch(1);
    tabs->addTab(speechPage, i18n::tr("aipdf::app::ReadingOptionsDialog", "朗读设置"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(rateSlider_, &QSlider::valueChanged, this, [this](int) { updateValueLabels(); });
    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int) { updateValueLabels(); });

    updateValueLabels();
}

speech::SpeechSettings ReadingOptionsDialog::settings() const {
    speech::SpeechSettings result;
    result.voice = voiceBox_->currentData().toString();
    result.rate = rateSlider_->value();
    result.volume = volumeSlider_->value();
    return speech::normalizeSpeechSettings(result);
}

void ReadingOptionsDialog::updateValueLabels() {
    rateLabel_->setText(i18n::tr("aipdf::app::ReadingOptionsDialog", "当前语速: %1")
                            .arg(rateSlider_->value()));
    volumeLabel_->setText(i18n::tr("aipdf::app::ReadingOptionsDialog", "当前音量: %1")
                              .arg(volumeSlider_->value()));
}

}  // namespace aipdf::app
