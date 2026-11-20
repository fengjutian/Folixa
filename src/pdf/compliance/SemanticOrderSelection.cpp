#include "pdf/compliance/SemanticOrderSelection.h"

#include "i18n/Translate.h"

#include "pdf/compliance/SemanticReadingOrder.h"
#include "pdf/compliance/StructTreeReader.h"
#include "pdf/compliance/TagTreeWriter.h"
#include "pdf/content/ContentStream.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <memory>
#include <vector>
#endif

namespace aipdf::pdf {
namespace {

// 视觉顺序里「算同一行」的容差（PDF 点）。
//
// 为什么是 0.5：与「自动添加文档标签」里页内排序的口径一致
// （pdf/compliance/UaGenerator.cpp 的页内排序），两条命令给出的
// 「视觉顺序」才是一套，用户不会看到两套先后。
constexpr double kSameLineTolerance = 0.5;

// 一个语义元素在页面上的包围盒。
struct ElementBox {
    QRectF rect;
    bool valid = false;
};

// ---------------------------------------------------------------------------
// 内容流：标记内容与文本运行的对应关系
// ---------------------------------------------------------------------------
//
// 下面两个函数与 pdf/compliance/SemanticReadingOrder.cpp 里的同名件是**同一套口径**
// （内容流里的标记内容 = 扫描出来的同一批显示操作符），但这里是各写一份：
// 那个文件本轮由别的 worker 在改，本文件不能碰它；两边都是纯函数，
// 口径一致由测试钉住（选区重排后的文字必须与重排前逐字相同）。

#ifdef AIPDF_WITH_QPDF
// 每一页的内容流（按页顺序拼起来）。
QVector<QByteArray> pageContentStreams(const QByteArray& pdfBytes, QString* error) {
    QVector<QByteArray> result;
    try {
        QPDF pdf;
        pdf.processMemoryFile("semantic-scope-content.pdf", pdfBytes.constData(),
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
#endif  // AIPDF_WITH_QPDF

int previousSignificant(const QVector<ContentToken>& tokens, int index) {
    for (int i = index - 1; i >= 0; --i) {
        if (tokens.at(i).type == ContentTokenType::Whitespace) continue;
        if (tokens.at(i).type == ContentTokenType::Comment) continue;
        return i;
    }
    return -1;
}

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
// （顺序一致、数量一致），按下标对齐就不依赖两套解码器给出同样的文字。
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
                    *error = i18n::tr("aipdf::pdf::SemanticOrderSelection",
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

// ---------------------------------------------------------------------------
// 元素的位置
// ---------------------------------------------------------------------------

// 读出每个语义元素在页面上的包围盒。
//
// nodeIndex（结构树元素下标）→ 包围盒。取不到位置的元素不在表里。
//
// 为什么元素的位置要从内容流里取：结构树只给角色与 MCID 编号，
// 不给「这个元素印在页面的哪一块」；而「视觉顺序」说的就是位置。
bool collectElementBoxes(const QByteArray& pdfBytes, const StructTreeReport& report,
                         QHash<int, ElementBox>* boxes, QString* error) {
    boxes->clear();
    ScannedDocument scan;
    if (!scanDocumentContent(pdfBytes, &scan, error)) return false;
#ifdef AIPDF_WITH_QPDF
    QString contentError;
    const QVector<QByteArray> contents = pageContentStreams(pdfBytes, &contentError);
    if (contents.isEmpty() && !contentError.isEmpty()) {
        if (error) *error = contentError;
        return false;
    }
    for (int pageIndex = 0; pageIndex < contents.size(); ++pageIndex) {
        int opCount = 0;
        QString markedError;
        const QHash<int, QVector<int>> marked =
            scanMarkedContentRuns(contents.at(pageIndex), &opCount, &markedError);
        if (!markedError.isEmpty()) {
            if (error) *error = markedError;
            return false;
        }
        const int runCount = pageIndex < scan.pages.size() ? scan.pages.at(pageIndex).runs.size() : 0;
        if (opCount != runCount) {
            // 数量对不上说明内容流里有扫描器没认出来的显示操作符，
            // 这时按下标配对会把文字配错元素——宁可不做。
            if (error) {
                *error = i18n::tr("aipdf::pdf::SemanticOrderSelection",
                                  "第 %1 页的内容流与扫描结果对不上（%2 个显示操作符 / %3 个文本运行），"
                                  "无法确定语义元素的位置，文档没有修改。")
                             .arg(pageIndex + 1)
                             .arg(opCount)
                             .arg(runCount);
            }
            return false;
        }

        // MCID → 包围盒。
        QHash<int, ElementBox> boxOfMcid;
        for (auto it = marked.constBegin(); it != marked.constEnd(); ++it) {
            ElementBox box;
            for (int runIndex : it.value()) {
                if (runIndex < 0 || runIndex >= runCount) continue;
                const ScannedTextRun& run = scan.pages.at(pageIndex).runs.at(runIndex);
                // 字形画在基线**上方**：框的下边是基线，上边是基线 + 字高。
                const double bottom = run.baseline;
                const double top = run.baseline + std::max(run.fontSize, 1.0);
                const QRectF runBox(QPointF(run.origin.x(), bottom),
                                    QPointF(std::max(run.end.x(), run.origin.x()), top));
                if (!box.valid) {
                    box.rect = runBox;
                    box.valid = true;
                } else {
                    box.rect = box.rect.united(runBox);
                }
            }
            if (box.valid) boxOfMcid.insert(it.key(), box);
        }

        // 元素 → 它的 MCID 的包围盒之和。
        for (int i = 0; i < report.elements.size(); ++i) {
            const StructElement& element = report.elements.at(i);
            if (element.pageIndex != pageIndex) continue;
            ElementBox box;
            for (int mcid : element.mcids) {
                const auto found = boxOfMcid.constFind(mcid);
                if (found == boxOfMcid.constEnd()) continue;
                if (!box.valid) {
                    box.rect = found.value().rect;
                    box.valid = true;
                } else {
                    box.rect = box.rect.united(found.value().rect);
                }
            }
            if (box.valid) boxes->insert(i, box);
        }
    }
    return true;
#else
    if (error) {
        *error = i18n::tr("aipdf::pdf::SemanticOrderSelection", "当前构建没有启用 qpdf，无法确定语义元素的位置");
    }
    return false;
#endif  // AIPDF_WITH_QPDF
}

// ---------------------------------------------------------------------------
// 视觉顺序
// ---------------------------------------------------------------------------

// 视觉顺序的排序键。
//
// 口径与「自动添加文档标签」一致：先按页，页内从上到下，同一行从左到右。
struct VisualKey {
    int page = -1;
    // 量化后的「下边」：PDF 的 y 向上为正，下边越大越靠上。
    qint64 bottomBucket = 0;
    double left = 0.0;
};

VisualKey visualKeyOf(int pageIndex, int nodeIndex, const QHash<int, ElementBox>& boxes) {
    VisualKey key;
    key.page = pageIndex;
    const auto found = boxes.constFind(nodeIndex);
    if (found == boxes.constEnd()) return key;
    const QRectF& rect = found.value().rect;
    // 量化到 0.5 点一档：同一行里高矮不同的元素不会被上下分开
    // （比「差值大于 0.5 才算不同行」更稳，也不会出现非严格弱序）。
    key.bottomBucket = static_cast<qint64>(std::llround(rect.bottom() / kSameLineTolerance));
    key.left = rect.left();
    return key;
}

bool visualLess(const VisualKey& lhs, const VisualKey& rhs) {
    if (lhs.page != rhs.page) return lhs.page < rhs.page;
    if (lhs.bottomBucket != rhs.bottomBucket) return lhs.bottomBucket > rhs.bottomBucket;
    if (lhs.left != rhs.left) return lhs.left < rhs.left;
    return false;
}

// ---------------------------------------------------------------------------
// 选区判定
// ---------------------------------------------------------------------------

// 这个元素在不在选区里。
//
// 区域限制下，取不到位置的元素**不算在选区里**：
// 动不了、也定位不了的东西，宁可不碰。
bool isInScope(const SemanticOrderEntry& entry, const SemanticOrderScope& scope,
               const QHash<int, ElementBox>& boxes) {
    const int slot = scope.slotOfPage(entry.pageIndex);
    if (slot < 0) return false;
    const QRectF region = scope.regionAt(slot);
    if (region.isEmpty()) return true;  // 整页
    const auto found = boxes.constFind(entry.nodeIndex);
    if (found == boxes.constEnd()) return false;
    return found.value().rect.intersects(region);
}

// ---------------------------------------------------------------------------
// 选区分析
// ---------------------------------------------------------------------------

struct ScopeAnalysis {
    // 槽位（结构树里的容器）。**不能叫 slots**：Qt 把 slots 定义成空宏，
    // 拿它当变量名会被预处理成空，编译直接报错。
    QVector<SemanticOrderSlot> positions;
    QHash<int, ElementBox> boxes;
    QVector<SemanticOrderScopeElement> elements;  // 拍平（结构树顺序）
    QVector<int> pages;                            // 有语义元素的页（升序）
    int scopedCount = 0;
    QString error;
};

bool analyzeScope(const QByteArray& pdfBytes, const SemanticOrderScope& scope, ScopeAnalysis* analysis) {
    analysis->error.clear();
    if (pdfBytes.isEmpty()) {
        analysis->error =
            i18n::tr("aipdf::pdf::SemanticOrderSelection", "输入的 PDF 内容为空");
        return false;
    }

    QString readError;
    analysis->positions = readSemanticReadingOrder(pdfBytes, &readError);
    if (analysis->positions.isEmpty()) {
        analysis->error =
            readError.isEmpty()
                ? i18n::tr("aipdf::pdf::SemanticOrderSelection",
                           "这份文档还没有语义标签树；先运行「自动添加文档标签」生成语义树")
                : readError;
        return false;
    }

    const StructTreeReport report = readStructTree(pdfBytes);
    if (!report.error.isEmpty()) {
        analysis->error = report.error;
        return false;
    }
    QString geometryError;
    if (!collectElementBoxes(pdfBytes, report, &analysis->boxes, &geometryError)) {
        analysis->error = geometryError.isEmpty()
                              ? i18n::tr("aipdf::pdf::SemanticOrderSelection",
                                         "无法确定语义元素在页面上的位置")
                              : geometryError;
        return false;
    }

    QSet<int> pageSet;
    for (const SemanticOrderSlot& slot : analysis->positions) {
        for (const SemanticOrderEntry& entry : slot.entries) {
            SemanticOrderScopeElement element;
            element.nodeIndex = entry.nodeIndex;
            element.pageIndex = entry.pageIndex;
            element.role = entry.role;
            element.text = entry.text;
            const auto found = analysis->boxes.constFind(entry.nodeIndex);
            if (found != analysis->boxes.constEnd()) element.bounds = found.value().rect;
            element.inScope = isInScope(entry, scope, analysis->boxes);
            if (element.inScope) ++analysis->scopedCount;
            pageSet.insert(entry.pageIndex);
            analysis->elements.append(element);
        }
    }
    analysis->pages = QVector<int>(pageSet.begin(), pageSet.end());
    std::sort(analysis->pages.begin(), analysis->pages.end());

    // 视觉名次：选区内、位置已知的元素按视觉顺序编号。
    {
        QVector<int> ranked;
        for (int i = 0; i < analysis->elements.size(); ++i) {
            const SemanticOrderScopeElement& element = analysis->elements.at(i);
            if (!element.inScope) continue;
            if (!analysis->boxes.contains(element.nodeIndex)) continue;
            ranked.append(i);
        }
        std::stable_sort(ranked.begin(), ranked.end(), [&](int lhs, int rhs) {
            const SemanticOrderScopeElement& left = analysis->elements.at(lhs);
            const SemanticOrderScopeElement& right = analysis->elements.at(rhs);
            return visualLess(visualKeyOf(left.pageIndex, left.nodeIndex, analysis->boxes),
                              visualKeyOf(right.pageIndex, right.nodeIndex, analysis->boxes));
        });
        for (int rank = 0; rank < ranked.size(); ++rank) {
            analysis->elements[ranked.at(rank)].visualRank = rank;
        }
    }
    return true;
}

// 按视觉顺序重排选区内的元素，返回新的槽位顺序。
//
// 关键口径：参与重排的元素**只在它们已经占着的位置之间换位**。
// 选区外的元素连下标都不变——这是「只作用选区」最硬的一条保证。
QVector<SemanticOrderSlot> buildReorderedSlots(const QVector<SemanticOrderSlot>& positions,
                                                const SemanticOrderScope& scope,
                                                const QHash<int, ElementBox>& boxes, int* movedCount) {
    QVector<SemanticOrderSlot> updated = positions;
    if (movedCount) *movedCount = 0;
    for (int slotIndex = 0; slotIndex < positions.size(); ++slotIndex) {
        const SemanticOrderSlot& slot = positions.at(slotIndex);
        QVector<int> movable;
        for (int i = 0; i < slot.entries.size(); ++i) {
            const SemanticOrderEntry& entry = slot.entries.at(i);
            if (!isInScope(entry, scope, boxes)) continue;
            if (!boxes.contains(entry.nodeIndex)) continue;  // 位置取不到：不碰
            movable.append(i);
        }
        if (movable.size() < 2) continue;

        QVector<int> sorted = movable;
        std::stable_sort(sorted.begin(), sorted.end(), [&](int lhs, int rhs) {
            const SemanticOrderEntry& left = slot.entries.at(lhs);
            const SemanticOrderEntry& right = slot.entries.at(rhs);
            return visualLess(visualKeyOf(left.pageIndex, left.nodeIndex, boxes),
                              visualKeyOf(right.pageIndex, right.nodeIndex, boxes));
        });

        QVector<SemanticOrderEntry> reordered = slot.entries;
        for (int k = 0; k < movable.size(); ++k) {
            const int target = movable.at(k);
            const int source = sorted.at(k);
            if (target == source) continue;
            reordered[target] = slot.entries.at(source);
            if (movedCount) ++(*movedCount);
        }
        updated[slotIndex].entries = reordered;
    }
    return updated;
}

}  // namespace

// ---------------------------------------------------------------------------
// 选区
// ---------------------------------------------------------------------------

int SemanticOrderScope::slotOfPage(int pageIndex) const {
    for (int i = 0; i < pageIndexes.size(); ++i) {
        if (pageIndexes.at(i) == pageIndex) return i;
    }
    return -1;
}

QRectF SemanticOrderScope::regionAt(int slot) const {
    if (slot < 0 || slot >= regions.size()) return QRectF();
    return regions.at(slot);
}

// ---------------------------------------------------------------------------
// 读
// ---------------------------------------------------------------------------

SemanticOrderScopeReport readSemanticOrderScope(const QByteArray& pdfBytes,
                                                const SemanticOrderScope& scope, QString* error) {
    SemanticOrderScopeReport report;
    if (error) error->clear();
    ScopeAnalysis analysis;
    if (!analyzeScope(pdfBytes, scope, &analysis)) {
        report.error = analysis.error;
        if (error) *error = analysis.error;
        return report;
    }
    report.pages = analysis.pages;
    report.elements = analysis.elements;
    report.scopedCount = analysis.scopedCount;
    return report;
}

// ---------------------------------------------------------------------------
// 写
// ---------------------------------------------------------------------------

SemanticOrderApplyResult applySemanticOrderScope(const QByteArray& pdfBytes,
                                                 const SemanticOrderScope& scope, QString* error) {
    SemanticOrderApplyResult result;
    if (error) error->clear();
    if (scope.isEmpty()) {
        result.error =
            i18n::tr("aipdf::pdf::SemanticOrderSelection", "没有选中任何页面，文档没有修改。");
        if (error) *error = result.error;
        return result;
    }

    ScopeAnalysis analysis;
    if (!analyzeScope(pdfBytes, scope, &analysis)) {
        result.error = analysis.error;
        if (error) *error = analysis.error;
        return result;
    }
    result.scopedCount = analysis.scopedCount;

    int moved = 0;
    const QVector<SemanticOrderSlot> updated =
        buildReorderedSlots(analysis.positions, scope, analysis.boxes, &moved);
    result.movedCount = moved;
    if (moved == 0) {
        // 本来就按视觉顺序排好：不写回，文档保持原样。
        result.ok = true;
        result.changed = false;
        result.bytes = pdfBytes;
        return result;
    }

    QString writeError;
    const QByteArray written = writeSemanticReadingOrder(pdfBytes, updated, &writeError);
    if (written.isEmpty()) {
        result.error = writeError.isEmpty()
                           ? i18n::tr("aipdf::pdf::SemanticOrderSelection", "写入失败，文档没有修改。")
                           : writeError;
        if (error) *error = result.error;
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.bytes = written;
    return result;
}

}  // namespace aipdf::pdf
