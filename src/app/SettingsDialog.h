#pragma once

#include <QDialog>
#include <QFont>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QSpinBox;

namespace aipdf::app {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    QFont selectedFont() const;
    QString languagePreference() const;
    bool navigationVisibleOnStartup() const;
    bool aiVisibleOnStartup() const;

signals:
    void aiSettingsRequested();

private:
    void refreshAiSummary();
    // 「安装语言包…」：走一遍选文件 → 校验 → 复制 → 切语言的流程。
    void installLanguagePack();

    QComboBox* language_ = nullptr;
    QCheckBox* navigationVisible_ = nullptr;
    QCheckBox* aiVisible_ = nullptr;
    QFontComboBox* fontFamily_ = nullptr;
    QSpinBox* fontSize_ = nullptr;
    QLabel* fontPreview_ = nullptr;
    QLabel* aiProviderValue_ = nullptr;
    QLabel* aiModelValue_ = nullptr;
    QLabel* aiDataModeValue_ = nullptr;
    QLabel* aiCredentialValue_ = nullptr;
};

}  // namespace aipdf::app
