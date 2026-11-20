#pragma once

#include "pdf/security/SignatureInspector.h"
#include "pdf/security/SignatureVerifier.h"
#include "pdf/security/CertificateChain.h"

namespace aipdf::app {

class SignatureInspectionController final {
public:
    struct Result {
        QVector<pdf::SignatureInfo> signatures;
        QVector<pdf::SignatureCryptoVerdict> verdicts;
        QString inspectionError;
        QString verdictError;
    };

    Result inspect(const QByteArray& documentBytes) const;
    pdf::CertificateChainReport certificateChain(
        const pdf::SignatureInfo& signature) const;
};

}  // namespace aipdf::app
