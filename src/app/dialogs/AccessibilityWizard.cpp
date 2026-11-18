#include "app/dialogs/AccessibilityWizard.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "speech/SpeechEngine.h"

namespace aipdf::app {

namespace {

// 阅读配色的下拉项。
struct SchemeEntry {
    theme::ReadingScheme scheme;
    const char* label;
};

const SchemeEntry kSchemes[] = {
    {theme::ReadingScheme::Light, QT_TRANSLATE_NOOP("aipdf::app::AccessibilityWizard", "浅色")},
    {theme::ReadingScheme::Night, QT_TRANSLATE_NOOP("aipdf::app::AccessibilityWizard", "夜间")},
    {theme::ReadingScheme::EyeCare, QT_TRANSLATE_NOOP("aipdf::app::AccessibilityWizard", "护眼")},
    {theme::ReadingScheme::HighContrast,
     QT_TRANSLATE_NOOP("aipdf::app::AccessibilityWizard", "高对比")},
};

}  // namespace

AccessibilityWizard::AccessibilityWizard(const speech::SpeechSettings& speech, QWidget* parent)
    : AccessibilityWizard(speech, speech::SpeechEngine::availableVoices(), parent) {
}

AccessibilityWizard::AccessibilityWizard(const speech::SpeechSettings& speech,
                                         const QVector<speech::SpeechVoice>& voices,
                                         QWidget* parent)
    : QDialog(parent), speech_(speech) {
    setObjectName(QStringLiteral("accessibility.wizard"));
    setWindowTitle(tr("无障碍配置向导"));
    resize(560, 460);

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("三步把阅读配置调好：先看得清，再听得清，最后确认。随时可以按「完成」结束。"),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    pages_ = new QStackedWidget(this);
    layout->addWidget(pages_, 1);

    // ---- 第 1 页：显示与阅读 ----
    auto* displayPage = new QWidget(pages_);
    auto* displayForm = new QFormLayout(displayPage);
    fontSize_ = new QSpinBox(displayPage);
    fontSize_->setObjectName(QStringLiteral("accessibility.wizard.fontSize"));
    fontSize_->setRange(kAccessibilityFontSizeMin, kAccessibilityFontSizeMax);
    fontSize_->setSuffix(tr(" 磅"));
    fontSize_->setValue(qApp->font().pointSize() > 0 ? qApp->font().pointSize() : 10);
    displayForm->addRow(tr("界面字号:"), fontSize_);

    schemeBox_ = new QComboBox(displayPage);
    schemeBox_->setObjectName(QStringLiteral("accessibility.wizard.scheme"));
    for (const SchemeEntry& entry : kSchemes) {
        schemeBox_->addItem(tr(entry.label), static_cast<int>(entry.scheme));
    }
    const int schemeIndex = schemeBox_->findData(static_cast<int>(theme::currentScheme()));
    schemeBox_->setCurrentIndex(schemeIndex >= 0 ? schemeIndex : 0);
    displayForm->addRow(tr("阅读配色:"), schemeBox_);

    navigationBox_ = new QCheckBox(tr("显示左侧导航面板"), displayPage);
    navigationBox_->setObjectName(QStringLiteral("accessibility.wizard.navigation"));
    navigationBox_->setChecked(true);
    displayForm->addRow(QString(), navigationBox_);
    pages_->addWidget(displayPage);

    // ---- 第 2 页：朗读 ----
    auto* speechPage = new QWidget(pages_);
    auto* speechForm = new QFormLayout(speechPage);
    voiceBox_ = new QComboBox(speechPage);
    voiceBox_->setObjectName(QStringLiteral("accessibility.wizard.voice"));
    voiceBox_->addItem(tr("自动（按文本语言选择）"), QString());
    for (const speech::SpeechVoice& voice : voices) {
        const QString label = voice.culture.isEmpty()
            ? voice.name
            : tr("%1（%2）").arg(voice.name, voice.culture);
        voiceBox_->addItem(label, voice.name);
    }
    const int voiceIndex = voiceBox_->findData(speech_.voice);
    voiceBox_->setCurrentIndex(voiceIndex >= 0 ? voiceIndex : 0);
    speechForm->addRow(tr("音色:"), voiceBox_);

    rateSlider_ = new QSlider(Qt::Horizontal, speechPage);
    rateSlider_->setObjectName(QStringLiteral("accessibility.wizard.rate"));
    rateSlider_->setRange(speech::kSpeechRateMin, speech::kSpeechRateMax);
    rateSlider_->setValue(qBound(speech::kSpeechRateMin, speech_.rate, speech::kSpeechRateMax));
    speechForm->addRow(tr("语速:"), rateSlider_);
    rateLabel_ = new QLabel(speechPage);
    rateLabel_->setObjectName(QStringLiteral("accessibility.wizard.rateValue"));
    speechForm->addRow(QString(), rateLabel_);

    volumeSlider_ = new QSlider(Qt::Horizontal, speechPage);
    volumeSlider_->setObjectName(QStringLiteral("accessibility.wizard.volume"));
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(qBound(0, speech_.volume, 100));
    speechForm->addRow(tr("音量:"), volumeSlider_);
    volumeLabel_ = new QLabel(speechPage);
    volumeLabel_->setObjectName(QStringLiteral("accessibility.wizard.volumeValue"));
    speechForm->addRow(QString(), volumeLabel_);
    pages_->addWidget(speechPage);

    // ---- 第 3 页：摘要 ----
    auto* summaryPage = new QWidget(pages_);
    auto* summaryLayout = new QVBoxLayout(summaryPage);
    summaryLabel_ = new QLabel(summaryPage);
    summaryLabel_->setObjectName(QStringLiteral("accessibility.wizard.summary"));
    summaryLabel_->setWordWrap(true);
    summaryLayout->addWidget(summaryLabel_);
    pages_->addWidget(summaryPage);

    // ---- 按钮 ----
    auto* buttons = new QDialogButtonBox(this);
    backButton_ = buttons->addButton(tr("上一步"), QDialogButtonBox::ActionRole);
    backButton_->setObjectName(QStringLiteral("accessibility.wizard.back"));
    nextButton_ = buttons->addButton(tr("下一步"), QDialogButtonBox::ActionRole);
    nextButton_->setObjectName(QStringLiteral("accessibility.wizard.next"));
    QPushButton* finishButton = buttons->addButton(tr("完成"), QDialogButtonBox::AcceptRole);
    finishButton->setObjectName(QStringLiteral("accessibility.wizard.finish"));
    layout->addWidget(buttons);

    connect(backButton_, &QPushButton::clicked, this, [this] {
        showPage(pages_->currentIndex() - 1);
    });
    connect(nextButton_, &QPushButton::clicked, this, [this] {
        showPage(pages_->currentIndex() + 1);
    });
    connect(finishButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(rateSlider_, &QSlider::valueChanged, this, [this](int) {
        rateLabel_->setText(tr("当前语速: %1").arg(rateSlider_->value()));
        refreshSummary();
    });
    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int) {
        volumeLabel_->setText(tr("当前音量: %1").arg(volumeSlider_->value()));
    });

    showPage(0);
}

void AccessibilityWizard::showPage(int index) {
    const int clamped = qBound(0, index, pages_->count() - 1);
    pages_->setCurrentIndex(clamped);
    if (clamped == pages_->count() - 1) {
        refreshSummary();
    }
    updateButtons();
}

void AccessibilityWizard::updateButtons() {
    backButton_->setEnabled(pages_->currentIndex() > 0);
    // 最后一页没有"下一步"了：改成完成。
    nextButton_->setEnabled(pages_->currentIndex() < pages_->count() - 1);
}

void AccessibilityWizard::refreshSummary() {
    QStringList lines;
    lines << tr("界面字号: %1 磅").arg(fontSize_->value());
    const int schemeValue = schemeBox_->currentData().toInt();
    QString schemeName;
    for (const SchemeEntry& entry : kSchemes) {
        if (static_cast<int>(entry.scheme) == schemeValue) {
            schemeName = tr(entry.label);
            break;
        }
    }
    lines << tr("阅读配色: %1").arg(schemeName.isEmpty() ? tr("浅色") : schemeName);
    lines << tr("导航面板: %1").arg(navigationBox_->isChecked() ? tr("显示") : tr("隐藏"));
    lines << tr("音色: %1").arg(voiceBox_->currentText());
    lines << tr("语速: %1").arg(rateSlider_->value());
    lines << tr("音量: %1").arg(volumeSlider_->value());
    summaryLabel_->setText(lines.join(QLatin1Char('\n')));
}

int AccessibilityWizard::interfaceFontSize() const {
    return fontSize_->value();
}

theme::ReadingScheme AccessibilityWizard::readingScheme() const {
    return static_cast<theme::ReadingScheme>(schemeBox_->currentData().toInt());
}

bool AccessibilityWizard::navigationVisible() const {
    return navigationBox_->isChecked();
}

speech::SpeechSettings AccessibilityWizard::speechSettings() const {
    speech::SpeechSettings result;
    result.voice = voiceBox_->currentData().toString();
    result.rate = rateSlider_->value();
    result.volume = volumeSlider_->value();
    return speech::normalizeSpeechSettings(result);
}

AccessibilityPlan AccessibilityWizard::plan() const {
    AccessibilityPlan result;
    result.interfaceFontSize = fontSize_->value();
    result.readingScheme = readingScheme();
    result.navigationVisible = navigationBox_->isChecked();
    result.speech = speechSettings();
    return normalizeAccessibilityPlan(result);
}

QStringList AccessibilityWizard::summaryLines() const {
    QStringList lines;
    lines << tr("界面字号: %1 磅").arg(fontSize_->value());
    lines << tr("导航面板: %1").arg(navigationBox_->isChecked() ? tr("显示") : tr("隐藏"));
    lines << tr("音色: %1").arg(voiceBox_->currentText());
    lines << tr("语速: %1").arg(rateSlider_->value());
    lines << tr("音量: %1").arg(volumeSlider_->value());
    return lines;
}

}  // namespace aipdf::app
