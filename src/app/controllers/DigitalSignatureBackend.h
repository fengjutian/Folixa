#pragma once

#include <QString>
#include <QStringList>

namespace aipdf::app {

struct SignatureCommand {
    QString program;
    QStringList arguments;
    QString error;
    bool isValid() const { return !program.isEmpty() && error.isEmpty(); }
};

class DigitalSignatureBackend final {
public:
    static QString findPyHanko();
    static SignatureCommand signPkcs12(const QString& inputPdf,
                                       const QString& outputPdf,
                                       const QString& pkcs12File,
                                       const QString& passFile,
                                       const QString& timestampUrl = QString(),
                                       bool embedValidationInfo = false,
                                       const QString& executable = QString());
    static SignatureCommand verify(const QString& inputPdf,
                                   const QString& executable = QString());
};

}  // namespace aipdf::app
