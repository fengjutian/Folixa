#include "app/controllers/SignatureWorkflowController.h"

namespace aipdf::app {

bool SignatureWorkflowController::ensureCreated(const QString& title,
                                                  const QString& initiator,
                                                  QString* error) {
    if (!workflow_.id().isEmpty()) return true;
    return !workflow_.createWorkflow(title.trimmed(), initiator.trimmed(), error).isEmpty();
}

bool SignatureWorkflowController::addSignatory(pdf::Signatory signatory, QString* error) {
    workflow_.addSignatory(signatory, error);
    return error == nullptr || error->isEmpty();
}

bool SignatureWorkflowController::start(QString* error) {
    return workflow_.startSigning(error);
}

bool SignatureWorkflowController::signNext(const QDateTime& at, QString* error) {
    const QString name = workflow_.nextPendingSignatory();
    if (name.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("没有待签署的人。");
        return false;
    }
    return workflow_.recordSigned(name, at, error);
}

bool SignatureWorkflowController::rejectNext(const QString& reason, const QDateTime& at,
                                              QString* error) {
    const QString name = workflow_.nextPendingSignatory();
    if (name.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("没有待签署的人。");
        return false;
    }
    return workflow_.recordRejected(name, reason.trimmed(), at, error);
}

bool SignatureWorkflowController::skipNext(QString* error) {
    const QString name = workflow_.nextPendingSignatory();
    if (name.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("没有待签署的人。");
        return false;
    }
    return workflow_.skipSignatory(name, error);
}

bool SignatureWorkflowController::complete(QString* error) {
    return workflow_.completeWorkflow(error);
}

bool SignatureWorkflowController::abort(const QString& reason, QString* error) {
    return workflow_.abortWorkflow(reason.trimmed(), error);
}

}  // namespace aipdf::app
