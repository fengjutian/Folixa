#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "speech/SpeechSettings.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QSlider;

namespace aipdf::app {

// 阅读选项：两个页签。
//
//   * 阅读顺序 —— 当前页的逻辑阅读顺序（人工核对屏幕阅读器的读序）；
//   * 朗读设置 —— 音色、语速、音量。
//
// 为什么合成一个对话框：福昕的「阅读选项」就是一个总入口，
// 拆成两个命令反而让用户找不到。
class ReadingOptionsDialog : public QDialog {
    Q_OBJECT
public:
    ReadingOptionsDialog(const QString& readingOrder, const speech::SpeechSettings& settings,
                         const QVector<speech::SpeechVoice>& voices, QWidget* parent = nullptr);

    // 用户确认后的朗读设置。
    speech::SpeechSettings settings() const;

private:
    // 刷新语速与音量的数值标签。
    void updateValueLabels();

    QComboBox* voiceBox_ = nullptr;
    QSlider* rateSlider_ = nullptr;
    QSlider* volumeSlider_ = nullptr;
    QLabel* rateLabel_ = nullptr;
    QLabel* volumeLabel_ = nullptr;
};

}  // namespace aipdf::app
