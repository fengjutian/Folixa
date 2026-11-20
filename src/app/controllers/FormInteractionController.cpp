#include "app/controllers/FormInteractionController.h"

#include <QHash>

namespace aipdf::app {

bool FormInteractionController::validateKeystroke(const QByteArray& documentBytes,
                                                   const QString& fieldName,
                                                   const QString& candidate,
                                                   QString* reason) {
    if (documentBytes.isEmpty()) return true;
    if (!session_ || sessionBytes_ != documentBytes) {
        QString error;
        pdf::FormRuntime runtime = pdf::FormRuntime::load(documentBytes, &error);
        if (!runtime.isValid()) {
            reset();
            return true;
        }
        session_ = std::make_unique<pdf::FormRuntime>(std::move(runtime));
        sessionBytes_ = documentBytes;
    }

    const pdf::FormFieldState* state = session_->field(fieldName);
    if (state == nullptr) return true;
    const QString current = state->value;
    // Deletion and whole-value replacement are validated when the edit is
    // committed. This path only handles text appended by a keystroke.
    if (!candidate.startsWith(current)) return true;
    const QString typed = candidate.mid(current.size());
    if (typed.isEmpty()) return true;

    // Trial on a copy: a rejected script must not leave a half-applied value in
    // the persistent typing session.
    pdf::FormRuntime trial = *session_;
    const pdf::FormEventResult result = trial.typeText(fieldName, typed);
    if (!result.ok) {
        if (reason != nullptr) *reason = result.error;
        return false;
    }
    *session_ = std::move(trial);
    return true;
}

FormInteractionController::MutationResult FormInteractionController::setValue(
    const QByteArray& documentBytes, const QString& fieldName, const QString& value) const {
    MutationResult mutation;
    pdf::FormRuntime runtime = pdf::FormRuntime::load(documentBytes, &mutation.error);
    if (!runtime.isValid()) return mutation;
    mutation.error.clear();

    const pdf::FormEventResult applied = runtime.setValue(fieldName, value);
    if (!applied.ok) {
        mutation.error = applied.error;
        return mutation;
    }
    const pdf::FormAppearanceResult written = runtime.applyTo(documentBytes);
    if (!written.ok()) {
        mutation.error = written.report.error;
        return mutation;
    }
    mutation.bytes = written.bytes;
    mutation.applied = 1;
    if (const pdf::FormFieldState* state = runtime.field(fieldName)) {
        mutation.firstPage = state->pageIndex;
    }
    return mutation;
}

FormInteractionController::MutationResult FormInteractionController::applyValues(
    const QByteArray& documentBytes,
    const QVector<QPair<QString, QString>>& values) const {
    MutationResult mutation;
    pdf::FormRuntime runtime = pdf::FormRuntime::load(documentBytes, &mutation.error);
    if (!runtime.isValid()) return mutation;
    mutation.error.clear();

    for (const QPair<QString, QString>& pair : values) {
        const pdf::FormFieldState* state = runtime.field(pair.first);
        if (state == nullptr) {
            mutation.failures.append(
                {Failure::Kind::MissingField, pair.first, QString()});
            continue;
        }
        if (state->readOnly) {
            mutation.failures.append({Failure::Kind::ReadOnly, pair.first, QString()});
            continue;
        }
        const pdf::FormEventResult result = runtime.setValue(pair.first, pair.second);
        if (!result.ok) {
            mutation.failures.append(
                {Failure::Kind::Rejected, pair.first, result.error});
            continue;
        }
        if (mutation.firstPage < 0) mutation.firstPage = state->pageIndex;
        ++mutation.applied;
    }
    if (mutation.applied == 0) return mutation;

    const pdf::FormAppearanceResult written = runtime.applyTo(documentBytes);
    if (!written.ok()) {
        mutation.error = written.report.error;
        return mutation;
    }
    mutation.bytes = written.bytes;
    return mutation;
}

FormInteractionController::MutationResult FormInteractionController::writeRichValue(
    const QByteArray& documentBytes, const QString& fieldName,
    const QVector<pdf::FormTextRun>& runs) const {
    MutationResult mutation;
    pdf::FormValueUpdate update;
    update.fieldName = fieldName;
    for (const pdf::FormTextRun& run : runs) update.value += run.text;
    update.runs = runs;
    const pdf::FormAppearanceResult written = pdf::writeFormValues(documentBytes, {update});
    if (!written.ok()) {
        mutation.error = written.report.error;
        return mutation;
    }
    mutation.bytes = written.bytes;
    mutation.applied = 1;
    return mutation;
}

FormInteractionController::MutationResult FormInteractionController::writeScripts(
    const QByteArray& documentBytes, const QVector<pdf::FormFieldScript>& scripts,
    const QStringList& order) const {
    MutationResult mutation;
    const pdf::FormScriptWriteResult written =
        pdf::writeFormScripts(documentBytes, scripts, order);
    if (!written.ok) {
        mutation.error = written.error;
        return mutation;
    }
    mutation.bytes = written.bytes;
    mutation.applied = scripts.size();
    return mutation;
}

QVector<pdf::FormFieldScript> FormInteractionController::scripts(
    const QByteArray& documentBytes) const {
    return documentBytes.isEmpty() ? QVector<pdf::FormFieldScript>()
                                   : pdf::readFormFieldScripts(documentBytes);
}

QStringList FormInteractionController::calculationOrder(
    const QByteArray& documentBytes) const {
    return documentBytes.isEmpty() ? QStringList() : pdf::readCalculationOrder(documentBytes);
}

QVector<QPair<QString, QString>> FormInteractionController::resetUpdates(
    const QByteArray& documentBytes) const {
    QHash<QString, QString> defaults;
    for (const pdf::FormFieldScript& script : scripts(documentBytes)) {
        defaults.insert(script.fieldName, script.defaultValue);
    }
    QVector<QPair<QString, QString>> updates;
    QString error;
    const pdf::FormRuntime runtime = pdf::FormRuntime::load(documentBytes, &error);
    if (!runtime.isValid()) return updates;
    for (const pdf::FormFieldState& field : runtime.fields()) {
        if (!field.readOnly) updates.append({field.name, defaults.value(field.name)});
    }
    return updates;
}

void FormInteractionController::reset() {
    session_.reset();
    sessionBytes_.clear();
}

}  // namespace aipdf::app
