#include "pdf/compliance/SemanticReadingOrder.h"

#include "i18n/Translate.h"

#include "pdf/compliance/StructTreeModel.h"
#include "pdf/compliance/StructTreeReader.h"
#include "pdf/compliance/TagTreeWriter.h"
#include "pdf/content/ContentStream.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <memory>
#include <vector>
#endif

namespace aipdf::pdf {
namespace {

// ---------------------------------------------------------------------------
// 角色与容器
// ---------------------------------------------------------------------------

// 结构容器：只负责分组，本身不是可排序的内容。
//
// 为什么把 Sect 算容器而不是内容：Sect 是「一节」的壳，用户心智里的
// 「一段 / 一个标题 / 一张表 / 一张图」都在它下面；重排容器没有意义。
bool isContainerTag(const QString& tag) {
    static const QSet<QString> kContainers = {
        QStringLiteral("Document"), QStringLiteral("Part"),     QStringLiteral("Sect"),
        QStringLiteral("Div"),     QStringLiteral("Art"),      QStringLiteral("BlockQuote"),
        QStringLiteral("TOC"),     QStringLiteral("TOCI"),     QStringLiteral("Index"),
        QStringLiteral("NonStruct"), QStringLiteral("Private"),
    };
    return kContainers.contains(tag);
}

// 文字预览的长度上限。
//
// 为什么截断：结构树里可能有整页那么长的段落，全塞进列表会让界面没法看。
constexpr int kPreviewLimit = 120;

QString previewText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.size() <= kPreviewLimit) return trimmed;
    return trimmed.left(kPreviewLimit) + QStringLiteral("…");
}

// ---------------------------------------------------------------------------
// 结构树视图
// ---------------------------------------------------------------------------

// 从读回的结构树里整理出来的东西：父子关系、每页的可排序单元。
struct TreeView {
    // 元素下标 → 子元素下标（树序）。
    QVector<QVector<int>> children;
    // 页序号 → 这一页的可排序单元（元素下标，树序）。
    QVector<QVector<int>> unitsOfPage;
    // 元素下标 → 它是不是可排序单元。
    QSet<int> unitSet;
    // 出错时的中文原因。
    QString error;
};

// 一棵结构树的视图。
//
// 什么叫「可排序单元」：父元素是结构容器（Part / Sect / Div…）的那个内容元素。
// 更深的子结构（LI、TR、TD…）跟着它的父单元走——表格整张是一个单元，
// 把表头格单独拎出来重排，表格就散了。
TreeView buildTreeView(const StructTreeReport& report) {
    TreeView view;
    view.children.resize(report.elements.size());
    for (int i = 0; i < report.elements.size(); ++i) {
        const int parent = report.elements.at(i).parentIndex;
        if (parent >= 0 && parent < view.children.size()) {
            view.children[parent].append(i);
        }
    }

    for (int i = 0; i < report.elements.size(); ++i) {
        const StructElement& element = report.elements.at(i);
        if (element.tag.isEmpty()) {
            // 没有 /S 的元素是「注解引用」（表单域的 widget 挂在 /K 里）。
            // 写手重建结构树时需要 widget 的对象号才能把它挂回去，
            // 而读回的结构元素里没有这个信息——写不了就明说，不能悄悄丢掉表单域。
            view.error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                   "这份文档的语义树里含表单域（/Form）结构，"
                                   "语义级重排暂不支持表单域，文档没有修改。");
            return view;
        }
        if (isContainerTag(element.tag)) continue;
        const int parent = element.parentIndex;
        const bool parentIsContainer =
            parent < 0 || (parent < report.elements.size() &&
                           isContainerTag(report.elements.at(parent).tag));
        if (!parentIsContainer) continue;  // 单元内部的子结构，跟着单元走
        if (element.pageIndex < 0) continue;  // 不挂在任何页上的内容不参与排序

        view.unitSet.insert(i);
        while (view.unitsOfPage.size() <= element.pageIndex) {
            view.unitsOfPage.append(QVector<int>());
        }
        view.unitsOfPage[element.pageIndex].append(i);
    }
    return view;
}

// 一个单元（含它的子结构）的文字。
QString subtreeText(int index, const QVector<QVector<int>>& children,
                     const QVector<StructElement>& elements, int depth = 0) {
    if (index < 0 || index >= elements.size() || depth > 512) return QString();
    QString text = elements.at(index).text;
    for (int child : children.at(index)) {
        text += subtreeText(child, children, elements, depth + 1);
    }
    return text;
}

// ---------------------------------------------------------------------------
// 内容流里的标记内容
// ---------------------------------------------------------------------------

int previousSignificant(const QVector<ContentToken>& tokens, int index) {
    for (int i = index - 1; i >= 0; --i) {
        if (tokens.at(i).type == ContentTokenType::Whitespace) continue;
        if (tokens.at(i).type == ContentTokenType::Comment) continue;
        return i;
    }
    return -1;
}

// 在 token 区间里找字典的某个键对应的值记号。
int findDictValue(const QVector<ContentToken>& tokens, int start, int end, const QByteArray& key) {
    for (int i = start; i < end; ++i) {
        if (tokens.at(i).type != ContentTokenType::Name) continue;
        if (tokens.at(i).raw != key) continue;
        for (int j = i + 1; j < end; ++j) {
            if (tokens.at(j).type == ContentTokenType::Whitespace) continue;
            if (tokens.at(j).type == ContentTokenType::Comment) continue;
            return j;
        }
        return -1;
    }
    return -1;
}

// 一页的内容流里，MCID → 它包住的显示操作符下标。
//
// 为什么按「显示操作符下标」对齐而不是按文字对齐：
// 内容流里的标记内容与扫描出来的文本运行是**同一批显示操作符**
// （顺序一致、数量一致），按下标对齐就不依赖两套解码器给出同样的文字——
// 解码有差异时也不会把文字配错元素。按文字对齐会在字体编码特殊时错位。
QHash<int, QVector<int>> scanMarkedContentRuns(const QByteArray& content, int* displayOpCount,
                                                QString* error) {
    QHash<int, QVector<int>> result;
    const ContentParseResult parsed = ContentStream::tokenize(content);
    if (!parsed.isValid()) {
        if (error) *error = parsed.error;
        return result;
    }
    const QVector<ContentToken>& tokens = parsed.tokens;

    struct Frame {
        int mcid = -1;
        QVector<int> ops;
    };
    QVector<Frame> stack;
    int opCount = 0;

    for (int i = 0; i < tokens.size(); ++i) {
        const ContentToken& token = tokens.at(i);
        if (token.type != ContentTokenType::Operator) continue;
        const QByteArray& op = token.raw;

        if (op == "BDC" || op == "BMC") {
            int mcid = -1;
            if (op == "BDC") {
                const int propsIndex = previousSignificant(tokens, i);
                if (propsIndex >= 0 && tokens.at(propsIndex).type == ContentTokenType::DictionaryEnd) {
                    int depth = 0;
                    int dictStart = -1;
                    for (int j = propsIndex; j >= 0; --j) {
                        if (tokens.at(j).type == ContentTokenType::DictionaryEnd) {
                            ++depth;
                        } else if (tokens.at(j).type == ContentTokenType::DictionaryStart) {
                            --depth;
                            if (depth == 0) {
                                dictStart = j;
                                break;
                            }
                        }
                    }
                    if (dictStart >= 0) {
                        const int mcidIndex =
                            findDictValue(tokens, dictStart + 1, propsIndex, "/MCID");
                        if (mcidIndex >= 0 && tokens.at(mcidIndex).type == ContentTokenType::Number) {
                            mcid = tokens.at(mcidIndex).raw.toInt();
                        }
                    }
                }
            }
            stack.append({mcid, QVector<int>()});
            continue;
        }

        if (op == "EMC") {
            if (stack.isEmpty()) continue;
            const Frame frame = stack.takeLast();
            if (frame.mcid < 0) continue;
            if (result.contains(frame.mcid)) {
                if (error && error->isEmpty()) {
                    *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                      "内容流里 MCID %1 出现了两次，无法确定它对应哪段文字。")
                                 .arg(frame.mcid);
                }
                continue;
            }
            result.insert(frame.mcid, frame.ops);
            continue;
        }

        if (op == "Tj" || op == "TJ" || op == "'" || op == "\"") {
            const int index = opCount++;
            // 嵌套的标记内容：里层的显示操作符也属于外层。
            for (Frame& frame : stack) frame.ops.append(index);
        }
    }
    if (displayOpCount) *displayOpCount = opCount;
    return result;
}

#ifdef AIPDF_WITH_QPDF
// 每一页的内容流（按页顺序拼起来）。
QVector<QByteArray> pageContentStreams(const QByteArray& pdfBytes, QString* error) {
    QVector<QByteArray> result;
    try {
        QPDF pdf;
        pdf.processMemoryFile("semantic-order-content.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        for (const QPDFObjectHandle& pageHandle : pages) {
            QByteArray content;
            // qpdf 的取内容流接口不是 const 方法，所以句柄要按值取一份可改的。
            QPDFObjectHandle page = pageHandle;
            const std::vector<QPDFObjectHandle> streams = page.getPageContents();
            for (const QPDFObjectHandle& streamHandle : streams) {
                QPDFObjectHandle stream = streamHandle;
                if (!stream.isStream()) continue;
                const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
                if (!data) continue;
                content += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                      static_cast<qsizetype>(data->getSize()));
                content += '\n';
            }
            result.append(content);
        }
    } catch (const std::exception& failure) {
        if (error) *error = QString::fromUtf8(failure.what());
        return {};
    }
    return result;
}
#endif

// ---------------------------------------------------------------------------
// 顺序的对号与校验
// ---------------------------------------------------------------------------

// 把传入的顺序与当前文档的单元一一对上。
//
// 为什么这么严：写回时少一个元素就等于**丢一段内容**，
// 多一个元素就等于**凭空多出一段**。宁可当场失败，也不能写出一份丢元素的树。
bool matchOrder(const QVector<SemanticOrderPage>& order, const TreeView& view,
                QVector<QVector<int>>* newOrder, QString* error) {
    newOrder->clear();

    // 文档里有单元的页，必须都在传入的顺序里。
    for (int page = 0; page < view.unitsOfPage.size(); ++page) {
        if (view.unitsOfPage.at(page).isEmpty()) continue;
        bool found = false;
        for (const SemanticOrderPage& candidate : order) {
            if (candidate.pageIndex == page) {
                found = true;
                break;
            }
        }
        if (!found) {
            if (error) {
                *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                  "第 %1 页的语义元素没有出现在传入的顺序里，文档没有修改。")
                             .arg(page + 1);
            }
            return false;
        }
    }

    for (const SemanticOrderPage& page : order) {
        const int pageIndex = page.pageIndex;
        if (pageIndex < 0 || pageIndex >= view.unitsOfPage.size()) {
            if (error) {
                *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                  "第 %1 页没有语义元素，无法按这份顺序重排。")
                             .arg(pageIndex + 1);
            }
            return false;
        }
        const QVector<int>& current = view.unitsOfPage.at(pageIndex);
        if (current.size() != page.entries.size()) {
            if (error) {
                *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                  "第 %1 页的语义元素数量对不上（文档里 %2 个，传入 %3 个），"
                                  "文档没有修改。")
                             .arg(pageIndex + 1)
                             .arg(current.size())
                             .arg(page.entries.size());
            }
            return false;
        }
        QVector<int> sequence;
        QSet<int> used;
        for (const SemanticOrderEntry& entry : page.entries) {
            if (!current.contains(entry.nodeIndex)) {
                if (error) {
                    *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                      "第 %1 页的顺序里有一个不属于这一页的语义元素，文档没有修改。")
                                 .arg(pageIndex + 1);
                }
                return false;
            }
            if (used.contains(entry.nodeIndex)) {
                if (error) {
                    *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                      "第 %1 页的顺序里有重复的语义元素，文档没有修改。")
                                 .arg(pageIndex + 1);
                }
                return false;
            }
            used.insert(entry.nodeIndex);
            sequence.append(entry.nodeIndex);
        }
        newOrder->append(sequence);
    }
    return true;
}

// 每个元素的「角色 + 文字」签名（用来比写前写后有没有变）。
QStringList elementSignatures(const StructTreeReport& report) {
    QStringList signatures;
    signatures.reserve(report.elements.size());
    for (const StructElement& element : report.elements) {
        signatures.append(element.tag + QLatin1Char('\t') + element.text);
    }
    signatures.sort();
    return signatures;
}

}  // namespace

// ---------------------------------------------------------------------------
// 读
// ---------------------------------------------------------------------------

QVector<SemanticOrderPage> readSemanticReadingOrder(const QByteArray& input, QString* error) {
    if (error) error->clear();
    if (input.isEmpty()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder", "输入的 PDF 内容为空");
        }
        return {};
    }

    const StructTreeReport report = readStructTree(input);
    if (!report.error.isEmpty()) {
        if (error) *error = report.error;
        return {};
    }
    if (!report.hasStructTree) {
        // 没有语义树就说清楚为什么——不静默退化成行级顺序。
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "这份文档还没有语义标签树；先运行「自动添加文档标签」生成语义树");
        }
        return {};
    }

    const TreeView view = buildTreeView(report);
    if (!view.error.isEmpty()) {
        if (error) *error = view.error;
        return {};
    }

    QVector<SemanticOrderPage> pages;
    for (int pageIndex = 0; pageIndex < view.unitsOfPage.size(); ++pageIndex) {
        const QVector<int>& units = view.unitsOfPage.at(pageIndex);
        if (units.isEmpty()) continue;
        SemanticOrderPage page;
        page.pageIndex = pageIndex;
        for (int unit : units) {
            const StructElement& element = report.elements.at(unit);
            SemanticOrderEntry entry;
            entry.pageIndex = pageIndex;
            entry.role = element.tag;
            entry.text = previewText(subtreeText(unit, view.children, report.elements));
            entry.mcids = element.mcids;
            entry.nodeIndex = unit;
            page.entries.append(entry);
        }
        pages.append(page);
    }
    return pages;
}

// ---------------------------------------------------------------------------
// 写
// ---------------------------------------------------------------------------

QByteArray writeSemanticReadingOrder(const QByteArray& input,
                                      const QVector<SemanticOrderPage>& order,
                                      QString* error) {
    if (error) error->clear();
    if (input.isEmpty()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder", "输入的 PDF 内容为空");
        }
        return {};
    }

    const StructTreeReport report = readStructTree(input);
    if (!report.error.isEmpty()) {
        if (error) *error = report.error;
        return {};
    }
    if (!report.hasStructTree) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "这份文档还没有语义标签树；先运行「自动添加文档标签」生成语义树");
        }
        return {};
    }

    const TreeView view = buildTreeView(report);
    if (!view.error.isEmpty()) {
        if (error) *error = view.error;
        return {};
    }

    // ---- 1. 对号：把传入的顺序与当前的单元对上 ----
    QVector<QVector<int>> newOrder;
    if (!matchOrder(order, view, &newOrder, error)) {
        return {};
    }

    // ---- 2. 新的子节点顺序：同一父容器下的单元按新顺序排 ----
    //
    // 为什么只动单元之间的相对次序：父容器（Part / Sect）本身也是内容顺序的一部分，
    // 换掉它等于改结构。表格、列表这类「一个单元带一串子结构」的整块搬家，
    // 子结构跟着走，表格不会散。
    QVector<QVector<int>> newChildren = view.children;
    for (const QVector<int>& sequence : newOrder) {
        QHash<int, QVector<int>> unitsOfParent;
        for (int unit : sequence) {
            const int parent = report.elements.at(unit).parentIndex;
            unitsOfParent[parent].append(unit);
        }
        for (auto it = unitsOfParent.constBegin(); it != unitsOfParent.constEnd(); ++it) {
            const int parent = it.key();
            if (parent < 0 || parent >= newChildren.size()) continue;
            QVector<int> replaced;
            int cursor = 0;
            for (int child : newChildren.at(parent)) {
                if (!view.unitSet.contains(child)) {
                    replaced.append(child);
                    continue;
                }
                if (cursor < it.value().size()) {
                    replaced.append(it.value().at(cursor));
                    ++cursor;
                }
            }
            newChildren[parent] = replaced;
        }
    }

    // ---- 3. 根节点 ----
    int rootElement = -1;
    for (int i = 0; i < report.elements.size(); ++i) {
        if (report.elements.at(i).parentIndex < 0) {
            if (rootElement >= 0) {
                if (error) {
                    *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                      "这份文档的语义树有多个根节点，无法安全重排，文档没有修改。");
                }
                return {};
            }
            rootElement = i;
        }
    }
    if (rootElement < 0) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "这份文档的语义树没有根节点，无法安全重排，文档没有修改。");
        }
        return {};
    }

    // ---- 4. MCID → 文本运行 ----
    //
    // 写手是按「块里的文本运行」发 MCID 的，所以要知道每个 MCID 对应哪些运行，
    // 才能保证「顺序变了、文字还在原来的元素上」。
    ScannedDocument scan;
    QString scanError;
    if (!scanDocumentContent(input, &scan, &scanError)) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder", "扫描文档内容失败：%1")
                         .arg(scanError);
        }
        return {};
    }

    QVector<QHash<int, QVector<int>>> runsOfMcid;
#ifdef AIPDF_WITH_QPDF
    QString contentError;
    const QVector<QByteArray> contents = pageContentStreams(input, &contentError);
    if (contents.isEmpty() && !contentError.isEmpty()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder", "读取页面内容流失败：%1")
                         .arg(contentError);
        }
        return {};
    }
    runsOfMcid.resize(contents.size());
    for (int pageIndex = 0; pageIndex < contents.size(); ++pageIndex) {
        int opCount = 0;
        QString markedError;
        const QHash<int, QVector<int>> marked =
            scanMarkedContentRuns(contents.at(pageIndex), &opCount, &markedError);
        if (!markedError.isEmpty()) {
            if (error) *error = markedError;
            return {};
        }
        const int runCount = pageIndex < scan.pages.size() ? scan.pages.at(pageIndex).runs.size() : 0;
        if (opCount != runCount) {
            // 数量对不上说明内容流里有扫描器没认出来的显示操作符，
            // 这时按下标配对会把文字配错元素——宁可不做。
            if (error) {
                *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                  "第 %1 页的内容流与扫描结果对不上（%2 个显示操作符 / %3 个文本运行），"
                                  "无法安全重排，文档没有修改。")
                             .arg(pageIndex + 1)
                             .arg(opCount)
                             .arg(runCount);
            }
            return {};
        }
        runsOfMcid[pageIndex] = marked;
    }
#else
    if (error) {
        *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                          "当前构建没有启用 qpdf，无法重排语义阅读顺序");
    }
    return {};
#endif

    // ---- 5. 组装写手计划 ----
    StructWritePlan plan;
    plan.rootIndex = 0;
    plan.documentLanguage = report.hasDocumentLanguage ? report.documentLanguage : QString();
    plan.displayDocTitle = report.hasDisplayDocTitle ? report.displayDocTitle : false;
    // 语义树与 Tab 顺序必须一致（PDF/UA-1 §7.4），写的时候一起更新。
    plan.writeTabsStructureOrder = true;

    const int pageCount = static_cast<int>(std::max(runsOfMcid.size(), scan.pages.size()));
    plan.pageBlockRuns.resize(std::max(pageCount, 1));

    QVector<int> planIndexOfElement(report.elements.size(), -1);
    // 每一页被语义元素认领走的文本运行。
    QVector<QSet<int>> claimedRuns(static_cast<int>(plan.pageBlockRuns.size()));

    struct Frame {
        int element = -1;
        int cursor = 0;
    };
    QVector<Frame> stack;
    stack.append({rootElement, 0});

    while (!stack.isEmpty()) {
        const int current = stack.last().element;
        const bool entering = stack.last().cursor == 0;
        if (entering) {
            const StructElement& element = report.elements.at(current);
            StructElementPlan planElement;
            planElement.type = element.tag;
            planElement.title = element.title;
            planElement.alt = element.alt;
            planElement.actualText = element.actualText;
            planElement.language = element.language;
            planElement.pageIndex = element.pageIndex;
            if (element.tag == QLatin1String("TH")) {
                // 表头格按列覆盖（与共用写手对表头格的默认处理一致）。
                planElement.scope = QStringLiteral("Column");
            }
            if (element.parentIndex >= 0) {
                planElement.parentIndex = planIndexOfElement.at(element.parentIndex);
            }

            // 内容块：元素自己的 MCID → 文本运行。
            if (!element.mcids.isEmpty()) {
                if (element.pageIndex < 0 || element.pageIndex >= runsOfMcid.size() ||
                    element.pageIndex >= plan.pageBlockRuns.size()) {
                    if (error) {
                        *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                          "语义树里有元素没有页码，无法安全重排，文档没有修改。");
                    }
                    return {};
                }
                const QHash<int, QVector<int>>& marked = runsOfMcid.at(element.pageIndex);
                QVector<int> runs;
                for (int mcid : element.mcids) {
                    const auto found = marked.constFind(mcid);
                    if (found == marked.constEnd()) {
                        if (error) {
                            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                              "第 %1 页的 MCID %2 在内容流里找不到对应的标记内容，"
                                              "无法安全重排，文档没有修改。")
                                         .arg(element.pageIndex + 1)
                                         .arg(mcid);
                        }
                        return {};
                    }
                    runs += found.value();
                }
                const int local = plan.pageBlockRuns.at(element.pageIndex).size();
                plan.pageBlockRuns[element.pageIndex].append(runs);
                planElement.blockIndices.append(local);
                for (int run : runs) {
                    claimedRuns[element.pageIndex].insert(run);
                }
            }

            planIndexOfElement[current] = plan.elements.size();
            plan.elements.append(planElement);
        }

        const QVector<int>& children = newChildren.at(current);
        if (stack.last().cursor < children.size()) {
            const int child = children.at(stack.last().cursor);
            ++stack.last().cursor;
            stack.append({child, 0});
            continue;
        }
        stack.removeLast();
    }

    // 每一处文字都要有归属：没被任何语义元素认领的运行，写手会给它发一个
    // 却没有结构元素的 MCID，/ParentTree 里那一格是空的——qpdf 写不出这种
    // 对象图。与其让写手报一句看不懂的错，不如这里说清楚是哪一页对不上。
    for (int pageIndex = 0; pageIndex < scan.pages.size(); ++pageIndex) {
        const int total = scan.pages.at(pageIndex).runs.size();
        const int claimed = pageIndex < claimedRuns.size() ? claimedRuns.at(pageIndex).size() : 0;
        if (claimed == total) continue;
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "第 %1 页有 %2 处文字不归属任何语义元素（结构树与内容流对不上），"
                              "无法安全重排，文档没有修改。")
                         .arg(pageIndex + 1)
                         .arg(total - claimed);
        }
        return {};
    }

    // 子节点清单要等子元素建好之后才准（写手按它写 /K），这里按新顺序补一次。
    {
        QVector<Frame> walk;
        walk.append({rootElement, 0});
        while (!walk.isEmpty()) {
            const int current = walk.last().element;
            const QVector<int>& children = newChildren.at(current);
            if (walk.last().cursor < children.size()) {
                const int child = children.at(walk.last().cursor);
                ++walk.last().cursor;
                const int parentPlan = planIndexOfElement.value(current, -1);
                const int childPlan = planIndexOfElement.value(child, -1);
                if (parentPlan >= 0 && childPlan >= 0) {
                    plan.elements[parentPlan].childIndices.append(childPlan);
                }
                walk.append({child, 0});
                continue;
            }
            walk.removeLast();
        }
    }

    // ---- 6. 写 ----
    QString writeError;
    const QByteArray written = writeStructPlan(input, plan, &writeError);
    if (written.isEmpty()) {
        if (error) {
            *error = writeError.isEmpty()
                         ? i18n::tr("aipdf::pdf::SemanticReadingOrder", "写语义标签树失败，文档没有修改。")
                         : writeError;
        }
        return {};
    }

    // ---- 7. 读回校验：角色与文字必须与写之前一致，只是顺序变了 ----
    const StructTreeReport after = readStructTree(written);
    if (!after.error.isEmpty()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "写回后的语义树读不出来（%1），文档没有修改。")
                         .arg(after.error);
        }
        return {};
    }
    if (after.elements.size() != report.elements.size()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "写回后的语义元素数量对不上（写之前 %1 个，写之后 %2 个），"
                              "文档没有修改。")
                         .arg(report.elements.size())
                         .arg(after.elements.size());
        }
        return {};
    }
    if (elementSignatures(after) != elementSignatures(report)) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "写回后有语义元素的角色或文字变了，文档没有修改。");
        }
        return {};
    }

    // 每页的单元顺序必须与传入的一致。
    const TreeView afterView = buildTreeView(after);
    if (!afterView.error.isEmpty()) {
        if (error) *error = afterView.error;
        return {};
    }
    for (const QVector<int>& sequence : newOrder) {
        if (sequence.isEmpty()) continue;
        const int pageIndex = report.elements.at(sequence.first()).pageIndex;
        if (pageIndex < 0 || pageIndex >= afterView.unitsOfPage.size()) continue;
        const QVector<int>& actual = afterView.unitsOfPage.at(pageIndex);
        if (actual.size() != sequence.size()) {
            if (error) {
                *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                  "第 %1 页写回后的语义元素数量对不上，文档没有修改。")
                             .arg(pageIndex + 1);
            }
            return {};
        }
        for (int i = 0; i < sequence.size(); ++i) {
            const StructElement& expected = report.elements.at(sequence.at(i));
            const StructElement& got = after.elements.at(actual.at(i));
            if (expected.tag != got.tag ||
                subtreeText(sequence.at(i), view.children, report.elements) !=
                    subtreeText(actual.at(i), afterView.children, after.elements)) {
                if (error) {
                    *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                      "第 %1 页写回后的语义顺序与要求的不一致，文档没有修改。")
                                 .arg(pageIndex + 1);
                }
                return {};
            }
        }
    }

    return written;
}

}  // namespace aipdf::pdf
