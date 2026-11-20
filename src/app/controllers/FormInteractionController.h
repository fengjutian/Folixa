#pragma once

#include "pdf/forms/FormRuntime.h"

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>
#include <memory>

namespace aipdf::app {

// Owns the long-lived form typing session. Keeping it outside PdfView makes the
// form event state independently testable and prevents the view from knowing
// how FormRuntime caches and rolls back tentative keystrokes.
class FormInteractionController final {
public:
    struct Failure {
        enum class Kind { MissingField, ReadOnly, Rejected };
        Kind kind = Kind::Rejected;
        QString fieldName;
        QString detail;
    };

    struct MutationResult {
        QByteArray bytes;
        QString error;
        int applied = 0;
        int firstPage = -1;
        QVector<Failure> failures;

        [[nodiscard]] bool ok() const { return error.isEmpty() && !bytes.isEmpty(); }
    };

    bool validateKeystroke(const QByteArray& documentBytes, const QString& fieldName,
                           const QString& candidate, QString* reason = nullptr);
    MutationResult setValue(const QByteArray& documentBytes, const QString& fieldName,
                            const QString& value) const;
    MutationResult applyValues(
        const QByteArray& documentBytes,
        const QVector<QPair<QString, QString>>& values) const;
    void reset();

private:
    std::unique_ptr<pdf::FormRuntime> session_;
    QByteArray sessionBytes_;
};

}  // namespace aipdf::app
