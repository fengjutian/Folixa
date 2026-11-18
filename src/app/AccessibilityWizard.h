#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

#include "app/AccessibilityPlan.h"
#include "app/ReadingScheme.h"
#include "speech/SpeechSettings.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace aipdf::app {

// 无障碍配置向导：一步一步把「看得清 / 听得清 / 找得到」配好。
//
// 为什么是向导而不是设置对话框：设置对话框是**平铺**的选项，
// 无障碍用户往往不知道自己该改哪一项（字号？配色？朗读？）；
// 向导按「显示与阅读 → 朗读 → 摘要」一步步问，每一步只问一件事，
// 最后给一份"改了哪些"的清单。
class AccessibilityWizard final : public QDialog {
    Q_OBJECT
public:
    // 从系统里查音色（正常用法）。
    AccessibilityWizard(const speech::SpeechSettings& speech, QWidget* parent = nullptr);

    // 直接给定音色列表：测试用，不去问系统。
    //
    // 为什么要这条口子：查音色要起一个 PowerShell 进程，
    // 单测里跑这个既慢又不稳 —— 界面层应该能在没有系统依赖的情况下被测。
    AccessibilityWizard(const speech::SpeechSettings& speech,
                        const QVector<speech::SpeechVoice>& voices,
                        QWidget* parent = nullptr);

    // 用户配好的结果。
    int interfaceFontSize() const;
    theme::ReadingScheme readingScheme() const;
    bool navigationVisible() const;
    speech::SpeechSettings speechSettings() const;

    // 收成一份计划：规则（字号夹取、朗读归一）在这里统一生效。
    AccessibilityPlan plan() const;

    // 摘要页上要显示的几行（界面文案，生成在界面层）。
    QStringList summaryLines() const;

private:
    // 切到第 index 页（0 起）。
    void showPage(int index);
    // 刷新摘要页的文字。
    void refreshSummary();
    // 当前页的按钮能不能点。
    void updateButtons();

    QStackedWidget* pages_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;

    QSpinBox* fontSize_ = nullptr;
    QComboBox* schemeBox_ = nullptr;
    QCheckBox* navigationBox_ = nullptr;
    QComboBox* voiceBox_ = nullptr;
    QSlider* rateSlider_ = nullptr;
    QSlider* volumeSlider_ = nullptr;
    QLabel* rateLabel_ = nullptr;
    QLabel* volumeLabel_ = nullptr;

    speech::SpeechSettings speech_;
};

}  // namespace aipdf::app
