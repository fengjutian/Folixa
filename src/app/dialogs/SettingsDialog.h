#pragma once

#include <QDialog>
#include <QFont>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QSpinBox;

namespace aipdf::app {

class AiSettingsDialog;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    QFont selectedFont() const;
    QString languagePreference() const;
    bool navigationVisibleOnStartup() const;
    bool aiVisibleOnStartup() const;
    // 主题（light / dark / system）。
    QString selectedTheme() const;

private:
    // 重建语言下拉框：列出**现在能选**的语言（内置的 + 用户装了语言包的）。
    //
    // 为什么每次都重建而不是构造时填一次：用户可能刚在「安装语言包…」里
    // 装了一个新语言，下拉框里必须立刻能选到它。
    void rebuildLanguageList();
    // 「安装语言包…」：走一遍选文件 → 校验 → 复制 → 切语言的流程。
    void installLanguagePack();

    QComboBox* language_ = nullptr;
    QComboBox* theme_ = nullptr;
    QCheckBox* navigationVisible_ = nullptr;
    QCheckBox* aiVisible_ = nullptr;
    QFontComboBox* fontFamily_ = nullptr;
    QSpinBox* fontSize_ = nullptr;
    QLabel* fontPreview_ = nullptr;
    AiSettingsDialog* aiEditor_ = nullptr;
};

}  // namespace aipdf::app
