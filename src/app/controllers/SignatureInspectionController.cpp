#include "app/controllers/SignatureInspectionController.h"

namespace aipdf::app {

SignatureInspectionController::Result SignatureInspectionController::inspect(
    const QByteArray& documentBytes) const {
    Result result;
    result.signatures = pdf::inspectSignatures(documentBytes, &result.inspectionError);
    if (!result.signatures.isEmpty()) {
        result.verdicts =
            pdf::verifyDocumentSignatures(documentBytes, &result.verdictError);
    }
    return result;
}

pdf::CertificateChainReport SignatureInspectionController::certificateChain(
    const pdf::SignatureInfo& signature) const {
    const QVector<QByteArray> chainDer = pdf::signatureCertificateDer(signature);
    if (chainDer.isEmpty()) return {};
    return pdf::inspectCertificateChain(chainDer.first(), chainDer.mid(1));
}

}  // namespace aipdf::app
