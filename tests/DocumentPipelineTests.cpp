// 端到端流水线集成测试。
//
// 这个文件守的是「模块串起来」这一层。仓库里每个领域层模块都有自己的专项单测，
// 单独跑都是绿的；但「各自都对」不等于「串起来也对」——
// 前一个模块写进去的东西，可能被后一个模块踩坏（内容流被重写、页树被重建、
// 标签树被丢弃……），而这种问题在单模块测试里是看不见的。
//
// 所以这里的做法是：拿一份夹具 PDF，从头走到尾，每走一步都问同一个问题：
// **这份产物还是不是一份能打开、页数没变、内容没丢的 PDF？**
//
// 本文件**不实现**任何领域逻辑：所有动作都走仓库已有的公开 API
// （aipdf::pdf / aipdf::doc / aipdf::ai 名下的函数），
// 测试自己只负责造输入与验结果。

#include "ai/rag/OfflineQa.h"

#include "doc/DocumentChunk.h"
#include "doc/LocalIndex.h"
#include "doc/UniversalConverter.h"

#include "pdf/ops/DocumentMerger.h"
#include "pdf/ops/DocumentReflow.h"
#include "pdf/convert/ExportFormats.h"
#include "pdf/content/InvisibleTextLayer.h"
#include "pdf/document/NamedDestination.h"
#include "pdf/ops/OptionalContent.h"
#include "pdf/ops/PageLabels.h"
#include "pdf/compliance/PdfUaChecker.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/compliance/PreflightChecker.h"
#include "pdf/convert/PortfolioWriter.h"
#include "pdf/annot/RichMedia3d.h"
#include "pdf/compliance/StructTreeReader.h"
#include "pdf/compliance/TagTreeWriter.h"
#include "pdf/kernel/TextPage.h"

#include <QFile>
#include <QHash>
#include <QProcess>
#include <QRectF>
#include <QSizeF>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

#include <algorithm>
#include <optional>

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// --------------------------------------------------------------------------
// 小工具
// --------------------------------------------------------------------------

// 读夹具 PDF。读不到时返回空字节（用例里会 QSKIP）。
QByteArray fixtureBytes(const QString& name) {
    QFile file(QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

// 用 PDFium 打开一段字节，返回页数；打不开返回 -1。
//
// 为什么统一走 pdf::openDocumentFromBytes：这是仓库自己的 PDFium 适配层，
// 它内部负责 FPDF_InitLibrary，用例里就不必再各自初始化一遍。
int pdfiumPageCount(const QByteArray& bytes) {
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) return -1;
    return opened.document.pageCount();
}

// PDFium 读出来的纯文本（全部页拼起来）。
QString pdfiumPlainText(const QByteArray& bytes) {
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) return {};
    QString text;
    const int pages = opened.document.pageCount();
    for (int index = 0; index < pages; ++index) {
        const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(index);
        if (!page.has_value()) continue;
        const std::optional<aipdf::pdf::TextPage> textPage = page->extractText();
        if (!textPage.has_value()) continue;
        text += textPage->plainText();
    }
    return text;
}

// 每页的文本层（PDFium 抽出来的）。没有文字的页也给一个空壳，
// 这样列表长度永远等于页数——writeInvisibleTextLayer 要求两者一致。
QVector<aipdf::pdf::TextPage> pdfiumTextPages(const QByteArray& bytes) {
    QVector<aipdf::pdf::TextPage> pages;
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) return pages;
    const int count = opened.document.pageCount();
    for (int index = 0; index < count; ++index) {
        aipdf::pdf::TextPage empty;
        empty.pageIndex = index;
        const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(index);
        if (!page.has_value()) {
            pages.append(empty);
            continue;
        }
        const std::optional<aipdf::pdf::TextPage> textPage = page->extractText();
        if (!textPage.has_value()) {
            pages.append(empty);
            continue;
        }
        pages.append(*textPage);
    }
    return pages;
}

// qpdf --check 的结果。
struct QpdfCheckResult {
    bool ran = false;
    int exitCode = -1;
    QString output;
};

// 跑一次 qpdf --check（结构校验）。
//
// 为什么用命令行而不是链接 qpdf 库：--check 会重写整个文件并把结构问题
// 逐条报出来，库接口没有直接给出这一套报告。
QpdfCheckResult qpdfCheck(const QByteArray& bytes) {
    QpdfCheckResult result;
    const QString cli = QString::fromUtf8(AIPDF_QPDF_CLI);
    if (cli.isEmpty()) return result;

    QTemporaryDir directory;
    if (!directory.isValid()) return result;
    const QString path = directory.filePath(QStringLiteral("pipeline-check.pdf"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return result;
    file.write(bytes);
    file.close();

    QProcess process;
    process.start(cli, {QStringLiteral("--check"), path});
    if (!process.waitForFinished(60000)) return result;
    result.ran = true;
    result.exitCode = process.exitCode();
    result.output = QString::fromUtf8(process.readAllStandardOutput()) +
                    QString::fromUtf8(process.readAllStandardError());
    return result;
}

// 一份产物的健康状况。
struct HealthReport {
    bool opened = false;    // PDFium 能打开
    int pageCount = -1;     // PDFium 读到的页数（打不开时是 -1）
    bool qpdfRan = false;   // 跑过 qpdf --check
    int qpdfExitCode = -1;  // qpdf --check 的退出码
    QString qpdfOutput;     // qpdf 的输出
    QString problem;        // 有问题时写明是什么问题
};

// 问一次「这份产物健康吗」。
HealthReport checkHealth(const QByteArray& bytes) {
    HealthReport report;
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) {
        report.problem = QStringLiteral("PDFium 打不开：%1").arg(opened.errorMessage);
        return report;
    }
    report.opened = true;
    report.pageCount = opened.document.pageCount();

    const QpdfCheckResult checked = qpdfCheck(bytes);
    report.qpdfRan = checked.ran;
    report.qpdfExitCode = checked.exitCode;
    report.qpdfOutput = checked.output;
    if (checked.ran && checked.exitCode != 0) {
        report.problem = QStringLiteral("qpdf --check 退出码 %1：%2")
                             .arg(checked.exitCode)
                             .arg(checked.output.trimmed());
    }
    return report;
}

// --------------------------------------------------------------------------
// 流水线
// --------------------------------------------------------------------------

// 流水线上的一个步骤与它的产物。
struct PipelineStage {
    QString name;
    QByteArray bytes;
};

// 跑完整条流水线的结果。
struct PipelineRun {
    QVector<PipelineStage> stages;
    QString failure;   // 某一步失败时写明是哪一步、为什么
    bool ok = false;
};

// 给一份 PDF 打上标签树（用仓库自己的标签写入器）。
//
// 为什么要打：流水线里要验「读得出结构树」，而夹具本身没有标签树。
// 区域按 PDFium 抽出来的文字行来分，这样标签树的粒度与真实排版一致。
//
// 注意：这一步只用领域层的公开 API（writeTaggedStructure），
// 没有自己另写一套「打标签」的实现。
QByteArray tagDocument(const QByteArray& bytes, QString* error) {
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) {
        if (error) *error = opened.errorMessage;
        return {};
    }
    const int pageTotal = opened.document.pageCount();
    QVector<QVector<aipdf::pdf::TaggedRegion>> pageRegions;
    for (int index = 0; index < pageTotal; ++index) {
        QVector<aipdf::pdf::TaggedRegion> regions;
        const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(index);
        if (page.has_value()) {
            const std::optional<aipdf::pdf::TextPage> textPage = page->extractText();
            if (textPage.has_value()) {
                // 按字符框的下边把字分行：同一行的字合成一个区域。
                QVector<QPair<double, QRectF>> lines;
                for (const aipdf::pdf::TextChar& ch : textPage->chars) {
                    if (ch.text.trimmed().isEmpty()) continue;
                    const double baseline = ch.box.y();
                    bool merged = false;
                    for (auto& line : lines) {
                        if (qAbs(line.first - baseline) <= 2.0) {
                            line.second = line.second.united(ch.box);
                            merged = true;
                            break;
                        }
                    }
                    if (!merged) lines.append(qMakePair(baseline, ch.box));
                }
                // 从上往下排（PDF 的 y 向上，所以 y 大的在上）。
                std::sort(lines.begin(), lines.end(), [](const auto& left, const auto& right) {
                    return left.first > right.first;
                });
                int order = 0;
                for (const auto& line : lines) {
                    aipdf::pdf::TaggedRegion region;
                    region.rect = line.second;
                    region.order = order++;
                    regions.append(region);
                }
            }
        }
        pageRegions.append(regions);
    }
    return aipdf::pdf::writeTaggedStructure(bytes, pageRegions, error);
}

// 跑一遍「读夹具 → 印前检查 → 写图层 → 写不可见文字层 → 写命名目标 → 写页面标签」。
//
// 每一步的产物都记进 stages，交给调用方做健康检查。
PipelineRun runPipeline(const QByteArray& source) {
    PipelineRun run;
    QByteArray current = source;

    // ---- 步骤 3：写一层 OCG 图层 ----
    {
        aipdf::pdf::OptionalContentPlan plan;
        aipdf::pdf::OptionalContentLayer layer;
        layer.name = QStringLiteral("PipelineLayer");
        layer.defaultVisible = true;
        plan.layers.append(layer);
        aipdf::pdf::OptionalContentMark mark;
        mark.pageIndex = 0;
        mark.layerName = layer.name;
        plan.marks.append(mark);
        plan.openLayerPanel = true;

        QString error;
        const QByteArray next = aipdf::pdf::writeOptionalContent(current, plan, &error);
        if (next.isEmpty()) {
            run.failure = QStringLiteral("writeOptionalContent 失败：%1").arg(error);
            return run;
        }
        current = next;
        run.stages.append({QStringLiteral("写图层"), current});
    }

    // ---- 步骤 5：写不可见文字层 ----
    {
        const QVector<aipdf::pdf::TextPage> pages = pdfiumTextPages(current);
        const int expected = pdfiumPageCount(current);
        if (pages.size() != expected) {
            run.failure = QStringLiteral("写不可见文字层失败：取到 %1 页的文本层，文档有 %2 页")
                              .arg(pages.size())
                              .arg(expected);
            return run;
        }
        const aipdf::pdf::InvisibleTextLayerResult result =
            aipdf::pdf::writeInvisibleTextLayer(current, pages);
        if (!result.ok()) {
            run.failure = QStringLiteral("writeInvisibleTextLayer 失败：%1").arg(result.error);
            return run;
        }
        current = result.bytes;
        run.stages.append({QStringLiteral("写不可见文字层"), current});
    }

    // ---- 步骤 8：写命名目标 ----
    {
        QVector<aipdf::pdf::NamedDestination> destinations;
        aipdf::pdf::NamedDestination cover;
        cover.name = QStringLiteral("封面");
        cover.pageIndex = 0;
        cover.position = QPointF(0.0, 0.0);
        destinations.append(cover);
        aipdf::pdf::NamedDestination last;
        last.name = QStringLiteral("末页");
        last.pageIndex = qMax(0, pdfiumPageCount(current) - 1);
        destinations.append(last);

        QString error;
        const QByteArray next = aipdf::pdf::writeNamedDestinations(current, destinations, &error);
        if (next.isEmpty()) {
            run.failure = QStringLiteral("writeNamedDestinations 失败：%1").arg(error);
            return run;
        }
        current = next;
        run.stages.append({QStringLiteral("写命名目标"), current});
    }

    // ---- 步骤 9：写页面标签 ----
    {
        aipdf::pdf::PageLabelScheme scheme;
        aipdf::pdf::PageLabelRange range;
        range.startIndex = 0;
        range.style = QStringLiteral("/D");
        range.prefix = QStringLiteral("P-");
        scheme.ranges.append(range);
        scheme.firstPageNumber = 1;

        QString error;
        const QByteArray next = aipdf::pdf::writePageLabels(current, scheme, &error);
        if (next.isEmpty()) {
            run.failure = QStringLiteral("writePageLabels 失败：%1").arg(error);
            return run;
        }
        current = next;
        run.stages.append({QStringLiteral("写页面标签"), current});
    }

    run.ok = true;
    return run;
}

// --------------------------------------------------------------------------
// 自造的带标签 PDF（给重排用）
// --------------------------------------------------------------------------

// 造一份 PDF。
//
// xref 表必须覆盖到最大的对象号：表短了的话，后面几个对象就成了「找不到的引用」。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects) {
    int maxId = 0;
    for (const auto& object : objects) maxId = qMax(maxId, object.first);
    QByteArray out("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = maxId + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        if (!offsets.contains(id)) {
            out += "0000000000 65535 f \n";
            continue;
        }
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id, 0), 10, 10, QLatin1Char('0'))
                   .toLatin1();
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

QByteArray streamObject(const QByteArray& content) {
    return QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
        .arg(content.size())
        .arg(QString::fromLatin1(content))
        .toLatin1();
}

// 带标签的两页 PDF。
//
// 结构刻意做成常见的样子：Document 下面挂 H1、P（里面再套 Span）、Figure、
// Table（里面挂 TD）、Head。
//   * H1、P、TD 是正文 → 应该被重排；
//   * Figure 是图片 → 搬不走，要进「丢弃清单」；
//   * Table 的表格结构搬不走，但单元格文字按独立段落重排；
//   * Head 是页眉 → 按约定不搬。
//
// 为什么自己造而不用夹具：重排要靠标签树里的**语义标签**判断正文与版式，
// 夹具里没有这种形状。这段造文件的代码只属于测试，不涉及任何领域逻辑。
QByteArray buildTaggedSamplePdf() {
    const QByteArray pageOne =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 250 Td (Chapter one) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 1 >> BDC\n"
        "BT /F1 12 Tf 40 220 Td (Body paragraph text) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 2 >> BDC\n"
        "% figure placeholder\n"
        "EMC";
    const QByteArray pageTwo =
        "/Tag << /MCID 3 >> BDC\n"
        "BT /F1 12 Tf 40 250 Td (Cell text) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 4 >> BDC\n"
        "BT /F1 12 Tf 40 220 Td (Running head) Tj ET\n"
        "EMC";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /Lang (zh-CN) "
                                            "/StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(pageOne)));
    objects.append(qMakePair(5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 6 0 R /StructParents 1 >>")));
    objects.append(qMakePair(6, streamObject(pageTwo)));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [9 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R "
                                            "/K [10 0 R 11 0 R 12 0 R 13 0 R 16 0 R] >>")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /H1 /P 9 0 R "
                                             "/Pg 3 0 R /K 0 >>")));
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /P /P 9 0 R "
                                             "/Pg 3 0 R /K [14 0 R] >>")));
    objects.append(qMakePair(12, QByteArray("<< /Type /StructElem /S /Figure /P 9 0 R "
                                             "/Pg 3 0 R /Alt (Figure alt) /K 2 >>")));
    objects.append(qMakePair(13, QByteArray("<< /Type /StructElem /S /Table /P 9 0 R "
                                             "/Pg 5 0 R /K [15 0 R] >>")));
    objects.append(qMakePair(14, QByteArray("<< /Type /StructElem /S /Span /P 11 0 R /K 1 >>")));
    objects.append(qMakePair(15, QByteArray("<< /Type /StructElem /S /TD /P 13 0 R "
                                             "/Pg 5 0 R /K 3 >>")));
    objects.append(qMakePair(16, QByteArray("<< /Type /StructElem /S /Head /P 9 0 R "
                                             "/Pg 5 0 R /K 4 >>")));
    return buildPdf(objects);
}

// 一份常规的重排参数。
aipdf::pdf::ReflowOptions reflowOptions() {
    aipdf::pdf::ReflowOptions options;
    options.pageWidth = 300.0;
    options.pageHeight = 400.0;
    options.marginLeft = 20.0;
    options.marginRight = 20.0;
    options.marginTop = 20.0;
    options.marginBottom = 20.0;
    options.paragraph.fontSize = 11.0;
    options.paragraph.lineSpacing = 1.2;
    return options;
}

// --------------------------------------------------------------------------
// 离线问答用的语料
// --------------------------------------------------------------------------

// 一段样例语料：三个片段、两页。
//
// 主题刻意分散（自动保存 / 导出 / 打印），这样「无关问题」才真的无关。
QVector<aipdf::doc::DocumentChunk> sampleChunks() {
    QVector<aipdf::doc::DocumentChunk> chunks;

    aipdf::doc::DocumentChunk first;
    first.id = QStringLiteral("p0b0");
    first.pageIndex = 0;
    first.order = 0;
    first.text = QStringLiteral("本软件在编辑文档时会自动保存草稿。自动保存的间隔默认为 5 分钟。"
                                "用户可以在设置里调整这个间隔。");
    chunks.append(first);

    aipdf::doc::DocumentChunk second;
    second.id = QStringLiteral("p0b1");
    second.pageIndex = 0;
    second.order = 1;
    second.text = QStringLiteral("导出功能支持 PDF、Word 与纯文本三种格式。导出前会提示用户确认目标路径。");
    chunks.append(second);

    aipdf::doc::DocumentChunk third;
    third.id = QStringLiteral("p1b0");
    third.pageIndex = 1;
    third.order = 2;
    third.text = QStringLiteral("打印模块支持双面打印与纸张缩放。打印队列里可以暂停或取消任务。");
    chunks.append(third);

    return chunks;
}

// 造一份内存里的本地索引（不落盘）。
aipdf::doc::LocalIndex buildSampleIndex() {
    aipdf::doc::LocalIndex index;
    index.setDocumentPath(QStringLiteral("memory://pipeline"));
    QVector<aipdf::doc::DocumentChunk> pageZero;
    QVector<aipdf::doc::DocumentChunk> pageOne;
    for (const aipdf::doc::DocumentChunk& chunk : sampleChunks()) {
        if (chunk.pageIndex == 0) {
            pageZero.append(chunk);
        } else {
            pageOne.append(chunk);
        }
    }
    index.replacePage(0, pageZero, QStringLiteral("fp0"));
    index.replacePage(1, pageOne, QStringLiteral("fp1"));
    return index;
}

}  // namespace

class DocumentPipelineTests : public QObject {
    Q_OBJECT

private slots:
    // ---------------------------------------------------------- 流水线主流程 ----
    void preflightReportIsInternallyConsistent();
    void optionalContentRoundTripKeepsDocumentHealthy();
    void invisibleTextLayerKeepsPageCountAndStaysOpenable();
    void structTreeIsReadableAfterThePipeline();
    void pdfUaReportIsInternallyConsistent();
    void namedDestinationsSurviveThePipeline();
    void pageLabelsSurviveThePipeline();
    void mergeAddsPagesAndCarriesCrossFileOutlines();

    // ------------------------------------------------------------ 分支流程 ----
    void reflowOfTaggedDocumentProducesOpenableOutput();
    void reflowRefusesUntaggedDocumentWithAReason();
    void threeDAnnotationRoundTrips();
    void mediaAnnotationRoundTrips();
    void convertToPdfWithChineseTextKeepsTheLossAccountedFor();
    void offlineQaAnswersFromLocalIndexWithLocatableCitation();
    void offlineQaRefusesToInventAnAnswerForUnrelatedQuestion();

    // ---------------------------------------------------------- 健康检查专项 ----
    void everyPipelineStageProducesAHealthyPdf();
    void pageCountIsPreservedThroughTheWholePipeline();
    void exportsProduceUsableOutputs();
    void portfolioRoundTripsEntriesAndMetadata();
};

using aipdf::pdf::PdfOpenResult;

// ---------------------------------------------------------------------------
// 流水线主流程
// ---------------------------------------------------------------------------

// 印前检查的报告必须自洽：ok 等价于「没有 Error」，计数与 findings 对得上。
//
// 为什么守这条：报告是直接给人看的界面数据（「查了几项、过了几项」），
// 计数与结论对不上时，用户看到的结论会自相矛盾。
void DocumentPipelineTests::preflightReportIsInternallyConsistent() {
    REQUIRE_QPDF();
    const QByteArray bytes = fixtureBytes(QStringLiteral("text.pdf"));
    if (bytes.isEmpty()) QSKIP("夹具不可用");

    const aipdf::pdf::PreflightReport report = aipdf::pdf::runPreflightCheck(bytes);
    QVERIFY(!report.findings.isEmpty() || report.ok);

    // 结论与计数必须对得上。
    int errors = 0;
    int warnings = 0;
    for (const aipdf::pdf::PreflightFinding& finding : report.findings) {
        // 每一条结论都要有机器可读码、人可读标题与具体说明：
        // 少任何一项，界面上就没法把这条结论说清楚。
        QVERIFY2(!finding.code.isEmpty(), "结论必须有 code");
        QVERIFY2(!finding.title.isEmpty(), "结论必须有 title");
        QVERIFY2(!finding.detail.isEmpty(), "结论必须有 detail");
        if (finding.severity == aipdf::pdf::PreflightSeverity::Error) ++errors;
        if (finding.severity == aipdf::pdf::PreflightSeverity::Warning) ++warnings;
    }
    QCOMPARE(report.errorCount, errors);
    QCOMPARE(report.warningCount, warnings);
    // 「可以送印」的定义就是没有 Error。
    QCOMPARE(report.ok, report.errorCount == 0);
    // 摘要里也要带上数量，不能是一句空话。
    QVERIFY(!report.summary().isEmpty());
}

// 写一层 OCG 图层再读回来：图层与可见性要一致，文档还得是健康的。
void DocumentPipelineTests::optionalContentRoundTripKeepsDocumentHealthy() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");
    const int sourcePages = pdfiumPageCount(source);
    QCOMPARE(sourcePages, 2);

    aipdf::pdf::OptionalContentPlan plan;
    aipdf::pdf::OptionalContentLayer visible;
    visible.name = QStringLiteral("Watermark");
    visible.defaultVisible = true;
    plan.layers.append(visible);
    aipdf::pdf::OptionalContentLayer hidden;
    hidden.name = QStringLiteral("Hidden");
    hidden.defaultVisible = false;
    plan.layers.append(hidden);
    aipdf::pdf::OptionalContentMark mark;
    mark.pageIndex = 0;
    mark.layerName = QStringLiteral("Watermark");
    plan.marks.append(mark);

    QString error;
    const QByteArray written = aipdf::pdf::writeOptionalContent(source, plan, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const aipdf::pdf::OptionalContentSummary summary =
        aipdf::pdf::readOptionalContent(written, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(summary.hasOptionalContent);
    QCOMPARE(summary.groups.size(), 2);
    QCOMPARE(summary.groups.at(0).name, QStringLiteral("Watermark"));
    QCOMPARE(summary.groups.at(0).visible, true);
    QCOMPARE(summary.groups.at(1).name, QStringLiteral("Hidden"));
    QCOMPARE(summary.groups.at(1).visible, false);

    // 健康检查：页数没变、PDFium 能打开、qpdf 说得它结构没问题。
    const HealthReport health = checkHealth(written);
    QVERIFY2(health.problem.isEmpty(), qPrintable(health.problem));
    QCOMPARE(health.pageCount, sourcePages);
    // 原来的文字还在。
    QVERIFY(pdfiumPlainText(written).contains(QStringLiteral("Folixa")));
}

// 写不可见文字层：页数不能变，文档还得能打开，文字还得能抽回来。
void DocumentPipelineTests::invisibleTextLayerKeepsPageCountAndStaysOpenable() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");
    const int sourcePages = pdfiumPageCount(source);
    QCOMPARE(sourcePages, 2);

    // 输入是「每页一段识别出来的文字」：这里直接用 PDFium 抽出来的文本层，
    // 相当于把刚扫描完的文档回写回去。
    const QVector<aipdf::pdf::TextPage> pages = pdfiumTextPages(source);
    QCOMPARE(pages.size(), sourcePages);

    const aipdf::pdf::InvisibleTextLayerResult result =
        aipdf::pdf::writeInvisibleTextLayer(source, pages);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(!result.bytes.isEmpty());
    QCOMPARE(result.pageCount, sourcePages);
    QVERIFY(result.charCount > 0);

    const HealthReport health = checkHealth(result.bytes);
    QVERIFY2(health.problem.isEmpty(), qPrintable(health.problem));
    QCOMPARE(health.pageCount, sourcePages);
    // 不可见文字层是**加**上去的，原来的可见文字不能因此丢。
    QVERIFY(pdfiumPlainText(result.bytes).contains(QStringLiteral("Folixa")));
}

// 流水线走完之后，标签树还得读得出；没有标签树时必须给出明确说明。
void DocumentPipelineTests::structTreeIsReadableAfterThePipeline() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");

    // 先给夹具打上标签（用仓库自己的标签写入器）。
    QString tagError;
    const QByteArray tagged = tagDocument(source, &tagError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(tagError));

    const aipdf::pdf::StructTreeReport tree = aipdf::pdf::readStructTree(tagged);
    QVERIFY2(tree.ok(), qPrintable(tree.error));
    QVERIFY2(tree.hasStructTree, "打过标签的文档必须能读出结构树");
    QVERIFY(!tree.elements.isEmpty());

    // 再跑一遍流水线，标签树不能被后面的步骤踩掉。
    const PipelineRun run = runPipeline(tagged);
    QVERIFY2(run.ok, qPrintable(run.failure));
    const QByteArray last = run.stages.last().bytes;
    const aipdf::pdf::StructTreeReport afterTree = aipdf::pdf::readStructTree(last);
    QVERIFY2(afterTree.ok(), qPrintable(afterTree.error));
    QVERIFY2(afterTree.hasStructTree,
             "流水线走完之后，标签树必须还在（不能被写图层/写文字层踩掉）");
    QVERIFY(!afterTree.elements.isEmpty());

    // 反过来：没有标签树的文档必须**明确说明**，不能静默返回空。
    const aipdf::pdf::StructTreeReport plain = aipdf::pdf::readStructTree(source);
    QVERIFY2(plain.ok(), qPrintable(plain.error));
    QVERIFY(!plain.hasStructTree);
    QVERIFY2(!plain.notes.isEmpty(),
             "没有结构树不是错误，但必须写明「为什么没有」，不能静默返回空");
    QVERIFY(plain.notes.join(QLatin1Char('\n')).contains(QStringLiteral("标签树")));
}

// PDF/UA 报告必须自洽：compliant 等价于「没有 Error」，每条结论三要素齐全。
void DocumentPipelineTests::pdfUaReportIsInternallyConsistent() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");

    QString tagError;
    const QByteArray tagged = tagDocument(source, &tagError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(tagError));

    const aipdf::pdf::PdfUaReport report = aipdf::pdf::checkPdfUa(tagged);
    QVERIFY(!report.findings.isEmpty());

    int errors = 0;
    int warnings = 0;
    int passes = 0;
    for (const aipdf::pdf::PdfUaFinding& finding : report.findings) {
        QVERIFY2(!finding.code.isEmpty(), "结论必须有 code");
        QVERIFY2(!finding.title.isEmpty(), "结论必须有 title");
        QVERIFY2(!finding.detail.isEmpty(), "结论必须有 detail");
        if (finding.severity == aipdf::pdf::PdfUaSeverity::Error) ++errors;
        if (finding.severity == aipdf::pdf::PdfUaSeverity::Warning) ++warnings;
        if (finding.severity == aipdf::pdf::PdfUaSeverity::Pass) ++passes;
    }
    QCOMPARE(report.errorCount, errors);
    QCOMPARE(report.warningCount, warnings);
    QCOMPARE(report.passCount, passes);
    QCOMPARE(report.findings.size(), errors + warnings + passes);
    // 「合规」的定义就是没有 Error（Warning 不影响判定）。
    QCOMPARE(report.compliant, report.errorCount == 0);
    QVERIFY(!report.summary().isEmpty());
}

// 命名目标写进去要能读回来，而且读回来的页号要落在文档范围内。
void DocumentPipelineTests::namedDestinationsSurviveThePipeline() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");
    const int sourcePages = pdfiumPageCount(source);

    QVector<aipdf::pdf::NamedDestination> destinations;
    aipdf::pdf::NamedDestination cover;
    cover.name = QStringLiteral("封面");
    cover.pageIndex = 0;
    cover.position = QPointF(12.0, 34.0);
    destinations.append(cover);
    aipdf::pdf::NamedDestination tail;
    tail.name = QStringLiteral("末页");
    tail.pageIndex = sourcePages - 1;
    tail.fitPage = true;
    destinations.append(tail);

    QString error;
    const QByteArray written = aipdf::pdf::writeNamedDestinations(source, destinations, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const QVector<aipdf::pdf::NamedDestination> read =
        aipdf::pdf::readNamedDestinations(written, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(read.size(), 2);

    bool foundCover = false;
    bool foundTail = false;
    for (const aipdf::pdf::NamedDestination& destination : read) {
        if (destination.name == QStringLiteral("封面")) {
            foundCover = true;
            QCOMPARE(destination.pageIndex, 0);
        }
        if (destination.name == QStringLiteral("末页")) {
            foundTail = true;
            QCOMPARE(destination.pageIndex, sourcePages - 1);
        }
    }
    QVERIFY(foundCover);
    QVERIFY(foundTail);

    const HealthReport health = checkHealth(written);
    QVERIFY2(health.problem.isEmpty(), qPrintable(health.problem));
    QCOMPARE(health.pageCount, sourcePages);
}

// 页面标签写进去要能读回来。
void DocumentPipelineTests::pageLabelsSurviveThePipeline() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");
    const int sourcePages = pdfiumPageCount(source);

    aipdf::pdf::PageLabelScheme scheme;
    aipdf::pdf::PageLabelRange range;
    range.startIndex = 0;
    range.style = QStringLiteral("/D");
    range.prefix = QStringLiteral("P-");
    scheme.ranges.append(range);
    scheme.firstPageNumber = 1;

    QString error;
    const QByteArray written = aipdf::pdf::writePageLabels(source, scheme, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const QVector<aipdf::pdf::PageLabelRange> read = aipdf::pdf::readPageLabels(written, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(read.size(), 1);
    QCOMPARE(read.at(0).startIndex, 0);
    QCOMPARE(read.at(0).style, QStringLiteral("/D"));
    QCOMPARE(read.at(0).prefix, QStringLiteral("P-"));
    // 标签算出来的显示文本也要对得上。
    QCOMPARE(aipdf::pdf::pageLabelText(read, 0), QStringLiteral("P-1"));

    const HealthReport health = checkHealth(written);
    QVERIFY2(health.problem.isEmpty(), qPrintable(health.problem));
    QCOMPARE(health.pageCount, sourcePages);
}

// 合并：页数相加，另一个文件的书签要搬过来（目标页跟着偏移）。
void DocumentPipelineTests::mergeAddsPagesAndCarriesCrossFileOutlines() {
    REQUIRE_QPDF();
    const QByteArray first = fixtureBytes(QStringLiteral("text.pdf"));
    const QByteArray second = fixtureBytes(QStringLiteral("navigation.pdf"));
    if (first.isEmpty() || second.isEmpty()) QSKIP("夹具不可用");
    QCOMPARE(pdfiumPageCount(first), 2);
    QCOMPARE(pdfiumPageCount(second), 3);

    QVector<aipdf::pdf::MergeInput> inputs;
    inputs.append({first, QStringLiteral("甲")});
    inputs.append({second, QStringLiteral("乙")});

    aipdf::pdf::MergeOutcome outcome;
    QString error;
    QVERIFY2(aipdf::pdf::mergeDocuments(inputs, aipdf::pdf::MergeOptions(), &outcome, &error),
             qPrintable(error));
    QCOMPARE(outcome.pageCount, 5);
    QCOMPARE(outcome.pageOffsets.size(), 2);
    QCOMPARE(outcome.pageOffsets.at(0), 0);
    QCOMPARE(outcome.pageOffsets.at(1), 2);

    // 页数守恒：合并结果的页数就是两份之和。
    const HealthReport health = checkHealth(outcome.bytes);
    QVERIFY2(health.problem.isEmpty(), qPrintable(health.problem));
    QCOMPARE(health.pageCount, 5);

    // 跨文件书签被搬过来了：navigation.pdf 的 4 条大纲一条不少。
    const QVector<aipdf::pdf::SourceOutlineEntry> entries =
        aipdf::pdf::readOutlineEntries(outcome.bytes, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(entries.size(), 4);
    // 目标页跟着搬：源文件里的第 0 页 → 合并结果里的第 2 页。
    QCOMPARE(entries.at(0).sourcePage, 2);
    QCOMPARE(entries.at(3).sourcePage, 4);
    // 标题带来源名前缀，用户一眼看得出这条来自哪份文件。
    QVERIFY2(entries.at(0).title.contains(QStringLiteral("乙")), qPrintable(entries.at(0).title));
    QVERIFY2(entries.at(0).title.contains(QStringLiteral("第一章")),
             qPrintable(entries.at(0).title));
}

// ---------------------------------------------------------------------------
// 分支流程
// ---------------------------------------------------------------------------

// 带标签的文档重排：产出能打开、页数大于 0、丢弃清单与丢弃计数一致。
void DocumentPipelineTests::reflowOfTaggedDocumentProducesOpenableOutput() {
    REQUIRE_QPDF();
    const aipdf::pdf::DocumentReflowResult result =
        aipdf::pdf::reflowTaggedDocument(buildTaggedSamplePdf(), reflowOptions());
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    QVERIFY(!result.bytes.isEmpty());
    QVERIFY(result.report.outputPageCount > 0);

    // 丢弃清单与丢弃计数必须一致。
    QCOMPARE(result.report.droppedCount, result.report.dropped.size());
    QVERIFY2(!result.report.dropped.isEmpty(),
             "这份文档里有图片、表格与页眉，丢弃清单不该是空的");
    QStringList droppedTags;
    for (const aipdf::pdf::ReflowDropped& item : result.report.dropped) {
        QVERIFY2(!item.reason.isEmpty(), "每一条丢弃都要写明原因");
        droppedTags.append(item.tag);
    }
    QVERIFY2(droppedTags.contains(QStringLiteral("Figure")), qPrintable(droppedTags.join(QLatin1Char(','))));
    QVERIFY2(droppedTags.contains(QStringLiteral("Head")), qPrintable(droppedTags.join(QLatin1Char(','))));

    // 健康检查：产出能打开，页数与报告说的一致。
    const HealthReport health = checkHealth(result.bytes);
    QVERIFY2(health.problem.isEmpty(), qPrintable(health.problem));
    QCOMPARE(health.pageCount, result.report.outputPageCount);
    // 正文搬过去了。
    const QString text = pdfiumPlainText(result.bytes);
    QVERIFY2(text.contains(QStringLiteral("Chapter one")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("Body paragraph text")), qPrintable(text));
}

// 没有标签的文档必须**明确拒绝**并写明原因，而不是硬凑一个版面。
void DocumentPipelineTests::reflowRefusesUntaggedDocumentWithAReason() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");

    const aipdf::pdf::DocumentReflowResult result =
        aipdf::pdf::reflowTaggedDocument(source, reflowOptions());
    QVERIFY(!result.ok());
    QVERIFY2(!result.report.error.isEmpty(), "拒绝时必须写明原因");
    QVERIFY2(result.report.error.contains(QStringLiteral("标签")),
             qPrintable(result.report.error));
    QVERIFY2(result.report.error.contains(QStringLiteral("拒绝")),
             qPrintable(result.report.error));
    // 不产出半成品。
    QVERIFY(result.bytes.isEmpty());
}

// 3D 注解写进去要能读回来，参数一项不差。
void DocumentPipelineTests::threeDAnnotationRoundTrips() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");
    const int sourcePages = pdfiumPageCount(source);

    aipdf::pdf::ThreeDAnnotationSpec spec;
    spec.pageIndex = 0;
    spec.rect = QRectF(40.0, 60.0, 160.0, 120.0);
    // 注意写法："\x00\x01\x02fake" 会被当成 "\x02f" 一个十六进制转义，
    // 所以这里把两段拼起来写。
    spec.modelBytes = QByteArray("\x00\x01\x02" "fake-u3d-bytes", 20);
    spec.view.externalName = QStringLiteral("Main");
    spec.view.internalName = QStringLiteral("main");
    spec.view.projectionType = QStringLiteral("/Orthographic");
    spec.view.renderStyle = QStringLiteral("/Shaded");
    spec.view.lightingScheme = QStringLiteral("/Night");
    spec.interactive = false;
    spec.activation = QStringLiteral("/XA");

    QString error;
    const QByteArray written = aipdf::pdf::writeThreeDAnnotation(source, spec, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const aipdf::pdf::RichMediaReadResult read = aipdf::pdf::readRichMedia(written);
    QVERIFY2(read.ok(), qPrintable(read.error));
    QCOMPARE(read.threeD.size(), 1);
    const aipdf::pdf::ThreeDAnnotationInfo& info = read.threeD.first();
    QCOMPARE(info.pageIndex, 0);
    QCOMPARE(info.rect.left(), spec.rect.left());
    QCOMPARE(info.rect.width(), spec.rect.width());
    QCOMPARE(info.view.externalName, spec.view.externalName);
    QCOMPARE(info.view.internalName, spec.view.internalName);
    QCOMPARE(info.view.projectionType, spec.view.projectionType);
    QCOMPARE(info.view.renderStyle, spec.view.renderStyle);
    QCOMPARE(info.view.lightingScheme, spec.view.lightingScheme);
    QCOMPARE(info.interactive, spec.interactive);
    QCOMPARE(info.activation, spec.activation);
    QVERIFY(info.hasModel);
    QVERIFY(info.modelEmbedded);
    QCOMPARE(info.modelSize, static_cast<qint64>(spec.modelBytes.size()));

    const HealthReport health = checkHealth(written);
    QVERIFY2(health.problem.isEmpty(), qPrintable(health.problem));
    QCOMPARE(health.pageCount, sourcePages);
}

// 多媒体注解写进去要能读回来，参数一项不差。
void DocumentPipelineTests::mediaAnnotationRoundTrips() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");
    const int sourcePages = pdfiumPageCount(source);

    aipdf::pdf::MediaAnnotationSpec spec;
    spec.pageIndex = 1;
    spec.rect = QRectF(30.0, 50.0, 200.0, 100.0);
    spec.mediaBytes = QByteArray("fake-media-payload", 18);
    spec.fileName = QStringLiteral("clip.mp4");
    spec.embedded = true;

    QString error;
    const QByteArray written = aipdf::pdf::writeMediaAnnotation(source, spec, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const aipdf::pdf::RichMediaReadResult read = aipdf::pdf::readRichMedia(written);
    QVERIFY2(read.ok(), qPrintable(read.error));
    QCOMPARE(read.media.size(), 1);
    const aipdf::pdf::MediaAnnotationInfo& info = read.media.first();
    QCOMPARE(info.pageIndex, 1);
    QCOMPARE(info.rect.left(), spec.rect.left());
    QCOMPARE(info.rect.width(), spec.rect.width());
    QCOMPARE(info.embedded, true);
    QCOMPARE(info.mediaSize, static_cast<qint64>(spec.mediaBytes.size()));
    QVERIFY2(info.mimeType.contains(QStringLiteral("mp4")), qPrintable(info.mimeType));
    QVERIFY2(info.instanceSubtype.contains(QStringLiteral("Video")),
             qPrintable(info.instanceSubtype));

    const HealthReport health = checkHealth(written);
    QVERIFY2(health.problem.isEmpty(), qPrintable(health.problem));
    QCOMPARE(health.pageCount, sourcePages);
}

// 任意文件转 PDF：中文会被替换成 '?'，但**替换的数量必须对得上**。
//
// 这条守的是「不静默丢字」：默认路径用标准 14 号字体，中文显示不了，
// 模块会把它们写成 '?' 并如实报出替换了多少个字符。
// 这里断言的是「报告里的数量」与「文件里实际的问号数」一致——
// 不是说中文一定能正确显示（那需要调用方自备字体）。
void DocumentPipelineTests::convertToPdfWithChineseTextKeepsTheLossAccountedFor() {
    const QString chinese = QStringLiteral("中文测试：这一段用来验证字形替换的计数。");
    const QString mixed = QStringLiteral("Mixed ASCII line.\n") + chinese;
    const QByteArray content = mixed.toUtf8();

    const aipdf::doc::UniversalConversionResult result =
        aipdf::doc::convertToPdf(content, QStringLiteral("集成测试.txt"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.bytes.isEmpty());
    QVERIFY(result.pageCount > 0);

    // 标准 14 字体表示不了中文 → 替换计数必须大于 0。
    QVERIFY2(result.replacedCharacterCount > 0, "中文应当被如实报告为替换");
    QVERIFY2(!result.warning.isEmpty(), "有损时必须给出警告");

    // 产出能被 PDFium 打开，页数与报告一致。
    const HealthReport health = checkHealth(result.bytes);
    QVERIFY2(health.problem.isEmpty(), qPrintable(health.problem));
    QCOMPARE(health.pageCount, result.pageCount);

    // 抽回来的文字里，问号的数量必须正好等于报告的替换字符数。
    const QString extracted = pdfiumPlainText(result.bytes);
    QVERIFY2(!extracted.isEmpty(), "产出的 PDF 必须能抽回文字");
    const int questionMarks = extracted.count(QLatin1Char('?'));
    QCOMPARE(questionMarks, result.replacedCharacterCount);
    // 纯 ASCII 的那半句必须原样抽回来。
    QVERIFY2(extracted.contains(QStringLiteral("Mixed ASCII line.")), qPrintable(extracted));
}

// 离线问答：找到答案时，引用必须能定位回源片段。
void DocumentPipelineTests::offlineQaAnswersFromLocalIndexWithLocatableCitation() {
    const aipdf::doc::LocalIndex index = buildSampleIndex();
    const aipdf::ai::OfflineQa qa(&index);

    const aipdf::ai::OfflineAnswer answer = qa.answer(QStringLiteral("自动保存的间隔是多久"));
    QVERIFY2(answer.hasAnswer(), qPrintable(answer.notice));
    QCOMPARE(answer.status, aipdf::ai::OfflineAnswerStatus::Found);
    QVERIFY(!answer.citations.isEmpty());
    // 引用必须全部核过。
    QVERIFY(answer.citationsVerified());
    QCOMPARE(answer.verifiedCitationCount, answer.citations.size());

    // 引用要能定位回源片段：块 id 存在，且引文在源片段里逐字找得到。
    bool located = false;
    for (const aipdf::ai::OfflineCitation& citation : answer.citations) {
        aipdf::doc::DocumentChunk chunk;
        QVERIFY2(index.chunkByIdOnPage(citation.pageIndex, citation.chunkId, &chunk),
                 qPrintable(citation.chunkId));
        QVERIFY2(chunk.text.contains(citation.quote),
                 qPrintable(QStringLiteral("引用不在源片段里：%1").arg(citation.quote)));
        // 字符下标要指向引文在源片段里的位置。
        QVERIFY(citation.charOffset >= 0);
        QCOMPARE(chunk.text.mid(citation.charOffset, citation.quote.size()), citation.quote);
        if (citation.chunkId == QStringLiteral("p0b0")) located = true;
    }
    QVERIFY2(located, "回答「自动保存」时，引用必须来自讲自动保存的那个片段");
}

// 离线问答：完全无关的问题必须明确说「没找到」，而且**一个字都不编**。
void DocumentPipelineTests::offlineQaRefusesToInventAnAnswerForUnrelatedQuestion() {
    const aipdf::doc::LocalIndex index = buildSampleIndex();
    const aipdf::ai::OfflineQa qa(&index);

    const aipdf::ai::OfflineAnswer answer = qa.answer(QStringLiteral("如何申请年假与报销流程"));
    QVERIFY(answer.isNoMatch());
    QVERIFY(!answer.hasAnswer());
    // 答案正文必须完全是空的：编一句「看起来像答案」的话比不给答案坏得多。
    QVERIFY2(answer.text.isEmpty(), qPrintable(answer.text));
    QVERIFY(answer.citations.isEmpty());
    QCOMPARE(answer.verifiedCitationCount, 0);
    QVERIFY2(answer.notice.contains(QStringLiteral("没有找到")), qPrintable(answer.notice));
}

// ---------------------------------------------------------------------------
// 健康检查专项
// ---------------------------------------------------------------------------

// 流水线的每一步，产物都必须能被 PDFium 打开、且 qpdf 说得它结构没问题。
void DocumentPipelineTests::everyPipelineStageProducesAHealthyPdf() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");

    const PipelineRun run = runPipeline(source);
    QVERIFY2(run.ok, qPrintable(run.failure));
    QVERIFY2(!run.stages.isEmpty(), "流水线至少要走一步");

    for (const PipelineStage& stage : run.stages) {
        const HealthReport health = checkHealth(stage.bytes);
        QVERIFY2(health.opened, qPrintable(QStringLiteral("%1：%2").arg(stage.name, health.problem)));
        QVERIFY2(health.problem.isEmpty(),
                 qPrintable(QStringLiteral("%1：%2").arg(stage.name, health.problem)));
        // 配了 qpdf 命令行就必须真的跑过一次结构校验——
        // 否则「qpdf 校验通过」这句话是空的，等于没验。
        if (!QString::fromUtf8(AIPDF_QPDF_CLI).isEmpty()) {
            QVERIFY2(health.qpdfRan,
                     qPrintable(QStringLiteral("%1：qpdf --check 没有跑起来").arg(stage.name)));
            QCOMPARE(health.qpdfExitCode, 0);
        }
    }
}

// 页数守恒：流水线走一圈，页数必须一步都不变。
void DocumentPipelineTests::pageCountIsPreservedThroughTheWholePipeline() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");
    const int sourcePages = pdfiumPageCount(source);
    QCOMPARE(sourcePages, 2);

    const PipelineRun run = runPipeline(source);
    QVERIFY2(run.ok, qPrintable(run.failure));
    for (const PipelineStage& stage : run.stages) {
        QCOMPARE(pdfiumPageCount(stage.bytes), sourcePages);
        // 结构校验也得真的跑过。
        if (!QString::fromUtf8(AIPDF_QPDF_CLI).isEmpty()) {
            const HealthReport health = checkHealth(stage.bytes);
            QVERIFY2(health.qpdfRan,
                     qPrintable(QStringLiteral("%1：qpdf --check 没有跑起来").arg(stage.name)));
            QCOMPARE(health.qpdfExitCode, 0);
        }
    }
}

// 导出三件（HTML / 图像化 PDF / DXF）：产物必须真的能用。
//
// 这一组**不需要** qpdf 库（三种导出都走 PDFium），所以不写 REQUIRE_QPDF()：
// 没有 qpdf 的构建里它照样跑。
void DocumentPipelineTests::exportsProduceUsableOutputs() {
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");
    const int sourcePages = pdfiumPageCount(source);

    // ---- HTML ----
    QVector<aipdf::pdf::HtmlPage> htmlPages;
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(source);
    QVERIFY(opened.document.isValid());
    for (int index = 0; index < opened.document.pageCount(); ++index) {
        const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(index);
        aipdf::pdf::HtmlPage htmlPage;
        if (page.has_value()) {
            htmlPage.sizePt = QSizeF(page->widthPoints(), page->heightPoints());
            const std::optional<aipdf::pdf::TextPage> textPage = page->extractText();
            if (textPage.has_value()) htmlPage.text = *textPage;
        }
        htmlPages.append(htmlPage);
    }
    const aipdf::pdf::HtmlExportResult html =
        aipdf::pdf::exportToHtml(htmlPages, aipdf::pdf::HtmlExportOptions());
    QVERIFY2(html.ok, qPrintable(html.error));
    QVERIFY2(html.html.contains(QStringLiteral("Folixa")), qPrintable(html.html.left(400)));
    QCOMPARE(html.html.count(QStringLiteral("class=\"page\"")), sourcePages);

    // ---- 图像化 PDF ----
    aipdf::pdf::RasterizeOptions rasterOptions;
    rasterOptions.dpi = 72.0;
    QString error;
    const QByteArray raster = aipdf::pdf::exportToImageBasedPdf(source, rasterOptions, &error);
    QVERIFY2(!raster.isEmpty(), qPrintable(error));
    const HealthReport rasterHealth = checkHealth(raster);
    QVERIFY2(rasterHealth.problem.isEmpty(), qPrintable(rasterHealth.problem));
    QCOMPARE(rasterHealth.pageCount, sourcePages);

    // ---- DXF ----
    aipdf::pdf::DxfExportOptions dxfOptions;
    dxfOptions.includeText = true;
    const QByteArray dxf = aipdf::pdf::exportPdfToDxf(source, dxfOptions, &error);
    QVERIFY2(!dxf.isEmpty(), qPrintable(error));
    QVERIFY2(dxf.contains("SECTION"), qPrintable(dxf.left(200)));
    QVERIFY2(dxf.contains("EOF"), qPrintable(dxf.left(200)));
}

// 活页夹：打包再读回，条目与文档属性都得对得上。
void DocumentPipelineTests::portfolioRoundTripsEntriesAndMetadata() {
    REQUIRE_QPDF();
    const QByteArray source = fixtureBytes(QStringLiteral("text.pdf"));
    if (source.isEmpty()) QSKIP("夹具不可用");

    QVector<aipdf::pdf::PortfolioEntry> entries;
    aipdf::pdf::PortfolioEntry first;
    first.name = QStringLiteral("正文");
    first.bytes = source;
    first.description = QStringLiteral("第一份");
    entries.append(first);
    aipdf::pdf::PortfolioEntry second;
    second.name = QStringLiteral("附件");
    second.bytes = QByteArray("plain attachment payload", 23);
    second.description = QStringLiteral("第二份");
    entries.append(second);

    aipdf::pdf::PortfolioOptions options;
    options.title = QStringLiteral("集成测试包");
    options.author = QStringLiteral("测试员");
    options.subject = QStringLiteral("流水线");

    QString error;
    const QByteArray packed = aipdf::pdf::writePortfolio(entries, options, &error);
    QVERIFY2(!packed.isEmpty(), qPrintable(error));

    const aipdf::pdf::PortfolioReadResult read = aipdf::pdf::readPortfolio(packed);
    QVERIFY2(read.ok, qPrintable(read.error));
    QCOMPARE(read.title, options.title);
    QCOMPARE(read.author, options.author);
    QCOMPARE(read.subject, options.subject);
    QCOMPARE(read.entries.size(), 2);
    QCOMPARE(read.entries.at(0).name, QStringLiteral("正文"));
    QCOMPARE(read.entries.at(1).name, QStringLiteral("附件"));
    // 条目的内容必须原样回来。
    QCOMPARE(read.entries.at(0).bytes, source);
    QCOMPARE(read.entries.at(1).bytes, QByteArray("plain attachment payload", 23));

    // 打包后的文档也得是健康的：每个条目一页封面。
    const HealthReport health = checkHealth(packed);
    QVERIFY2(health.problem.isEmpty(), qPrintable(health.problem));
    QCOMPARE(health.pageCount, 2);
}

// 这里用 QTEST_MAIN 而不是 QTEST_GUILESS_MAIN。
//
// 原因：pdf::layoutParagraph 用 QFontMetricsF 量文字宽度，而 Qt 的字体库必须在
// QGuiApplication 之后才能访问——QTEST_GUILESS_MAIN 建的是 QCoreApplication，
// 会在 QFontDatabase 处直接 QFATAL（整个测试进程 abort）。
// 仓库里 tests/DocumentReflowTests.cpp 与 tests/ParagraphFormatTests.cpp 有同样的做法。
QTEST_MAIN(DocumentPipelineTests)
#include "DocumentPipelineTests.moc"
