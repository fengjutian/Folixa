#pragma once

#include <QDialog>
#include <QFont>
#include <QFontComboBox>
#include <QSpinBox>

namespace aipdf::app {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    QFont selectedFont() const;

signals:
    void aiSettingsRequested();

private:
    QFontComboBox* fontFamily_ = nullptr;
    QSpinBox* fontSize_ = nullptr;
};

}  // namespace aipdf::app
