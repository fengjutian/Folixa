#include "pdf/compliance/UaGenerator.h"

#include "i18n/Translate.h"

#include "pdf/compliance/AltTextGenerator.h"
#include "pdf/compliance/RoleInference.h"
#include "pdf/compliance/SemanticTagWriter.h"
#include "pdf/compliance/TagTreeWriter.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

namespace aipdf::pdf {
namespace {

// 两列之间的空隙超过这个数（点），就当是另一列。
//
// 为什么要有这个数：一行里的字之间本来就有空格（一个空格大约 3~4 点），
// 不给个门槛，所有行都会被切成一列一列的。
constexpr double kColumnGap = 6.0;

// 一个表单域（AcroForm 里的一个终端字段）。
struct FormField {
    QString name;
    QString tooltip;
    int pageIndex = -1;
    QRectF rect;
    QVector<ObjectRef> widgetRefs;
};

#ifdef AIPDF_WITH_QPDF

// 读 AcroForm 时用的中间结构（要拿着 qpdf 的对象句柄）。
struct RawFormField {
    QString name;
    QString tooltip;
    QVector<QPDFObjectHandle> widgets;
};

// 字典里某个键的字符串值；不是字符串就当没有。
QString stringValue(const QPDFObjectHandle& dict, const char* key) {
    const QPDFObjectHandle value = dict.getKey(key);
    if (!value.isString()) return {};
    return QString::fromUtf8(value.getUTF8Value().c_str());
}

// 字典里某个键的名字值（去掉前导斜杠）；不是名字就当没有。
QString nameValue(const QPDFObjectHandle& dict, const char* key) {
    const QPDFObjectHandle value = dict.getKey(key);
    if (!value.isName()) return {};
    const std::string name = value.getName();
    if (name.empty()) return {};
    return QString::fromLatin1(name.c_str() + 1);
}

// 从 AcroForm 的字段树里收集终端域（与 pdf::autoTagFormFields 同一套口径：
// /T、/TU、/FT 都可以从父字段继承）。
void collectFields(const QPDFObjectHandle& node, const QString& inheritedName,
                   const QString& inheritedTooltip, int depth, QSet<int>* visited,
                   QVector<RawFormField>* fields) {
    if (!node.isDictionary() || depth > 64) return;
    if (node.isIndirect()) {
        const int id = node.getObjectID();
        if (visited->contains(id)) return;
        visited->insert(id);
    }

    const QString ownName = stringValue(node, "/T");
    const QString ownTooltip = stringValue(node, "/TU");
    const QString fullName = inheritedName.isEmpty()
                                 ? ownName
                                 : (ownName.isEmpty() ? inheritedName
                                                     : inheritedName + QLatin1Char('.') + ownName);
    const QString tooltip = ownTooltip.isEmpty() ? inheritedTooltip : ownTooltip;

    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (kids.isArray()) {
        QVector<QPDFObjectHandle> widgetKids;
        QVector<QPDFObjectHandle> fieldKids;
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            if (!kid.isDictionary()) continue;
            if (nameValue(kid, "/Subtype") == "Widget") widgetKids.append(kid);
            else fieldKids.append(kid);
        }
        if (!widgetKids.isEmpty()) {
            RawFormField field;
            field.name = fullName;
            field.tooltip = tooltip;
            field.widgets = widgetKids;
            fields->append(field);
        }
        for (const QPDFObjectHandle& kid : fieldKids) {
            collectFields(kid, fullName, tooltip, depth + 1, visited, fields);
        }
        return;
    }

    if (nameValue(node, "/Subtype") == "Widget") {
        RawFormField field;
        field.name = fullName;
        field.tooltip = tooltip;
        field.widgets.append(node);
        fields->append(field);
    }
}

// 读出文档里的表单域（字段名、提示文字、所在页、widget）。
QVector<FormField> readFormFields(const QByteArray& pdfBytes, QString* error) {
    QVector<FormField> fields;
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("ua-generator-form.pdf", pdfBytes.constData(),
                          static_cast<size_t>(pdfBytes.size()));
    const QPDFObjectHandle catalog = pdf.getRoot();
    const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();

    // 页序号 → 该页 /Annots 里的 widget 对象号。
    QHash<int, int> pageOfWidget;
    for (size_t i = 0; i < pages.size(); ++i) {
        const QPDFObjectHandle annots = pages.at(i).getKey("/Annots");
        if (!annots.isArray()) continue;
        for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
            if (!annot.isDictionary() || !annot.isIndirect()) continue;
            if (nameValue(annot, "/Subtype") != "Widget") continue;
            pageOfWidget.insert(annot.getObjectID(), static_cast<int>(i));
        }
    }

    const QPDFObjectHandle acroForm = catalog.getKey("/AcroForm");
    if (!acroForm.isDictionary()) return fields;
    const QPDFObjectHandle list = acroForm.getKey("/Fields");
    if (!list.isArray()) return fields;
    QSet<int> visited;
    QVector<RawFormField> rawFields;
    for (const QPDFObjectHandle& field : list.getArrayAsVector()) {
        collectFields(field, QString(), QString(), 0, &visited, &rawFields);
    }

    for (const RawFormField& raw : rawFields) {
        FormField field;
        field.name = raw.name;
        field.tooltip = raw.tooltip;
        // 域可能跨多个 widget：页与矩形取第一个能定位到的。
        for (const QPDFObjectHandle& widget : raw.widgets) {
            if (!widget.isIndirect()) continue;
            const auto found = pageOfWidget.constFind(widget.getObjectID());
            if (found == pageOfWidget.constEnd()) continue;
            if (field.pageIndex < 0) field.pageIndex = found.value();

            ObjectRef reference;
            reference.pageIndex = found.value();
            reference.objectId = widget.getObjectID();
            reference.generation = widget.getGeneration();
            field.widgetRefs.append(reference);

            if (field.rect.isNull()) {
                // widget 的矩形（/Rect：[左 下 右 上]）。
                const QPDFObjectHandle rect = widget.getKey("/Rect");
                if (rect.isArray()) {
                    const std::vector<QPDFObjectHandle> items = rect.getArrayAsVector();
                    if (items.size() == 4) {
                        double values[4] = {0, 0, 0, 0};
                        bool ok = true;
                        for (int i = 0; i < 4; ++i) {
                            if (!items.at(static_cast<size_t>(i)).isNumber()) {
                                ok = false;
                                break;
                            }
                            values[i] = items.at(static_cast<size_t>(i)).getNumericValue();
                        }
                        if (ok) {
                            field.rect = QRectF(QPointF(values[0], values[1]),
                                                QPointF(values[2], values[3]));
                        }
                    }
                }
            }
        }
        if (field.pageIndex < 0) continue;  // 找不到所在页：这个域没法进结构树
        fields.append(field);
    }
    Q_UNUSED(error)
    return fields;
}

#endif  // AIPDF_WITH_QPDF

// 一行按横向空隙切成几格：每格的起点 x、包含的文本运行、拼起来的文字。
//
// 为什么按「空隙」切：表格的列之间有明显的横向间隔，
// 而一句话里的字之间只有空格的间隔。两者的区别就是间隔的大小。
struct LineCells {
    QVector<double> starts;
    QVector<QVector<int>> runIndices;
    QStringList texts;
};

LineCells splitLineIntoCells(const ScannedPage& page, const ScannedTextLine& line) {
    LineCells cells;
    QVector<int> ordered = line.runIndices;
    std::sort(ordered.begin(), ordered.end(), [&page](int lhs, int rhs) {
        return page.runs.at(lhs).origin.x() < page.runs.at(rhs).origin.x();
    });
    double previousEnd = 0.0;
    bool first = true;
    for (int index : ordered) {
        const ScannedTextRun& run = page.runs.at(index);
        if (first || run.origin.x() - previousEnd > kColumnGap) {
            // 新的一列。
            cells.starts.append(run.origin.x());
            cells.runIndices.append(QVector<int>());
            cells.texts.append(QString());
            first = false;
        }
        const int cell = cells.runIndices.size() - 1;
        cells.runIndices[cell].append(index);
        cells.texts[cell] += run.text;
        previousEnd = std::max(previousEnd, run.end.x());
    }
    return cells;
}

// 生成 XMP 元数据（声明 PDF/UA-1）。
//
// 为什么写 part="1"：本项目的口径是 PDF/UA-1（ISO 14289-1）。
// PDF/UA-2 是另一部标准，本项目不支持，不能声明成 2。
QByteArray buildUaXmp() {
    return QByteArray(
        "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
        "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
        "<rdf:Description rdf:about=\"\" xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\" "
        "pdfuaid:part=\"1\" pdfuaid:amd=\"PDF/UA-1\"/>\n"
        "</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>");
}

}  // namespace

QString uaGenerationDisclaimer() {
    return QStringLiteral(
        "本功能按 PDF/UA-1（ISO 14289-1）给 PDF 加上标签结构，并用本项目自带的检查器复检产物。"
        "它**不是** PDF/UA 合规认证：正式的认证要由 veraPDF 这类专业校验器完成，"
        "读屏实测（NVDA / JAWS）也只能人工做。"
        "检查器通过只说明文件结构符合本项目实现的那些条款。");
}

UaGeneratorResult generateUaPdf(const QByteArray& pdfBytes, const UaGeneratorOptions& options,
                                QString* error) {
    UaGeneratorResult result;
    if (error) error->clear();

    if (pdfBytes.isEmpty()) {
        result.error = i18n::tr("aipdf::pdf::UaGenerator", "输入的 PDF 内容为空");
        if (error) *error = result.error;
        return result;
    }

    // ---- 1. 扫内容流 ----
    ScannedDocument scan;
    QString scanError;
    if (!scanDocumentContent(pdfBytes, &scan, &scanError)) {
        result.error = scanError.isEmpty()
                           ? i18n::tr("aipdf::pdf::UaGenerator", "扫描 PDF 内容失败")
                           : scanError;
        if (error) *error = result.error;
        return result;
    }

    // ---- 2. 把页面上的东西整理成「块」 ----
    QVector<SemanticBlock> blocks;
    for (int pageIndex = 0; pageIndex < scan.pages.size(); ++pageIndex) {
        const ScannedPage& page = scan.pages.at(pageIndex);

        // 文字行。
        for (const ScannedTextLine& line : page.lines) {
            SemanticBlock block;
            block.pageIndex = pageIndex;
            block.rect = line.bounds;
            block.text = line.text.trimmed();
            block.fontSize = line.fontSize;
            block.bold = line.bold;
            block.runIndices = line.runIndices;
            const LineCells cells = splitLineIntoCells(page, line);
            block.columnStarts = cells.starts;
            block.columnRunIndices = cells.runIndices;
            block.columnTexts = cells.texts;
            if (block.text.isEmpty() && block.runIndices.isEmpty()) continue;
            blocks.append(block);
        }
        // 图像。
        for (const ScannedImage& image : page.images) {
            SemanticBlock block;
            block.pageIndex = pageIndex;
            block.rect = image.bounds;
            block.image = true;
            block.imageObjectId = image.objectId;
            block.imageGeneration = image.generation;
            blocks.append(block);
        }
    }

    // 表单域（AcroForm）。
#ifdef AIPDF_WITH_QPDF
    {
        QString formError;
        const QVector<FormField> fields = readFormFields(pdfBytes, &formError);
        for (const FormField& field : fields) {
            if (field.pageIndex < 0) continue;
            SemanticBlock block;
            block.pageIndex = field.pageIndex;
            block.rect = field.rect;
            block.formField = true;
            block.fieldName = field.name;
            block.fieldTooltip = field.tooltip;
            block.widgetRefs = field.widgetRefs;
            blocks.append(block);
        }
    }
#endif

    // 页内按位置排（从上到下、从左到右）：读屏念的顺序要跟人读的一致。
    std::stable_sort(blocks.begin(), blocks.end(), [](const SemanticBlock& left, const SemanticBlock& right) {
        if (left.pageIndex != right.pageIndex) return left.pageIndex < right.pageIndex;
        const double leftTop = left.rect.isNull() ? 0.0 : left.rect.bottom();
        const double rightTop = right.rect.isNull() ? 0.0 : right.rect.bottom();
        if (std::abs(leftTop - rightTop) > 0.5) return leftTop > rightTop;
        return left.rect.left() < right.rect.left();
    });

    // ---- 3. 推断角色，搭语义树 ----
    const RoleInferenceResult inference = inferSemanticRoles(blocks);
    SemanticTree tree = inference.tree;
    for (const RoleInferenceIssue& issue : inference.issues) {
        result.issues.append(QStringLiteral("%1：%2").arg(issue.where, issue.reason));
    }

    // ---- 4. 替代文本 ----
    const AltTextResult altTexts = generateAltTexts(inference.blocks);
    for (const AltTextIssue& issue : altTexts.issues) {
        result.issues.append(QStringLiteral("%1：%2").arg(issue.where, issue.reason));
    }
    for (int i = 0; i < tree.nodes.size(); ++i) {
        SemanticNode& node = tree.nodes[i];
        if (node.role == StructRole::Table) {
            // 表格的替代文本取自表头文字。
            for (int blockIndex : node.blockIndices) {
                const auto found = altTexts.tableAlternatives.constFind(blockIndex);
                if (found == altTexts.tableAlternatives.constEnd()) continue;
                node.alt = found.value();
                break;
            }
            continue;
        }
        for (int blockIndex : node.blockIndices) {
            if (blockIndex < 0 || blockIndex >= altTexts.alternatives.size()) continue;
            const QString alternative = altTexts.alternatives.at(blockIndex);
            if (alternative.isEmpty()) continue;
            node.alt = alternative;
            break;
        }
    }

    // ---- 5. 写语义树 + 补元数据 ----
    SemanticWriteOptions writeOptions;
    writeOptions.documentLanguage = options.language;
    writeOptions.documentTitle = options.title;
    writeOptions.displayDocTitle = options.displayDocTitle;
    writeOptions.writeTabsStructureOrder = true;
    writeOptions.xmpMetadata = buildUaXmp();

    const SemanticWriteResult written = writeSemanticStructure(pdfBytes, inference.blocks, tree,
                                                                writeOptions);
    if (!written.ok) {
        result.error = written.error;
        if (error) *error = written.error;
        return result;
    }
    result.bytes = written.bytes;
    result.tree = written.tree;

    // ---- 6. 对产物重跑检查器 ----
    result.report = checkPdfUa(result.bytes);
    result.passedChecks = result.report.passCount;
    for (const PdfUaFinding& finding : result.report.findings) {
        if (finding.severity == PdfUaSeverity::Error) {
            result.issues.append(QStringLiteral("%1（%2）").arg(finding.title, finding.detail));
        }
    }
    result.manualReviewItems = result.issues.size();
    result.ok = result.report.errorCount == 0;
    result.summaryText =
        QStringLiteral("过了本项目检查器的 %1 项检查，另有 %2 项需人工确认。")
            .arg(result.passedChecks)
            .arg(result.manualReviewItems);
    if (!result.ok && error) {
        *error = QStringLiteral("产物仍有 %1 项检查没通过，已列出问题。").arg(result.report.errorCount);
    }
    return result;
}

}  // namespace aipdf::pdf
