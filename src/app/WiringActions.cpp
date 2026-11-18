#include "app/WiringActions.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QStandardPaths>
#include <QUuid>

#include "ai/AiContentDetection.h"
#include "ai/AiImageGeneration.h"
#include "ai/PptGeneration.h"
#include "app/AiPanel.h"
#include "app/AiToolService.h"
#include "app/AiToolsDialog.h"
#include "app/AnnotationToolboxDialog.h"
#include "app/ConversionInputDialog.h"
#include "app/ConversionReports.h"
#include "app/DocumentCompareDialog.h"
#include "app/OneNoteExporter.h"
#include "app/EncryptionDialog.h"
#include "app/ScannerCapture.h"
#include "app/ConversionExports.h"
#include "app/ConversionToolsDialog.h"
#include "app/DocumentOutput.h"
#include "app/LastWaveBridge.h"
#include "app/LastWavePanels.h"
#include "app/OcrController.h"
#include "app/PageTextLines.h"
#include "app/PdfAPreflightPanel.h"
#include "app/PdfView.h"
#include "app/PrintToolsDialog.h"
#include "doc/ActionWizard.h"
#include "doc/BatchProcessor.h"
#include "doc/FontFlattening.h"
#include "doc/OfdExport.h"
#include "doc/OfficeExport.h"
#include "doc/UniversalConverter.h"
#include "i18n/Translate.h"
#include "ipc/WorkerClient.h"
#include "pdf/DocumentReflow.h"
#include "pdf/ExportFormats.h"
#include "pdf/InvisibleTextLayer.h"
#include "pdf/OcrSuspicion.h"
#include "pdf/OptionalContent.h"
#include "pdf/PdfiumAdapter.h"
#include "pdf/FormAutoTag.h"
#include "pdf/PdfSecurity.h"
#include "pdf/PdfAChecker.h"
#include "pdf/PdfUaChecker.h"
#include "pdf/PortfolioWriter.h"
#include "pdf/PreflightChecker.h"
#include "pdf/RichMedia3d.h"
#include "pdf/XfaSupport.h"

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
    QVector<pdf::HtmlPage> htmlPages;
    for (const pdf::TextPage& page : view->documentTextPages()) {
        pdf::HtmlPage htmlPage;
        htmlPage.text = page;
        htmlPage.sizePt = QSizeF(page.contentBox.width(), page.contentBox.height());
        htmlPages.append(htmlPage);
    }
    QString error;
    const QByteArray bytes = ConversionTools::exportHtml(htmlPages, &error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 HTML"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "导出失败。") : error);
        return;
    }
    if (!writeFile(path, bytes, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 HTML"), error);
        return;
    }
    QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 HTML"), htmlExportNoticeText());
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
    pdf::DxfExportOptions options;
    const QByteArray result = ConversionTools::exportDxf(bytes, options, &error);
    if (result.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 CAD 文件"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "导出失败。") : error);
        return;
    }
    if (!writeFile(path, result, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 CAD 文件"), error);
        return;
    }
    QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出 CAD 文件"), dxfExportNoticeText());
}

void WiringActions::exportOffice() {
    PdfView* view = requireView();
    if (view == nullptr) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"), QStringLiteral("document.docx"),
        i18n::tr("aipdf::app::MainWindow", "Word 文档 (*.docx)"));
    if (path.isEmpty()) {
        return;
    }
    const QVector<pdf::TextPage> pages = view->documentTextPages();
    if (pages.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"),
                             i18n::tr("aipdf::app::MainWindow", "这一页没有可导出的文字层。"));
        return;
    }
    const pdf::TextPage& page = pages.first();
    const QSizeF size(page.contentBox.width(), page.contentBox.height());
    QString error;
    const QByteArray bytes =
        ConversionTools::exportOffice(doc::ExportFormat::WordOpenXml, page, size, &error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "导出失败。") : error);
        return;
    }
    if (!writeFile(path, bytes, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"), error);
        return;
    }
    QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 MS Office"),
                             officeExportNoticeText(i18n::tr("aipdf::app::MainWindow", "Word 文档")));
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
    const QSizeF size(pages.first().contentBox.width(), pages.first().contentBox.height());
    doc::OfdExportOptions options;
    options.title = view->filePath();
    QString error;
    const QByteArray bytes = ConversionTools::exportOfd(pages, size, options, &error);
    if (bytes.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 OFD"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "导出失败。") : error);
        return;
    }
    if (!writeFile(path, bytes, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 OFD"), error);
        return;
    }
    QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "导出到 OFD"), ofdExportNoticeText());
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
    const QByteArray result = ConversionTools::flattenFonts(bytes, options, &error);
    if (result.isEmpty()) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "PDF 转曲"),
                             error.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "转曲失败。") : error);
        return;
    }
    if (!writeFile(path, result, &error)) {
        QMessageBox::warning(parentWidget_, i18n::tr("aipdf::app::MainWindow", "PDF 转曲"), error);
        return;
    }
    // 转曲是视觉等价的栅格化版本：文案必须照实说。
    QMessageBox::information(parentWidget_, i18n::tr("aipdf::app::MainWindow", "PDF 转曲"), flattenFontsNoticeText());
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
            });
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const pdf::ReflowReport& report = dialog.report();
    if (!report.ok() || report.outputPageCount <= 0) {
        return;
    }
    notify(i18n::tr("aipdf::app::MainWindow", "重排完成：%1 页。").arg(report.outputPageCount));
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
