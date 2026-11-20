#pragma once

#include "pdf/security/SignatureWorkflow.h"

namespace aipdf::app {

// Application-facing command surface for the signature workflow. UI code reads
// the immutable model and sends intent through these methods.
class SignatureWorkflowController final {
public:
    const pdf::SignatureWorkflow& model() const { return workflow_; }

    bool ensureCreated(const QString& title, const QString& initiator, QString* error);
    bool addSignatory(pdf::Signatory signatory, QString* error);
    bool start(QString* error);
    bool signNext(const QDateTime& at, QString* error);
    bool rejectNext(const QString& reason, const QDateTime& at, QString* error);
    bool skipNext(QString* error);
    bool complete(QString* error);
    bool abort(const QString& reason, QString* error);

private:
    pdf::SignatureWorkflow workflow_;
};

}  // namespace aipdf::app
