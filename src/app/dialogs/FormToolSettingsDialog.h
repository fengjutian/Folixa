#pragma once

#include <QDialog>
#include <QColor>

#include "pdf/forms/FormToolSettings.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;

namespace aipdf::app {

// 「表单工具设置」对话框：新建表单域时默认用什么字体、字号与颜色。
class FormToolSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit FormToolSettingsDialog(QWidget* parent = nullptr);

    // 用户改完的设置（还没写盘，由调用方决定什么时候保存）。
    pdf::FormToolSettings settings() const;

private:
    void pickColor();

    QComboBox* font_ = nullptr;
    QDoubleSpinBox* size_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    QColor color_;
};

}  // namespace aipdf::app
