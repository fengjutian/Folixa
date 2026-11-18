#pragma once

#include <QString>
#include <QStringList>

namespace aipdf::app {

enum class ConversionKind {
    OfficeToPdf,
    HtmlToPdf,
    PdfA2b,
    OptimizePdf,
};

struct ConversionPlan {
    QString program;
    QStringList arguments;
    QString expectedOutput;
    QString engineName;
    QString error;

    bool isValid() const { return !program.isEmpty() && error.isEmpty(); }
};

// 只负责发现转换引擎和生成确定性的命令行，不启动进程。这样 UI 可以显示进度、
// 支持取消，而测试不依赖本机是否真的安装 LibreOffice / Ghostscript。
class DocumentConverter final {
public:
    static QString findLibreOffice();
    static QString findGhostscript();
    static ConversionPlan plan(ConversionKind kind,
                               const QString& input,
                               const QString& output,
                               const QString& libreOffice = QString(),
                               const QString& ghostscript = QString());
};

}  // namespace aipdf::app
