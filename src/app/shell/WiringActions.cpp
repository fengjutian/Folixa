#include "app/shell/WiringActions.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QMap>
#include <QProcess>
#include <QPrintDialog>
#include <QPrinter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "ai/content/AiContentDetection.h"
#include "ai/content/AiImageGeneration.h"
#include "ai/content/PptGeneration.h"
#include "app/panels/AiPanel.h"
#include "app/controllers/AiToolService.h"
#include "app/dialogs/AiToolsDialog.h"
#include "app/dialogs/AnnotationToolboxDialog.h"
#include "app/dialogs/ConversionInputDialog.h"
#include "app/util/ConversionReports.h"
#include "app/dialogs/DocumentCompareDialog.h"
#include "app/controllers/OneNoteExporter.h"
#include "app/dialogs/EncryptionDialog.h"
#include "app/controllers/ScannerCapture.h"
#include "app/util/ConversionExports.h"
#include "app/dialogs/ConversionToolsDialog.h"
#include "app/controllers/DocumentOutput.h"
#include "app/controllers/LastWaveBridge.h"
#include "app/panels/LastWavePanels.h"
#include "app/editor/OcrController.h"
#include "app/layout/PageTextLines.h"
#include "app/panels/PdfAPreflightPanel.h"
#include "app/editor/PdfView.h"
#include "app/dialogs/PrintToolsDialog.h"
#include "doc/batch/ActionWizard.h"
#include "doc/batch/BatchProcessor.h"
#include "doc/convert/FontFlattening.h"
#include "doc/convert/ScannedPdfOptimizer.h"
#include "doc/export/OfdExport.h"
#include "doc/export/OfficeExport.h"
#include "doc/export/PdfToOffice.h"
#include "doc/convert/UniversalConverter.h"
#include "i18n/Translate.h"
#include "pdf/compliance/UaGenerator.h"
#include "pdf/content/PageGraphics.h"
#include "pdf/compliance/VeraPdfChecker.h"
#include "ipc/WorkerClient.h"
#include "pdf/ops/DocumentReflow.h"
#include "pdf/convert/ExportFormats.h"
#include "pdf/content/InvisibleTextLayer.h"
#include "pdf/ops/OcrSuspicion.h"
#include "pdf/ops/OptionalContent.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/forms/FormAutoTag.h"
#include "pdf/security/PdfSecurity.h"
#include "pdf/compliance/PdfAChecker.h"
#include "pdf/compliance/PdfUaChecker.h"
#include "pdf/convert/PortfolioWriter.h"
#include "pdf/compliance/PreflightChecker.h"
#include "pdf/annot/RichMedia3d.h"
#include "pdf/annot/ScreenAnnotation.h"
#include "pdf/annot/ThreeDModel.h"
#include "pdf/forms/XfaSupport.h"
#include "pdf/ops/StreamingEdit.h"
#include "pdf/ops/TranslationWriter.h"
#include "app/dialogs/ThreeDViewerDialog.h"
#include "app/dialogs/TextBlockMergeDialog.h"
#include "app/util/WorkerTranslationProvider.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>

namespace aipdf::app {

namespace {

// 把 "1-3,5" 这样的页码范围解析成打印用的区间（0 基、闭区间）。
//
// 领域层不解析页码范围（那是执行打印那一层的事），所以解析放在这里。
bool parsePageRange(const QString& value, OutputRange* range, QString* error) {
    if (range == nullptr) {
        return false;
    }
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        range->firstPage = 0;
        range->lastPage = -1;
        return true;
    }
    const QStringList parts = trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts);
    int first = -1;
    int last = -1;
    for (const QString& part : parts) {
        const QString piece = part.trimmed();
        if (piece.isEmpty()) {
            continue;
        }
        bool ok = false;
        int from = 0;
        int to = 0;
        if (piece.contains(QLatin1Char('-'))) {
            const QStringList rangeParts = piece.split(QLatin1Char('-'));
            if (rangeParts.size() != 2) {
                if (error != nullptr) {
                    *error = i18n::tr("aipdf::app::MainWindow", "页码范围写错了：%1").arg(piece);
                }
                return false;
            }
            from = rangeParts.at(0).trimmed().toInt(&ok);
            if (!ok) {
                if (error != nullptr) {
                    *error = i18n::tr("aipdf::app::MainWindow", "页码范围写错了：%1").arg(piece);
                }
                return false;
            }
            to = rangeParts.at(1).trimmed().toInt(&ok);
            if (!ok) {
                if (error != nullptr) {
                    *error = i18n::tr("aipdf::app::MainWindow", "页码范围写错了：%1").arg(piece);
                }
                return false;
            }
        } else {
            from = piece.toInt(&ok);
            if (!ok) {
                if (error != nullptr) {
                    *error = i18n::tr("aipdf::app::MainWindow", "页码范围写错了：%1").arg(piece);
                }
                return false;
            }
            to = from;
        }
        if (from < 1 || to < from) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::app::MainWindow", "页码范围写错了：%1").arg(piece);
            }
            return false;
        }
        const int zeroBased = from - 1;
        const int zeroBasedLast = to - 1;
        if (first < 0 || zeroBased < first) {
            first = zeroBased;
        }
        if (zeroBasedLast > last) {
            last = zeroBasedLast;
        }
    }
    if (first < 0) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::MainWindow", "页码范围写错了：%1").arg(trimmed);
        }
        return false;
    }
    range->firstPage = first;
    range->lastPage = last;
    return true;
}

bool writeFile(const QString& path, const QByteArray& bytes, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::MainWindow", "写不了这个文件：%1").arg(path);
        }
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        file.close();
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::MainWindow", "写入失败：%1").arg(path);
        }
        return false;
    }
    file.close();
    return true;
}

}  // namespace

WiringActions::WiringActions(QWidget* parent) : QObject(parent), parentWidget_(parent) {
    aiToolService_ = new AiToolService(this);
}

WiringActions::~WiringActions() = default;

void WiringActions::setViewProvider(std::function<PdfView*()> provider) {
    viewProvider_ = std::move(provider);
}

void WiringActions::setOpenDocumentHandler(std::function<void(const QString&)> handler) {
    openDocumentHandler_ = std::move(handler);
}

void WiringActions::setWorkerClient(ipc::WorkerClient* client) {
    workerClient_ = client;
    if (aiToolService_ != nullptr) {
        aiToolService_->setWorkerClient(client);
    }
}

PdfView* WiringActions::currentView() const {
    return viewProvider_ ? viewProvider_() : nullptr;
}

PdfView* WiringActions::requireView() {
    PdfView* view = currentView();
    if (view == nullptr) {
        notify(i18n::tr("aipdf::app::MainWindow", "请先打开一份 PDF。"));
    }
    return view;
}

AiToolService* WiringActions::aiService() {
    if (aiToolService_ == nullptr) {
        aiToolService_ = new AiToolService(this);
    }
    if (workerClient_ != nullptr) {
        aiToolService_->setWorkerClient(workerClient_);
    }
    return aiToolService_;
}

void WiringActions::notify(const QString& message) {
    emit this->message(message);
}

// ------------------------------------------------------------ 文件三项 ----

void WiringActions::newDocument() {
    NewDocumentDialog dialog(parentWidget_);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QString error;
    const QByteArray bytes = dialog.createDocument(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "新建文档"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "新建失败。") : error);
        return;
    }
    // 现成的打开流程要的是路径，所以先落一个临时文件再打开。
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = QDir(directory).filePath(
        QStringLiteral("folixa-new-%1.pdf").arg(QUuid::createUuid().toString(QUuid::Id128)));
    if (!writeFile(path, bytes, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "新建文档"), error);
        return;
    }
    if (openDocumentHandler_) {
        openDocumentHandler_(path);
    }
    notify(i18n::tr("aipdf::app::MainWindow", "已新建空白文档。"));
}

void WiringActions::printBatch() {
    PrintBatchDialog dialog(parentWidget_);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const doc::PrintBatchPlan plan = dialog.plan();
    if (!plan.ok) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "批量打印"), plan.error);
        return;
    }
    if (plan.expanded.isEmpty()) {
        QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "批量打印"), i18n::tr("aipdf::app::MainWindow", "队列里没有任务。"));
        return;
    }
    // 提醒要先说清楚：跳过的项、被夹住的参数都是用户该知道的。
    if (!plan.warnings.isEmpty()) {
        QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "批量打印"),
                                 i18n::tr("aipdf::app::MainWindow", "提醒：%1").arg(plan.warnings.join(QStringLiteral("；"))));
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog2(&printer, parentWidget_);
    dialog2.setWindowTitle(i18n::tr("aipdf::app::MainWindow", "批量打印"));
    if (dialog2.exec() != QDialog::Accepted) {
        return;
    }

    int printed = 0;
    QStringList failures;
    for (const doc::PrintBatchItem& item : plan.expanded) {
        const pdf::PdfOpenResult opened = pdf::openDocument(item.filePath);
        if (!opened.document.isValid()) {
            failures.append(i18n::tr("aipdf::app::MainWindow", "%1：%2").arg(item.filePath, opened.errorMessage));
            continue;
        }
        PrintOptions options;
        options.copies = qMax(1, item.copies);
        QString rangeError;
        if (!parsePageRange(item.pageRange, &options.range, &rangeError)) {
            failures.append(i18n::tr("aipdf::app::MainWindow", "%1：%2").arg(item.filePath, rangeError));
            continue;
        }
        QString error;
        if (!app::printDocument(opened.document, &printer, options, &error)) {
            failures.append(i18n::tr("aipdf::app::MainWindow", "%1：%2").arg(item.filePath, error));
            continue;
        }
        ++printed;
    }
    if (failures.isEmpty()) {
        notify(i18n::tr("aipdf::app::MainWindow", "批量打印完成：%1 个任务。").arg(printed));
        return;
    }
    QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "批量打印"),
                         i18n::tr("aipdf::app::MainWindow", "打印了 %1 个任务，%2 个失败：\n%3")
                             .arg(printed)
                             .arg(failures.size())
                             .arg(failures.join(QStringLiteral("\n"))));
}

void WiringActions::printTemplates() {
    PrintTemplatesDialog dialog(parentWidget_);
    connect(&dialog, &PrintTemplatesDialog::applyRequested, this, [this](const doc::PrintTemplate& value) {
        PdfView* view = currentView();
        if (view == nullptr) {
            notify(i18n::tr("aipdf::app::MainWindow", "请先打开一份 PDF。"));
            return;
        }
        QPrinter printer(QPrinter::HighResolution);
        QPrintDialog printDialog(&printer, parentWidget_);
        printDialog.setWindowTitle(i18n::tr("aipdf::app::MainWindow", "按模板打印"));
        if (printDialog.exec() != QDialog::Accepted) {
            return;
        }
        PrintOptions options;
        options.copies = qBound(1, value.copies, 99);
        QString error;
        if (!parsePageRange(value.pageRange, &options.range, &error)) {
            QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "打印模板"), error);
            return;
        }
        QString printError;
        if (!view->printWithOptions(options, &printError)) {
            QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "打印模板"), printError);
            return;
        }
        notify(i18n::tr("aipdf::app::MainWindow", "已按模板打印：%1。").arg(value.name));
    });
    dialog.exec();
}

// -------------------------------------------------------------- AI 五项 ----

void WiringActions::imageGeneration() {
    ImageGenerationDialog dialog(aiService(), parentWidget_);
    connect(&dialog, &ImageGenerationDialog::insertRequested, this,
            [this](const QByteArray& bytes) {
                PdfView* view = requireView();
                if (view == nullptr) {
                    return;
                }
                view->insertGeneratedImage(bytes, i18n::tr("aipdf::app::MainWindow", "AI 生成图"));
            });
    dialog.exec();
}

void WiringActions::summaryImage() {
    PdfView* view = currentView();
    SummaryImageDialog dialog(aiService(), parentWidget_);
    if (view != nullptr) {
        dialog.setDocumentText(view->currentPagePlainText());
    }
    dialog.exec();
}

void WiringActions::pptGeneration() {
    PdfView* view = currentView();
    PptGenerationDialog dialog(aiService(), parentWidget_);
    if (view != nullptr) {
        dialog.setDocumentText(view->wholeDocumentPlainText());
    }
    connect(&dialog, &PptGenerationDialog::savePptxRequested, this,
            [this](const QByteArray& bytes) {
                const QString path = QFileDialog::getSaveFileName(
                    parentWidget_, i18n::tr("aipdf::app::MainWindow", "存为 PPTX"),
                    QStringLiteral("ppt.pptx"),
                    i18n::tr("aipdf::app::MainWindow", "PowerPoint 演示文稿 (*.pptx)"));
                if (path.isEmpty()) {
                    return;
                }
                QString error;
                if (!writeFile(path, bytes, &error)) {
                    QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "AI 生成 PPT"), error);
                    return;
                }
                notify(i18n::tr("aipdf::app::MainWindow", "已保存：%1").arg(path));
            });
    connect(&dialog, &PptGenerationDialog::saveMarkdownRequested, this,
            [this](const QString& markdown) {
                const QString path = QFileDialog::getSaveFileName(
                    parentWidget_, i18n::tr("aipdf::app::MainWindow", "存为 Markdown"), QStringLiteral("ppt.md"),
                    i18n::tr("aipdf::app::MainWindow", "Markdown 文件 (*.md)"));
                if (path.isEmpty()) {
                    return;
                }
                QString error;
                if (!writeFile(path, markdown.toUtf8(), &error)) {
                    QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "AI 生成 PPT"), error);
                    return;
                }
                notify(i18n::tr("aipdf::app::MainWindow", "已保存：%1").arg(path));
            });
    dialog.exec();
}

void WiringActions::detection() {
    PdfView* view = currentView();
    DetectionDialog dialog(aiService(), parentWidget_);
    if (view != nullptr) {
        const QString selection = view->currentSelectionText();
        dialog.setSourceText(selection.isEmpty() ? view->currentPagePlainText() : selection);
    }
    dialog.exec();
}

void WiringActions::narration() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    view->speakNarration();
}

// -------------------------------------------------------- 注释三件套 ----

void WiringActions::calculator() {
    CalculatorDialog dialog(parentWidget_);
    connect(&dialog, &CalculatorDialog::insertRequested, this, [this](const QString& value) {
        PdfView* view = requireView();
        if (view == nullptr) {
            return;
        }
        view->insertTextAnnotation(value);
    });
    dialog.exec();
}

void WiringActions::symbolPanel() {
    SymbolPanelDialog dialog(parentWidget_);
    connect(&dialog, &SymbolPanelDialog::insertRequested, this, [this](const QString& value) {
        PdfView* view = requireView();
        if (view == nullptr) {
            return;
        }
        view->insertTextAnnotation(value);
    });
    dialog.exec();
}

void WiringActions::toggleContinuousAnnotationMode(bool enabled) {
    PdfView* view = currentView();
    if (view == nullptr) {
        // 没有文档也要记住这个选择：用户先开开关、再打开文档。
        emit continuousAnnotationModeChanged(enabled);
        return;
    }
    view->setContinuousAnnotationMode(enabled);
    emit continuousAnnotationModeChanged(view->continuousAnnotationMode());
}

// ------------------------------------------------------ 转换与导出 ----

void WiringActions::preflight() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "印前检查"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    const pdf::PreflightReport report = pdf::runPreflightCheck(bytes, &error);
    PreflightDialog dialog(parentWidget_);
    dialog.setReport(report);
    dialog.exec();
}

void WiringActions::portfolio() {
    PortfolioDialog dialog(parentWidget_);
    dialog.exec();
}

void WiringActions::ocrSuspicion() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QVector<pdf::TextPage> pages = view->documentTextPages();
    const QVector<pdf::SuspiciousChar> items =
        pdf::findSuspiciousCharacters(pages, view->currentPagePlainText());
    OcrSuspicionDialog dialog(parentWidget_);
    dialog.setSuspicious(items);
    dialog.exec();
}

void WiringActions::exportHtml() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 HTML"), QStringLiteral("document.html"),
        i18n::tr("aipdf::app::MainWindow", "HTML 文件 (*.html)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 HTML"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    // 每页要把三样东西凑齐：文字（PDFium）、矢量图形与真实字号（内容流）。
    // 少一样就退回旧口径（只有文字 + 估算字号），所以必须走 buildHtmlPage 这一个入口。
    QVector<pdf::HtmlPage> htmlPages;
    const QVector<pdf::TextPage> textPages = view->documentTextPages();
    for (int index = 0; index < textPages.size(); ++index) {
        const pdf::TextPage& page = textPages.at(index);
        htmlPages.append(pdf::buildHtmlPage(bytes, page,
                                             QSizeF(page.contentBox.width(), page.contentBox.height()), index));
    }
    pdf::HtmlExportResult exportResult;
    const QByteArray exported = ConversionTools::exportHtml(htmlPages, &exportResult, &error);
    if (exported.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 HTML"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "导出失败。") : error);
        return;
    }
    if (!writeFile(path, exported, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 HTML"), error);
        return;
    }
    QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 HTML"),
                             htmlExportNoticeText(exportResult));
}

void WiringActions::exportImagePdf() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出图像化 PDF"), QStringLiteral("document-image.pdf"),
        i18n::tr("aipdf::app::MainWindow", "PDF 文件 (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出图像化 PDF"), error);
        return;
    }
    pdf::RasterizeOptions options;
    const QByteArray result = ConversionTools::exportImageBasedPdf(bytes, options, &error);
    if (result.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出图像化 PDF"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "导出失败。") : error);
        return;
    }
    if (!writeFile(path, result, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出图像化 PDF"), error);
        return;
    }
    QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出图像化 PDF"), imageBasedPdfNoticeText());
}

void WiringActions::exportDxf() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 CAD 文件"), QStringLiteral("document.dxf"),
        i18n::tr("aipdf::app::MainWindow", "DXF 文件 (*.dxf)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 CAD 文件"), error);
        return;
    }
    pdf::CadDxfOptions options;
    // 文字也导：CAD 里没有文字就只剩一堆线，看不出画的是什么。
    options.includeText = true;
    const pdf::CadDxfResult cadResult = ConversionTools::exportCadDxf(bytes, options);
    if (!cadResult.ok || cadResult.dxf.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 CAD 文件"),
                             cadResult.error.isEmpty()
                                 ? i18n::tr("aipdf::app::MainWindow", "导出失败。")
                                 : cadResult.error);
        return;
    }
    if (!writeFile(path, cadResult.dxf, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 CAD 文件"), error);
        return;
    }
    QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 CAD 文件"),
                             dxfExportNoticeText(cadResult));
}

void WiringActions::exportOffice() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    // 三种格式都是「导**整篇**」。先问要哪种，别让用户以为只导当前页。
    const QStringList formats = {
        i18n::tr("aipdf::app::MainWindow", "Word 文档（.docx）"),
        i18n::tr("aipdf::app::MainWindow", "Excel 工作簿（.xlsx）"),
        i18n::tr("aipdf::app::MainWindow", "PowerPoint 演示文稿（.pptx）"),
    };
    bool accepted = false;
    // Qt 6 的 getItem 没有 nitems 参数了：项目数由 items 自己决定。
    const QString choice = QInputDialog::getItem(
        parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"),
        i18n::tr("aipdf::app::MainWindow", "要导出成哪种格式？"), formats, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    const int formatIndex = formats.indexOf(choice);
    if (formatIndex < 0) {
        return;
    }

    const QString suffix = formatIndex == 0 ? QStringLiteral("docx")
                                            : (formatIndex == 1 ? QStringLiteral("xlsx")
                                                                : QStringLiteral("pptx"));
    const QString filter = formatIndex == 0
                               ? i18n::tr("aipdf::app::MainWindow", "Word 文档 (*.docx)")
                               : (formatIndex == 1
                                      ? i18n::tr("aipdf::app::MainWindow", "Excel 工作簿 (*.xlsx)")
                                      : i18n::tr("aipdf::app::MainWindow",
                                                 "PowerPoint 演示文稿 (*.pptx)"));
    const QString path = QFileDialog::getSaveFileName(
        parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"),
        QStringLiteral("document.") + suffix, filter);
    if (path.isEmpty()) {
        return;
    }

    QString error;
    const QByteArray documentBytes = view->documentBytes(&error);
    if (documentBytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "导出失败。") : error);
        return;
    }

    QByteArray bytes;
    QString detail;
    // 这个格式带不带图片：Word / PPT 带图片与表格对象，Excel 不带（XLSX 里没有图片）。
    bool keepsImages = false;
    if (formatIndex == 1) {
        const doc::ExcelExportResult result = doc::exportPdfToExcel(documentBytes);
        if (!result.ok) {
            QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"),
                                 result.error.isEmpty()
                                     ? i18n::tr("aipdf::app::MainWindow", "导出失败。")
                                     : result.error);
            return;
        }
        bytes = result.bytes;
        detail = i18n::tr("aipdf::app::MainWindow", "共 %1 页、%2 张工作表（认出 %3 个表格）")
                     .arg(result.pageCount)
                     .arg(result.sheetCount)
                     .arg(result.tableCount);
    } else if (formatIndex == 2) {
        const doc::PptxExportResult result = doc::exportPdfToPptx(documentBytes);
        if (!result.ok) {
            QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"),
                                 result.error.isEmpty()
                                     ? i18n::tr("aipdf::app::MainWindow", "导出失败。")
                                     : result.error);
            return;
        }
        bytes = result.bytes;
        keepsImages = true;
        detail = i18n::tr("aipdf::app::MainWindow", "共 %1 张幻灯片（%2 张图片、%3 个表格）")
                     .arg(result.slideCount)
                     .arg(result.imageCount)
                     .arg(result.tableCount);
    } else {
        const doc::WordExportResult result = doc::exportPdfToWord(documentBytes);
        if (!result.ok) {
            QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"),
                                 result.error.isEmpty()
                                     ? i18n::tr("aipdf::app::MainWindow", "导出失败。")
                                     : result.error);
            return;
        }
        bytes = result.bytes;
        keepsImages = true;
        detail = i18n::tr("aipdf::app::MainWindow",
                          "共 %1 页（%2 张图片、%3 个表格，页与页之间有分页符）")
                     .arg(result.pageCount)
                     .arg(result.imageCount)
                     .arg(result.tableCount);
    }

    if (!writeFile(path, bytes, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"), error);
        return;
    }
    QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"),
                             officeExportNoticeText(formats.at(formatIndex), detail, keepsImages));
}

void WiringActions::exportOfd() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 OFD"), QStringLiteral("document.ofd"),
        i18n::tr("aipdf::app::MainWindow", "OFD 文件 (*.ofd)"));
    if (path.isEmpty()) {
        return;
    }
    const QVector<pdf::TextPage> pages = view->documentTextPages();
    if (pages.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 OFD"),
                             i18n::tr("aipdf::app::MainWindow", "这一页没有可导出的文字层。"));
        return;
    }
    // 图形要从内容流里取（qpdf 通路），所以要拿到文档字节。
    QString error;
    const QByteArray documentBytes = view->documentBytes(&error);
    const QVector<pdf::PageGraphics> graphics =
        documentBytes.isEmpty() ? QVector<pdf::PageGraphics>() : pdf::extractDocumentGraphics(documentBytes);
    QVector<doc::OfdPageContent> contents;
    for (int index = 0; index < pages.size(); ++index) {
        doc::OfdPageContent content;
        content.text = pages.at(index);
        if (index < graphics.size() && graphics.at(index).ok) {
            content.graphics = graphics.at(index);
        }
        contents.append(content);
    }
    const QSizeF size(pages.first().contentBox.width(), pages.first().contentBox.height());
    doc::OfdExportOptions options;
    options.title = view->filePath();
    doc::OfdExportResult exportResult;
    const QByteArray bytes = ConversionTools::exportOfd(contents, size, options, &exportResult, &error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 OFD"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "导出失败。") : error);
        return;
    }
    if (!writeFile(path, bytes, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 OFD"), error);
        return;
    }
    QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 OFD"),
                             ofdExportNoticeText(exportResult));
}

void WiringActions::flattenFonts() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        parentWidget_, i18n::tr("aipdf::app::MainWindow", "PDF 转曲"), QStringLiteral("document-flattened.pdf"),
        i18n::tr("aipdf::app::MainWindow", "PDF 文件 (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "PDF 转曲"), error);
        return;
    }
    doc::FlattenOptions options;
    // 默认是矢量轮廓化：文字变成路径，产物里没有字体依赖。
    // 想保留旧的栅格化行为就显式写 options.mode = doc::FlattenMode::Raster。
    options.mode = doc::FlattenMode::Outline;
    doc::FlattenReport report;
    const QByteArray result = ConversionTools::flattenFonts(bytes, options, &report, &error);
    if (result.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "PDF 转曲"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "转曲失败。") : error);
        return;
    }
    if (!writeFile(path, result, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "PDF 转曲"), error);
        return;
    }
    // 转曲结果必须照实说：取不到轮廓的那些字符去了哪里，要逐条讲清楚。
    QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "PDF 转曲"),
                             flattenFontsNoticeText(report));
}

// ================================================== 第三批接线（任务 J~U）====

void WiringActions::setOcrController(OcrController* controller) {
    ocrController_ = controller;
}

void WiringActions::setAiPanel(AiPanel* panel) {
    aiPanel_ = panel;
}

// ---------------------------------------------------------------- 任务 J ----
//
// OCR 隐藏文本层回写。
//
// 流程：识别结果（词级坐标）→ 转成 pdf::TextPage（字符级框）→
// 领域层写不可见文字层 → 用产出的字节替换当前文档。
//
// 页数对不上要**明说**：不凑页数，不糊弄过去。
void WiringActions::writeInvisibleTextLayer() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "写入隐藏文字层");
    if (ocrController_ == nullptr) {
        QMessageBox::information(parentWidget_, title,
                                 i18n::tr("aipdf::app::MainWindow", "识别控制器不可用。"));
        return;
    }
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    const int pageCount = view->pageCount();
    const int ocrPages = ocrController_->ocrResultCount();
    if (ocrPages == 0) {
        QMessageBox::information(
            parentWidget_, title,
            i18n::tr("aipdf::app::MainWindow",
                     "这份文档还没有识别结果：先在页面上点「识别」（OCR），再回来写隐藏文字层。"));
        return;
    }
    if (ocrPages != pageCount) {
        QMessageBox::warning(parentWidget_, title,
                             invisibleTextLayerPageMismatchText(ocrPages, pageCount));
        return;
    }

    QVector<pdf::TextPage> textPages;
    textPages.reserve(pageCount);
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        ocr::OcrPageResult result;
        if (!ocrController_->ocrResult(pageIndex, &result)) {
            QMessageBox::warning(parentWidget_, title,
                                 invisibleTextLayerPageMismatchText(ocrPages, pageCount));
            return;
        }
        pdf::TextPage textPage;
        QString reason;
        if (!ocrPageToTextPage(result, view->pageContentBox(pageIndex), &textPage, &reason)) {
            QMessageBox::warning(parentWidget_, title, invisibleTextLayerFailureText(reason));
            return;
        }
        textPages.append(textPage);
    }

    const pdf::InvisibleTextLayerResult result = pdf::writeInvisibleTextLayer(bytes, textPages);
    if (!result.ok()) {
        QMessageBox::warning(parentWidget_, title, invisibleTextLayerFailureText(result.error));
        return;
    }
    QString replaceError;
    if (!view->replaceDocumentBytes(result.bytes, &replaceError)) {
        QMessageBox::warning(parentWidget_, title, replaceError);
        return;
    }
    QMessageBox::information(parentWidget_, title,
                             invisibleTextLayerSuccessText(result) + QStringLiteral("\n\n") +
                                 invisibleTextLayerReplaceNoticeText());
}

// ---------------------------------------------------------------- 任务 K ----

void WiringActions::paragraphFormat() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    view->paragraphFormatDialog();
}

// ---------------------------------------------------------------- 任务 L ----

void WiringActions::structTree() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    view->showStructTreeDialog();
}

// ---------------------------------------------------------------- 任务 M ----

void WiringActions::ocgLayers() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "图层面板");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    const pdf::OptionalContentSummary summary = pdf::readOptionalContent(bytes, &error);
    OcgLayerPanel dialog(parentWidget_);
    dialog.setSummary(summary);
    connect(&dialog, &OcgLayerPanel::applyRequested, this,
            [this, view, title](const pdf::OptionalContentPlan& plan) {
                QString readError;
                const QByteArray current = view->documentBytes(&readError);
                if (current.isEmpty()) {
                    QMessageBox::warning(parentWidget_, title, readError);
                    return;
                }
                QString writeError;
                const QByteArray written = pdf::writeOptionalContent(current, plan, &writeError);
                if (written.isEmpty()) {
                    QMessageBox::warning(parentWidget_, title,
                                         writeError.isEmpty()
                                             ? i18n::tr("aipdf::app::MainWindow", "写回图层失败，文档没有修改。")
                                             : writeError);
                    return;
                }
                QString replaceError;
                if (!view->replaceDocumentBytes(written, &replaceError)) {
                    QMessageBox::warning(parentWidget_, title, replaceError);
                    return;
                }
                notify(i18n::tr("aipdf::app::MainWindow", "已写回图层设置。"));
            });
    dialog.exec();
}

// ---------------------------------------------------------------- 任务 N ----

void WiringActions::universalConvert() {
    const QString title = i18n::tr("aipdf::app::MainWindow", "从文件新建 PDF");
    UniversalConvertDialog dialog(parentWidget_);
    connect(&dialog, &UniversalConvertDialog::saveRequested, this,
            [this, &dialog, title](const QString& suggestedName) {
                const QString path = QFileDialog::getSaveFileName(
                    parentWidget_, title,
                    suggestedName.isEmpty() ? QStringLiteral("document.pdf") : suggestedName,
                    i18n::tr("aipdf::app::MainWindow", "PDF 文件 (*.pdf)"));
                if (path.isEmpty()) {
                    return;
                }
                QString error;
                if (!writeFile(path, dialog.result().bytes, &error)) {
                    QMessageBox::warning(parentWidget_, title, error);
                    return;
                }
                notify(i18n::tr("aipdf::app::MainWindow", "已保存：%1").arg(path));
                if (openDocumentHandler_) {
                    openDocumentHandler_(path);
                }
            });
    dialog.exec();
}

// ---------------------------------------------------------------- 任务 O ----

void WiringActions::offlineQa() {
    if (aiPanel_ == nullptr) {
        notify(i18n::tr("aipdf::app::MainWindow", "AI 面板还没打开。"));
        return;
    }
    aiPanel_->requestOfflineQa();
}

// ---------------------------------------------------------------- 任务 P ----

void WiringActions::pdfUaCheck() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "PDF/UA 合规检查");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    const pdf::PdfUaReport report = pdf::checkPdfUa(bytes, &error);
    PdfUaReportDialog dialog(parentWidget_);
    dialog.setReport(report);
    dialog.exec();
}

void WiringActions::pdfAPreflight() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "PDF/A-2b 预检");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    // 预检，不是认证：报告里只有逐条的结构性结论，
    // 界面文案也只说「预检发现 N 个问题」，不说「已通过 PDF/A 校验」。
    //
    // 为什么失败时也照样弹对话框：报告里有一条「本次构建没有启用 qpdf」之类的结论，
    // 那正是用户要看到的原因，弹框反而比一句「失败」有用。
    const pdf::PdfAPreflightReport report = pdf::checkPdfAPreflight(bytes, &error);
    PdfAPreflightDialog dialog(parentWidget_);
    dialog.setReport(report);
    dialog.exec();
}

// PDF/A 合规认证：调本机的 veraPDF。
//
// 为什么单独一条命令而不是并进预检：预检是本程序看文件结构，
// 认证是 veraPDF 的结论 —— 两件事的**出具方**不一样，混在一起用户就会
// 把「预检没问题」当成「已经合规」。这里只转述 veraPDF 的结论，本程序不自己判断。
void WiringActions::pdfAVeraPdfCertification() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "PDF/A 合规认证（veraPDF）");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }

    // 1) 找 veraPDF。没有就如实说没有，不拿预检冒充认证。
    const pdf::VeraPdfTool tool = pdf::findVeraPdf();
    if (!tool.found) {
        QMessageBox::warning(parentWidget_, title, pdf::veraPdfUnavailableNotice());
        return;
    }

    // 2) 落到临时文件：veraPDF 读文件，不读内存。
    QTemporaryDir workDir;
    if (!workDir.isValid()) {
        QMessageBox::warning(parentWidget_, title,
                             i18n::tr("aipdf::app::MainWindow", "建不出临时目录，认证没法跑。"));
        return;
    }
    const QString inputPath = QDir(workDir.path()).filePath(QStringLiteral("input.pdf"));
    const QString reportPath = QDir(workDir.path()).filePath(QStringLiteral("report.xml"));
    {
        QFile input(inputPath);
        if (!input.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(parentWidget_, title,
                                 i18n::tr("aipdf::app::MainWindow", "临时文件写不出来，认证没法跑。"));
            return;
        }
        input.write(bytes);
    }

    // 3) 跑 veraPDF。
    const pdf::VeraPdfPlan plan =
        pdf::planVeraPdfValidation(tool.executable, inputPath, reportPath);
    if (!plan.valid) {
        QMessageBox::warning(parentWidget_, title, plan.error);
        return;
    }
    QProcess process;
    process.setProgram(plan.program);
    process.setArguments(plan.arguments);
    process.start();
    if (!process.waitForStarted(30000)) {
        QMessageBox::warning(parentWidget_, title,
                             i18n::tr("aipdf::app::MainWindow", "启动不了 veraPDF：%1")
                                 .arg(process.errorString()));
        return;
    }
    if (!process.waitForFinished(180000)) {
        process.kill();
        process.waitForFinished(3000);
        QMessageBox::warning(parentWidget_, title,
                             i18n::tr("aipdf::app::MainWindow", "veraPDF 跑了 3 分钟还没结束，已经停掉。"));
        return;
    }

    // 4) 读它的报告。报告文件没有就退一步看标准输出（有的版本直接打在 stdout）。
    QByteArray xml;
    QFile reportFile(reportPath);
    if (reportFile.open(QIODevice::ReadOnly)) {
        xml = reportFile.readAll();
    }
    if (xml.trimmed().isEmpty()) {
        xml = process.readAllStandardOutput();
    }

    // 5) 结论只转述 veraPDF 的。
    QString parseError;
    const pdf::VeraPdfReport report = pdf::parseVeraPdfXmlReport(xml, &parseError);
    if (!report.ok) {
        QString message = parseError;
        const QByteArray standardError = process.readAllStandardError();
        if (!standardError.trimmed().isEmpty()) {
            message += QStringLiteral("\n") + QString::fromUtf8(standardError).trimmed();
        }
        QMessageBox::warning(parentWidget_, title, message);
        return;
    }
    if (report.compliant) {
        QMessageBox::information(parentWidget_, title, report.summary);
        return;
    }
    QMessageBox::warning(parentWidget_, title, pdf::veraPdfFailedNotice(report));
}

// ---------------------------------------------------------------- 任务 Q ----

void WiringActions::xfaFields() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "XFA 字段");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    const pdf::XfaReport report = pdf::inspectXfa(bytes, &error);
    XfaFieldsDialog dialog(parentWidget_);
    dialog.setReport(report);
    dialog.exec();
}

void WiringActions::xfaFormEdit() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "编辑静态 XFA");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    const pdf::XfaReport report = pdf::inspectXfa(bytes, &error);
    if (!report.isXfa) {
        // 不是 XFA 不是错误，但要说清楚为什么这个入口在这儿没用。
        QMessageBox::information(
            parentWidget_, title,
            i18n::tr("aipdf::app::MainWindow", "这份文档没有 XFA 表单（没有 /XFA 数据）。"));
        return;
    }
    if (report.status == pdf::XfaStatus::Error) {
        QMessageBox::warning(parentWidget_, title, report.error);
        return;
    }
    XfaFieldsDialog dialog(parentWidget_);
    dialog.setReport(report);
    dialog.setEditable(true);
    connect(&dialog, &XfaFieldsDialog::applyRequested, this,
            [this, view, title, &dialog](const QVector<QPair<QString, QString>>& values) {
                if (values.isEmpty()) {
                    return;
                }
                // 写回前重新读一遍当前内容：对话框开着的时候文档可能被别处改过，
                // 拿旧字节去写会把那部分改动覆盖掉。
                QString readError;
                const QByteArray current = view->documentBytes(&readError);
                if (current.isEmpty()) {
                    QMessageBox::warning(parentWidget_, title, readError);
                    return;
                }
                QString writeError;
                QByteArray written;
                const pdf::XfaWriteReport writeReport =
                    pdf::writeXfaFieldValues(current, values, &written, &writeError);
                const QStringList failures = app::xfaWriteFailureLines(writeReport);
                if (written.isEmpty()) {
                    // 一个字段都没改成：文档保持原样，原因要一条条说出来。
                    QMessageBox::warning(
                        parentWidget_, title,
                        failures.isEmpty()
                            ? (writeError.isEmpty()
                                   ? i18n::tr("aipdf::app::MainWindow", "没有字段被改动。")
                                   : writeError)
                            : failures.join(QStringLiteral("\n")));
                    return;
                }
                QString replaceError;
                if (!view->replaceDocumentBytes(written, &replaceError)) {
                    QMessageBox::warning(parentWidget_, title, replaceError);
                    return;
                }
                // 表格里刷新一次：当前值变成改完之后的值，待写回的改动清空。
                // 不刷新的话，用户会看着一份还是旧值的清单，再点一次就是重复写。
                const pdf::XfaReport refreshed = pdf::inspectXfa(written, &readError);
                if (refreshed.isXfa) {
                    dialog.setReport(refreshed);
                }
                // 部分成功也要说清楚：改了几项、哪几项没改成。
                if (failures.isEmpty()) {
                    notify(app::xfaWriteResultText(writeReport));
                } else {
                    QMessageBox::warning(parentWidget_, title,
                                         app::xfaWriteResultText(writeReport) +
                                             QStringLiteral("\n") +
                                             failures.join(QStringLiteral("\n")));
                }
            });
    dialog.exec();
}

// ---------------------------------------------------------------- 任务 R ----

void WiringActions::insertRichMedia(bool preferThreeD) {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "插入 3D 模型 / 多媒体");
    RichMediaDialog dialog(parentWidget_);
    dialog.setKind(preferThreeD ? RichMediaDialog::Kind::ThreeD : RichMediaDialog::Kind::Media);
    connect(&dialog, &RichMediaDialog::insertRequested, this,
            [this, view, &dialog, title](const QString& path, RichMediaDialog::Kind kind) {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    QMessageBox::warning(parentWidget_, title,
                                         i18n::tr("aipdf::app::MainWindow", "读不了这个文件：%1").arg(path));
                    return;
                }
                const QByteArray content = file.readAll();
                file.close();
                if (content.isEmpty()) {
                    QMessageBox::warning(parentWidget_, title,
                                         i18n::tr("aipdf::app::MainWindow", "这个文件是空的：%1").arg(path));
                    return;
                }
                QString error;
                const QByteArray bytes = view->documentBytes(&error);
                if (bytes.isEmpty()) {
                    QMessageBox::warning(parentWidget_, title, error);
                    return;
                }
                const pdf::MediaBox box = view->pageContentBox(view->currentPage());
                const QRectF rect(box.left + box.width() * 0.25,
                                  box.bottom + box.height() * 0.25,
                                  box.width() * 0.5, box.height() * 0.5);
                QByteArray written;
                if (kind == RichMediaDialog::Kind::ThreeD) {
                    pdf::ThreeDAnnotationSpec spec;
                    spec.pageIndex = view->currentPage();
                    spec.rect = rect;
                    spec.modelBytes = content;
                    written = pdf::writeThreeDAnnotation(bytes, spec, &error);
                } else {
                    pdf::MediaAnnotationSpec spec;
                    spec.pageIndex = view->currentPage();
                    spec.rect = rect;
                    spec.mediaBytes = content;
                    spec.fileName = QFileInfo(path).fileName();
                    spec.embedded = true;
                    written = pdf::writeMediaAnnotation(bytes, spec, &error);
                }
                if (written.isEmpty()) {
                    QMessageBox::warning(parentWidget_, title,
                                         error.isEmpty()
                                             ? i18n::tr("aipdf::app::MainWindow", "写入失败，文档没有修改。")
                                             : error);
                    return;
                }
                // 回读校验：结构写得对不对，要读回来看过才算数。
                const pdf::RichMediaReadResult read = pdf::readRichMedia(written);
                dialog.setReadResult(read);
                if (!read.ok()) {
                    QMessageBox::warning(parentWidget_, title, read.error);
                    return;
                }
                QString replaceError;
                if (!view->replaceDocumentBytes(written, &replaceError)) {
                    QMessageBox::warning(parentWidget_, title, replaceError);
                    return;
                }
                notify(i18n::tr("aipdf::app::MainWindow", "已写入注解。"));
            });
    dialog.exec();
}

// ---------------------------------------------------------- 页面内音视频 ----

void WiringActions::insertScreenMedia() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "页面内音视频");
    const QString path = QFileDialog::getOpenFileName(
        parentWidget_, title, QString(),
        i18n::tr("aipdf::app::MainWindow", "页面内音视频 (*.mp4 *.avi *.wav)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(
            parentWidget_, title,
            i18n::tr("aipdf::app::MainWindow", "读不了这个文件：%1").arg(path));
        return;
    }
    const QByteArray content = file.readAll();
    file.close();
    if (content.isEmpty()) {
        QMessageBox::warning(
            parentWidget_, title,
            i18n::tr("aipdf::app::MainWindow", "这个文件是空的：%1").arg(path));
        return;
    }

    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title, error);
        return;
    }
    // 摆在当前页正中：先给一个能用的位置，用户可以在阅读器里再调。
    const pdf::MediaBox box = view->pageContentBox(view->currentPage());
    const QRectF rect(box.left + box.width() * 0.25,
                      box.bottom + box.height() * 0.25,
                      box.width() * 0.5, box.height() * 0.5);

    pdf::ScreenMediaSpec spec;
    spec.pageIndex = view->currentPage();
    spec.rect = rect;
    spec.mediaBytes = content;
    spec.fileName = QFileInfo(path).fileName();
    const QByteArray written = pdf::writeScreenMediaAnnotation(bytes, spec, &error);
    if (written.isEmpty()) {
        // 这里最常见的原因是容器不在白名单里：页面内播放按**文件头**认
        // MP4 / AVI / WAV 三种容器，其余格式明确失败（错误文案里会列出支持项）。
        // 注意：结构写对不等于阅读器放得出来 —— Acrobat 不播 AVI，
        // WAV 在 PDF 里更适合走音频注解。
        QMessageBox::warning(
            parentWidget_, title,
            error.isEmpty()
                ? i18n::tr("aipdf::app::MainWindow", "写入失败，文档没有修改。")
                : error);
        return;
    }
    // 回读校验：结构写对没有，读回来看过才算数。
    const QVector<pdf::ScreenMediaAnnotation> found =
        pdf::readScreenMediaAnnotations(written, &error);
    if (found.isEmpty()) {
        QMessageBox::warning(parentWidget_, title, error);
        return;
    }
    QString replaceError;
    if (!view->replaceDocumentBytes(written, &replaceError)) {
        QMessageBox::warning(parentWidget_, title, replaceError);
        return;
    }
    notify(i18n::tr("aipdf::app::MainWindow",
                    "已写入页面内播放的视频（%1）。阅读器需支持媒体注解才能在页面里放出来。")
               .arg(QFileInfo(path).fileName()));
}

// ---------------------------------------------------------- 3D 模型查看 ----
// 读文档里的 3D 注解 → 渲染 → 给出可拖动旋转的查看器。
//
// 判定一律在领域层：这里不自己解析模型，也不自己判断「这是模型还是海报」，
// 只把 pdf::threeDRenderModel 给的结果摊给用户。
void WiringActions::view3dModel() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "3D 模型");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    const pdf::RichMediaReadResult read = pdf::readRichMedia(bytes);
    if (!read.ok()) {
        QMessageBox::warning(parentWidget_, title, read.error);
        return;
    }
    if (read.threeD.isEmpty()) {
        QMessageBox::information(parentWidget_, title,
                                 i18n::tr("aipdf::app::MainWindow", "这份文档里没有 3D 注解。"));
        return;
    }
    // 多个 3D 注解时取当前页上的第一个；当前页没有就取整份文档的第一个。
    const pdf::ThreeDAnnotationInfo* picked = nullptr;
    int pickedIndexOnPage = 0;
    const int currentPage = view->currentPage();
    int samePageSeen = 0;
    for (const pdf::ThreeDAnnotationInfo& info : read.threeD) {
        if (info.pageIndex == currentPage) {
            if (picked == nullptr) {
                picked = &info;
                pickedIndexOnPage = samePageSeen;
            }
        }
        if (info.pageIndex == currentPage) {
            ++samePageSeen;
        }
    }
    if (picked == nullptr) {
        picked = &read.threeD.first();
        pickedIndexOnPage = 0;
    }

    // 降级海报读不到不算失败：模型能渲染出来时根本用不到它。
    pdf::ThreeDPosterImage poster;
    QString posterError;
    pdf::FallbackImage fallback;
    if (pdf::readThreeDFallbackImage(bytes, picked->pageIndex, pickedIndexOnPage, fallback, &posterError)) {
        poster.width = fallback.width;
        poster.height = fallback.height;
        poster.rgb = fallback.rgb;
    }

    ThreeDViewerDialog dialog(*picked, poster, parentWidget_);
    dialog.exec();
}

// ------------------------------------------------------------ 全文流式 ----
//
// 把整篇按新的正文栏宽重新流排并写回。
//
// 判定全在领域层（pdf::reflowDocumentStream）：它自己决定哪些段排得下、
// 哪些段保留原文，写回之后用 PDFium 回读校验。
// 界面只负责问参数、收结果、写回文档。
void WiringActions::reflowWholeDocumentStream() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "全文流式重排");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    const pdf::MediaBox box = view->pageContentBox(view->currentPage());
    bool accepted = false;
    pdf::StreamingReflowOptions options;
    options.marginLeft = 36.0;
    options.marginRight = 36.0;
    const double column = QInputDialog::getDouble(
        parentWidget_, title, i18n::tr("aipdf::app::MainWindow", "正文栏宽（点）:"),
        qMax(120.0, box.width() - 72.0), 72.0, 2000.0, 1, &accepted);
    if (!accepted) {
        return;
    }
    options.targetPageWidth = column + options.marginLeft + options.marginRight;

    const pdf::StreamingReflowResult result = pdf::reflowDocumentStream(bytes, options);
    if (!result.ok()) {
        // 领域层拒绝的原因原样显示：它说「没有结构标签」就是没有结构标签，
        // 界面不自己另搞一套启发式重排。
        QMessageBox::warning(parentWidget_, title, result.report.error);
        return;
    }
    // 流式重排是就地改，放不下的段只能保留原文。带结构标签的文档改用整篇
    // 重排：它按阅读顺序重新分页，段落和图表会一起搬到下一页。
    QByteArray finalBytes = result.bytes;
    QStringList crossPageNotes;
    if (result.report.overflowBlockCount > 0) {
        if (result.report.hasStructTags) {
            pdf::ReflowOptions full;
            full.pageWidth = options.targetPageWidth;
            full.marginLeft = options.marginLeft;
            full.marginRight = options.marginRight;
            const pdf::DocumentReflowResult fullResult = pdf::reflowTaggedDocument(bytes, full);
            if (fullResult.ok()) {
                finalBytes = fullResult.bytes;
                crossPageNotes.append(
                    i18n::tr("aipdf::app::MainWindow",
                             "有 %1 段在原页放不下，已改用整篇重排，把它们连同图表搬到下一页。")
                        .arg(result.report.overflowBlockCount));
            } else {
                crossPageNotes.append(
                    i18n::tr("aipdf::app::MainWindow",
                             "有 %1 段在原页放不下；整篇重排也失败：%2")
                        .arg(result.report.overflowBlockCount)
                        .arg(fullResult.report.error));
            }
        } else {
            crossPageNotes.append(
                i18n::tr("aipdf::app::MainWindow",
                         "有 %1 段在原页放不下；文档没有结构标签，无法跨页搬运，已保留原文。")
                    .arg(result.report.overflowBlockCount));
        }
    }
    QString replaceError;
    if (!view->replaceDocumentBytes(finalBytes, &replaceError)) {
        QMessageBox::warning(parentWidget_, title, replaceError);
        return;
    }
    QString text = i18n::tr("aipdf::app::MainWindow",
                            "全文流式重排完成：%1 页、%2 段里重排了 %3 段（%4 字）。")
                        .arg(result.report.pageCount)
                        .arg(result.report.blockCount)
                        .arg(result.report.reflowedBlockCount)
                        .arg(result.report.outputCharCount);
    if (!result.report.hasStructTags) {
        text += QLatin1Char('\n') +
                i18n::tr("aipdf::app::MainWindow", "这份文档没有结构标签，本次只按内容流里的段落逐段处理。");
    }
    if (result.report.degradedBlockCount > 0) {
        text += QLatin1Char('\n') +
                i18n::tr("aipdf::app::MainWindow", "有 %1 段里有标准 14 号字体表示不了的字符，写成了 '?' 占位符。")
                    .arg(result.report.degradedBlockCount);
    }
    for (const QString& note : crossPageNotes) {
        text += QLatin1Char('\n') + note;
    }
    for (const QString& note : result.report.notes) {
        text += QLatin1Char('\n') + note;
    }
    QMessageBox::information(parentWidget_, title, text);
}

// -------------------------------------------------------- 跨文本块合并 ----
void WiringActions::mergeTextBlocksAcrossSelection() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "跨文本块合并");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    QVector<pdf::ContentTextBlock> blocks;
    if (!pdf::readContentTextBlocks(bytes, &blocks, &error)) {
        QMessageBox::warning(parentWidget_, title, error);
        return;
    }
    const int page = view->currentPage();
    QVector<pdf::ContentTextBlock> pageBlocks;
    for (const pdf::ContentTextBlock& block : blocks) {
        if (block.pageIndex == page) {
            pageBlocks.append(block);
        }
    }
    if (pageBlocks.size() < 2) {
        QMessageBox::information(parentWidget_, title,
                                 i18n::tr("aipdf::app::MainWindow", "这一页上的文本块不足两块，没有可合并的东西。"));
        return;
    }
    TextBlockMergeDialog dialog(pageBlocks, parentWidget_);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QVector<int> ids = dialog.selectedBlockIds();
    if (ids.size() < 2) {
        return;
    }
    pdf::MergeBlocksOptions options;
    const pdf::MediaBox box = view->pageContentBox(page);
    options.targetWidth = qMax(80.0, box.width() - 72.0);

    const pdf::MergeBlocksResult result = pdf::mergeTextBlocksAcrossSelection(bytes, ids, options);
    if (!result.ok()) {
        QMessageBox::warning(parentWidget_, title, result.report.error);
        return;
    }
    QString replaceError;
    if (!view->replaceDocumentBytes(result.bytes, &replaceError)) {
        QMessageBox::warning(parentWidget_, title, replaceError);
        return;
    }
    QString text = i18n::tr("aipdf::app::MainWindow",
                            "合并完成：%1 块合成一段，共 %2 行（%3 字）。")
                        .arg(result.report.blockCount)
                        .arg(result.report.mergedLineCount)
                        .arg(result.report.outputCharCount);
    if (result.report.imageBetween || result.report.tableBetween) {
        text += QLatin1Char('\n') +
                i18n::tr("aipdf::app::MainWindow", "块与块之间有图片或表格，合并后的位置可能与原位置有出入。");
    }
    QMessageBox::information(parentWidget_, title, text);
}

// ------------------------------------------------------------ 整篇翻译 ----
void WiringActions::translateWholeDocument() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "整篇翻译");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    WorkerTranslationProvider provider(workerClient_);
    if (!provider.isConfigured()) {
        QMessageBox::warning(parentWidget_, title,
                             i18n::tr("aipdf::app::MainWindow", "AI Worker 没有连上，翻译不了。"));
        return;
    }
    if (provider.isDemo()) {
        QMessageBox::warning(
            parentWidget_, title,
            i18n::tr("aipdf::app::MainWindow", "当前是演示模式，翻译出来的示例不能写回文档。"));
        return;
    }
    // 语言名用各自的自称（不翻译）：翻译出来的语言名用户认不出来。
    const QStringList languages{QStringLiteral("中文（简体）"), QStringLiteral("English"),
                                QStringLiteral("日本語"), QStringLiteral("한국어"),
                                QStringLiteral("Français"), QStringLiteral("Deutsch")};
    bool accepted = false;
    const QString language = QInputDialog::getItem(parentWidget_, title,
                                                   i18n::tr("aipdf::app::MainWindow", "译成哪种语言？"),
                                                   languages, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    // 整篇翻译会把全文分段发给 AI 服务：这一句必须说清楚，不能默默发出去。
    const QMessageBox::StandardButton answer = QMessageBox::question(
        parentWidget_, title,
        i18n::tr("aipdf::app::MainWindow",
                 "整篇翻译会把这份文档的正文按段发给 AI 服务。确定继续吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    pdf::TranslationOptions options;
    options.targetLanguage = language;
    const pdf::TranslationWriteResult result = pdf::translateDocument(bytes, &provider, options);
    if (!result.ok()) {
        QMessageBox::warning(parentWidget_, title, result.report.error);
        return;
    }
    QString replaceError;
    if (!view->replaceDocumentBytes(result.bytes, &replaceError)) {
        QMessageBox::warning(parentWidget_, title, replaceError);
        return;
    }
    QString text = i18n::tr("aipdf::app::MainWindow",
                            "翻译完成：%1 段里写回了 %2 段译文（%3 字），%4 段保留原文。")
                        .arg(result.report.unitCount)
                        .arg(result.report.writtenCount)
                        .arg(result.report.translatedCharCount)
                        .arg(result.report.failedCount);
    for (const pdf::TranslationUnit& unit : result.report.units) {
        if (!unit.written && !unit.reason.isEmpty()) {
            text += QLatin1Char('\n') +
                    i18n::tr("aipdf::app::MainWindow", "第 %1 页：%2").arg(unit.pageIndex + 1).arg(unit.reason);
        }
    }
    QMessageBox::information(parentWidget_, title, text);
}

// ---------------------------------------------------------------- 任务 S ----

void WiringActions::documentReflow() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "按新页宽重排");
    ReflowDialog dialog(parentWidget_);
    connect(&dialog, &ReflowDialog::reflowRequested, this,
            [this, view, &dialog, title](const pdf::ReflowOptions& options) {
                QString error;
                const QByteArray bytes = view->documentBytes(&error);
                if (bytes.isEmpty()) {
                    QMessageBox::warning(parentWidget_, title, error);
                    return;
                }
                const pdf::DocumentReflowResult result = pdf::reflowTaggedDocument(bytes, options);
                if (!result.ok()) {
                    // 领域层拒绝的原因原样显示：它说「没有标签树」就是没有标签树，
                    // 界面不自己另搞一套启发式重排。
                    dialog.setReport(result.report);
                    QMessageBox::warning(parentWidget_, title, result.report.error);
                    return;
                }
                dialog.setReport(result.report);
                // 写回打开的文档：重排完不落到用户正在看的那份上，
                // 「重排完成」就只是一句报告，用户的文档一点没变。
                //
                // 领域层已经把「搬不过去的对象」拦下来了（拦下来时 bytes 是空的，
                // report.error 里写明少了什么），所以这里只处理写盘本身失败。
                if (result.bytes.isEmpty()) {
                    return;
                }
                QString replaceError;
                if (!view->replaceDocumentBytes(result.bytes, &replaceError)) {
                    QMessageBox::warning(parentWidget_, title, replaceError);
                    return;
                }
                notify(i18n::tr("aipdf::app::MainWindow", "重排完成：%1 页。")
                           .arg(result.report.outputPageCount));
            });
    // 对话框是模态的：用户点「重排」时领域层就跑完了，写回也在那时完成。
    // 关闭对话框不需要再做别的动作 —— 关闭按钮触发的是 reject，
    // exec() 返回的是 Rejected，所以收尾不能挂在 exec() 后面（那样永远走不到）。
    dialog.exec();
}

// ---------------------------------------------------------------- 任务 T ----

void WiringActions::actionWizard() {
    const QString title = i18n::tr("aipdf::app::MainWindow", "动作向导");
    WizardDialog dialog(parentWidget_);
    connect(&dialog, &WizardDialog::runRequested, this,
            [this, &dialog, title](const doc::ActionWizard& wizard) {
                QString error;
                // 校验交给领域层：参数缺了、类型不对、取值不合法都在这里报出来。
                if (!doc::validateActionWizard(wizard, &error)) {
                    QMessageBox::warning(parentWidget_, title, error);
                    return;
                }
                const doc::WizardStepExecutor executor =
                    [this](const doc::WizardStep& step, QString* reason) {
                        if (step.type == doc::WizardActionType::OpenFile) {
                            QString path;
                            for (const doc::WizardParam& param : step.params) {
                                if (param.name == QStringLiteral("path")) {
                                    path = param.stringValue;
                                }
                            }
                            if (path.isEmpty()) {
                                if (reason != nullptr) {
                                    *reason = i18n::tr("aipdf::app::MainWindow", "这一步没有文件路径。");
                                }
                                return false;
                            }
                            if (!openDocumentHandler_) {
                                if (reason != nullptr) {
                                    *reason = i18n::tr("aipdf::app::MainWindow", "没有可以打开文件的入口。");
                                }
                                return false;
                            }
                            openDocumentHandler_(path);
                            return true;
                        }
                        PdfView* view = currentView();
                        if (view == nullptr) {
                            if (reason != nullptr) {
                                *reason = i18n::tr("aipdf::app::MainWindow", "请先打开一份 PDF。");
                            }
                            return false;
                        }
                        QString stepReason;
                        if (!view->applyDocumentAction(step.type, step.params, &stepReason)) {
                            if (reason != nullptr) {
                                *reason = stepReason;
                            }
                            return false;
                        }
                        return true;
                    };
                const doc::WizardRunReport report =
                    doc::runActionWizard(wizard, executor);
                dialog.setRunReport(report);
            });
    dialog.exec();
}

// ---------------------------------------------------------------- 任务 U ----

namespace {

// 把领域层排好的发票版式画到打印机 / PDF 上。
//
// 行位置由 doc::renderInvoiceText 算好，这里只负责把行画出来：
// 界面不自己算行位置（那是领域层的活）。
void drawInvoiceText(QPainter* painter, const QString& text) {
    if (painter == nullptr) {
        return;
    }
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPointSize(9);
    painter->setFont(font);
    const QFontMetrics metrics(font);
    const double lineHeight = metrics.height();
    const double marginPt = 36.0;
    const QRectF page = painter->viewport();
    double y = marginPt;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        if (y + lineHeight > page.bottom()) {
            painter->end();
            return;
        }
        painter->drawText(QPointF(page.left() + marginPt, y), line);
        y += lineHeight;
    }
}

}  // namespace

void WiringActions::invoicePrint() {
    const QString title = i18n::tr("aipdf::app::MainWindow", "发票打印");
    InvoiceDialog dialog(parentWidget_);
    connect(&dialog, &InvoiceDialog::printRequested, this, [this, &dialog, title] {
        QString error;
        const QString text = dialog.previewText(&error);
        if (text.isEmpty()) {
            QMessageBox::warning(parentWidget_, title,
                                 error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "发票还没填完整。") : error);
            return;
        }
        QPrinter printer(QPrinter::HighResolution);
        QPrintDialog printDialog(&printer, parentWidget_);
        printDialog.setWindowTitle(title);
        if (printDialog.exec() != QDialog::Accepted) {
            return;
        }
        QPainter painter;
        if (!painter.begin(&printer)) {
            QMessageBox::warning(parentWidget_, title,
                                 i18n::tr("aipdf::app::MainWindow", "打印机没有接受这次打印。"));
            return;
        }
        drawInvoiceText(&painter, text);
        painter.end();
        notify(i18n::tr("aipdf::app::MainWindow", "已送到打印机。"));
    });
    connect(&dialog, &InvoiceDialog::exportRequested, this, [this, &dialog, title] {
        QString error;
        const QString text = dialog.previewText(&error);
        if (text.isEmpty()) {
            QMessageBox::warning(parentWidget_, title,
                                 error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "发票还没填完整。") : error);
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            parentWidget_, title, QStringLiteral("invoice.pdf"),
            i18n::tr("aipdf::app::MainWindow", "PDF 文件 (*.pdf)"));
        if (path.isEmpty()) {
            return;
        }
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        QPainter painter;
        if (!painter.begin(&printer)) {
            QMessageBox::warning(parentWidget_, title,
                                 i18n::tr("aipdf::app::MainWindow", "写不了这个文件：%1").arg(path));
            return;
        }
        drawInvoiceText(&painter, text);
        painter.end();
        notify(i18n::tr("aipdf::app::MainWindow", "已导出：%1").arg(path));
    });
    dialog.exec();
}


// ------------------------------------------------ 第四批接线（补齐缺口）----

// 把转换出来的 PDF 存盘并打开。
//
// 为什么统一走「存盘再打开」而不是直接塞进新标签页：转换出来的结果没有来源文件，
// 用户得能拿到一份真实文件（发给别人、接着编辑）。直接开而不落盘，
// 用户就没法把它存回去。
void WiringActions::saveConvertedPdf(const QString& title,
                                     const ConversionInputOutcome& outcome) {
    const QString suggested = outcome.suggestedFileName.isEmpty()
                                  ? QStringLiteral("document.pdf")
                                  : outcome.suggestedFileName;
    const QString path = QFileDialog::getSaveFileName(
        parentWidget_, title, suggested,
        i18n::tr("aipdf::app::MainWindow", "PDF 文件 (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!writeFile(path, outcome.bytes, &error)) {
        QMessageBox::warning(parentWidget_, title, error);
        return;
    }
    // 警告与逐条说明用信息框 / 状态栏交代：转换成功但有损时不能一声不吭。
    if (!outcome.warning.isEmpty()) {
        QMessageBox::information(parentWidget_, title, outcome.warning);
    }
    for (const QString& line : outcome.detailLines) {
        notify(line);
    }
    notify(i18n::tr("aipdf::app::MainWindow", "已保存：%1").arg(path));
    if (openDocumentHandler_) {
        openDocumentHandler_(path);
    }
}

void WiringActions::convertFromClipboard() {
    const QString title = i18n::tr("aipdf::app::MainWindow", "从剪贴板新建 PDF");
    const ConversionInputOutcome outcome = convertClipboardToPdf();
    if (!outcome.ok) {
        QMessageBox::warning(parentWidget_, title,
                             outcome.error.isEmpty()
                                 ? i18n::tr("aipdf::app::MainWindow", "没能从剪贴板生成 PDF。")
                                 : outcome.error);
        return;
    }
    saveConvertedPdf(title, outcome);
}

void WiringActions::convertFromWeb() {
    const QString title = i18n::tr("aipdf::app::MainWindow", "从网页新建 PDF");
    const ConversionInputOutcome outcome = convertWebToPdf(parentWidget_);
    if (!outcome.ok) {
        // 对话框里已经说过原因（状态区），这里只在有明确原因时再弹一次。
        if (!outcome.error.isEmpty()) {
            QMessageBox::warning(parentWidget_, title, outcome.error);
        }
        return;
    }
    saveConvertedPdf(title, outcome);
}

void WiringActions::convertFromFormFile() {
    const QString title = i18n::tr("aipdf::app::MainWindow", "从表单文件新建 PDF");
    const ConversionInputOutcome outcome = convertFormFileToPdf(parentWidget_);
    if (!outcome.ok) {
        if (!outcome.error.isEmpty()) {
            QMessageBox::warning(parentWidget_, title, outcome.error);
        }
        return;
    }
    saveConvertedPdf(title, outcome);
}

void WiringActions::optimizeScannedPages() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "优化扫描件");
    const QString path = QFileDialog::getSaveFileName(
        parentWidget_, title, QStringLiteral("scan-optimized.pdf"),
        i18n::tr("aipdf::app::MainWindow", "PDF 文件 (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    doc::ScannedOptimizeOptions options;
    doc::ScannedOptimizeReport report;
    const QByteArray optimized = doc::optimizeScannedPdf(bytes, options, &report, &error);
    if (optimized.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "优化失败。") : error);
        return;
    }
    if (!writeFile(path, optimized, &error)) {
        QMessageBox::warning(parentWidget_, title, error);
        return;
    }
    // 纠偏是有损的：哪几页纠了多少度要逐条报出来。
    QStringList lines;
    for (const doc::ScannedPageOptimize& page : report.pages) {
        if (!page.note.isEmpty()) {
            lines.append(i18n::tr("aipdf::app::MainWindow", "第 %1 页：%2").arg(page.pageIndex + 1).arg(page.note));
        }
    }
    QString summary = i18n::tr("aipdf::app::MainWindow", "优化完成：%1 页，已去噪并纠偏，已保存：%2")
                          .arg(report.pageCount)
                          .arg(path);
    if (!lines.isEmpty()) {
        summary += QLatin1Char('\n') + lines.join(QLatin1Char('\n'));
    }
    for (const QString& note : report.notes) {
        summary += QLatin1Char('\n') + note;
    }
    QMessageBox::information(parentWidget_, title, summary);
}

void WiringActions::scanFromScanner() {
    const QString title = i18n::tr("aipdf::app::MainWindow", "从扫描仪新建 PDF");
    ScannerCaptureDialog dialog(parentWidget_);
    if (dialog.exec() != QDialog::Accepted) {
        // 关掉的原因要说清楚：「没装扫描仪」和「用户自己取消」是两回事。
        const ScanErrorKind reason = dialog.cancelReason();
        if (reason != ScanErrorKind::None && reason != ScanErrorKind::UserCancelled) {
            QMessageBox::information(parentWidget_, title, scanErrorMessage(reason));
        }
        return;
    }
    const QByteArray bytes = dialog.pdfBytes();
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             i18n::tr("aipdf::app::MainWindow", "没有扫到任何页面。"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        parentWidget_, title, QStringLiteral("scan.pdf"),
        i18n::tr("aipdf::app::MainWindow", "PDF 文件 (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!writeFile(path, bytes, &error)) {
        QMessageBox::warning(parentWidget_, title, error);
        return;
    }
    notify(i18n::tr("aipdf::app::MainWindow", "扫描完成：%1 页，已保存：%2")
               .arg(dialog.pageCount()).arg(path));
    if (openDocumentHandler_) {
        openDocumentHandler_(path);
    }
}

void WiringActions::compareDocuments() {
    const QString title = i18n::tr("aipdf::app::MainWindow", "文档比较");
    DocumentCompareDialog dialog(parentWidget_);
    // 当前打开的文档就是比较的一边时，把它预填进去：用户少点一次文件选择。
    PdfView* view = currentView();
    if (view != nullptr) {
        const QString current = view->filePath();
        if (!current.isEmpty()) {
            dialog.setFiles(current, QString());
        }
    }
    // 引用定位：点一条差异就跳到那一页。
    //
    // 跳的是**当前打开的文档**，所以页号越界（新文里的页）时不能瞎跳 ——
    // 跳错页比不跳更糟，这里如实说清楚。
    connect(&dialog, &DocumentCompareDialog::navigateToPage, this, [this, view](int page, const QString&) {
        if (view == nullptr) {
            return;
        }
        if (page < 0 || page >= view->pageCount()) {
            notify(i18n::tr("aipdf::app::MainWindow",
                            "这条差异在新文里，当前打开的是原文，页面跳不过去。"));
            return;
        }
        view->goToPage(page);
    });
    // 版面差异带矩形：跳过去并把那一块选上，用户一眼看到说的是哪一块。
    connect(&dialog, &DocumentCompareDialog::navigateToBlock, this,
            [this, view](int page, const QRectF& bounds, const QString&) {
                if (view == nullptr) {
                    return;
                }
                if (page < 0 || page >= view->pageCount()) {
                    notify(i18n::tr("aipdf::app::MainWindow",
                                    "这条差异在新文里，当前打开的是原文，页面跳不过去。"));
                    return;
                }
                view->goToBlock(page, bounds);
            });
    dialog.exec();
}

void WiringActions::batchEncrypt() {
    const QString title = i18n::tr("aipdf::app::MainWindow", "批量加密");
    const QString directory = QFileDialog::getExistingDirectory(
        parentWidget_, i18n::tr("aipdf::app::MainWindow", "选择要批量加密的目录"));
    if (directory.isEmpty()) {
        return;
    }
    QDir source(directory);
    const QStringList files =
        source.entryList(QStringList{QStringLiteral("*.pdf")}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        QMessageBox::information(parentWidget_, title,
                                 i18n::tr("aipdf::app::MainWindow", "这个目录里没有 PDF。"));
        return;
    }
    // 密码用现成的「加密与权限」对话框收：单文档加密与批量加密必须是同一套语义，
    // 两处各写一套密码规则迟早会对不上。
    EncryptionDialog dialog(doc::defaultEncryptOptions(QString()), true, parentWidget_);
    if (dialog.exec() != QDialog::Accepted || !dialog.encryptionEnabled()) {
        return;
    }
    const pdf::PdfEncryptionOptions options = dialog.options();
    if (options.userPassword.isEmpty()) {
        // 空密码意味着谁都能直接打开，那不叫加密。批处理那一步也会失败，
        // 这里先说清楚，免得用户以为加密过了。
        QMessageBox::warning(parentWidget_, title,
                             i18n::tr("aipdf::app::MainWindow", "打开密码不能为空。"));
        return;
    }
    const QString outputDirectory = QFileDialog::getExistingDirectory(
        parentWidget_, i18n::tr("aipdf::app::MainWindow", "选择加密后的输出目录"));
    if (outputDirectory.isEmpty()) {
        return;
    }
    QStringList inputPaths;
    for (const QString& name : files) {
        inputPaths.append(source.filePath(name));
    }
    QVector<doc::BatchStep> steps;
    steps.append(doc::makeEncryptStep(i18n::tr("aipdf::app::MainWindow", "加密"), options));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const doc::BatchReport report = doc::runBatch(inputPaths, QStringList(), steps, true,
                                                  outputDirectory);
    QApplication::restoreOverrideCursor();
    // 批量结果只给一行汇总：每个文件为什么失败，报告 JSON 里逐条写着。
    if (report.failedCount > 0) {
        QMessageBox::warning(
            parentWidget_, title,
            i18n::tr("aipdf::app::MainWindow", "批量加密完成：成功 %1，失败 %2。失败原因见输出目录里的报告。")
                .arg(report.succeededCount).arg(report.failedCount));
    } else {
        notify(i18n::tr("aipdf::app::MainWindow", "批量加密完成：成功 %1，失败 %2。")
                   .arg(report.succeededCount).arg(report.failedCount));
    }
}


// ------------------------------------------------ 第四批接线（第二批）----

void WiringActions::togglePresentationLoop() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const bool enabled = !view->presentationLoop();
    view->setPresentationLoop(enabled);
    notify(enabled ? i18n::tr("aipdf::app::MainWindow", "循环放映已打开：放映到最后一页会回到第一页。")
                    : i18n::tr("aipdf::app::MainWindow", "循环放映已关闭：放映到最后一页会停住。"));
}

void WiringActions::autoTagFormFields() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "自动添加表单域标签");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    const pdf::FormAutoTagResult result = pdf::autoTagFormFields(bytes);
    if (!result.ok) {
        QMessageBox::warning(parentWidget_, title, result.error);
        return;
    }
    QString replaceError;
    if (!view->replaceDocumentBytes(result.bytes, &replaceError)) {
        QMessageBox::warning(parentWidget_, title, replaceError);
        return;
    }
    // 填不上的域逐条说明：没有替代文本就说没有，不替用户编一句。
    QStringList lines;
    lines.append(i18n::tr("aipdf::app::MainWindow", "已给 %1 个表单域加上标签。")
                     .arg(result.taggedFieldCount));
    // 合并还是新建要说清楚：文档本来就有标签树时，这次是往里合并，
    // 原来的章节标题与阅读顺序都保住了 —— 这是用户最关心的一点。
    if (result.mergedIntoExistingTree) {
        lines.append(i18n::tr("aipdf::app::MainWindow", "文档本来就有标签树，这次是合并进去的，保留了 %1 个既有元素。")
                     .arg(result.preservedElementCount));
        QStringList tagParts;
        for (const pdf::FormTagPreservedTag& preserved : result.preservedTags) {
            tagParts.append(QStringLiteral("%1×%2").arg(preserved.tag).arg(preserved.count));
        }
        if (!tagParts.isEmpty()) {
            lines.append(i18n::tr("aipdf::app::MainWindow", "保留下来的元素类型：%1")
                         .arg(tagParts.join(QStringLiteral("、"))));
        }
    } else {
        lines.append(i18n::tr("aipdf::app::MainWindow", "这份文档本来没有标签树，这次新建了一棵。"));
    }
    for (const pdf::FormTagIssue& issue : result.issues) {
        lines.append(QStringLiteral("- ") + issue.fieldName + QStringLiteral("：") + issue.reason);
    }
    QMessageBox::information(parentWidget_, title, lines.join(QLatin1Char('\n')));
}

void WiringActions::autoTagDocument() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "自动添加文档标签");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    // 写一棵语义标签树：Document → Part（每页） → Sect → 标题 / 段落 / 列表 / 表格 / 图 / 表单域。
    pdf::SemanticWriteOptions options;
    const pdf::SemanticWriteResult result = pdf::writeSemanticTagTree(bytes, options, &error);
    if (!result.ok) {
        QMessageBox::warning(parentWidget_, title,
                             result.error.isEmpty()
                                 ? i18n::tr("aipdf::app::MainWindow", "写标签树失败。")
                                 : result.error);
        return;
    }
    QString replaceError;
    if (!view->replaceDocumentBytes(result.bytes, &replaceError)) {
        QMessageBox::warning(parentWidget_, title, replaceError);
        return;
    }
    // 报告：写进去多少个结构元素、按角色怎么分布。
    // 为什么说清楚：用户需要知道机器把什么归成了标题、什么归成了段落。
    QMap<QString, int> roleCounts;
    for (const pdf::SemanticNode& node : result.tree.nodes) {
        roleCounts[pdf::roleToName(node.role)] += 1;
    }
    QStringList parts;
    for (auto it = roleCounts.constBegin(); it != roleCounts.constEnd(); ++it) {
        parts.append(QStringLiteral("%1×%2").arg(it.key()).arg(it.value()));
    }
    QStringList lines;
    lines.append(i18n::tr("aipdf::app::MainWindow", "已写入语义标签树：%1 个结构元素。")
                     .arg(result.tree.nodes.size()));
    if (!parts.isEmpty()) {
        lines.append(i18n::tr("aipdf::app::MainWindow", "结构元素类型：%1")
                         .arg(parts.join(QStringLiteral("、"))));
    }
    // 需要人工确认的地方逐条列出来：机器归类拿不准的、缺替代文本的。
    for (const QString& issue : result.issues) {
        lines.append(QStringLiteral("- ") + issue);
    }
    QMessageBox::information(parentWidget_, title, lines.join(QLatin1Char('\n')));
}

void WiringActions::generateUaPdf() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "生成 PDF/UA 文件");
    QString error;
    const QByteArray bytes = view->documentBytes(&error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "读不到文档内容。") : error);
        return;
    }
    // 选项：语言默认 zh-CN（中文文档最常见），标题留空 = 沿用原文件里的标题。
    pdf::UaGeneratorOptions options;
    const pdf::UaGeneratorResult result = pdf::generateUaPdf(bytes, options, &error);
    if (result.bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, title,
                             result.error.isEmpty() ? error : result.error);
        return;
    }
    // 写回打开的文档：生成的标签树要落在用户正在看的这份文件上。
    QString replaceError;
    if (!view->replaceDocumentBytes(result.bytes, &replaceError)) {
        QMessageBox::warning(parentWidget_, title, replaceError);
        return;
    }
    // 报告：结论用生成器自己的摘要（不是「已通过 PDF/UA 校验」），
    // 边界说明原样用生成器给的那句 —— 检查器判定不等于合规认证。
    PdfUaReportDialog dialog(parentWidget_);
    dialog.setReport(result.report);
    dialog.setConclusionText(result.summaryText);
    QString scope = pdf::uaGenerationDisclaimer();
    if (!result.issues.isEmpty()) {
        scope += QStringLiteral("\n");
        scope += i18n::tr("aipdf::app::MainWindow", "需人工确认：%1")
                     .arg(result.issues.join(QStringLiteral("；")));
    }
    dialog.setScopeNotice(scope);
    dialog.exec();
    notify(result.summaryText);
}


void WiringActions::sendToOneNote() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString title = i18n::tr("aipdf::app::MainWindow", "发送到 OneNote");
    const QString documentTitle = view->filePath().isEmpty()
                                     ? i18n::tr("aipdf::app::MainWindow", "当前文档")
                                     : QFileInfo(view->filePath()).completeBaseName();
    // 送什么：整篇当前文档，每页一张图（页码留空 = 全部页，由计划去归一化）。
    OneNoteExportOptions options;
    options.documentTitle = documentTitle;
    const OneNoteExportPlan plan = buildOneNoteExportPlan(documentTitle, view->pageCount(),
                                                          options);
    if (plan.pageNumbers.isEmpty()) {
        QMessageBox::information(parentWidget_, title,
                                 i18n::tr("aipdf::app::MainWindow", "这份文档没有可送出的页面。"));
        return;
    }
    // 页 → 图：交给渲染层，OneNote 那边只管「送」和「说清送了什么」。
    const OneNotePageRenderer renderer = [view](int pageIndex, int dpi) {
        const pdf::MediaBox box = view->pageContentBox(pageIndex);
        const double pointsWide = box.width() > 0.0 ? box.width() : 612.0;
        const int targetWidth = qMax(64, qRound(pointsWide * dpi / 72.0));
        return view->renderThumbnail(pageIndex, targetWidth);
    };
    // 页 → 文字：把这一页的文本层也送过去，OneNote 里才搜得到这页的文字。
    //
    // 为什么按 page.pageIndex 找而不是直接下标取：documentTextPages() 会跳过
    // 没有文字的页，返回的数组下标跟页号对不上。
    const QVector<pdf::TextPage> textPages = view->documentTextPages();
    const OneNotePageTextProvider textProvider = [textPages](int pageIndex) {
        QVector<OneNoteTextBlock> blocks;
        for (const pdf::TextPage& page : textPages) {
            if (page.pageIndex != pageIndex) {
                continue;
            }
            for (const UserSpaceLine& line : userSpaceTextLines(page)) {
                blocks.append(OneNoteTextBlock{line.text});
            }
            break;
        }
        return blocks;
    };
    OneNoteExporter exporter;
    const OneNoteExportResult result = exporter.exportToOneNote(plan, renderer, textProvider);
    if (!result.ok) {
        // 失败原因已经按种类分好了（没装 / COM 起不来 / 写页失败），不笼统说「发送失败」。
        QMessageBox::warning(parentWidget_, title, result.message);
        return;
    }
    QMessageBox::information(parentWidget_, title, result.message);
}

}  // namespace aipdf::app
