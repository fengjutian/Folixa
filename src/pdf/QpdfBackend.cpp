#include "pdf/QpdfBackend.h"

#ifdef AIPDF_WITH_QPDF

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>

#include <exception>
#include <vector>

#endif

namespace aipdf::pdf {

bool qpdfBackendAvailable() {
#ifdef AIPDF_WITH_QPDF
    return true;
#else
    return false;
#endif
}

bool verifyWithQpdf(const QByteArray& bytes, QString* error) {
#ifdef AIPDF_WITH_QPDF
    if (bytes.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("没有可校验的内容");
        }
        return false;
    }
    try {
        QPDF pdf;
        pdf.processMemoryFile("candidate.pdf", bytes.constData(),
                              static_cast<size_t>(bytes.size()));
        // 不抑制警告：我们刚写出的文件不该有任何"已修复"的痕迹。
        if (pdf.anyWarnings()) {
            const std::vector<QPDFExc> warnings = pdf.getWarnings();
            if (!warnings.empty()) {
                if (error != nullptr) {
                    *error = QStringLiteral("qpdf 报告结构问题：%1")
                                 .arg(QString::fromStdString(warnings.front().what()));
                }
            } else if (error != nullptr) {
                *error = QStringLiteral("qpdf 报告了结构问题但没有给出细节");
            }
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = QStringLiteral("qpdf 解析失败：%1").arg(QString::fromUtf8(exception.what()));
        }
        return false;
    }
#else
    Q_UNUSED(bytes);
    if (error != nullptr) {
        error->clear();
    }
    return true;
#endif
}

}  // namespace aipdf::pdf
