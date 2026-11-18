#pragma once

#include <QDialog>
#include <QString>

#include "pdf/AnnotationToolkit.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace aipdf::app {

// 编号图章设置：编号形式（数字 / 大写字母 / 小写字母）、起始序号、
// 补零位数、前缀与后缀。
//
// 为什么单独一个对话框：这些设置在放第一个图章之前就该定下来，
// 放完再弹就晚了；而且编号要连着排，中途弹窗会把编号打断。
class StampNumberDialog : public QDialog {
    Q_OBJECT
public:
    explicit StampNumberDialog(const pdf::StampNumberOptions& options, QWidget* parent = nullptr);

    // 用户确认后的设置（未确认时不要用这个值）。
    pdf::StampNumberOptions options() const;

private:
    // 刷新预览：把当前设置算出来的第一个编号显示出来。
    void updatePreview();

    QComboBox* styleBox_ = nullptr;
    QSpinBox* startSpin_ = nullptr;
    QSpinBox* digitsSpin_ = nullptr;
    QLineEdit* prefixEdit_ = nullptr;
    QLineEdit* suffixEdit_ = nullptr;
    QLabel* previewLabel_ = nullptr;
};

}  // namespace aipdf::app
