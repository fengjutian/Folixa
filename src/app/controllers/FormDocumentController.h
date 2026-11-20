#pragma once

#include "pdf/forms/FormFieldWriter.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>

namespace aipdf::app {

// Performs form-structure mutations against serialized document bytes. The
// editor view remains responsible for permissions, undo history and repainting.
class FormDocumentController final {
public:
    struct Result {
        QByteArray bytes;
        QString error;
        [[nodiscard]] bool ok() const { return error.isEmpty() && !bytes.isEmpty(); }
    };

    Result appendField(const QByteArray& source, const pdf::FormFieldInsert& insert) const;
    Result updateFieldProperties(const QByteArray& source, int pageIndex,
                                 const QString& name, const QString& newName,
                                 const QString& toolTip, bool readOnly, bool required,
                                 bool multiline,
                                 const std::optional<QStringList>& options) const;
    Result flattenPage(const QByteArray& source, int pageIndex) const;

private:
    Result validate(QByteArray bytes) const;
};

}  // namespace aipdf::app
