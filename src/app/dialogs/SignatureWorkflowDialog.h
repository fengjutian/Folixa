#pragma once

#include <QDialog>

namespace aipdf::app {

class SignatureWorkflowController;

class SignatureWorkflowDialog final : public QDialog {
public:
    explicit SignatureWorkflowDialog(SignatureWorkflowController& controller,
                                     QWidget* parent = nullptr);
};

}  // namespace aipdf::app
