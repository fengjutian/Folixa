#include "app/controllers/FormDocumentController.h"

#include "pdf/kernel/PdfiumAdapter.h"

namespace aipdf::app {

FormDocumentController::Result FormDocumentController::appendField(
    const QByteArray& source, const pdf::FormFieldInsert& insert) const {
    const pdf::FormFieldWriteResult written = pdf::appendFormField(source, insert);
    if (!written.ok) return {{}, written.error};
    return validate(written.bytes);
}

FormDocumentController::Result FormDocumentController::updateFieldProperties(
    const QByteArray& source, int pageIndex, const QString& name,
    const QString& newName, const QString& toolTip, bool readOnly, bool required,
    bool multiline, const std::optional<QStringList>& options) const {
    auto opened = pdf::openDocumentFromBytes(source);
    if (!opened.document.isValid()) return {{}, opened.errorMessage};
    if (!opened.document.setFormFieldProperties(pageIndex, name, newName, toolTip,
                                                 readOnly, required, multiline)) {
        return {{}, QStringLiteral("Unable to update form field properties.")};
    }
    QString error;
    QByteArray bytes = opened.document.serialize(&error);
    if (bytes.isEmpty()) return {{}, error};
    if (options.has_value()) {
        const pdf::FormFieldWriteResult updated =
            pdf::updateFormFieldOptions(bytes, newName, *options);
        if (!updated.ok) return {{}, updated.error};
        bytes = updated.bytes;
    }
    return validate(std::move(bytes));
}

FormDocumentController::Result FormDocumentController::flattenPage(
    const QByteArray& source, int pageIndex) const {
    auto opened = pdf::openDocumentFromBytes(source);
    if (!opened.document.isValid()) return {{}, opened.errorMessage};
    if (!opened.document.flattenPage(pageIndex)) {
        return {{}, QStringLiteral("Unable to flatten forms on this page.")};
    }
    QString error;
    QByteArray bytes = opened.document.serialize(&error);
    if (bytes.isEmpty()) return {{}, error};
    return validate(std::move(bytes));
}

FormDocumentController::Result FormDocumentController::validate(QByteArray bytes) const {
    auto reopened = pdf::openDocumentFromBytes(bytes);
    if (!reopened.document.isValid()) return {{}, reopened.errorMessage};
    return {std::move(bytes), {}};
}

}  // namespace aipdf::app
